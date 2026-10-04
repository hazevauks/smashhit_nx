/* sh.h -- what Smash Hit's port files share: the engine's module, the
 * activity that hosts it, and each file's entry points. MIT.
 */
#ifndef SH_H
#define SH_H

#include <stdint.h>

#include "jni.h"
#include "so_util.h"

/* ------------------------------------------------------------- the engine */
extern so_module g_mod_game;

/* sh_loader.c */
int sh_load_engine(void);       /* 0, or negative (logged) */
void sh_run_constructors(void); /* System.loadLibrary: the init array */

/* sh_locale.c: the console's language, or config.ini's ("pt", "BR") */
const char *sh_language(void);
const char *sh_country(void);

/* sh_assets.c: the APK's assets/, as libandroid's AAssetManager */
void sh_assets_init(void);
void sh_assets_report(void);

/* sh_java.c */
extern JObj *g_activity;      /* com.mediocre.smashhit.MainActivity */
extern JObj *g_surface;       /* android.view.Surface: the window */
extern JObj *g_asset_manager; /* android.content.res.AssetManager */
void sh_java_init(void);

/* A touch event as android.view.MotionEvent hands it to GameActivity: the
 * object the engine's GameActivityMotionEvent_fromJava reads through JNI
 * (sh_java.c answers from this). Positions are pixels of the window. */
#define SH_MAX_POINTERS 10
typedef struct {
  int action;          /* ACTION_* | (pointer index << 8) */
  int64_t time_ms;     /* SystemClock.uptimeMillis() of this event */
  int64_t down_ms;     /* ... of the first finger's ACTION_DOWN */
  int count;
  struct {
    int id;
    float x, y;
  } p[SH_MAX_POINTERS];
} ShMotion;
/* The MotionEvent object that reads `m` until the next call (one event at a
 * time: the engine copies what it reads before the native returns). */
JObj *sh_motion_event(const ShMotion *m);

/* A key as android.view.KeyEvent. */
typedef struct {
  int action; /* 0 down, 1 up */
  int code;   /* KEYCODE_* */
  int64_t time_ms, down_ms;
} ShKey;
JObj *sh_key_event(const ShKey *k);

/* sh_command.c: MainActivity.command(String), the engine's one way of asking
 * the Java side for anything ("getlanguage", "isproductowned <id>"...). The
 * answer is static or in `out`. */
const char *sh_command(const char *line, char *out, size_t cap);

/* sh_activity.c: GameActivity, from onCreate to onDestroy */
int sh_activity_run(void);
void sh_request_exit(void); /* Activity.finish() */
/* onTouchEvent / onKeyDown / onKeyUp, as the Java forwards them */
void sh_activity_touch(const ShMotion *m);
void sh_activity_key(const ShKey *k);

/* sh_input.c */
void sh_input_init(void);
void sh_input_poll(void);
void sh_input_reset(void); /* focus lost: held touches let go */
void sh_pointer_draw(void); /* the stick's pointer, over the frame */

#endif /* SH_H */
