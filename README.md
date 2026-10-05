# Oboro

**Stream your own gaming PC to the New Nintendo 3DS.**

Oboro (朧, "hazy moon") is [Kasumi](https://github.com/p0mpurin/Kasumi)'s
interface and player with [Moonlight](https://github.com/zoeyjodon/moonlight-N3DS)'s
streaming underneath: the library, game pages, stream menu, zoom zones, gyro
aiming, remote keyboard and frame pacing of Kasumi, but the games come from
[Sunshine](https://github.com/LizardByte/Sunshine) on your PC instead of
GeForce NOW. It also shows what the PC and the console are doing while you
play.

> **Status: alpha, not yet run on a console.** The code compiles and
> type-checks against the 3DS SDK headers (`tools/typecheck.sh`), but no
> `.3dsx` has been built or tested on hardware yet. Expect the first real
> build to need fixes. See [What is and isn't verified](#what-is-and-isnt-verified).

## Features

- **Your own game library**, not Sunshine's app list: your installed Steam
  games are found automatically, and you add custom games (any `.exe`,
  shortcut, or Epic / GOG / other launcher link) yourself. With Steam's
  cover art, saved on the SD card so it opens instantly; All / Favourites /
  Recent tabs, search, and a page for every game.
- **Pair once with a PIN.** Type the PC's IP address, enter the PIN shown on
  the 3DS in Sunshine's web page. No password is typed on the console.
- **Hardware video decoding** (MVD) at 800x480 in the top screen's
  800-pixel mode, or 400x240, at 30 FPS, with Kasumi's frame pacer.
- **Plays like a controller**: PlayStation-style or letter layout, touch
  L3 / R3 / PS, per-game custom button mapping, optional gyro aiming.
- **Stream menu** (hold START + SELECT): screenshots, controls sheet, zoom
  zones, gyro, sound, disconnect.
- **Remote keyboard and touchpad** for launchers and sign-in screens.
- **Reconnects by itself** after a dropped connection or closing the lid.
  Leaving a stream leaves the game running on the PC, so you can resume it.
- **Live stats on the lower screen**, five pages (tap the panel to turn the
  page):

  | Page | Tiles |
  |---|---|
  | Stream | FPS shown, bitrate (Mbps), ping (ms), packets rebuilt by error correction per second |
  | Network | a verdict naming the likely cause of lag, the last minute as a strip, and a rated bar each for ping, packet loss, frames arriving from the PC, data rate and the console's input upload |
  | PC | CPU %, GPU %, RAM %, GPU temperature |
  | PC, more | the game's own FPS, VRAM %, encode time (ms), frames the PC sent per second |
  | Console | battery %, Wi-Fi bars, Wi-Fi throughput (Mbps), video decode time (ms) |

## Requirements

- A **New** Nintendo 3DS, New 3DS XL or New 2DS XL with custom firmware
  ([Luma3DS](https://3ds.hacks.guide/)). The original 3DS lacks the video
  decoder.
- A PC running [Sunshine](https://github.com/LizardByte/Sunshine) and
  Oboro Host (`host/oboro_host.py`, needs [Python 3](https://www.python.org/)),
  on the same network as the console. To play away from home, see
  [Playing away from home](docs/remote-play.md).
- 2.4 GHz Wi-Fi with a good signal (3 bars is best).

## Install

In FBI on the 3DS, choose **Remote Install > Scan QR Code** and scan this. It
always fetches the newest build of `main`:

![QR code for Oboro.cia](docs/install-qr.png)

Or download `Oboro.cia` (FBI) or `Oboro.3dsx` (Homebrew Launcher) from the
[latest build](https://github.com/U-Shazlee/oboro/releases/tag/latest-build).
If the HOME Menu still shows an old icon after an update, delete the title in
FBI and install it again; settings and pairing stay on the SD card.

## Getting started

1. Install and start Sunshine on the PC. Note the PC's IP address
   (`ipconfig` on Windows).
2. On the PC, run `python host/oboro_host.py --install` once (it then starts
   with Windows), and `python host/oboro_host.py` to start it now.
3. Open Oboro, press **A**, and type the PC's IP address, then Oboro Host's
   12-digit key (`python host/oboro_host.py --key`, or the top of
   `http://localhost:48100` on the PC).
4. Oboro shows a 4-digit PIN. On the PC, open Sunshine's web page
   (`https://localhost:47990`), go to **PIN**, and enter it.
5. Your games load. Press **A** on one to open its page, **A** again to play.
6. During play, hold **START + SELECT** for the stream menu.

To use another PC, open **Settings > Your PC > Paired PC** and forget the
current one.

### Oboro Host: library, launching and PC stats

Sunshine only knows the apps you list in it by hand, and the Moonlight
protocol carries nothing about the PC's CPU or GPU. Both can only be read on
the PC itself, so one small program runs there next to Sunshine:

```
python host/oboro_host.py --install     (once: start it with Windows, add it to the Start menu)
python host/oboro_host.py               (start it now)
```

It listens on port **48100**; allow it through the firewall on your private
network. It does three things:

- **Library.** Installed Steam games are found from Steam's own files, with
  their covers. To add anything else, open **http://localhost:48100** in a
  browser on the PC and enter a name and the path of the game's `.exe` or
  shortcut (or a launcher link such as `com.epicgames.launcher://...`), and
  optionally a cover image. Press **Y** in the 3DS library to refresh.
- **Launching.** The 3DS streams Sunshine's **Desktop** app and Oboro Host
  starts the chosen game on it. "Desktop" in the library streams the desktop
  alone. (Sunshine ships with a Desktop app; keep it.)
- **PC stats.** CPU and RAM always; GPU load, VRAM and temperature on NVIDIA
  cards (through `nvidia-smi`); the game's frame rate on Windows when
  RivaTuner Statistics Server / MSI Afterburner is running. Anything it
  cannot measure shows as `-` on the 3DS.

`python host/oboro_host.py --once` prints the library and one stats sample.

Who can do what: a device that sends Oboro Host's key can read the library
and the stats, and can start a game that is already in the library. Without
the key it gets nothing. Adding or removing games only works from a browser
on the PC itself.

Without Oboro Host the 3DS falls back to Sunshine's own app list, and the PC
tiles stay empty.

## Controls

| In menus | |
|---|---|
| D-Pad / Circle Pad | Move |
| A / B | Select / Back |
| L / R | Library tabs |
| X / Y | Search / Refresh the app list |
| SELECT | Settings |

| In game | |
|---|---|
| Face buttons | PlayStation positions (bottom = Cross); Letters layout in Settings |
| Circle Pad / C-Stick | Left / right stick |
| L R / ZL ZR | L1 R1 / L2 R2 (swappable) |
| Touch screen | L3, R3 and PS; KEYS, POINTER, ZOOM and MENU; tap the stats to turn the page |
| START + SELECT (hold) | Stream menu |

The 3DS appears to the PC as an Xbox 360 controller.

## Tips

- **Choppy picture?** Move closer to the router, or set Bitrate to
  *Steady 1 Mbps*. The limit is the 3DS's Wi-Fi radio, not your PC.
- **Far from the router?** Settings > Network > *Connection type*:
  **Weak / hotspot** (0.8 Mbps, bigger frame reserve).
- **Small text?** Use ZOOM and drag the map; save the spot as a zoom zone.
- **"PC is busy"?** Another app is still running in Sunshine. Oboro offers
  to quit it; unsaved progress in that app is lost.

## Building

The build needs devkitARM, the 3DS portlibs, and two libraries built from
source (OpenSSL for pairing, libexpat). The Docker image has all of it:

```sh
docker build -t oboro-build .
tools/fetch-deps.sh                      # moonlight-common-c into third_party/
docker run --rm -v "$PWD":/oboro -w /oboro oboro-build make       # Oboro.3dsx
docker run --rm -v "$PWD":/oboro -w /oboro oboro-build make cia   # Oboro.cia
```

Without Docker, from devkitPro MSYS (or Linux with devkitPro pacman):

```sh
dkp-pacman -S --needed 3ds-dev 3ds-curl 3ds-mbedtls 3ds-jansson 3ds-libopus 3ds-zlib
tools/fetch-deps.sh
tools/build-deps.sh     # builds OpenSSL and libexpat into portlibs, once
make
```

`.github/workflows/build.yml` does the same on GitHub Actions, uploads
`Oboro.3dsx` and `Oboro.cia` as artifacts, and on `main` publishes them as
the "Latest build" release.

Copy `Oboro.3dsx` to `sdmc:/3ds/` for the Homebrew Launcher, or install
`Oboro.cia` with FBI.

### What is and isn't verified

- **Verified:** `tools/typecheck.sh` compiles every file in `source/` for
  ARM against the real libctru / citro2d / moonlight-common-c headers with no
  errors or warnings, and links them with moonlight-common-c without duplicate
  symbols. `host/test_oboro_host.py` passes on Windows: Steam games and
  covers are found, custom games are added and removed, and launch requests
  are accepted or refused as intended. CPU and RAM readings work.
- **Not verified:** the relay scripts in `relay/` on a real server, a
  devkitARM build and link, the Docker image, the CI
  workflow, and anything at run time on a console: pairing, launching,
  video, audio, input, the stats layout. `libgamestream/client.c` (OpenSSL)
  is not covered by the typecheck. The GPU and game-FPS readings of
  Oboro Host were not exercised, and no real game was launched through it.

### Before a release

- Set `APP_REPOSITORY` in `include/app_paths.h` to your GitHub repository to
  switch the built-in updater on (it is off while it says `CHANGE-ME`).
- The HOME Menu icon, the banner and the seal are Oboro's own. The rest of
  the art in `gfx/` (hero, mist, ensō, lantern) and the banner sound are
  still Kasumi's.

## How it works

`host_client.c` talks to Sunshine through libgamestream (pairing, starting
the stream session) and to Oboro Host over plain HTTP (library, covers,
starting the game). `moon_transport.c` runs the stream with
moonlight-common-c: H.264 access units go to Kasumi's MVD decoder
(`mvd_video.c`), Opus packets to its NDSP output (`audio_output.c`), and the
gamepad, mouse and keyboard go back over Moonlight's control stream. A
network worker thread (`net_worker.c`) owns every blocking request, so the
interface never waits; it also asks Oboro Host for the PC's numbers every
2 s while a game streams.

Everything is stored in `sdmc:/3ds/oboro/`: `host.json` (the PC's address),
`keys/` (this console's pairing key: **private, never share**),
`settings.json`, `library.json`, `games.json`, `history.json`, `zones.json`,
`art/`, `screenshots/` and the diagnostic log `oboro-diagnostic.txt`.

Oboro sends nothing anywhere except to your PC (and to GitHub for updates,
once a repository is configured). Kasumi's opt-in diagnostic reporting is
switched off: no report service is configured, so its settings are hidden.

## Credits

Oboro is a derivative of two GPL-3.0 projects and would not exist without
them:

- [Kasumi](https://github.com/p0mpurin/Kasumi) by p0mpurin: the interface,
  MVD video path, frame pacer, audio output, input handling and most of the
  code in `source/`.
- [Moonlight-N3DS](https://github.com/zoeyjodon/moonlight-N3DS) by zoeyjodon
  (from Moonlight Embedded by Iwan Timmer): `libgamestream/`, the 3DS build
  of OpenSSL and libexpat, and the proof that this streams on a 3DS.
- [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c):
  the Moonlight protocol.

See [THIRD_PARTY.md](THIRD_PARTY.md) for every component and its licence.

Oboro is an unofficial fan project, not affiliated with Nintendo, NVIDIA, the
Moonlight project, LizardByte or the Kasumi project.

## License

[GNU General Public License v3.0](LICENSE). Anything you distribute that is
based on Oboro must stay under the GPL with its source available.
