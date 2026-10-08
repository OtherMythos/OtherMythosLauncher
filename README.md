# OtherMythos Launcher

A small native launcher for OtherMythos builds. It reads the build indexes CI publishes,
downloads a build once (sha256-checked), and launches it, including when you're offline later.
It's driven by a controller first, so it suits a Steam Deck as well as a desktop.

One file per platform, no installer: Linux x86_64 (glibc 2.34+, SteamOS included) and Windows x64.
It's C++ with SDL3 and Dear ImGui, and sits at 0% CPU when idle.

## Install

| Platform | Do this |
|---|---|
| Linux | Download `OtherMythosLauncher-linux-x86_64.tar.gz` from Releases, extract, run `./OtherMythosLauncher`. |
| Steam Deck | In Desktop mode, extract it into `~/Applications`, then in Steam: *Add a Non-Steam Game* → *Browse* → pick `OtherMythosLauncher`. In Gaming mode it opens fullscreen; pick a project and press A. **Always start it from Steam, in Desktop mode too.** Started any other way, Steam's Desktop controls send the bumpers as Ctrl and Alt, so pressing B while holding L1 + R1 (the panel's combo) is Ctrl + Alt + Esc: KDE's Kill Window, which locks the pointer until a click kills whatever it lands on. |
| Windows | Download `OtherMythosLauncher-windows-x64.exe` and run it. It needs no Visual C++ runtime. |

Downloading needs libcurl on Linux (`libcurl.so.4` or `libcurl-gnutls.so.4`). It's loaded at
runtime and is already on SteamOS and every desktop distro. If it's missing, the launcher still
runs and plays what's installed, and says why it can't download. Windows uses WinHTTP.

## Controls

| Button | Keyboard | Does |
|---|---|---|
| D-pad / left stick | Arrows | Move. Left/right switch between the project list and its builds |
| A | Enter, Space | Open a project, then Play, Download, or Cancel a download |
| B | Escape, Backspace | Back to the project list |
| X | D | Download (on Play: download the newer build) |
| Y | Delete | Delete the focused installed build (asks first) |
| LB / RB | Page Up / Down | Previous / next project |
| View | F5 | Check the indexes for new builds |
| Start | F1 | Menu: refresh, fullscreen, sources and their state, disk use, quit |
| | F11 | Toggle fullscreen |

**Quit** is the button at the top right: tap or click it, or press Up from the top of the project
list, then A. It asks first only while a download is running.

### While a game is running

**Hold LB + RB (L1 + R1) for a second** to open a panel along the right edge, over the game. It
says what's running (title, build, how long you've been playing) and offers **Resume** (or press B)
and **Quit game**. Quit asks the game to close the way a window's close button would: `SIGTERM` on
Linux, `WM_CLOSE` on Windows. If it hasn't closed after 5 seconds, **Force quit** appears, which
ends it straight away. The run record then says "quit from the launcher" (or "force quit").

On Linux, `kill -USR1 <launcher pid>` opens the panel too, which is handy over SSH.

In Steam's Gaming mode the panel is drawn over the game as a gamescope overlay, by a second copy
of the launcher (see Traps). It shows over the game but doesn't take input from it, so the game
also sees the presses that work the panel. Steam's own button → Exit game also works.

When a game exits, the launcher comes back and ignores input until every button is released,
so the press that quit the game can't start it again.

## Sources

`config.json` in the data directory lists the indexes to read. On first run it's created with
just the public host; a fuller one looks like this:

```json
{
    "sources": [
        {"name": "public", "url": "https://builds.othermythos.com/index.json"}
    ],
    "hiddenProjects": [],
    "titles": {"forestAtNight": "The Forest At Night"},
    "fullscreen": "auto"
}
```

