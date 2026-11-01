# AGENT

This document describes imclash as it stands. Descriptive documentation lives
in `doc/`; machine-local working notes, if any, live in `doc/notes/` and are not
tracked.

## What this project is

imclash is a small Windows tray application that manages exactly one Clash
subscription for a local mihomo installation. It is built in C++17 on
[EUI-NEO](https://github.com/sudoevolve/EUI-NEO), uses a borderless VS Code
"Dark Modern" window, and ships as one `imclash.exe` with no asset files.

The program has no configuration file of its own. All subscription state lives
in mihomo's directory:

```text
<directory of the running mihomo>\
  mihomo*.exe
  config.yaml        subscription content; first line may be "# url: ..."
  cache.db / geo*    owned by mihomo
```

At startup the directory is discovered from a running `mihomo*` process whose
parent contains `config.yaml`; the executable path of that process becomes the
managed core. If no such process exists, `%USERPROFILE%\.config\mihomo` is used
with `mihomo.exe` as the core name. An existing config may carry its source URL
in a labelled banner comment; the next update moves it to the first line.

No app profiles database or settings file exists. Autostart state is stored in
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`; temporary downloads live
in `%TEMP%\imclash`.

## Repository layout

```text
CMakeLists.txt     project definition and EUI-NEO integration
src/               application sources
tests/tests.cpp    logic tests (no window)
res/               Clash_Logo.png source, generated imclash.ico and version resource
doc/               descriptive documentation
3rd/EUI-NEO/       dependency, a pinned git submodule
3rd/THIRD-PARTY-NOTICES.md
build/             build output, NOT tracked
scripts/           local helper scripts, NOT tracked
datas/             local captures and samples, NOT tracked
doc/notes/         author's working notes, NOT tracked
```

## Build and run

Windows 10/11, a C++17 compiler and CMake 3.15+. EUI-NEO is a submodule:

```sh
git clone --recursive https://github.com/zixiangro/imclash.git
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Useful flags are in [doc/build.md](doc/build.md). The result is
`build/imclash.exe`; no assets folder is produced or needed. The tray icon is
embedded, and the MSVC runtime is linked statically.

Tests exercise the non-UI logic:

```sh
cd build && ctest --output-on-failure
```

## Process and data flow

- `src/app.cpp` owns the single `AppState`, all UI composition, menus, dialogs,
  subscription actions, node testing and startup/shutdown.
- `src/config.cpp` parses the first-line URL, top-level ports/secret/
  controller, proxy order and the Clash YAML check. It is pure and tested.
- `src/http.cpp` is WinHTTP: direct or local-proxy GET/PUT, in-memory text and
  file downloads.
- `src/mihomo_process.cpp` locates the mihomo directory, starts/stops
  `mihomo.exe`, captures its output and validates configs with `mihomo -t`.
- `src/mihomo_api.cpp` reads `/proxies`, tests node delays and reloads
  `/configs`.
- `src/core_update.cpp` checks GitHub releases, downloads and extracts the
  Windows zip with the system `tar.exe`, then installs it.
- `src/window_frame.cpp` is the Win32 borderless window hook and the tray
  double-click/single-click adapter.
- `src/fsutil.cpp` and `src/util.cpp` hold filesystem, UTF-8, formatting and
  version helpers.

Rendering is a pure function of state: `compose()` rebuilds the tree every
frame. Network work runs through EUI `async` and mutates state only in the
completion callback on the UI thread.

## Subscription rules

- Exactly one subscription exists: `config.yaml`.
- Adding from a URL or file replaces it after a confirmation.
- The update URL is `# url: https://...` on the first line. A bare
  `# https://...` first line is accepted when reading. There is no in-app URL
  setting beyond the Add/Import dialog; the file is the source of truth.
- Only Clash/Mihomo YAML is accepted. Base64 node lists and HTML error pages are
  rejected.
- Before replacing the config, the content is tested with
  `mihomo -t -f <temp>` when a local core is available.
- If mihomo is running, config reload uses the external controller. A failed
  reload restores the previous content.

## mihomo rules

- The managed core and `config.yaml` live in the discovered working directory:
  the directory of a running `mihomo*` process that contains `config.yaml`, or
  `%USERPROFILE%\.config\mihomo` when no core is running.
- On startup the app first probes the controller (adopting an already running
  instance) and otherwise starts its own core.
- imclash does not stop mihomo when it exits; the process it started keeps
  running. Stop and Restart are explicit in-app actions. The output reader is
  detached during app shutdown so the still-running core cannot block exit.
- The app reads `external-controller`, `secret`, `mixed-port` and `port` from
  `config.yaml`. If there is no external controller, it starts mihomo with
  `-ext-ctl 127.0.0.1:9090`.
- The GitHub release check and archive download both use mihomo's local
  `127.0.0.1:<mixed-port>` when `Via proxy` is on; when unavailable, the chip is
  disabled. It defaults on when a proxy is available.
- Core updates start from the menu. The GitHub asset is matched by Windows
  architecture; the archive is downloaded to `%TEMP%`, extracted with the
  system `tar.exe`, and validated with `-v`. Release archives name the
  executable `mihomo-windows-<arch>-<version>.exe`; match `mihomo*.exe`.
  The replacement sequence is
  automatic: download, stop the running core (owned or discovered external),
  replace the executable, start the core again. A failed install restores the
  previous executable and restarts it when it was running. The download uses a
  30 s network timeout and a 120 s UI watchdog; failure or timeout leaves the
  running core untouched.

## UI rules

- Borderless 36 px title bar. Left: `Mihomo`, then `Subscription` (no dropdown
  arrows). Right: `Via proxy`, `Startup`, then the only window control:
  minimize.
- A normal launch centers the window on the primary monitor work area.
  `--autostart` parks it off-screen during startup and hides it to the tray
  before it can appear. Tray restores and a second launch center it again.
- Fonts are the system UI font (Segoe UI plus the system CJK fallback); no font
  assets are shipped or read.
- There is no maximize or close button. The window is excluded from the taskbar
  (`WS_EX_TOOLWINDOW`); minimizing hides it to the tray. Tray `Show`, a tray
  double-click and a second app launch all restore it. The tray menu itself is
  opened with a left click.
- All actions are left clicks. Do not add context menus, global shortcuts, or
  drag-to-reorder behavior. Text inputs still use the keyboard.
- The main area shows the current subscription and a node list. Nodes are tested
  through mihomo's `/delay` API; `Default` follows config order and `Fastest`
  sorts by measured delay. The per-row `Test` action tests one node. Clicking a
  node row switches every Selector group whose `all` list contains that node and
  updates the main group's current selection; the main group is chosen from the
  `/proxies` groups (name containing `节点选择`, then `PROXY`, then largest).
- Palette and metrics are constants at the top of `src/app.cpp`; reuse them
  instead of adding colour literals.
- Use the existing helpers (`composeTextButton`, `composeToggleChip`,
  `composeClippedText`, `composeMenuButton`) rather than ad-hoc widgets.

## EUI-NEO traps

- `on*()` builder calls force the Hand cursor; `.cursor(...)` must come last.
- A `ui.rect` with no explicit colour is opaque white.
- `async::runOnce` rejects duplicate keys; use `async::restart` when a newer
  request must supersede an older one.
- The tray backend creates a window of class `TRAY` and its own `Show/Exit`
  menu. `window_frame.cpp` subclasses that window to delay the single-click menu
  and turn a double-click into the Show command. Do not modify files under
  `3rd/`.
- Native caption buttons appear if `WS_SYSMENU`/`WS_MINIMIZEBOX` stay set; the
  frame deliberately removes them.
- EUI creates the main GLFW window before `onStart` runs. A second instance
  already owns a window with the same class/title, so the instance check must
  skip windows whose PID equals the current process.

## Dependency rules

- `3rd/EUI-NEO` is a pinned git submodule. Never edit files under `3rd/`; bump
  the submodule commit deliberately.
- yyjson is read directly from the EUI-NEO tree for `/proxies` object iteration;
  other JSON is read with the same library. Do not add another JSON or YAML
  library for the current feature set.
- The system `tar.exe` is used only for mihomo release archives.

## Code conventions

- C++17, no exceptions for control flow.
- Application code lives in global `namespace app` (EUI's entry point); helpers
  live in their own `imclash::` namespaces.
- `app::compose()` and `app::dslAppConfig()` are the only symbols EUI's entry
  point links against.
- State is one anonymous-namespace `AppState` reached through `state()`.
- Strings are UTF-8. Paths built from UTF-8 strings go through
  `std::filesystem::u8path`; Windows APIs get UTF-16 through `imclash::utf8`.
- Four-space indentation, 100-column soft limit, braces on the same line.

## Documentation rules

`doc/*.md` is descriptive and committed; it states only what the code does
today. Do not record history, alternatives or rationale there. Local
investigation notes belong in `doc/notes/` (untracked). `README.md` owns the
user-facing picture; [doc/build.md](doc/build.md) owns commands and
[doc/release.md](doc/release.md) owns packaging and licence obligations.

Update the matching descriptive document whenever behaviour changes.

## Commit hygiene

- `.gitignore` keeps out `build/`, `scripts/`, `datas/` and `doc/notes/`. Do
  not add files from those paths.
- `3rd/EUI-NEO` is tracked as a submodule gitlink only; never add its contents.
  A plain file under `3rd/`, such as the notices, is tracked normally.
- Temporary self-test hooks, debug dumps and absolute paths that point at the
  author's machine must be removed before committing.
- Do not add generated files, captures or downloaded archives to the tree.

## Checklist before finishing

1. The project builds with [doc/build.md](doc/build.md).
2. `ctest` passes.
3. The tray still works: minimize hides, tray double-click shows, tray menu
   Exit works, and no taskbar button appears.
4. No native maximize/close buttons appeared.
5. `git status` shows only intended files; `scripts/`, `datas/` and
   `doc/notes/` stay untracked.
6. Descriptive docs match the new behaviour; `doc/release.md` is updated when
   the version or packaging changes.
