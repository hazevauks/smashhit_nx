/* sh_input.c -- the touch screen and the controller, as GameActivity's Java
 * handed them to the engine.
 *
 * On Android:
 *   touch   GameActivity.onTouchEvent -> onTouchEventNative(handle,
 *           MotionEvent): the glue copies the event (every pointer's id and
 *           axes) for the app thread, where Renderer::handleInput turns it
 *           into QiInput::registerTouchBegin / TouchPos / TouchEnd by the
 *           action: DOWN and POINTER_DOWN begin the pointer the action
 *           names, UP and POINTER_UP end it, MOVE moves them all.
 *           Positions are pixels of the window.
 *   keys    onKeyDown / onKeyUp -> onKeyDownNative / onKeyUpNative(handle,
 *           KeyEvent). handleInput knows (disassembled, its jump table at
 *           +0x184): DPAD_UP/DOWN/LEFT/RIGHT, DPAD_CENTER and BUTTON_A (the
 *           same button), BUTTON_L1/R1/L2/R2, BACK and MENU.
 *
 * The game is played by touching where the ball should go, and it knows no
 * analogue stick. So, besides the touch screen itself (handheld), a pointer:
 * either stick moves it over the picture (drawn at the end of this file),
 * and A, ZR or ZL touch the screen where it is, for as long as they are
 * held. The other buttons go to the game as the keys it knows: the D-pad,
 * X as BUTTON_A, L and R as L1 and R1, B and + as BACK, - as MENU.
 *
 * One thread does it all, between the UI thread's looper turns
 * (sh_activity.c), every 8 ms. MIT.
 */
#include <math.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "gl_layer.h"
#include "rt_pad.h"
#include "rt_window.h"
#include "sh.h"
#include "util.h"

/* android.view.MotionEvent */
enum { AM_DOWN = 0, AM_UP = 1, AM_MOVE = 2, AM_POINTER_DOWN = 5, AM_POINTER_UP = 6 };
/* android.view.KeyEvent */
enum {
  AK_BACK = 4,
  AK_DPAD_UP = 19,
  AK_DPAD_DOWN = 20,
  AK_DPAD_LEFT = 21,
  AK_DPAD_RIGHT = 22,
  AK_MENU = 82,
  AK_BUTTON_A = 96,
  AK_BUTTON_L1 = 102,
  AK_BUTTON_R1 = 103,
  AK_BUTTON_L2 = 104,
  AK_BUTTON_R2 = 105,
};

static int64_t now_ms(void) { return (int64_t)(armTicksToNs(armGetSystemTick()) / 1000000ull); }

static int g_w = 1280, g_h = 720; /* the window */

/* ==================================================================== keys */
/* Switch buttons to the engine's keys. Two buttons may be one key (B and +):
 * the key is down while either is. */
static const struct {
  u64 buttons;
  int code;
} k_keys[] = {
    {HidNpadButton_Up, AK_DPAD_UP},       {HidNpadButton_Down, AK_DPAD_DOWN},
    {HidNpadButton_Left, AK_DPAD_LEFT},   {HidNpadButton_Right, AK_DPAD_RIGHT},
    {HidNpadButton_X, AK_BUTTON_A},       {HidNpadButton_L, AK_BUTTON_L1},
    {HidNpadButton_R, AK_BUTTON_R1},      {HidNpadButton_B | HidNpadButton_Plus, AK_BACK},
    {HidNpadButton_Minus, AK_MENU},
};
/* What the pointer's buttons are to the game when the pointer is off. */
static const struct {
  u64 buttons;
  int code;
} k_keys_no_pointer[] = {
    {HidNpadButton_A, AK_BUTTON_A},
    {HidNpadButton_ZL, AK_BUTTON_L2},
    {HidNpadButton_ZR, AK_BUTTON_R2},
};
#define NKEYS (sizeof k_keys / sizeof k_keys[0])
#define NKEYS_NP (sizeof k_keys_no_pointer / sizeof k_keys_no_pointer[0])

static PadState g_pads[2]; /* player 1's controller, and the Joy-Cons on the console */
static uint8_t g_key_down[NKEYS + NKEYS_NP];
static int64_t g_key_since[NKEYS + NKEYS_NP];

static void key_set(unsigned slot, int code, int down) {
  if (g_key_down[slot] == (uint8_t)down)
    return;
  g_key_down[slot] = (uint8_t)down;
  const int64_t t = now_ms();
  if (down)
    g_key_since[slot] = t;
  if (dcr_config()->log_input)
    debugPrintf("[input] key %d %s\n", code, down ? "down" : "up");
  const ShKey k = {.action = down ? 0 : 1, .code = code, .time_ms = t, .down_ms = g_key_since[slot]};
  sh_activity_key(&k);
}