| Key | Meaning |
|---|---|
| `sources` | Index urls, in order of preference. A build several sources carry is downloaded from the first that works. `http://`, `https://` and `file://` all work. A private archive goes here, on the machines that can reach it. |
| `hiddenProjects` | Project names to leave out of the list. |
| `titles` | Display names, keyed by project name. |
| `fullscreen` | `auto` (fullscreen under Steam's gaming mode or gamescope, otherwise a 1280×800 window), `on` or `off`. |

`--source <url>` puts a source first for one run, which also works from a Steam shortcut's
launch options. If `config.json` doesn't parse, the launcher says so in the footer and uses the
defaults; it doesn't overwrite your file.

### What an index looks like

A source url is either a root index listing projects, or one project's index:

```json
{"projects": {"forestAtNight": {"index": "forestAtNight/index.json"}}}
```

```json
{"builds": [{"id": "20261007-194830-143694b", "commit": "143694b", "committed": "2026-10-07T19:48:30Z",
  "jobs": {"linux-Release": {"status": "ok", "files": [
    {"name": "TheForestAtNight-Release-143694b.AppImage", "size": 224926200, "sha256": "7fc1…", "path": "…", "executable": true}]}}}]}
```

- **Jobs** are `<platform>-<type>`. Only this machine's platform (`linux`, `windows`) is shown,
  Release first.
- **Usable jobs** are those whose `status` is `ok` (or absent) and that have files. `failed`,
  `pending` and `no result` jobs are skipped.
- **File urls** are `<project dir>/<path>` when the file has a `path` (the private archive's layout,
  `<id>/<job>/<name>`), and `<project dir>/<id>/<name>` when it doesn't (builds.othermythos.com).
- **Safety:** a file name or path that could escape the build's directory, or that Windows can't
  represent, makes the whole job unusable rather than partly downloaded.

## What runs

The launcher works out what to run from the downloaded files:

1. the job's `launch` field, if the index gives one;
2. otherwise the only `.AppImage`;
3. otherwise the only `.exe` (Windows) or executable (Linux) at the top level. If the top level
   holds a single directory, it looks inside that one.

Zips are extracted and then deleted. If none of these picks exactly one file, the download fails
and says so, rather than guessing.

The game starts in its own directory. It inherits the launcher's environment, plus:

| Variable | Value |
|---|---|
| `OTHERMYTHOS_LAUNCHER` | `1` |
| `OTHERMYTHOS_RUN_DIR` | This run's directory. Crash dumps or captures written here sit beside the log. |
| `OTHERMYTHOS_PROJECT`, `OTHERMYTHOS_BUILD` | The project and build id. |

While the game runs, the launcher hides its window, closes its gamepads and waits, using no CPU.
The game's stdout and stderr go to `output.log`; the exit code (or signal) goes to `run.json`.
The detail pane shows the last run, in red if it crashed.

## On disk

| Platform | Data directory |
|---|---|
| Linux | `$XDG_DATA_HOME/OtherMythosLauncher`, else `~/.local/share/OtherMythosLauncher` |
| Windows | `%LOCALAPPDATA%\OtherMythosLauncher` |

| Path | Holds |
|---|---|
| `config.json` | Sources, as above. |
| `cache/<source>/` | The last good copy of each index, so the list is there offline. |
| `projects/<project>/<buildId>/<job>/` | An installed build and its `build.json`. A directory with `build.json` is always complete. |
| `projects/<project>/.incoming-<buildId>-<job>/` | A download in progress. It's renamed into place once every file is verified, and cleared at startup if the launcher was closed mid-download. |
| `runs/<yyyymmdd-hhmmss>-<project>/` | `run.json` and `output.log` for each launch. The newest 20 are kept. |

## Command line

| Flag | Does |
|---|---|
| `--source <url>` | Read this index first, for this run. Repeatable. |
| `--data-dir <path>` | Keep everything here instead. |
| `--offline` | Don't fetch; use cached indexes. |
| `--fullscreen`, `--windowed` | Override `config.json`. |
| `--renderer <name>` | An SDL render driver: `opengl`, `direct3d11`, `software`… |
| `--platform <name>` | List another platform's builds (to preview the Deck's list from Windows, say). |
| `--input <keys>` | Press these once the indexes are loaded: `up down left right a b x y start view lb rb`, comma separated. |
| `--capture <file.bmp>` | Render without a display (dummy video, software renderer), after any `--input`, save the frame and exit. |
| `--capture-size <WxH>` | Size of the capture; default `1280x800`. |
| `--preview-panel <state>` | Draw the in-game panel beside a stand-in game, for `--capture`: `idle`, `quitting` or `force`. |
| `--show-panel` | Open just the in-game panel, for a stand-in game, over whatever is on screen. B closes it. |
| `--panel-for <title> <commit> <committed> <job> <source> <started>` | Internal: the panel's own process under gamescope. It prints `resume`, `quit` or `force` as they're chosen. |

The first line on stderr says which video and render driver SDL picked.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release     # Windows: -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

- **Dependencies:** SDL3, Dear ImGui, cJSON and miniz are fetched at configure time, each pinned
  to a release and its sha256 in `cmake/Dependencies.cmake`.
- **Linux packages:** the build needs the X11/Wayland/udev/dbus headers that SDL compiles
  against; `.github/workflows/build.yml` has the list for Ubuntu 22.04.
- **macOS:** it builds there for development, but nothing is shipped for it.

| Test | Covers |
|---|---|
| `core` | Index parsing for both index formats, merging, offline cache, install from `file://` (AppImage and zip), sha mismatch, source fallback, cancel, zip-slip, entry selection, launching a process and recording the run. |
| `network` | Fetches the public index over HTTPS through WinHTTP or the system libcurl. |
| `uiCapture` | Renders the UI headless against the fixtures. A crash fails it, and it leaves `build/tests/capture.bmp`. |

