/* sh_java.c -- the Java side of Smash Hit, as the engine sees it.
 *
 * The game's Java (com.mediocre.smashhit.MainActivity, on Google's
 * com.google.androidgamesdk.GameActivity) does not run here: sh_activity.c
 * and sh_input.c do what it did. What the ENGINE calls back through JNI is
 * answered from the tables below. It is little:
 *
 *   GameActivity   the glue's own calls: finish, setWindowFlags, the window
 *                  insets (none on a Switch), the soft keyboard (unused)
 *   MainActivity   command(String): the game's one question-and-answer
 *                  channel (sh_command.c)
 *   MotionEvent,   the events GameActivity's natives are handed: the glue
 *   KeyEvent       reads each through its getters (GameActivityMotionEvent_
 *                  fromJava, GameActivityKeyEvent_fromJava), answered here
 *                  from the event the port is delivering
 *
 * Everything else the Java did (Firebase, AdMob, Play Games, billing) the
 * engine reaches only through command(). An unhandled method answers its
 * type's zero and is logged once: that log is the to-do list. MIT.
 */
#include <string.h>

#include "config.h"
#include "dcr_manifest.h"
#include "sh.h"
#include "util.h"

#define GA "com/google/androidgamesdk/GameActivity"
#define MA "com/mediocre/smashhit/MainActivity"
#define ME "android/view/MotionEvent"
#define KE "android/view/KeyEvent"
#define INSETS "androidx/core/graphics/Insets"
#define INSETS_TYPE "androidx/core/view/WindowInsetsCompat$Type"
#define S "Ljava/lang/String;"

#define H(fn) static jvalue fn(JObj *self, const jvalue *a, const JMethod *m)

JObj *g_activity, *g_surface, *g_asset_manager;

H(h_getPackageName) {
  const char *pkg = dcr_manifest_loaded() && dcr_manifest_package()[0] ? dcr_manifest_package()
                                                                        : SH_PACKAGE;
  return jv_l(jni_str(pkg));
}

/* ------------------------------------------------------------ GameActivity */
/* Activity.finish(), from GameActivity_finish (the engine's "quit"). */
H(h_finish) {
  debugPrintf("[java] Activity.finish()\n");
  sh_request_exit();
  return jv_none();
}

/* getWindowInsets(type) / getWaterfallInsets(): no status bar, no cut-out,
 * no gesture area: an Insets of zeros (its fields are in jni_field_defs). */
H(h_insets) { return jv_l(jni_retain(jni_singleton(INSETS))); }

/* WindowInsetsCompat.Type.statusBars() and the others: the masks. */
H(h_insets_type) {
  static const struct {
    const char *name;
    jint mask;
  } k[] = {{"statusBars", 1},     {"navigationBars", 2},  {"captionBar", 4},
           {"ime", 8},            {"systemGestures", 16}, {"mandatorySystemGestures", 32},
           {"tappableElement", 64}, {"displayCutout", 128}, {"systemBars", 7}};
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    if (!strcmp(m->name, k[i].name))
      return jv_i(k[i].mask);
  return jv_i(0);
}

/* MainActivity.command(String): the answer as a new String. */
H(h_command) {
  char buf[96];
  return jv_l(jni_str(sh_command(jni_utf(a[0].l), buf, sizeof buf)));
}

/* ------------------------------------------------------------- MotionEvent */
#define AINPUT_SOURCE_TOUCHSCREEN 0x1002
#define AINPUT_SOURCE_GAMEPAD 0x401
#define AMOTION_EVENT_AXIS_X 0
#define AMOTION_EVENT_AXIS_Y 1
#define AMOTION_EVENT_AXIS_PRESSURE 2
#define AMOTION_EVENT_AXIS_SIZE 3
#define AMOTION_EVENT_TOOL_TYPE_FINGER 1
/* InputDevice ids: any, as long as the two differ */
#define DEVICE_TOUCH 2
#define DEVICE_PAD 3

static const ShMotion *motion(const JObj *self) { return self ? self->p : NULL; }

