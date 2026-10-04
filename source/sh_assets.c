/* sh_assets.c -- libandroid's AAssetManager over the player's APK.
 *
 * The engine reads every game file (levels, textures, sounds, shaders, the
 * Lua) the way QiFileInputStream::open does, disassembled:
 *
 *   AAssetManager_open(manager, name, AASSET_MODE_UNKNOWN)
 *   AAsset_openFileDescriptor(asset, &start, &length)   -> a descriptor of
 *                                 the APK itself, and where the file lies in it
 *   dup, fdopen(.., "rb"), close; fseek(start)          and it reads from there
 *   AAsset_close(asset)                                 when the stream closes
 *
 * That only works for files the APK stores uncompressed, which is why the
 * game's assets are all named *.mp3 (and *.png): aapt never deflates those.
 * All 2421 entries under assets/ are stored. So nothing is ever extracted:
 * the central directory is indexed once, an open answers with a descriptor of
 * the APK (through the runtime's open, so its reads come from the APK cache,
 * dcr_apkcache.c) and the entry's place in it. A deflated entry has no
 * descriptor, on Android as here. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "bionic.h"
#include "bionic_io.h"
#include "dcr_apkcache.h"
#include "dcr_path.h"
#include "rt_apkfind.h"
#include "sh.h"
#include "util.h"

#define ASSETS "assets/"

typedef struct {
  uint32_t hash;
  uint32_t name_off; /* into g_names: the path under assets/ */
  uint32_t lho;      /* its local file header */
  uint32_t size;
  uint32_t data_off; /* where its bytes begin; 0: not read from the header yet */
  uint16_t method;
} Entry;

static Entry *g_ent;
static int g_nent, g_cap;
static char *g_names;
static size_t g_names_len, g_names_cap;
static uint32_t *g_tab; /* open addressing: entry index + 1 */
static uint32_t g_mask;
static Mutex g_lock;
static uint32_t g_opens, g_misses;

static uint32_t fnv(const char *s) {
  uint32_t h = 2166136261u;
  while (*s)
    h = (h ^ (uint8_t)*s++) * 16777619u;
  return h;
}

/* ------------------------------------------------------------- the index */
static int add_entry(const RtZipEntry *e, void *ctx) {
  (void)ctx;
  const size_t skip = sizeof ASSETS - 1;
  if (e->name_len <= skip || e->name_len >= sizeof e->name || strncmp(e->name, ASSETS, skip))
    return 0;
  const char *name = e->name + skip;
  const size_t len = e->name_len - skip + 1;
  if (name[len - 2] == '/') /* a folder's own entry */
    return 0;
  if (g_nent == g_cap) {
    const int cap = g_cap ? g_cap * 2 : 4096;
    Entry *n = realloc(g_ent, (size_t)cap * sizeof *n);
    if (!n)
      return 1;
    g_ent = n;
    g_cap = cap;
  }
  if (g_names_len + len > g_names_cap) {
    const size_t cap = g_names_cap ? g_names_cap * 2 : 128 * 1024;
    char *n = realloc(g_names, cap);
    if (!n)
      return 1;
    g_names = n;
    g_names_cap = cap;
  }
  memcpy(g_names + g_names_len, name, len);
  g_ent[g_nent++] = (Entry){.hash = fnv(name), .name_off = (uint32_t)g_names_len, .lho = e->lho,
                            .size = e->size, .method = (uint16_t)e->method};
  g_names_len += len;
  return 0;
}

