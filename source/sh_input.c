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
 * held. The controller's motion sensor can move it too (gyro aiming: a
 * click of the right stick turns it on and off), Y puts it back in the
 * middle, and a click of the left stick changes what it looks like (a cross,
 * a dot, a ball). The other buttons go to the game as the keys it knows: the
 * D-pad, X as BUTTON_A, L and R as L1 and R1, B and + as BACK, - as MENU.
 *
 * One thread does it all, between the UI thread's looper turns
 * (sh_activity.c), every 8 ms. MIT.
 */
#include <math.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "gl_layer.h"
#include "rt_cfg.h"
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
static volatile int g_pstyle; /* PTR_* */
static u64 g_plast_used;      /* tick of the last stick movement or press */
static u64 g_plast_poll;

/* What the pointer looks like: [controls] pointer_style, and a click of the
 * left stick goes to the next one. */
enum { PTR_CROSS, PTR_DOT, PTR_BALL, PTR_STYLES };
static const char *const k_style_names[PTR_STYLES] = {"cross", "dot", "ball"};

#define POINTER_BUTTONS (HidNpadButton_A | HidNpadButton_ZR | HidNpadButton_ZL)
#define POINTER_DEADZONE 0.12f
#define POINTER_SPEED 1100.0f   /* pixels a second at full tilt, at 720p */
#define POINTER_HIDE_NS 6000000000ull

static void pointer_clamp(float x, float y) {
  g_px = x < 0.0f ? 0.0f : x > (float)(g_w - 1) ? (float)(g_w - 1) : x;
  g_py = y < 0.0f ? 0.0f : y > (float)(g_h - 1) ? (float)(g_h - 1) : y;
}

/* A choice made with a button is kept: config.ini is written again (on this
 * thread, not the game's). */
static void remember(const char *key, const char *value) {
  if (rt_config_set("controls", key, value) != 0 || rt_config_save() != 0)
    debugPrintf("[input] could not keep %s = %s in config.ini\n", key, value);
}

/* ------------------------------------------------------------------- gyro */
/* The game knows no motion sensor (it imports none of ASensor): the
 * controller's is the port's own way of moving the pointer. The sensors and
 * their frames are those of the runtime's other ports with a pointer or a
 * tilting board (Angry Birds Space, Labyrinth 2) and of ChanseyIsTheBest's
 * nx_pointer, all proven on hardware: the console with its Joy-Cons attached,
 * player 1's Pro Controller (its frame turned half a turn from a Joy-Con's),
 * player 1's pair of Joy-Cons (the right one, else the left).
 *
 * The sensor gives an angular velocity (turns a second) about its own axes:
 * x to the right, y to the top, z out of the face. What moves the pointer:
 *
 *   up and down   the turn about x (tipping the controller up or down)
 *   across        "world": the turn about the vertical, wherever that is in
 *                 the controller's frame -- the accelerometer says (as the
 *                 Labyrinth 2 port reads it). Turning left and right then
 *                 works the same lying flat on a table, held upright like a
 *                 console, or anywhere between.
 *                 "local" ([controls] gyro_space): the turn about y alone,
 *                 as in 0.1.0 -- right only for a controller held upright.
 *
 * As in those ports, it moves the pointer whenever it is on, and the pointer
 * stays on the screen with it: 0.1.0 put the pointer away after six seconds
 * of slow aiming, and the gyro with it. What makes it steady:
 *
 *   - a soft dead zone: below 0.4 degrees a second nothing, full by 2.2 (a
 *     hard one made slow aiming stick, axis by axis);
 *   - smoothing for slow turns only (a hand's tremor), none for fast ones;
 *   - fast turns carry further ([controls] gyro_acceleration), so the whole
 *     screen is a flick away and a slow turn stays precise. */
enum { SIX_HANDHELD, SIX_PRO, SIX_DUAL_LEFT, SIX_DUAL_RIGHT, SIX_COUNT };
static HidSixAxisSensorHandle g_six[SIX_COUNT];
static int g_gyro_ready, g_gyro_on;

