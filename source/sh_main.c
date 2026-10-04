/* sh_main.c -- Smash Hit's part of the boot: the runtime's main()
 * (runtime/source/main.c) does the rest -- the log, config.ini, the NRO
 * self-update, the APK found by what it holds, its package checked -- and
 * calls these.
 *
 * The first launch (runtime/source/dcr_setup.c, from the plan below):
 * libsmashhit.so out of lib/armeabi-v7a/ (the engine and the game) and
 * classes.txt (the Java class names its classes*.dex define, which
 * jni_core.c answers FindClass with), made again whenever the APK changes
 * (.setup stamps, keys "libsmashhit.so" and "classes.txt"). The game's
 * assets are read straight out of the APK (sh_assets.c). The bar, in
 * permille of the first launch:
 *     0- 200  (the APK found and checked: the runtime's main())
 *   200- 700  libsmashhit.so unpacked (by bytes written)
 *   700- 950  the Java class list
 *        1000 the game starts
 * MIT.
 */
#include "config.h"
#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "rt_boot.h"
#include "sh.h"
#include "util.h"

static const char *const k_libs[] = {SH_LIB};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .libs_what = "Unpacking the game's engine",
    .apk_requirement = "This port needs Smash Hit (com.mediocre.smashhit) for\n"
                       "32-bit ARM (armeabi-v7a): use the APK of your own copy.",
    .libs_p0 = 200,
    .libs_p1 = 700,
    .classes_p0 = 700,
    .classes_p1 = 950,
};

/* From the APK to the game's first code: libsmashhit.so and classes.txt
 * (again when the APK changed), then the engine loaded, relocated, resolved
 * against the shims and mapped as code. */
int port_load(const char *apk) {
  dcr_setup_from_apk(apk);
  if (sh_load_engine() != 0)
    fatal_error("Could not load the game engine from %s/" SH_LIB ".\n\n"
                "It is unpacked from the APK (lib/armeabi-v7a/) on launch: delete\n" SH_LIB
                " and .setup there to unpack it again. See debug.log.",
                dcr_game_root());
  return 0;
}

/* System.loadLibrary("smashhit"): the library's constructors; then
 * MainActivity, from onCreate to onDestroy. */
void port_run(void) {
  sh_java_init(); /* the glue takes the VM in initializeNativeCode */
  sh_run_constructors();
  sh_activity_run();
}

/* For the error screens. */
const char *port_apk_help(void) {
  return "Copy the APK of your own Smash Hit (com.mediocre.smashhit,\n"
         "armeabi-v7a) into /switch/" PORT_NAME ". Any file name ending in .apk\n"
         "works: the game's code and assets are read from it.";
}
