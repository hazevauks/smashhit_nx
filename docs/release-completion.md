# smashhit_nx release completion list

The source list for release notes.

Process:
1. Add finished work under "Ready for changelog", one concise, player-facing
   line each.
2. When cutting a release, move the shipped lines into the release notes and
   under the release's heading below.
3. Keep what is not done under "Carry forward".

## Ready for changelog

For the first release (tested on hardware, handheld, 720p):

- [x] Smash Hit 1.5.14 (armeabi-v7a) runs from the player's own APK, at 60
      fps, with sound; nothing is extracted from the APK but the engine.
- [x] The touch screen works as on a phone.
- [x] A pointer for playing on a TV: either stick moves it, A / ZR / ZL touch
      the screen where it is.
- [x] Gyro pointing: a click of the right stick turns it on and off, Y puts
      the pointer back in the middle (`[controls] gyro_pointer`, `gyro_speed`,
      `gyro_invert_x`, `gyro_invert_y` in config.ini).
- [x] The game's language follows the console's (`[game] language` in
      config.ini overrides it).
- [x] Nothing online: ads, purchases, Play Games and analytics are off; what
      the game sells is not unlocked.

## Released

(nothing yet)

## Carry forward

- [ ] HOME and sleep: the game is paused and resumed (onPause / onResume);
      not yet seen in a hardware log.
- [ ] Docked 1080p, a Pro Controller and a detached pair of Joy-Cons (the
      pointer's size and speed, the gyro's directions) are untested.
- [ ] What B, +, - and the D-pad do in each of the game's screens.
- [ ] `[game] tv_mode` (the game's Android TV mode) is untested.
- [ ] The launcher's icon is a placeholder drawn by the port.
- [ ] Other versions of the APK than 1.5.14 are untested.