#define GYRO_GAIN 10000.0f   /* pixels a second for one turn a second, at 1080p, slowly */
#define GYRO_DEAD0 0.0010f   /* turns a second: nothing below */
#define GYRO_DEAD1 0.0060f   /* ... all of it from here */
#define GYRO_SMOOTH0 0.010f  /* turns a second: smoothed below */
#define GYRO_SMOOTH1 0.050f  /* ... not at all from here */
#define GYRO_SMOOTH_TAU 0.045f
#define GYRO_FAST0 0.06f     /* turns a second: where fast turns begin to carry further */
#define GYRO_FAST1 0.40f     /* ... and where they carry [controls] gyro_acceleration times */
#define GRAVITY_TAU 0.20f    /* seconds: the accelerometer, steadied */

static struct {
  int kind;        /* SIX_* last read, -1 none: a change starts over */
  float up[3];     /* the accelerometer steadied (its sign is the sensor's) */
  float up_sign;   /* +1: it reads the reaction to gravity (up); -1: gravity; 0: not known yet */
  float vote;      /* towards up_sign */
  float sx, sy;    /* the smoothed turn */
  int logged;
} g_gyro = {.kind = -1};

static void gyro_init(void) {
  const Result r0 = hidGetSixAxisSensorHandles(&g_six[SIX_HANDHELD], 1, HidNpadIdType_Handheld,
                                               HidNpadStyleTag_NpadHandheld);
  const Result r1 =
      hidGetSixAxisSensorHandles(&g_six[SIX_PRO], 1, HidNpadIdType_No1, HidNpadStyleTag_NpadFullKey);
  const Result r2 = hidGetSixAxisSensorHandles(&g_six[SIX_DUAL_LEFT], 2, HidNpadIdType_No1,
                                               HidNpadStyleTag_NpadJoyDual);
  if (R_SUCCEEDED(r0) && R_SUCCEEDED(r1) && R_SUCCEEDED(r2)) {
    for (int i = 0; i < SIX_COUNT; i++)
      hidStartSixAxisSensor(g_six[i]);
    g_gyro_ready = 1;
  }
  g_gyro_on = g_gyro_ready && dcr_config()->gyro;
  debugPrintf("[input] motion sensors %s (0x%x 0x%x 0x%x); gyro aiming %s (right stick click: on/off), "
              "%s space, speed %.2f, acceleration %.2f\n",
              g_gyro_ready ? "ready" : "NOT available", (unsigned)r0, (unsigned)r1, (unsigned)r2,
              g_gyro_on ? "on" : "off", dcr_config()->gyro_space ? "local" : "world",
              (double)dcr_config()->gyro_speed, (double)dcr_config()->gyro_accel);
}

/* The sensor of the controller in the player's hands, in a Joy-Con's frame:
 * its angular velocity and its acceleration. 1 when one was read. */
static int gyro_sample(float w[3], float a[3], int *kind) {
  HidSixAxisSensorState s = {0};
  float turn = 1.0f; /* the Pro Controller's frame: half a turn about z */
  int got = 0;
  if (padIsConnected(&g_pads[1])) {
    *kind = SIX_HANDHELD;
    got = hidGetSixAxisSensorStates(g_six[SIX_HANDHELD], &s, 1) > 0;
  } else if (padIsConnected(&g_pads[0])) {
    const u64 style = padGetStyleSet(&g_pads[0]);
    if (style & HidNpadStyleTag_NpadFullKey) {
      turn = -1.0f;
      *kind = SIX_PRO;
      got = hidGetSixAxisSensorStates(g_six[SIX_PRO], &s, 1) > 0;
    } else if (style & HidNpadStyleTag_NpadJoyDual) {
      const u32 attr = padGetAttributes(&g_pads[0]);
      if (attr & HidNpadAttribute_IsRightConnected) {
        *kind = SIX_DUAL_RIGHT;
        got = hidGetSixAxisSensorStates(g_six[SIX_DUAL_RIGHT], &s, 1) > 0;
      } else if (attr & HidNpadAttribute_IsLeftConnected) {
        *kind = SIX_DUAL_LEFT;
        got = hidGetSixAxisSensorStates(g_six[SIX_DUAL_LEFT], &s, 1) > 0;
      }
    }
  }
  if (!got)
    return 0;
  w[0] = s.angular_velocity.x * turn, w[1] = s.angular_velocity.y * turn, w[2] = s.angular_velocity.z;
  a[0] = s.acceleration.x * turn, a[1] = s.acceleration.y * turn, a[2] = s.acceleration.z;
  return 1;
}

static float ramp(float v, float lo, float hi) {
  return v <= lo ? 0.0f : v >= hi ? 1.0f : (v - lo) / (hi - lo);
}

