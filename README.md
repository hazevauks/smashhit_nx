# smashhit_nx

**Smash Hit for Nintendo Switch** — a port of **version 1.5.14** of the
32-bit Android game, built on the
[android32](https://github.com/aks796/android32) runtime.

The port is a wrapper: it loads the game's own code from your APK and gives
it what it expects from Android. **No game files are included.** You need
your own copy of the game.

> Work in progress: not yet tested on hardware.

## What you need

- A Switch with Atmosphère and [sphaira](https://github.com/ITotalJustice/sphaira)
- Your own APK of **Smash Hit 1.5.14** (`com.mediocre.smashhit`,
  versionCode 1051400, with the `armeabi-v7a` library). The port is made for
  this version only: other versions are untested and may not work.

## Installing

1. Copy `smashhit_nx.nro` to `sd:/switch/smashhit_nx/`.
2. Copy your APK into the same folder (any file name ending in `.apk`).
3. In sphaira: **Homebrew › Smash Hit › Install Forwarder**.
4. Start the new icon on the HOME menu. The first start unpacks the game's
   engine from the APK.

To update, replace the NRO in the folder and start the icon: the port
updates itself.

To remove it, delete `sd:/switch/smashhit_nx/` and the folder under
`atmosphere/contents/` named in `title_id.txt`.

## Controls

The game is played by touching where the ball should go.

| Switch | Game |
| --- | --- |
| Touch screen | as on a phone (handheld) |
| Either stick | moves a pointer over the picture |
| A, ZR, ZL | touch the screen where the pointer is |
| D-pad, X | the game's own D-pad and select keys |
| B, + | back (pause) |
| − | menu |
| L, R | the game's shoulder keys |

## Settings

`sd:/switch/smashhit_nx/config.ini` is written on the first start. Each
option is explained in the file; among them the language (the console's by
default), the rendering resolution and the pointer's speed.

## What is different from Android

- Nothing online: no ads, purchases, leaderboards, achievements or cloud
  saves. The game is told what an offline phone tells it. What the game
  sells or gives for watching an ad is not available.
- Saves are kept in `sd:/switch/smashhit_nx/data/`.

## Reporting a problem

Send `debug.log` (and `crash.log`, if there is one) from the game's folder.

## Building

Every push builds in GitHub Actions (`.github/workflows/build.yml`), in the
runtime's toolchain containers; the NRO is in the run's artifacts. Locally,
with Docker: [libnx32](https://github.com/aks796/libnx32) next to this
folder, [mesa32](https://github.com/aks796/mesa32)'s `lib/` and `include/` in
`portlibs32/`, then `python3 runtime/tools/gen_imports.py`, `./build.sh` and
`launcher/build.sh`.

## Credits

- The game: Mediocre. This port is not affiliated with or endorsed by them.
  "Smash Hit" is Mediocre's name, used only to identify the game.
- [android32](https://github.com/aks796/android32),
  [libnx32](https://github.com/aks796/libnx32) and
  [mesa32](https://github.com/aks796/mesa32) by aks796, and the projects
  they credit (the `.so` loader by TheOfficialFloW and fgsfds, vita2hos by
  xerpi, libnx by switchbrew, Mesa).
- The port: hazevauks.

## License

MIT for the port's own code: see [LICENSE](LICENSE). The game and its name
are not covered by it.