To check a UI change from any machine:

```sh
build/OtherMythosLauncher --data-dir build/tests/captureData --platform linux --input down,right --capture /tmp/ui.bmp
```

CI (`.github/workflows/build.yml`) builds and tests both platforms on every push and pull
request. It also checks that the Linux binary needs nothing beyond libc and that the Windows exe
needs no Visual C++ runtime. Pushing a `v*` tag publishes both binaries as a GitHub release.

## Traps

| Symptom | Cause | What we do |
|---|---|---|
| ~130 MB and 17 threads on GNOME Wayland | libdecor draws Wayland title bars through GTK when the compositor won't, pulling in GTK, cairo and pango | SDL is built without libdecor and the launcher prefers X11 (XWayland), where the window manager draws the title bar. `SDL_VIDEO_DRIVER=wayland` still overrides. |
| Focusing the Play button started the game | ImGui 1.92's `SetKeyboardFocusHere()` also *activates* the item | Focus moves with `ImGui::SetFocusID` (`focusLastItem()` in `Ui.cpp`). |
| D-pad left from a build row did nothing; down from the last project landed on a build row | ImGui's directional navigation only crosses to targets more beside than above or below, and the shared navigation scope let down fall through into the builds | `Ui::handleButtons` moves between panes itself, and the project list stops at its ends. |
| Scripted input moved two rows for one press | A window the compositor isn't showing (a locked screen) runs at 1 fps, so a key held for two frames hit ImGui's key repeat | `--input` releases each key on the next frame. |
| Windows build: `'_beginthreadex': is not a member of the global namespace` | A header called `Process.h` in the include path shadows the CRT's `<process.h>` on a case-insensitive filesystem | It's `ChildProcess.h`. |
| The exe needed `VCRUNTIME140.dll` | `if(MSVC)` around `CMAKE_MSVC_RUNTIME_LIBRARY` before `project()` is always false | The variable is set unconditionally. |
| A killed AppImage game "crashed (SIGBUS)" | Killing the AppImage's runtime unmounts the image under the running game | To stop a game from outside, signal the game process (the launcher's child), not every process with the AppImage's name. |
| On the Deck in Desktop mode, one press acts twice | Run outside Steam, Steam's Desktop layout turns the Deck's buttons into keys (A Return, B Escape, the d-pad arrows) while SDL also reads the controller | While the Deck's own controller is open, `main.cpp` drops those keys. A USB keyboard's arrows, Enter and Escape are dropped too then. Launched from Steam the app gets a virtual pad and this doesn't apply. |
| An idle launcher used 5% of a Deck core | SDL polls open pads every millisecond (`SDL_AUTO_UPDATE_JOYSTICKS`), and the Deck's stick jitter arrived as raw joystick events that each triggered a redraw | `main.cpp` turns SDL's polling off and calls `SDL_UpdateJoysticks()` itself (16 ms in use, 50 ms idle or in game), and redraws only for `isUserInput()`. Now 0.3%. |
| View + Start didn't open the panel | SDL makes gamepad button events out of joystick button events, and switching those off during play switched the gamepad ones off too. On the Deck, Steam also keeps a long press of ☰ for switching its action sets (the "Action set activated" popup), so Start never arrives while it's held | Only stick, touchpad and sensor events are switched off, the combo is read from button state, and it's LB + RB now. |
| In Gaming mode the panel worked (Down, A quit the game) but never showed | gamescope shows only the focused game's windows. It draws a window marked `GAMESCOPE_EXTERNAL_OVERLAY` over them, but only from its first X server (`:0`, Steam's), and games run on `:1`. SDL's OpenGL renderer also replaces the X window it's given, dropping properties set before it | Under gamescope `GamePanel` runs a copy of the launcher on `:0` with `--panel-for`, which marks its window after creating the renderer and reports choices on stdout. It dies with the launcher. |
| A file that now exists still 404s from builds.othermythos.com | The host's CDN caches 404s | Only fetch files the index lists. |

Closing the launcher doesn't close a game it started.

While a game runs, the launcher keeps the gamepads open and checks their buttons 20 times a
second, to catch LB + RB. That costs about 0.4% of a Steam Deck core. It logs to stderr which pads
it can see and when the panel opens or closes, which is the first thing to check if the combo
does nothing: under Steam it sees Steam's virtual pad, not the Deck's own controller.

## Not done yet

Telemetry, crash upload and flight-capture monitoring are planned. The hooks for them are
`OTHERMYTHOS_RUN_DIR` and the run records, but nothing is uploaded yet.

Also not built:

- launcher self-update;
- resumable downloads;
- downloading new builds automatically (downloads are manual by design for now).