static void poll_keys(u64 buttons) {
  for (unsigned i = 0; i < NKEYS; i++)
    key_set(i, k_keys[i].code, (buttons & k_keys[i].buttons) != 0);
  if (!dcr_config()->pointer)
    for (unsigned i = 0; i < NKEYS_NP; i++)
      key_set(NKEYS + i, k_keys_no_pointer[i].code, (buttons & k_keys_no_pointer[i].buttons) != 0);
}

/* ================================================================ pointers */
/* What touches the screen now, as Android numbers it: each finger keeps a
 * small id (the lowest free one) from its DOWN to its UP. `src` tells the
 * touch screen's fingers (their hid finger id) from the stick's pointer. */
#define SRC_STICK 0x10000u
typedef struct {
  uint32_t src;
  int id;
  float x, y;
} Pt;
static Pt g_pt[SH_MAX_POINTERS];
static int g_npt;
static int64_t g_down_ms;

static void send(int action, int index) {
  ShMotion m = {.action = action | index << 8, .time_ms = now_ms(), .down_ms = g_down_ms,
                .count = g_npt};
  for (int i = 0; i < g_npt; i++) {
    m.p[i].id = g_pt[i].id;
    m.p[i].x = g_pt[i].x;
    m.p[i].y = g_pt[i].y;
  }
  if (dcr_config()->log_input && action != AM_MOVE)
    debugPrintf("[input] touch action %d, pointer %d (id %d) at %.0f,%.0f; %d down\n", action, index,
                g_pt[index].id, (double)g_pt[index].x, (double)g_pt[index].y, g_npt);
  sh_activity_touch(&m);
}

static int free_id(void) {
  for (int id = 0;; id++) {
    int used = 0;
    for (int i = 0; i < g_npt && !used; i++)
      used = g_pt[i].id == id;
    if (!used)
      return id;
  }
}

/* The pointers that should be down now: lifted ones end, new ones begin,
 * the rest move. */
static void apply(const Pt *now, int n) {
  /* lifted: down before, gone now */
  for (int i = 0; i < g_npt;) {
    int still = 0;
    for (int j = 0; j < n && !still; j++)
      still = now[j].src == g_pt[i].src;
    if (still) {
      i++;
      continue;
    }
    send(g_npt == 1 ? AM_UP : AM_POINTER_UP, i);
    memmove(&g_pt[i], &g_pt[i + 1], (size_t)(g_npt - i - 1) * sizeof g_pt[0]);
    g_npt--;
  }
  /* moved */
  int moved = 0;
  for (int i = 0; i < g_npt; i++)
    for (int j = 0; j < n; j++)
      if (now[j].src == g_pt[i].src && (now[j].x != g_pt[i].x || now[j].y != g_pt[i].y)) {
        g_pt[i].x = now[j].x;
        g_pt[i].y = now[j].y;
        moved = 1;
      }
  if (moved)
    send(AM_MOVE, 0);
  /* new */
  for (int j = 0; j < n && g_npt < SH_MAX_POINTERS; j++) {
    int was = 0;
    for (int i = 0; i < g_npt && !was; i++)
      was = g_pt[i].src == now[j].src;
    if (was)
      continue;
    g_pt[g_npt] = now[j];
    g_pt[g_npt].id = free_id();
    g_npt++;
    if (g_npt == 1)
      g_down_ms = now_ms();
    send(g_npt == 1 ? AM_DOWN : AM_POINTER_DOWN, g_npt - 1);
  }
}

/* ============================================================= the pointer */
/* Read by the app thread when it draws (sh_pointer_draw): plain words. */
static volatile float g_px = 640.0f, g_py = 360.0f;
static volatile int g_pshown, g_ppressed;
static u64 g_plast_used; /* tick of the last stick movement or press */
static u64 g_plast_poll;

#define POINTER_BUTTONS (HidNpadButton_A | HidNpadButton_ZR | HidNpadButton_ZL)
#define POINTER_DEADZONE 0.12f
#define POINTER_SPEED 1100.0f   /* pixels a second at full tilt, at 720p */
#define POINTER_HIDE_NS 6000000000ull