/* A pointer index argument, or 0 for the getters without one. */
static int pointer_arg(const ShMotion *e, const jvalue *a, const JMethod *m) {
  const int i = m->sig[1] == 'I' ? a[0].i : 0;
  return e && i >= 0 && i < e->count ? i : -1;
}

H(h_me_action) { return jv_i(motion(self) ? motion(self)->action : 0); }
H(h_me_time) { return jv_j(motion(self) ? motion(self)->time_ms : 0); }
H(h_me_down_time) { return jv_j(motion(self) ? motion(self)->down_ms : 0); }
H(h_me_count) { return jv_i(motion(self) ? motion(self)->count : 0); }
H(h_me_source) { return jv_i(AINPUT_SOURCE_TOUCHSCREEN); }
H(h_me_device) { return jv_i(DEVICE_TOUCH); }
H(h_me_precision) { return jv_f(1.0f); }
H(h_me_tool) { return jv_i(AMOTION_EVENT_TOOL_TYPE_FINGER); }

H(h_me_pointer_id) {
  const ShMotion *e = motion(self);
  const int i = pointer_arg(e, a, m);
  return jv_i(i < 0 ? 0 : e->p[i].id);
}

/* getAxisValue(axis, pointerIndex) */
H(h_me_axis) {
  const ShMotion *e = motion(self);
  const int i = e && a[1].i >= 0 && a[1].i < e->count ? a[1].i : -1;
  if (i < 0)
    return jv_f(0.0f);
  switch (a[0].i) {
  case AMOTION_EVENT_AXIS_X: return jv_f(e->p[i].x);
  case AMOTION_EVENT_AXIS_Y: return jv_f(e->p[i].y);
  case AMOTION_EVENT_AXIS_PRESSURE: return jv_f(1.0f);
  case AMOTION_EVENT_AXIS_SIZE: return jv_f(0.05f);
  default: return jv_f(0.0f);
  }
}

/* getRawX() / getRawX(pointerIndex): the window is the whole screen */
H(h_me_raw_x) {
  const ShMotion *e = motion(self);
  const int i = pointer_arg(e, a, m);
  return jv_f(i < 0 ? 0.0f : e->p[i].x);
}
H(h_me_raw_y) {
  const ShMotion *e = motion(self);
  const int i = pointer_arg(e, a, m);
  return jv_f(i < 0 ? 0.0f : e->p[i].y);
}

JObj *sh_motion_event(const ShMotion *e) {
  JObj *o = jni_singleton(ME);
  o->p = (void *)e;
  return o;
}

/* ---------------------------------------------------------------- KeyEvent */
static const ShKey *key(const JObj *self) { return self ? self->p : NULL; }

H(h_ke_action) { return jv_i(key(self) ? key(self)->action : 0); }
H(h_ke_code) { return jv_i(key(self) ? key(self)->code : 0); }
H(h_ke_time) { return jv_j(key(self) ? key(self)->time_ms : 0); }
H(h_ke_down_time) { return jv_j(key(self) ? key(self)->down_ms : 0); }
H(h_ke_source) { return jv_i(AINPUT_SOURCE_GAMEPAD); }
H(h_ke_device) { return jv_i(DEVICE_PAD); }

JObj *sh_key_event(const ShKey *k) {
  JObj *o = jni_singleton(KE);
  o->p = (void *)k;
  return o;
}

