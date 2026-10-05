# smashhit_nx — port notes

A port of **Smash Hit 1.5.14** (com.mediocre.smashhit, versionCode 1051400,
armeabi-v7a) to the Nintendo Switch on the
[android32](https://github.com/aks796/android32) runtime (a submodule at
`runtime/`, commit `50b352c`).

Status: **released (0.1.0); runs on hardware at 60 fps with sound.** It is
built by GitHub Actions (`.github/workflows/build.yml`): the machine it is
developed on has no Docker, Python or compiler.

## The game

| | |
| --- | --- |
| Engine | Mediocre's own ("Qi": `QiRenderer`, `QiAudio`, `QiInput`), with Lua, libpng, libjpeg, Vorbis and libc++ linked in (`libsmashhit.so`, 2.3 MB, Thumb-2) |
| Android entry | Google's `GameActivity` (AGDK) + `android_main` — not `NativeActivity` |
| Graphics | OpenGL ES 2 (69 `gl*` functions) with its own EGL (`Renderer::initContext`) |
| Sound | OpenSL ES only (`QiAudioDeviceOpenSl`: an engine, an output mix, one buffer-queue player) |
| `DT_NEEDED` | libc, libm, libdl, liblog, libandroid, libnativewindow, libEGL, libGLESv2, libOpenSLES — system libraries only |
| Imports | 373; 69 `gl*`, 303 bound, 1 weak one left NULL (`__cxa_thread_atexit_impl`) |
| Symbols | 5,955 exported (named C++) |
| ELF | plain REL relocations, `DT_GNU_HASH` only (no `DT_HASH`), 3 `PT_LOAD`, 11 constructors |
| Other libraries | `libcrashlytics*.so`, `libdatastore_shared_counter.so` — not loaded |
| Assets | 2,421 files under `assets/`, **all stored uncompressed** (hence the `*.mp3` names) |