/* The controller's turn as pointer movement (x right, y down), in pixels,
 * over dt seconds: 1 when the sensor was read. *aiming: it was turned on
 * purpose. */
static int gyro_move(float dt, float *dx, float *dy, int *aiming) {
  float w[3], a[3];
  int kind = -1;
  if (!gyro_sample(w, a, &kind))
    return 0;
  if (kind != g_gyro.kind) { /* another controller: nothing carries over */
    g_gyro.kind = kind;
    g_gyro.up[0] = a[0], g_gyro.up[1] = a[1], g_gyro.up[2] = a[2];
    g_gyro.up_sign = g_gyro.vote = 0.0f;
    g_gyro.sx = g_gyro.sy = 0.0f;
    g_gyro.logged = 0;
  }
  /* the vertical: the accelerometer, steadied (a moving hand adds to it) */
  const float k = dt / (GRAVITY_TAU + dt);
  for (int i = 0; i < 3; i++)
    g_gyro.up[i] += (a[i] - g_gyro.up[i]) * k;
  const float ul = sqrtf(g_gyro.up[0] * g_gyro.up[0] + g_gyro.up[1] * g_gyro.up[1] +
                         g_gyro.up[2] * g_gyro.up[2]);
  /* Whether it reads gravity or the reaction to it is the sensor's own
   * convention, and constant: settled once, from half a second of an
   * ordinary hold -- face up, top edge up, or anywhere between, where "up"
   * has no negative y or z. Kept from then on, so it stays right when the
   * controller is later held above the player's head. */
  if (g_gyro.up_sign == 0.0f && ul > 0.5f) {
    const float lean = (g_gyro.up[1] + g_gyro.up[2]) / ul;
    if (fabsf(lean) > 0.5f) {
      g_gyro.vote += (lean > 0.0f ? dt : -dt);
      if (fabsf(g_gyro.vote) >= 0.5f) {
        g_gyro.up_sign = g_gyro.vote > 0.0f ? 1.0f : -1.0f;
        debugPrintf("[input] gyro: controller %d at rest reads %+.2f %+.2f %+.2f g: %s\n", kind,
                    (double)g_gyro.up[0], (double)g_gyro.up[1], (double)g_gyro.up[2],
                    g_gyro.up_sign > 0.0f ? "the reaction to gravity (up)" : "gravity (down)");
      }
    }
  }
  /* across: the turn about the vertical, or about the controller's y */
  float vx = w[1];
  if (!dcr_config()->gyro_space && g_gyro.up_sign != 0.0f && ul > 0.5f)
    vx = g_gyro.up_sign * (w[0] * g_gyro.up[0] + w[1] * g_gyro.up[1] + w[2] * g_gyro.up[2]) / ul;
  /* up and down: tipping up moves the pointer up (hardware, 0.1.0) */
  float vy = -w[0];
  if (dcr_config()->gyro_invert_x)
    vx = -vx;
  if (dcr_config()->gyro_invert_y)
    vy = -vy;
  if (g_gyro.logged < 4 && (fabsf(vx) > 0.05f || fabsf(vy) > 0.05f)) {
    g_gyro.logged++;
    debugPrintf("[input] gyro: turn %+.3f %+.3f %+.3f, up %+.2f %+.2f %+.2f -> across %+.3f, down %+.3f\n",
                (double)w[0], (double)w[1], (double)w[2], (double)g_gyro.up[0], (double)g_gyro.up[1],
                (double)g_gyro.up[2], (double)vx, (double)vy);
  }
  /* slow turns smoothed, fast ones as they are */
  const float mag = sqrtf(vx * vx + vy * vy);
  const float ks = dt / (GYRO_SMOOTH_TAU + dt);
  g_gyro.sx += (vx - g_gyro.sx) * ks;
  g_gyro.sy += (vy - g_gyro.sy) * ks;
  const float direct = ramp(mag, GYRO_SMOOTH0, GYRO_SMOOTH1);
  vx = g_gyro.sx + (vx - g_gyro.sx) * direct;
  vy = g_gyro.sy + (vy - g_gyro.sy) * direct;
  /* the soft dead zone, and what a fast turn adds */
  const float m2 = sqrtf(vx * vx + vy * vy);
  float t = ramp(m2, GYRO_DEAD0, GYRO_DEAD1);
  t = t * t * (3.0f - 2.0f * t);
  const float fast = 1.0f + (dcr_config()->gyro_accel - 1.0f) * ramp(m2, GYRO_FAST0, GYRO_FAST1);
  const float gain = GYRO_GAIN * dcr_config()->gyro_speed * ((float)g_h / 1080.0f) * dt * t * fast;
  *dx = vx * gain;
  *dy = vy * gain;
  *aiming = m2 > GYRO_FAST0;
  return 1;
}