static void pointer_poll(u64 buttons, const float sticks[4], int touching) {
  const u64 now = armGetSystemTick();
  float dt = g_plast_poll ? (float)armTicksToNs(now - g_plast_poll) / 1e9f : 0.0f;
  g_plast_poll = now;
  if (dt > 0.05f)
    dt = 0.05f;
  /* whichever stick is pushed further */
  float sx = sticks[0], sy = sticks[1];
  if (sticks[2] * sticks[2] + sticks[3] * sticks[3] > sx * sx + sy * sy)
    sx = sticks[2], sy = sticks[3];
  const float mag = sqrtf(sx * sx + sy * sy);
  int used = 0;
  if (mag > POINTER_DEADZONE) {
    /* slow near the centre for aiming, fast at the rim for crossing the screen */
    const float t = (mag > 1.0f ? 1.0f : mag) - POINTER_DEADZONE;
    const float speed = POINTER_SPEED * (0.25f + 0.75f * t / (1.0f - POINTER_DEADZONE)) *
                        (t / (1.0f - POINTER_DEADZONE)) * dcr_config()->pointer_speed *
                        ((float)g_h / 720.0f);
    float x = g_px + sx / mag * speed * dt, y = g_py - sy / mag * speed * dt; /* stick y is up */
    x = x < 0.0f ? 0.0f : x > (float)(g_w - 1) ? (float)(g_w - 1) : x;
    y = y < 0.0f ? 0.0f : y > (float)(g_h - 1) ? (float)(g_h - 1) : y;
    g_px = x;
    g_py = y;
    used = 1;
  }
  const int pressed = (buttons & POINTER_BUTTONS) != 0;
  if (pressed)
    used = 1;
  g_ppressed = pressed;
  if (used)
    g_plast_used = now;
  /* a finger on the screen puts it away; so do a few idle seconds */
  if (touching && !used)
    g_plast_used = 0;
  g_pshown = g_plast_used && armTicksToNs(now - g_plast_used) < POINTER_HIDE_NS;
}

/* ==================================================================== poll */
void sh_input_init(void) {
  dcr_window_size(&g_w, &g_h);
  g_px = (float)g_w * 0.5f;
  g_py = (float)g_h * 0.5f;
  rt_pad_setup(1, 1);
  rt_pad_slot(&g_pads[0], 0);
  rt_pad_slot(&g_pads[1], RT_PAD_HANDHELD);
  hidInitializeTouchScreen();
  debugPrintf("[input] window %dx%d; the touch screen %s, the stick pointer %s\n", g_w, g_h,
              dcr_config()->touch ? "on" : "off", dcr_config()->pointer ? "on" : "off");
}

void sh_input_poll(void) {
  /* the controller: player 1's and the attached Joy-Cons, as one */
  u64 buttons = 0;
  float sticks[4] = {0};
  for (int i = 0; i < 2; i++) {
    padUpdate(&g_pads[i]);
    if (!padIsConnected(&g_pads[i]))
      continue;
    float s[4];
    buttons |= rt_pad_read(&g_pads[i], s);
    for (int a = 0; a < 4; a += 2)
      if (s[a] * s[a] + s[a + 1] * s[a + 1] > sticks[a] * sticks[a] + sticks[a + 1] * sticks[a + 1])
        sticks[a] = s[a], sticks[a + 1] = s[a + 1];
  }
  poll_keys(buttons);

  Pt now[SH_MAX_POINTERS];
  int n = 0;
  /* the touch screen reports in 1280x720 whatever the rendering size */
  if (dcr_config()->touch) {
    HidTouchScreenState ts = {0};
    if (hidGetTouchScreenStates(&ts, 1))
      for (int i = 0; i < ts.count && n < SH_MAX_POINTERS - 1; i++)
        now[n++] = (Pt){.src = ts.touches[i].finger_id & 0xffffu,
                        .x = (float)ts.touches[i].x * (float)g_w / 1280.0f,
                        .y = (float)ts.touches[i].y * (float)g_h / 720.0f};
  }
  if (dcr_config()->pointer) {
    pointer_poll(buttons, sticks, n > 0);
    if (g_ppressed)
      now[n++] = (Pt){.src = SRC_STICK, .x = g_px, .y = g_py};
  }
  apply(now, n);
}

/* Focus lost: Android ends held touches and keys. */
void sh_input_reset(void) {
  apply(NULL, 0);
  for (unsigned i = 0; i < NKEYS; i++)
    key_set(i, k_keys[i].code, 0);
  for (unsigned i = 0; i < NKEYS_NP; i++)
    key_set(NKEYS + i, k_keys_no_pointer[i].code, 0);
  g_ppressed = 0;
  g_plast_poll = 0;
}

