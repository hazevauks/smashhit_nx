/* dcr_config.c -- Smash Hit's settings: config.ini's options, on the
 * runtime's INI engine (runtime/source/rt_cfg.c).
 *
 * Written whole on the first start, the options a newer build adds appended
 * at the end ("# Added by build ..."), [config] version = 1 last. Never
 * rename or reorder an option once players have it: their config.ini files
 * must read the same. Booleans take true/false, yes/no, on/off, 1/0. Read
 * once at start-up: changes apply the next time the game starts. MIT.
 */
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg = {
    .pointer = 1,
    .pointer_speed = 1.0f,
    .touch = 1,
    .res_w = 1280,
    .res_h = 720,
    .boost = 1,
    .cpu_cores = 3,
    .language = "auto",
    .tv_mode = 0,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }

static const CfgOpt k_opts[] = {
    {"controls", "stick_pointer", "true",
     "A pointer on screen, moved with either stick; A, ZR or ZL touch the screen\n"
     "# where it is (the game is played by touch: this is how to aim on a TV).\n"
     "# The pointer hides while the touch screen is used.",
     CFG_BOOL, NULL, &g_cfg.pointer},
    {"controls", "pointer_speed", "1.0",
     "How fast the pointer moves: 0.25 (slow) to 4.0 (fast).", CFG_FLOAT, NULL,
     &g_cfg.pointer_speed, 0.25f, 4.0f},
    {"touch", "enabled", "true", "The touch screen works as on the phone (handheld mode).", CFG_BOOL,
     NULL, &g_cfg.touch},
    CFG_ROW_RESOLUTION("auto", CFG_HELP_RESOLUTION),
    CFG_ROW_BOOST(CFG_HELP_BOOST, &g_cfg.boost),
    {"performance", "cpu_cores", "3",
     "How many processor cores the game is told the device has (1 to 4): it\n"
     "# sizes its worker threads by it. The game's threads run on three cores.",
     CFG_INT, NULL, &g_cfg.cpu_cores, 1, 4},
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG(CFG_HELP_BOOT_LOG, &g_cfg.boot_log),
    CFG_ROW_LOG_JNI(CFG_HELP_LOG_JNI, &g_cfg.log_jni),
    {"debug", "log_input", "false",
     "Write every key and touch the game receives to debug.log (for bug reports).", CFG_BOOL, NULL,
     &g_cfg.log_input},
    {"debug", "log_commands", "false",
     "Write everything the game asks its Android side to debug.log (for bug\n"
     "# reports; the first time each question is asked is always written).",
     CFG_BOOL, NULL, &g_cfg.log_commands},
    {"game", "language", "auto",
     "The game's language. auto: the console's. Or a code: en, de, es, fr, it,\n"
     "# ja, ko, ru, zh (a language the game does not have plays in English).",
     CFG_TEXT, NULL, g_cfg.language, 0, 0, sizeof g_cfg.language},
    {"game", "tv_mode", "false",
     "Tell the game it runs on a television (Android TV), where it expects a\n"
     "# remote or a gamepad instead of a touch screen. Experimental.",
     CFG_BOOL, NULL, &g_cfg.tv_mode},
    /* [config] version = 1: the engine's row, last (CfgTable.version) */
};

static void apply(void) {
  const RtConfig *rt = rt_config(); /* the resolution: rt_cfg.c sets the window to it */
  g_cfg.res_w = rt->res_w;
  g_cfg.res_h = rt->res_h;
  const int docked = appletGetOperationMode() == AppletOperationMode_Console;
  debugPrintf("[config] %dx%d (%s, %s); stick pointer %s (speed %.2f), touch %s, CPU boost %s, "
              "%d cores, TV mode %s\n",
              g_cfg.res_w, g_cfg.res_h, rt_config_get("display", "resolution"),
              docked ? "docked" : "handheld", g_cfg.pointer ? "on" : "off",
              (double)g_cfg.pointer_speed, g_cfg.touch ? "on" : "off", g_cfg.boost ? "on" : "off",
              g_cfg.cpu_cores, g_cfg.tv_mode ? "on" : "off");
}

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = CFG_COUNT(k_opts),
    .version = 1,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