/* `down`: the buttons pressed since the last poll. */
static void pointer_poll(u64 buttons, u64 down, const float sticks[4], int touching) {
  const u64 now = armGetSystemTick();
  float dt = g_plast_poll ? (float)armTicksToNs(now - g_plast_poll) / 1e9f : 0.0f;
  g_plast_poll = now;
  if (dt > 0.05f)
    dt = 0.05f;
  int used = 0;
  /* a click of the right stick: gyro aiming on or off */
  if ((down & HidNpadButton_StickR) && g_gyro_ready) {
    g_gyro_on = !g_gyro_on;
    g_gyro.kind = -1;
    debugPrintf("[input] gyro aiming %s\n", g_gyro_on ? "on" : "off");
    remember("gyro_pointer", g_gyro_on ? "true" : "false");
    used = 1;
  }
  /* a click of the left stick: the next pointer */
  if (down & HidNpadButton_StickL) {
    g_pstyle = (g_pstyle + 1) % PTR_STYLES;
    debugPrintf("[input] pointer: %s\n", k_style_names[g_pstyle]);
    remember("pointer_style", k_style_names[g_pstyle]);
    used = 1;
  }
  /* Y: back to the middle */
  if (down & HidNpadButton_Y) {
    pointer_clamp((float)g_w * 0.5f, (float)g_h * 0.5f);
    used = 1;
  }
  /* the controller's motion: always while it is on, but for a finger on the
   * touch screen (the console is being held to be touched, not turned) */
  if (g_gyro_on && !touching && dt > 0.0f) {
    float dx, dy;
    int aiming = 0;
    if (gyro_move(dt, &dx, &dy, &aiming)) {
      if (dx != 0.0f || dy != 0.0f)
        pointer_clamp(g_px + dx, g_py + dy);
      if (aiming)
        used = 1;
    }
  }
  /* whichever stick is pushed further */
  float sx = sticks[0], sy = sticks[1];
  if (sticks[2] * sticks[2] + sticks[3] * sticks[3] > sx * sx + sy * sy)
    sx = sticks[2], sy = sticks[3];
  const float mag = sqrtf(sx * sx + sy * sy);
  if (mag > POINTER_DEADZONE) {
    /* slow near the centre for aiming, fast at the rim for crossing the screen */
    const float t = (mag > 1.0f ? 1.0f : mag) - POINTER_DEADZONE;
    const float speed = POINTER_SPEED * (0.25f + 0.75f * t / (1.0f - POINTER_DEADZONE)) *
                        (t / (1.0f - POINTER_DEADZONE)) * dcr_config()->pointer_speed *
                        ((float)g_h / 720.0f);
    pointer_clamp(g_px + sx / mag * speed * dt, g_py - sy / mag * speed * dt); /* stick y is up */
    used = 1;
  }
  const int pressed = (buttons & POINTER_BUTTONS) != 0;
  if (pressed)
    used = 1;
  g_ppressed = pressed;
  if (used)
    g_plast_used = now;
  /* Without the gyro a finger on the screen puts it away, and so do a few
   * idle seconds. With it the pointer is the aim: it stays, hidden only
   * while a finger is on the screen. */
  if (touching && !used)
    g_plast_used = 0;
  g_pshown = g_gyro_on ? !touching
                       : g_plast_used && armTicksToNs(now - g_plast_used) < POINTER_HIDE_NS;
}

