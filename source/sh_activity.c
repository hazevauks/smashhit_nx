/* sh_activity.c -- plays the part of the game's Android activity.
 *
 * The APK's launcher activity is com.mediocre.smashhit.MainActivity, on
 * Google's GameActivity (the Android Game Development Kit's replacement for
 * NativeActivity). What the Java does, in Android's order, and what this
 * file does for it:
 *
 *   onCreate   MainActivity.jniCrashlyticsInit() (an empty native), then
 *              GameActivity: System.loadLibrary("smashhit") (sh_loader.c)
 *              and initializeNativeCode(filesDir, obbDir, externalFilesDir,
 *              assets, savedState) -> the native handle. The glue registers
 *              the other natives (RegisterNatives), puts a pipe on this
 *              thread's looper for work it posts to the UI thread, and
 *              starts the app thread, which runs android_main.
 *   onStart / onResume          -> onStartNative / onResumeNative
 *   surfaceCreated / Changed    -> onSurfaceCreatedNative /
 *                                  onSurfaceChangedNative (the app thread
 *                                  then makes its EGL context and the game)
 *   onWindowFocusChanged(true)  -> onWindowFocusChangedNative
 *   onTouchEvent, onKeyDown/Up  -> onTouchEventNative, onKeyDown/UpNative
 *
 * After that this thread IS the UI thread: it runs its looper (the glue's
 * work: finish, window flags), feeds input (sh_input.c) and turns the
 * Switch's focus changes into onPause / onResume. The game itself runs on
 * the glue's app thread (Renderer::render and eglSwapBuffers).
 *
 * Each lifecycle native waits for the app thread to take the change (the
 * glue's android_app_set_activity_state / set_window), so by the time one
 * returns android_main has seen it. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "config.h"
#include "dcr_apkcache.h"
#include "dcr_boost.h"
#include "dcr_config.h"
#include "dcr_dircache.h"
#include "dcr_path.h"
#include "error.h"
#include "gl_layer.h"
#include "jni.h"
#include "rt_applet.h"
#include "sh.h"
#include "so_util.h"
#include "util.h"
#include "watchdog.h"

void dcr_looper_run_main(int timeout_ms); /* the runtime's android_ndk.c */
void dcr_config_locale(const char *lang, const char *country, int density);

#define GA "com/google/androidgamesdk/GameActivity"
#define A_FILES DCR_ANDROID_FILES
#define A_OBB "/storage/emulated/0/Android/obb/" SH_PACKAGE
#define A_EXT_FILES DCR_ANDROID_EXT_FILES

/* ------------------------------------------------------------ natives */
/* GameActivity's natives, as the Java declares them (classes2.dex). The
 * handle is a jlong: the compiler places it as the engine expects. */
typedef jlong (*fn_init)(void *env, void *thiz, void *files, void *obb, void *ext_files, void *assets,
                         void *saved_state);
typedef void (*fn_h)(void *env, void *thiz, jlong h);
typedef void (*fn_hz)(void *env, void *thiz, jlong h, jboolean z);
typedef void (*fn_hs)(void *env, void *thiz, jlong h, void *surface);
typedef void (*fn_surf_changed)(void *env, void *thiz, jlong h, void *surface, jint fmt, jint w,
                                jint hgt);
typedef jboolean (*fn_event)(void *env, void *thiz, jlong h, void *event);

static jlong g_handle;
static volatile int g_exit;
static int g_w, g_h;
static fn_event n_touch, n_key_down, n_key_up;

/* For the watchdog: frames presented. */
uint64_t dcr_boot_frames(void) { return dcr_gl_frames(); }

void sh_request_exit(void) { g_exit = 1; }

/* The glue registers them from initializeNativeCode (GameActivity_register). */
static void *native(const char *name) { return jni_native(GA, name); }

static void *need(const char *name) {
  void *p = native(name);
  if (!p)
    debugPrintf("[activity] MISSING native GameActivity.%s\n", name);
  return p;
}