/* ------------------------------------------------------------------ tables */
const JMethodDef jni_method_defs[] = {
    {"android/content/Context", "getPackageName", "()" S, h_getPackageName},
    /* GameActivity, as its native glue calls it (GameActivity_register) */
    {GA, "finish", "()V", h_finish},
    {GA, "setWindowFlags", "(II)V", jni_h_void},
    {GA, "setWindowFormat", "(I)V", jni_h_void},
    {GA, "getWindowInsets", NULL, h_insets},
    {GA, "getWaterfallInsets", NULL, h_insets},
    {GA, "setImeEditorInfoFields", "(III)V", jni_h_void},
    {GA, "setImeEditorInfo", NULL, jni_h_void},
    {GA, "setTextInputState", NULL, jni_h_void},
    {INSETS_TYPE, NULL, NULL, h_insets_type},
    /* the game's own */
    {MA, "command", "(" S ")" S, h_command},
    /* the soft keyboard (GameTextInput): never shown */
    {"com/google/androidgamesdk/gametextinput/InputConnection", NULL, NULL, jni_h_void},
    /* a touch event, read by GameActivityMotionEvent_fromJava */
    {ME, "getAction", "()I", h_me_action},
    {ME, "getEventTime", "()J", h_me_time},
    {ME, "getDownTime", "()J", h_me_down_time},
    {ME, "getPointerCount", "()I", h_me_count},
    {ME, "getPointerId", "(I)I", h_me_pointer_id},
    {ME, "getToolType", "(I)I", h_me_tool},
    {ME, "getAxisValue", "(II)F", h_me_axis},
    {ME, "getRawX", NULL, h_me_raw_x},
    {ME, "getRawY", NULL, h_me_raw_y},
    {ME, "getXPrecision", "()F", h_me_precision},
    {ME, "getYPrecision", "()F", h_me_precision},
    {ME, "getSource", "()I", h_me_source},
    {ME, "getDeviceId", "()I", h_me_device},
    /* no history (one event per sample), no flags, no buttons, no stylus */
    {ME, "getHistorySize", "()I", jni_h_zero},
    {ME, "getHistoricalEventTime", NULL, jni_h_zero},
    {ME, "getHistoricalAxisValue", NULL, jni_h_zero},
    {ME, "getFlags", "()I", jni_h_zero},
    {ME, "getMetaState", "()I", jni_h_zero},
    {ME, "getActionButton", "()I", jni_h_zero},
    {ME, "getButtonState", "()I", jni_h_zero},
    {ME, "getClassification", "()I", jni_h_zero},
    {ME, "getEdgeFlags", "()I", jni_h_zero},
    /* a key event, read by GameActivityKeyEvent_fromJava */
    {KE, "getAction", "()I", h_ke_action},
    {KE, "getKeyCode", "()I", h_ke_code},
    {KE, "getEventTime", "()J", h_ke_time},
    {KE, "getDownTime", "()J", h_ke_down_time},
    {KE, "getSource", "()I", h_ke_source},
    {KE, "getDeviceId", "()I", h_ke_device},
    {KE, "getFlags", "()I", jni_h_zero},
    {KE, "getMetaState", "()I", jni_h_zero},
    {KE, "getModifiers", "()I", jni_h_zero},
    {KE, "getRepeatCount", "()I", jni_h_zero},
    {KE, "getScanCode", "()I", jni_h_zero},
    {KE, "getUnicodeChar", NULL, jni_h_zero},
    {NULL, NULL, NULL, NULL},
};

const JFieldDef jni_field_defs[] = {
    {INSETS, "left", NULL, 0, NULL},
    {INSETS, "top", NULL, 0, NULL},
    {INSETS, "right", NULL, 0, NULL},
    {INSETS, "bottom", NULL, 0, NULL},
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {MA, GA},
    {GA, "androidx/appcompat/app/AppCompatActivity"},
    {"androidx/appcompat/app/AppCompatActivity", "androidx/fragment/app/FragmentActivity"},
    {"androidx/fragment/app/FragmentActivity", "androidx/activity/ComponentActivity"},
    {"androidx/activity/ComponentActivity", "android/app/Activity"},
    {"android/app/Activity", "android/view/ContextThemeWrapper"},
    {"android/view/ContextThemeWrapper", "android/content/ContextWrapper"},
    {"android/content/ContextWrapper", "android/content/Context"},
    {NULL, NULL},
};

/* Classes the engine probes for and must not find (when classes.txt is
 * absent): none known. */
const char *const jni_missing_classes[] = {
    NULL,
};

void sh_java_init(void) {
  jni_init();
  g_activity = jni_singleton(MA);
  g_surface = jni_singleton("android/view/Surface");
  g_asset_manager = jni_singleton("android/content/res/AssetManager");
  debugPrintf("[java] MainActivity %p; package %s\n", (void *)g_activity,
              dcr_manifest_loaded() ? dcr_manifest_package() : SH_PACKAGE " (default)");
}