**Note:** the APK also carries `arm64-v8a`. The community guide sends such
games down the 64-bit route; this port uses the `armeabi-v7a` library on
purpose (the port author's decision).

The runtime had all the imports but 31: 13 are `PASSTHROUGH` in
`tools/imports.cfg`, the rest have shims in `source/sh_libc.c` (FORTIFY's
`_chk` functions, `stdin` / `stdout` / `stderr`, `__register_atfork`,
`__android_log_assert`) and `source/sh_assets.c` (`AAssetManager`).

## Start-up sequence (from the Java, classes2.dex / classes3.dex)

1. `MainActivity.onCreate` → `jniCrashlyticsInit()` (an empty native)
2. `GameActivity.onCreate` → `System.loadLibrary("smashhit")` → constructors
3. `initializeNativeCode(filesDir, obbDir, externalFilesDir, assets, savedState)`
   → `GameActivity_register` (RegisterNatives for the other natives), a pipe
   on the UI thread's looper, the app thread running `android_main`
4. `onStartNative`, `onResumeNative`, `onSurfaceCreatedNative`,
   `onSurfaceChangedNative(fmt, w, h)`, `onWindowFocusChangedNative(true)`
5. per event: `onTouchEventNative(handle, MotionEvent)`,
   `onKeyDownNative` / `onKeyUpNative(handle, KeyEvent)`
6. leaving: `onPauseNative`, `onStopNative`, `onSurfaceDestroyedNative`,
   `terminateNativeCode`

There is no `JNI_OnLoad`. The port's main thread plays the UI thread
(`source/sh_activity.c`); the game runs on the glue's thread
(`android_main` → `Renderer::handleInput` / `render` → `eglSwapBuffers`).

## Reading files

`QiFileInputStream::open` (disassembled): `AAssetManager_open` →
`AAsset_openFileDescriptor(&start, &length)` → `dup` → `fdopen("rb")` →
`close` → `fseek(start)`. So the engine reads its assets straight out of the
APK through a file descriptor. `sh_assets.c` indexes the central directory
once and answers with a descriptor of the APK itself (through the runtime's
`open`, with its APK cache) and the file's place in it. Nothing is extracted.

## The Java channel: `MainActivity.command(String)`

Everything the engine asks the Java goes through
`JavaMessenger::sendCommand` → `command("name arg arg")` →
`CommandHandler.handleCommand`, which answers with text. The whole table (38
commands, plus `isphone`, which the engine asks and the Java does not know)
is in `source/sh_command.c`, answered as a phone that is offline and signed
in to nothing: no store, nothing owned, no ads, no Play Games, remote config
never fetched. **Nothing the game sells is unlocked** (`isproductowned` →
`false`). The free game keeps no progress between runs: its checkpoints
belong to the paid upgrade.

## Input

- Touch: a `MotionEvent` the glue reads through JNI (`getAction`,
  `getPointerId`, `getAxisValue(axis, i)`...); positions are pixels of the
  window. `handleInput` handles DOWN / POINTER_DOWN, UP / POINTER_UP and MOVE.
- Keys `handleInput` knows (its jump table at +0x184):
  `DPAD_UP/DOWN/LEFT/RIGHT` → `QiInput` buttons 6/7/4/5, `DPAD_CENTER` and
  `BUTTON_A` → 8, `L1` / `L2` / `R1` / `R2` → 12/13/14/15, `BACK` → 16,
  `MENU` → 17.
- The engine has no analogue stick: the port draws a pointer (the aim) that
  either stick moves, and A / ZR / ZL touch the screen where it is
  (`source/sh_input.c`). Y puts it back in the middle; a click of the left
  stick changes what it looks like (cross, dot, circle). It is drawn with
  `glScissor` + `glClear` only, touching none of the game's programs, buffers
  or textures.
- `[game] tv_mode` answers `istv` → `true` (the game's Android TV mode):
  experimental, untested.

### Gyro aiming

The game uses no sensor (it imports none of `ASensor`), so the gyro is the
port's own: `hidGetSixAxisSensorHandles` / `hidGetSixAxisSensorStates` for
the console with its Joy-Cons attached, a Pro Controller and a pair of
Joy-Cons. A click of the right stick turns it on and off.

0.1.0 took the reference ports' mapping as it was (the turn about the
sensor's y axis moves the pointer across, the turn about x moves it up and
down) and added two things of its own that made it uncomfortable: the gyro
only worked while the pointer was showing, and the pointer was put away after
six seconds without a button, a stick or a turn faster than 18 degrees a
second — so slow aiming made the aim disappear; and a hard dead zone on each
axis made fine movements stick. The references (the runtime's Angry Birds
Space port, `abs_cursor.c`; ChanseyIsTheBest's `nx_pointer.c`) move the
pointer whenever the gyro is on, keep it on the screen, and use a dead zone a
tenth the size.

Since 0.1.3:

- the pointer stays on the screen while the gyro is on, and the gyro always
  moves it (but while a finger is on the touch screen);
- the aim is where the controller points: the same turn moves it the same
  distance, fast or slow, there and back. A first build of 0.1.3 had fast
  turns carry further and a soft dead zone with speed-dependent smoothing:
  on hardware the aim had to be recentred all the time, because a flick out
  and a slow way back ended somewhere else, and so did the jolt of every
  press of a button. Now nothing depends on the speed of the turn and
  nothing of it is thrown away but a gate under 0.15 degrees a second;
- every reading counts: the sensor keeps its last 17 (200 a second), and
  all the new ones are added up at each poll instead of the newest alone;
- the aim may go a little past the edge of the screen (12% of it), so the
  pointer leaves the edge when the controller is back where it crossed it;
- what is drawn follows the aim over 30 ms, which takes a hand's tremor out
  without losing movement;
- "world" space (`[controls] gyro_space`): left and right is the turn about
  the vertical, found from the accelerometer as the Labyrinth 2 port reads
  it, so it works the same with the controller flat, upright or in between.
  Whether the accelerometer reads gravity or the reaction to it is settled
  from the first half second of an ordinary hold and logged (`[input] gyro:
  controller N at rest reads ...`). `local` is 0.1.0's mapping.

## Findings from hardware runs

- **Run 1 (build 202610041910):** the engine loads, registers
  `GameActivity`'s 21 natives, makes the GLES 2 context (nouveau, Mesa 20.1),
  opens OpenSL (44.1 kHz stereo 16-bit, 4096-byte blocks) and starts reading
  the APK. A crash on the audio thread: `QiAudio::fillBuffer` takes 128 KB of
  stack on entry (`sub sp, #0x20000`) and the thread of the runtime's
  `opensles.c` has 64 KB. Fixed without copying the file:
  `build/rt/opensles.o` is built with `threadCreate` renamed to
  `sh_audio_thread_create` (`source/sh_audio.c`), which gives it 1 MB.
- **Run 2 (build 202610041921):** the stack fix is in (`[audio] the OpenSL
  thread: stack 1024 KB instead of 64 KB`); the game starts, reaches its
  menu, plays sound and closes cleanly on its own `quit` (onPause → onStop →
  surfaceDestroyed → terminateNativeCode, without the 5 s guard). No `[jni]
  unhandled` line: `sh_java.c`'s table covers everything the engine called.
  507 asset opens, 189 "not found": the engine looks for each file in several
  folders in turn (noise, not an error). Long frames only while loading
  (frames 21-71, 270-670 ms).
- **Run 3 (build 202610041936), ~170 s of play:** a steady 60 fps after
  loading (59.3-60.1 in the 10 s reports), sound at 44.1 kHz with 0 underruns
  and 0 failed submits in 139 s, the heap steady at ~310-320 MB, 23 Java
  objects (no leak). The game pauses (`popup_shown type pause`) and sends a
  score (`updateleaderboard`, dropped). The gyro works, but **up and down
  came out inverted** in handheld mode with the Angry Birds Space port's
  signs: turned over in the code, with `gyro_invert_x` / `gyro_invert_y` in
  config.ini for a controller that reads differently.
- **Run 5 (build 202610050149, the first 0.1.3 build), handheld:** the
  accelerometer at rest reads `+0.06 -0.53 -0.88 g` and is taken as gravity
  (pointing down), so the console was held about 30 degrees up from flat; left
  and right in world space came out the right way round. The author still had
  to recentre the aim all the time (see "Gyro aiming"). The game does keep
  files under `data/files/`, in the free version too: `progression.xml`,
  `achievements.xml`, `quicksave.dat` (written), `config.xml`,
  `tutorials.json`, `key.dat`, `xpromomodel.json` (looked for).
- **Run 4 (build 202610050016), ~150 s:** the update from the NRO worked
  (1936 → 0016, restarting by itself) and config.ini got the 2 new options.
  The gyro's up and down are right (confirmed by the port's author). 60 fps,
  0 audio underruns, heap 300-314 MB, 21-23 Java objects; no `unhandled`, no
  unknown command, no GL error. One 247 ms frame on reaching a checkpoint
  (the next stretch loading). The log ends without the leaving sequence and
  without any focus line: closing from the HOME menu freezes the process and
  ends it, as in the other ports — the pause / resume path (`onPauseNative` /
  `onResumeNative`) **has not appeared in any log yet**.
- Program ids seen in other ports on GitHub: 100E, 100F, 1010, 1015, 10D7,
  1F1A. This port's 10E4 collides with none of them (the search only reaches
  indexed public repositories).