void sh_assets_init(void) {
  const u64 t0 = armGetSystemTick();
  mutexInit(&g_lock);
  if (rt_zip_walk_path(dcr_apk_path(), add_entry, NULL) != 0 || !g_nent) {
    debugPrintf("[assets] %s: no assets/ found in its directory\n", dcr_apk_path());
    return;
  }
  uint32_t n = 16;
  while (n < (uint32_t)g_nent * 2)
    n <<= 1;
  g_tab = calloc(n, sizeof *g_tab);
  if (!g_tab) {
    g_nent = 0;
    return;
  }
  g_mask = n - 1;
  int deflated = 0;
  for (int i = 0; i < g_nent; i++) {
    uint32_t s = g_ent[i].hash & g_mask;
    while (g_tab[s])
      s = (s + 1) & g_mask;
    g_tab[s] = (uint32_t)i + 1;
    deflated += g_ent[i].method != 0;
  }
  debugPrintf("[assets] %d files under assets/ (%d compressed: those cannot be opened), "
              "indexed in %llu ms\n", g_nent, deflated,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}

static Entry *find(const char *name) {
  if (!g_tab || !name)
    return NULL;
  while (*name == '/')
    name++;
  const uint32_t h = fnv(name);
  for (uint32_t s = h & g_mask; g_tab[s]; s = (s + 1) & g_mask) {
    Entry *e = &g_ent[g_tab[s] - 1];
    if (e->hash == h && !strcmp(g_names + e->name_off, name))
      return e;
  }
  return NULL;
}

/* Where the entry's bytes begin: past its local header, whose name and extra
 * field lengths are its own (not the central directory's). Read once. */
static uint32_t data_offset(Entry *e) {
  mutexLock(&g_lock);
  uint32_t off = e->data_off;
  if (!off) {
    uint8_t h[30];
    int ok = dcr_apkcache_read(e->lho, h, sizeof h) == (ssize_t)sizeof h;
    if (!ok) { /* the cache cannot serve it: the file itself */
      FILE *f = fopen(dcr_apk_path(), "rb");
      ok = f && !fseek(f, (long)e->lho, SEEK_SET) && fread(h, 1, sizeof h, f) == sizeof h;
      if (f)
        fclose(f);
    }
    if (ok && h[0] == 'P' && h[1] == 'K' && h[2] == 3 && h[3] == 4)
      off = e->data_off = e->lho + 30u + (uint32_t)(h[26] | h[27] << 8) + (uint32_t)(h[28] | h[29] << 8);
  }
  mutexUnlock(&g_lock);
  return off;
}

void sh_assets_report(void) {
  debugPrintf("[assets] %u opens, %u not found\n", (unsigned)g_opens, (unsigned)g_misses);
}

/* ================================ the NDK API ============================== */
#define ASSET_MAGIC 0x41535354u /* 'ASST' */

typedef struct {
  uint32_t magic;
  Entry *e;
} AAsset;

static uint32_t g_manager[4] = {0x4d475231u}; /* the one AAssetManager */

void *b_AAssetManager_fromJava(void *env, void *asset_manager) {
  (void)env, (void)asset_manager;
  return g_manager;
}

void *b_AAssetManager_open(void *mgr, const char *filename, int mode) {
  (void)mgr, (void)mode;
  Entry *e = find(filename);
  __atomic_add_fetch(&g_opens, 1, __ATOMIC_RELAXED);
  if (!e) {
    /* the engine looks for each file in several folders in turn (189 misses
     * for 318 files found, in the first hardware run): the first few are
     * worth a line, the rest are counted */
    if (__atomic_add_fetch(&g_misses, 1, __ATOMIC_RELAXED) <= 12)
      debugPrintf("[assets] open(%s): not in the APK\n", filename ? filename : "(null)");
    return NULL;
  }
  AAsset *a = calloc(1, sizeof *a);
  if (!a)
    return NULL;
  a->magic = ASSET_MAGIC;
  a->e = e;
  return a;
}

static AAsset *as_asset(void *p) {
  AAsset *a = p;
  return a && a->magic == ASSET_MAGIC ? a : NULL;
}

/* The APK opened through the runtime, by the app's own Android path: the
 * engine dup()s the descriptor, wraps the copy in a FILE and closes this
 * one. off_t is 32 bits in an armeabi-v7a library. */
int b_AAsset_openFileDescriptor(void *p, b_off_t *out_start, b_off_t *out_length) {
  AAsset *a = as_asset(p);
  if (!a || a->e->method != 0)
    return -1;
  const uint32_t off = data_offset(a->e);
  if (!off) {
    debugPrintf("[assets] %s: its header in the APK is unreadable\n", g_names + a->e->name_off);
    return -1;
  }
  const int fd = b_open(DCR_ANDROID_APK, 0 /* O_RDONLY */);
  if (fd < 0) {
    debugPrintf("[assets] cannot open the APK (%s) for %s\n", dcr_apk_path(), g_names + a->e->name_off);
    return -1;
  }
  if (out_start)
    *out_start = (b_off_t)off;
  if (out_length)
    *out_length = (b_off_t)a->e->size;
  return fd;
}

void b_AAsset_close(void *p) {
  AAsset *a = as_asset(p);
  if (!a)
    return;
  a->magic = 0;
  free(a);
}

/* The rest of what an asset answers without its bytes (dlsym): the engine
 * imports the four above. */
int32_t b_AAsset_getLength(void *p) {
  AAsset *a = as_asset(p);
  return a ? (int32_t)a->e->size : 0;
}

int64_t b_AAsset_getLength64(void *p) { return b_AAsset_getLength(p); }
