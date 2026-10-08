# Agent guide: OtherMythos Launcher

Downloads OtherMythos CI builds from their indexes and launches them, controller-first.
[README.md](README.md) is the reference for behaviour, flags, data layout and the traps. This
file covers how to work on it.

## Rules

- Don't commit unless asked. The default branch is `master`.
- Keep private hosts and URLs out of the repo. It's public; private sources live in each
  machine's own `config.json`.
- Code style: 4 spaces; `void foo(){` with no space before the brace; `//comment` with no space
  after the slashes, and only where it says *why*. Members are `mName`, constants `kName`, enum
  values `SCREAMING_CASE`. Everything is in `namespace OML{`, indented. Match the file you're in.

## Layout

| Path | What |
|---|---|
| `src/BuildIndex`, `Catalog`, `Installer`, `Archive`, `Runner`, `ChildProcess*`, `Http*`, `Config`, `Paths`, `Sha256`, `SafePath`, `TaskQueue`, `TimeFormat`, `Json` | `launcherCore`: no SDL, no ImGui, all unit-tested. |
| `src/App` | State shared between the UI thread and the two worker threads (index refresh, downloads). The UI reads a copied `AppView`. |
| `src/Ui`, `Theme`, `Capture`, `main.cpp` | ImGui screen, style, scripted input and capture, and the SDL event loop. |
| `tests/` | `launcherTests` (tiny runner in `Test.h`). Fixtures are synthetic, in both index formats. |
| `cmake/Dependencies.cmake` | Pinned SDL3, ImGui, cJSON, miniz. Bump the URL and the sha256 together. |

## Verifying a change

- **Logic:** `ctest --test-dir build --output-on-failure`. Add a case to `tests/` for anything in
  `launcherCore`.
- **UI:** render it, then look at it. `--input` drives the real UI the way a controller would,
  and `--capture` saves the frame:
  ```sh
  build/OtherMythosLauncher --data-dir build/tests/captureData --platform linux --input down,right,down --capture /tmp/ui.bmp
  ```
  A capture after a press shows the focus ring and the footer hints, which say what each button
  would do.
- **Downloads and launching:** point `--data-dir` at a scratch directory whose `config.json` lists
  a `file://` index with a small script as the "game". Check `projects/…/build.json` and
  `runs/…/run.json`, and make sure one A press gives exactly one run.
- **The real window, the GPU and a controller** can't be checked headless. Say so when a change
  touches them and hasn't been tried on a real desktop or a Steam Deck.

## Things that bite

- `ImGui::SetKeyboardFocusHere()` presses buttons. Use `focusLastItem()` in `Ui.cpp`.
- **Event-driven loop:** `main.cpp` blocks in `SDL_WaitEvent`, then draws a few frames per event.
  Anything a background thread changes has to call `App`'s wake callback, or the screen won't
  update.
- **Worker threads:** `TaskQueue` tasks run off the UI thread and may only touch `App` state
  under `mMutex`. Downloads take their own copy of everything they need (`InstallTarget`).
- **Windows:** never name a header after a CRT one (`process.h`, `io.h`, `time.h`…). `src/` is
  on the include path, and Windows filenames ignore case.