static void call_h(const char *name) {
  fn_h f = (fn_h)native(name);
  if (f) {
    debugPrintf("[activity] %s\n", name);
    f(g_jni_env, g_activity, g_handle);
  }
}

/* ------------------------------------------------------------------ input */
void sh_activity_touch(const ShMotion *m) {
  if (n_touch && g_handle)
    n_touch(g_jni_env, g_activity, g_handle, sh_motion_event(m));
}

void sh_activity_key(const ShKey *k) {
  fn_event f = k->action == 0 ? n_key_down : n_key_up;
  if (f && g_handle)
    f(g_jni_env, g_activity, g_handle, sh_key_event(k));
}

/* ------------------------------------------------------------- lifecycle */
static void window_focus(int on) {
  fn_hz f = (fn_hz)native("onWindowFocusChangedNative");
  if (f)
    f(g_jni_env, g_activity, g_handle, (jboolean)on);
}

/* The runtime (rt_applet.c) takes the HOME menu and sleep messages, writes
 * out the log and holds the clocks; the activity is paused and resumed here,
 * as Android does when another app covers the game: the window stays (no
 * surfaceDestroyed), so the engine keeps its GL context. Both run from
 * rt_applet_poll() in the loop below, on this thread. The game writes its
 * save on the pause. */
void port_focus_lost(void) {
  if (!g_handle)
    return;
  sh_input_reset();
  window_focus(0);
  call_h("onPauseNative");
}

void port_focus_gained(void) {
  if (!g_handle)
    return;
  call_h("onResumeNative");
  window_focus(1);
}

/* A backstop for the shutdown: if the game has not closed in 5 s (an engine
 * thread that never quits, say), end the process rather than hang on the
 * HOME menu's "closing" screen. */
static void exit_guard(void *arg) {
  (void)arg;
  svcSleepThread(5000000000ll);
  debugPrintf("[activity] the game did not close within 5 s: ending the process\n");
  log_flush_ring();
  svcExitProcess();
}

static void exit_guard_start(void) {
  static Thread t;
  if (R_FAILED(threadCreate(&t, exit_guard, NULL, NULL, 0x4000, 0x2B, -2)) ||
      R_FAILED(threadStart(&t)))
    debugPrintf("[activity] no exit backstop thread\n");
}

static void surface_up(void) {
  fn_hs created = (fn_hs)need("onSurfaceCreatedNative");
  fn_surf_changed changed = (fn_surf_changed)need("onSurfaceChangedNative");
  debugPrintf("[activity] surfaceCreated\n");
  if (created)
    created(g_jni_env, g_activity, g_handle, g_surface);
  debugPrintf("[activity] surfaceChanged %dx%d\n", g_w, g_h);
  if (changed)
    changed(g_jni_env, g_activity, g_handle, g_surface, 1 /* PixelFormat.RGBA_8888 */, g_w, g_h);
}

static void report(void) {
  static u64 last_tick;
  static unsigned long last_presented;
  const u64 tick = armGetSystemTick();
  const unsigned long presented = (unsigned long)dcr_gl_frames();
  const double fps = last_tick ? (double)(presented - last_presented) * 1e9 /
                                     (double)armTicksToNs(tick - last_tick)
                               : 0.0;
  last_tick = tick;
  last_presented = presented;
  debugPrintf("[activity] %lu frames presented (%.1f fps), %d Java objects\n", presented, fps,
              jni_live_objects());
  dcr_boost_report();
  dcr_apkcache_report();
  dcr_dircache_report();
  sh_assets_report();
}

