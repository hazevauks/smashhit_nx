/* port_config.h -- Smash Hit's settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "Smash Hit"
#define PORT_NAME     "smashhit_nx"
#define PORT_PACKAGE  "com.mediocre.smashhit"
#define PORT_BANNER   "smashhit_nx: Smash Hit (Mediocre's engine on GameActivity, armeabi-v7a)"
/* libsmashhit.so maps 0x247e11 bytes (~2.3 MB): the runtime's 32 MB default
 * region holds it. */

/* The APK, by what is in it (any file name): it holds the engine; of
 * several, the one that is this game at the version the port is made for. */
#define PORT_APK_DESC "Smash Hit 1.5.14 (com.mediocre.smashhit, armeabi-v7a)"
#define PORT_APK_ROLES                                                                     \
  {.what = "the game", .need = (const char *const[]){"lib/armeabi-v7a/libsmashhit.so", NULL}, \
   .package = "com.mediocre.smashhit", .version_code = 1051400, .flags = RT_APK_PACKAGE_BONUS}
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's engine from the APK)"

/* ------------------------------------------------------------------ sound */
/* The engine mixes itself (QiAudioMixer) and plays through OpenSL ES only
 * (QiAudioDeviceOpenSl: an engine, an output mix, one buffer-queue player):
 * the runtime's opensles.c plays that queue through audout. */
#define RT_OPENSLES 1

/* ------------------------------------------------------------------ frames */
/* The engine loads a level's segments inside a frame, and the UI thread's
 * loop cannot see that: a watcher thread boosts the CPU for a frame that has
 * run past 50 ms, until it ends. */
#define RT_BOOST_WATCH_THREAD 1

/* ------------------------------------------------------------------ input */
#define RT_PAD_MAX_PLAYERS 1 /* one player; the game itself only knows touch */

#endif