/* ==================================================================== poll */
void sh_input_init(void) {
  dcr_window_size(&g_w, &g_h);
  g_px = (float)g_w * 0.5f;
  g_py = (float)g_h * 0.5f;
  g_pstyle = dcr_config()->pointer_style % PTR_STYLES;
  rt_pad_setup(1, 1);
  rt_pad_slot(&g_pads[0], 0);
  rt_pad_slot(&g_pads[1], RT_PAD_HANDHELD);
  hidInitializeTouchScreen();
  debugPrintf("[input] window %dx%d; the touch screen %s, the stick pointer %s (%s; left stick click: "
              "the next one)\n",
              g_w, g_h, dcr_config()->touch ? "on" : "off", dcr_config()->pointer ? "on" : "off",
              k_style_names[g_pstyle]);
  if (dcr_config()->pointer)
    gyro_init();
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
  static u64 before;
  const u64 down = buttons & ~before;
  before = buttons;
  if (dcr_config()->pointer) {
    pointer_poll(buttons, down, sticks, n > 0);
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
  g_gyro.kind = -1; /* the sensor's history is not this moment's */
}

/* ================================================================= drawing */
/* The pointer, over the game's picture: just before eglSwapBuffers, on the
 * app thread with the game's context current (gl_layer.h's present hook).
 *
 * The game draws with OpenGL ES 2 and keeps track of the state it set, so
 * nothing of that may change under it: no program, no buffers, no textures
 * are touched. Every pointer is made of scissored glClears -- bars for the
 * cross, a row at a time for the round ones -- in plain colours, light on a
 * dark edge so it shows on any background; the four things that takes (the
 * scissor test and box, the clear colour, the colour mask) are read first
 * and put back.
 *
 *   cross   four bars around a dot
 *   dot     a small disc, for an aim that hides nothing
 *   ball    a shaded sphere of the port's own drawing, the size of a thrown
 *           ball in the distance */
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
  const int iw = w < 1.0f ? 1 : (int)w, ih = h < 1.0f ? 1 : (int)h;
  const int x = (int)(cx - w * 0.5f), y = g_h - (int)(cy + h * 0.5f);
  G.Scissor(x, y, iw, ih);
  G.Clear(GL_COLOR_BUFFER_BIT);
}

static void colour(float r, float g, float b) { G.ClearColor(r, g, b, 1.0f); }

/* A filled circle, a row (two at 1080p) at a time. */
static void disc(float cx, float cy, float r) {
  const float step = g_h >= 1080 ? 2.0f : 1.0f;
  const float y0 = (float)(int)(cy - r);
  for (float y = y0; y < cy + r; y += step) {
    const float d = y + step * 0.5f - cy, half2 = r * r - d * d;
    if (half2 <= 0.0f)
      continue;
    bar(cx, y + step * 0.5f, 2.0f * sqrtf(half2), step);
  }
}

/* The cross's four bars and its dot, each grown by `grow` on every side. */
static void cross(float x, float y, float s, float grow) {
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
  float was[4] = {0};
  unsigned char mask[4] = {1, 1, 1, 1};
  const unsigned char scissor = G.IsEnabled(GL_SCISSOR_TEST);
  G.GetIntegerv(GL_SCISSOR_BOX, box);
  G.GetFloatv(GL_COLOR_CLEAR_VALUE, was);
  G.GetBooleanv(GL_COLOR_WRITEMASK, mask);

  const float s = (float)g_h / 720.0f * (g_ppressed ? 0.8f : 1.0f); /* it tightens while held */
  const float x = g_px, y = g_py;
  G.Enable(GL_SCISSOR_TEST);
  G.ColorMask(1, 1, 1, 1);
  switch (g_pstyle) {
  case PTR_DOT:
    colour(0.0f, 0.0f, 0.0f);
    disc(x, y, 5.5f * s);
    colour(1.0f, 1.0f, 1.0f);
    disc(x, y, 3.5f * s);
    break;
  case PTR_BALL: {
    const float r = 12.0f * s;
    colour(0.05f, 0.06f, 0.08f); /* the edge */
    disc(x, y, r + 1.5f * s);
    colour(0.50f, 0.54f, 0.60f); /* the body, in shade */
    disc(x, y, r);
    colour(0.74f, 0.78f, 0.84f); /* where the light falls */
    disc(x - 0.20f * r, y - 0.22f * r, 0.66f * r);
    colour(1.0f, 1.0f, 1.0f);    /* the glint */
    disc(x - 0.36f * r, y - 0.40f * r, 0.24f * r);
    break;
  }
  default:
    colour(0.0f, 0.0f, 0.0f);
    cross(x, y, s, 1.5f * s);
    colour(1.0f, 1.0f, 1.0f);
    cross(x, y, s, 0.0f);
    break;
  }

  G.ColorMask(mask[0], mask[1], mask[2], mask[3]);
  G.ClearColor(was[0], was[1], was[2], was[3]);
  G.Scissor(box[0], box[1], box[2], box[3]);
  if (!scissor)
    G.Disable(GL_SCISSOR_TEST);
}
