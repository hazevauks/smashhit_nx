# smashhit_nx release completion list

The source list for release notes.

Process:
1. Add finished work under "Ready for changelog", one concise, player-facing
   line each.
2. When cutting a release, move the shipped lines into the release notes and
   under the release's heading below.
3. Keep what is not done under "Carry forward".

## Ready for changelog

For 0.1.3. **Not yet tested on hardware.**

- [ ] Gyro aiming points like a laser: where the controller points is a
  place on the screen and stays one, so the aim no longer has to be put back
  in the middle all the time. The pointer stays on the screen while the gyro
  is on (it used to disappear, and the gyro with it, after a few seconds of
  slow aiming), and a shaking hand is smoothed out without losing movement.
- [ ] Turning left and right now means a turn about the vertical, however
  the controller is held: flat, upright or in between (`[controls]
  gyro_space = local` is the old way).
- [ ] Three pointers: the cross, a dot, and a circle with a dot in its
  middle. A click of the left stick (L3) goes to the next one (`[controls]
  pointer_style`).
- [ ] What R3 (gyro on / off) and L3 (the pointer) choose is kept in
  config.ini.
- [ ] The repository's notes are in English.

## Released

### 0.1.0

Tested on hardware, handheld, 720p.

- Smash Hit 1.5.14 (armeabi-v7a) runs from the player's own APK, at 60
  fps, with sound; nothing is extracted from the APK but the engine.
- The touch screen works as on a phone.
- A pointer for playing on a TV: either stick moves it, A / ZR / ZL throw a
  ball where it is.
- Gyro pointing: a click of the right stick turns it on and off, Y puts
  the pointer back in the middle (`[controls] gyro_pointer`, `gyro_speed`,
  `gyro_invert_x`, `gyro_invert_y` in config.ini).
- The game's language follows the console's (`[game] language` in
  config.ini overrides it).
- Nothing online: ads, purchases, Play Games and analytics are off; what
  the game sells is not unlocked.

## Carry forward

- [ ] HOME and sleep: the game is paused and resumed (onPause / onResume);
  not yet seen in a hardware log.
- [ ] Docked 1080p, a Pro Controller and a detached pair of Joy-Cons (the
  pointer's size and speed, the gyro's directions) are untested.
- [ ] What B, +, - and the D-pad do in each of the game's screens.
- [ ] `[game] tv_mode` (the game's Android TV mode) is untested.
- [ ] Other versions of the APK than 1.5.14 are untested.