/* ================================================================= drawing */
/* The pointer, over the game's picture: just before eglSwapBuffers, on the
 * app thread with the game's context current (gl_layer.h's present hook).
 *
 * The game draws with OpenGL ES 2 and keeps track of the state it set, so
 * nothing of that may change under it: no program, no buffers, no textures
 * are touched. The pointer is a ring of four bars with a dot in the middle,
 * each a scissored glClear (white, on a black one a little larger); the four
 * things that takes -- the scissor test and box, the clear colour, the
 * colour mask -- are read first and put back. */
#define GL_SCISSOR_TEST 0x0C11
#define GL_SCISSOR_BOX 0x0C10
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_COLOR_WRITEMASK 0x0C23
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_FRAMEBUFFER_BINDING 0x8CA6

static struct {
  void (*GetIntegerv)(unsigned, int *);
  void (*GetFloatv)(unsigned, float *);
  void (*GetBooleanv)(unsigned, unsigned char *);
  unsigned char (*IsEnabled)(unsigned);
  void (*Enable)(unsigned);
  void (*Disable)(unsigned);
  void (*Scissor)(int, int, int, int);
  void (*ClearColor)(float, float, float, float);
  void (*ColorMask)(unsigned char, unsigned char, unsigned char, unsigned char);
  void (*Clear)(unsigned);
} G;
static int g_gl_state; /* 0 not set up, 1 ready, -1 unavailable */

#define L(name) (G.name = (void *)dcr_gl_lookup("gl" #name))
static int gl_setup(void) {
  L(GetIntegerv), L(GetFloatv), L(GetBooleanv), L(IsEnabled), L(Enable), L(Disable), L(Scissor),
      L(ClearColor), L(ColorMask), L(Clear);
  if (!G.GetIntegerv || !G.GetFloatv || !G.GetBooleanv || !G.IsEnabled || !G.Enable || !G.Disable ||
      !G.Scissor || !G.ClearColor || !G.ColorMask || !G.Clear) {
    debugPrintf("[input] the GL driver lacks a basic call: the pointer is not drawn\n");
    return -1;
  }
  return 1;
}

/* A rectangle of the window, centred on cx, cy (pixels from the top left). */
static void bar(float cx, float cy, float w, float h) {
  const int x = (int)(cx - w * 0.5f), y = g_h - (int)(cy + h * 0.5f);
  G.Scissor(x, y, (int)w, (int)h);
  G.Clear(GL_COLOR_BUFFER_BIT);
}

/* The ring's four bars and the dot, each grown by `grow` on every side. */
static void shape(float x, float y, float s, float grow) {
  const float gap = 9.0f * s, len = 11.0f * s, thick = 3.0f * s, g2 = grow * 2.0f;
  bar(x - gap - len * 0.5f, y, len + g2, thick + g2);
  bar(x + gap + len * 0.5f, y, len + g2, thick + g2);
  bar(x, y - gap - len * 0.5f, thick + g2, len + g2);
  bar(x, y + gap + len * 0.5f, thick + g2, len + g2);
  bar(x, y, thick + g2, thick + g2);
}

void sh_pointer_draw(void) {
  if (!g_pshown)
    return;
  if (!g_gl_state)
    g_gl_state = gl_setup();
  if (g_gl_state < 0)
    return;
  int fbo = 0;
  G.GetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  if (fbo)
    return; /* not drawing to the screen right now */
  int box[4] = {0};
  float colour[4] = {0};
  unsigned char mask[4] = {1, 1, 1, 1};
  const unsigned char scissor = G.IsEnabled(GL_SCISSOR_TEST);
  G.GetIntegerv(GL_SCISSOR_BOX, box);
  G.GetFloatv(GL_COLOR_CLEAR_VALUE, colour);
  G.GetBooleanv(GL_COLOR_WRITEMASK, mask);

  const float s = (float)g_h / 720.0f * (g_ppressed ? 0.8f : 1.0f); /* it tightens while held */
  const float x = g_px, y = g_py;
  G.Enable(GL_SCISSOR_TEST);
  G.ColorMask(1, 1, 1, 1);
  G.ClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  shape(x, y, s, 1.5f * s);
  G.ClearColor(1.0f, 1.0f, 1.0f, 1.0f);
  shape(x, y, s, 0.0f);

  G.ColorMask(mask[0], mask[1], mask[2], mask[3]);
  G.ClearColor(colour[0], colour[1], colour[2], colour[3]);
  G.Scissor(box[0], box[1], box[2], box[3]);
  if (!scissor)
    G.Disable(GL_SCISSOR_TEST);
}