- `dlopen(libcrashlytics.so)` fails and the engine goes on (Crashlytics stays
  off).

## To do

- [ ] 0.1.3's gyro on hardware: the direction of "across" in world space
      (the log's `[input] gyro:` lines tell), how the smoothing and the
      acceleration feel, the three pointers
- [ ] The gyro with a Pro Controller and a detached pair of Joy-Cons
- [ ] HOME and sleep: `onPauseNative` / `onResumeNative` in a hardware log
- [ ] Docked 1080p: the pointer's size and speed
- [ ] What `BACK` / `MENU` / the D-pad do in each of the game's screens
- [ ] What the free game writes under `data/` (settings, best distance):
      `dcr_path_traced` logs the first accesses to that folder
- [ ] `[game] tv_mode`

## Releasing (as for dantheman_nx)

- Commit identity: `334693818+hazevauks@users.noreply.github.com` (the
  repository's local config). The history from before the first release was
  rewritten with `git filter-branch --env-filter` (author and committer only;
  the same tree, `7f3a192`); the original is in
  `_refs/smashhit_nx-pre-public.bundle` (git-ignored).
- Checked before publishing: no personal e-mail address or real name in
  tracked files, commit messages or the icon.
- The private repository became `smashhit_nx-private` (remote
  `private-archive`) with the rewritten history; a new public `smashhit_nx`
  has `main`, the tags and the releases (the SD-card zip and the NRO, both
  from the CI build of the release commit).
- The launcher's icon is the game's, supplied by the port's author (256x256,
  no metadata); the README says it is Mediocre's artwork, outside the MIT
  licence.

## Tools (`tools/`)

Perl scripts (from dantheman_nx), in place of binutils and Python:

- `elfinfo.pl <lib.so> [needed|exports|imports|jni|all]`
- `dexinfo.pl <classes.dex> <class regex> [native | code [method regex]]`
- `thumbcalls.pl <lib.so> <symbol regex | @0xADDRESS:BYTES>` — what a Thumb
  function calls; the address form is for functions without a symbol
- `thumbxref.pl <lib.so> <regex>` — who calls a function or an import
  (through the PLT)
- `imports_needed.txt` — the symbols the game imports (names, not game
  content)

The last two scripts were adapted for libraries with `DT_GNU_HASH` only.

## What never goes into the repository

The APK and the folder extracted from it (`smash-hit-*/`) are in
`.gitignore`.