/* ----------------------------------------------------------------- run */
int sh_activity_run(void) {
  g_w = dcr_config()->res_w;
  g_h = dcr_config()->res_h;
  /* Configuration: the locale, and the density of a 6.2" 720p screen (xhdpi
   * when rendering 1080p, as a TV of that size reports) */
  dcr_config_locale(sh_language(), sh_country(), g_h >= 1080 ? 320 : 213);
  sh_assets_init();
  dcr_watchdog_start();
  sh_input_init();
  dcr_present_hook = sh_pointer_draw; /* the stick's pointer, over the game's picture */

  /* ---- GameActivity.onCreate ---- */
  fn_init init = (fn_init)so_try_find_addr_rx(
      &g_mod_game, "Java_com_google_androidgamesdk_GameActivity_initializeNativeCode");
  if (!init)
    fatal_error(SH_LIB " has no GameActivity.initializeNativeCode: the port is made for\n"
                PORT_APK_DESC ".");
  JObj *files = jni_str(A_FILES), *obb = jni_str(A_OBB), *ext = jni_str(A_EXT_FILES);
  debugPrintf("[activity] GameActivity.initializeNativeCode(%s)\n", A_FILES);
  g_handle = init(g_jni_env, g_activity, files, obb, ext, g_asset_manager, NULL);
  jni_exception_report("initializeNativeCode");
  jni_release(files);
  jni_release(obb);
  jni_release(ext);
  debugPrintf("[activity] native handle 0x%llx\n", (unsigned long long)g_handle);
  if (!g_handle)
    fatal_error("GameActivity.initializeNativeCode failed (see debug.log).");
  n_touch = (fn_event)need("onTouchEventNative");
  n_key_down = (fn_event)need("onKeyDownNative");
  n_key_up = (fn_event)need("onKeyUpNative");

  /* ---- onStart, onResume, the surface, focus ---- */
  /* The focus messages (HOME, sleep) are the runtime's from boot on
   * (rt_applet.c); they reach the activity from the loop below. */
  call_h("onStartNative");
  call_h("onResumeNative");
  surface_up();
  window_focus(1);
  debugPrintf("[activity] activity up; this thread is the UI thread now\n");
  log_flush_ring();

  /* ---- the UI thread ---- */
  u64 last_input = 0, last_report = armGetSystemTick();
  int launch_done = 0;
  unsigned long quiet_at = 0;
  const u64 input_period = armNsToTicks(8000000ull); /* 8 ms: twice per display frame */
  while (!g_exit && !rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll(); /* focus, freezes: port_focus_lost / gained */
    if (!rt_focused()) {
      dcr_looper_run_main(50);
      continue;
    }
    dcr_looper_run_main(4);
    const u64 now = armGetSystemTick();
    if (now - last_input >= input_period) {
      last_input = now;
      sh_input_poll();
    }
    dcr_boost_poll();
    const unsigned long frames = (unsigned long)dcr_gl_frames();
    if (!launch_done && frames > 0) {
      launch_done = 1;
      dcr_boost_launch_end();
      debugPrintf("[activity] first frame presented\n");
      quiet_at = frames + 180;
    }
    /* From ~3 s after the first picture the log goes to a RAM ring (util.c),
     * written out every 10 s and by the watchdog: a line per call on the SD
     * card costs real frame time. */
    if (quiet_at && frames >= quiet_at) {
      quiet_at = 0;
      log_set_quiet(1);
    }
    if (armTicksToNs(now - last_report) >= 10000000000ull) {
      last_report = now;
      report();
      log_flush_ring();
      log_console_update();
    }
  }

  /* Android's way out: onPause, onStop, surfaceDestroyed, then onDestroy's
   * terminateNativeCode, which waits for the app thread to leave
   * android_main. The game saves on the pause. Only then may exit() take fs,
   * audout and nv away. */
  debugPrintf("[activity] leaving (%s): onPause, onStop, surfaceDestroyed, terminateNativeCode\n",
              g_exit ? "the game closed itself" : "closed from the system");
  log_set_quiet(0);
  /* the hook off, no more hang reports, and the clocks running again if the
   * game was last told it lost focus (frozen clocks would stall any timed
   * wait in the shutdown) */
  rt_applet_stop();
  exit_guard_start();
  if (rt_focused()) {
    sh_input_reset();
    window_focus(0);
    call_h("onPauseNative");
  }
  call_h("onStopNative");
  call_h("onSurfaceDestroyedNative");
  call_h("terminateNativeCode");
  g_handle = 0;
  debugPrintf("[activity] the game has closed\n");
  log_flush_ring();
  return 0;
}
