/* sh_loader.c -- loading Smash Hit's one module, libsmashhit.so.
 *
 * The APK's lib/armeabi-v7a holds six libraries; only this one is the game:
 * Mediocre's engine with the game, Lua, libpng, libjpeg, Vorbis, Google's
 * GameActivity glue and libc++ linked in, Thumb-2 and GLES 2 (the four
 * libcrashlytics*.so are the crash reporter's, libdatastore_shared_counter.so
 * is Jetpack's: none is loaded). Its DT_NEEDED are system libraries only
 * (libc, libm, libdl, liblog, libandroid, libnativewindow, libEGL, libGLESv2,
 * libOpenSLES), all served by the shims: 373 imports, the gl* ones through
 * the GL layer, the rest from the import table (runtime/tools/gen_imports.py).
 * Plain REL relocations, a GNU hash table only (the loader reads .dynsym by
 * its section). Nothing in it writes code at run time, so the module is
 * mapped the plain way: staged, relocated, resolved, then sealed as code (RX
 * text, RW data) before anything in it runs. Its init array has 11
 * constructors.
 *
 * It has no JNI_OnLoad: its one entry from Java is
 * GameActivity.initializeNativeCode, which registers the other natives
 * (sh_activity.c). MIT.
 */
#include <stdio.h>
#include <switch.h>

#include "config.h"
#include "dcr_path.h"
#include "imports.h"
#include "sh.h"
#include "so_util.h"
#include "util.h"

so_module g_mod_game;

int sh_load_engine(void) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), SH_LIB);
  int rc = so_load(&g_mod_game, path, NULL, PORT_SO_REGION_BYTES);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than PORT_SO_REGION_BYTES"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(&g_mod_game);
  int missing = so_resolve(&g_mod_game, dcr_imports, dcr_imports_count, 1);
  debugPrintf("[boot] %s %u KB  staged %p -> %p  (%d unresolved imports)\n", g_mod_game.base_name,
              (unsigned)(g_mod_game.load_size >> 10), g_mod_game.load_base, g_mod_game.load_virtbase,
              missing);
  /* libgcc's __sync_* on ARM Linux call the kernel's user helpers through
   * literal pools: any such literal is pointed at the runtime's kuser.S. */
  so_fix_kuser_helpers(&g_mod_game);
  so_finalize(&g_mod_game);
  so_flush_caches(&g_mod_game);
  return 0;
}

/* Android runs a library's constructors inside System.loadLibrary, which
 * GameActivity.onCreate calls before initializeNativeCode. */
void sh_run_constructors(void) {
  const u64 t0 = armGetSystemTick();
  so_execute_init_array(&g_mod_game);
  debugPrintf("[boot] %s constructors done in %llu ms\n", SH_LIB,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}
