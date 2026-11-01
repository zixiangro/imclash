# Architecture

imclash is a single Windows executable built on EUI-NEO. Application code lives
in `src/`; EUI-NEO is used through its public headers.

## Source layout

| File | Responsibility |
|---|---|
| `src/app.cpp` | State, all UI composition, menus, dialogs, subscription actions, node tests, startup/tick |
| `src/config.cpp` / `.h` | First-line URL metadata, Clash YAML check, top-level scalars, proxy order, API endpoint |
| `src/fsutil.cpp` / `.h` | UTF-8 filesystem helpers, mihomo directory, temp directory, atomic writes, file times |
| `src/http.cpp` / `.h` | WinHTTP GET/PUT and streaming downloads, direct or named proxy |
| `src/mihomo_process.cpp` / `.h` | Core paths, `mihomo -v`, `mihomo -t`, start/stop, output capture |
| `src/mihomo_api.cpp` / `.h` | `/version`, `/proxies` nodes and groups, `/delay`, group selection, `/configs` reload |
| `src/core_update.cpp` / `.h` | GitHub release check, asset download, zip extraction, executable install |
| `src/shell_actions.cpp` / `.h` | Executable path, open file/folder/editor, startup registry value |
| `src/window_frame.cpp` / `.h` | Borderless window, resize frame, tray subclass, minimize, tick timer |
| `src/utf8.h` | UTF-8/UTF-16 helpers |
| `src/util.cpp` / `.h` | String trim/lower, URL encoding, byte/time formatting, version compare |
| `tests/tests.cpp` | Logic tests for the pure helpers |

`app::compose()` and `app::dslAppConfig()` have external linkage and are called
by EUI's entry point. Everything else in `app.cpp` lives in an anonymous
namespace and reaches state through `state()`.

## State model

```
AppState
├─ configDir / configPath / corePath
├─ configPresent, configInfo, sourceUrl, configBytes, configModified
├─ corePresent / coreRunning / coreOwned / coreStarting / apiReady
├─ coreVersion, coreOutput, startup probe and ping state
├─ proxyAvailable / viaProxy / proxyUserTouched / proxyUserWants
├─ nodes[], nodesLoaded, nodesLoading, nodesError
├─ testingAll, testGeneration, testPending/testDone/testTotal
├─ sortBySpeed, nodeScroll
├─ menu, dialog and their input strings
├─ coreCheck (latest release) and operation busy/status text
└─ node list geometry cached for scrolling
```

Rendering is a pure function of this state. `compose()` rebuilds the tree every
frame; wheel scrolling and clamping refresh the cached list geometry.

## Data flow

```
Add URL / Import / Update now
        │
        ▼
async worker ──► http::get (direct or 127.0.0.1:mixed-port)
        │             │
        │             ├─ config::looksLikeClashYaml
        │             └─ mihomo -t -f <temp> -d <dir>
        ▼
completion on UI thread ──► writeTextAtomic(config.yaml)
        │
        ├─ mihomo running ──► PUT /configs?force=true {path}
        │        └─ reject ──► restore old content, reload old
        ▼
refresh config info, reload nodes, update UI
```

The first line `# url: ...` is read and written by `config.cpp`;
`config.yaml` is the only persistent subscription state.

## Threading

| Thread | Work |
|---|---|
| UI / render | Composition, input, state mutation, mihomo process control |
| EUI worker pool (`async`) | HTTP, downloads, config validation, node delay tests |
| mihomo output reader | Drains mihomo stdout/stderr into a mutex-protected tail |

`async` completion callbacks are delivered on the UI thread. Workers capture
copies of `config::Info` and return plain values; they never touch `AppState`.
The mihomo output reader is joined when an owned process is stopped or polled;
when the application exits while the core keeps running, `detachForExit()`
leaves the pipe to process teardown so shutdown does not block.

## Mihomo process

- Path resolution first looks for a running `mihomo*` process whose parent
  directory contains `config.yaml`; that directory and executable become the
  managed paths. This is how an existing install such as `~/.local/clash` is
  picked up. Without one, paths fall back to `FOLDERID_Profile` +
  `.config\mihomo` and `mihomo.exe`.
- The managed executable is started with `-d <dir>` and, when the config has
  no `external-controller`, `-ext-ctl 127.0.0.1:9090`.
- Closing imclash does not stop the mihomo process it started; only an
  explicit Stop, Restart or core update terminates it.
- A pipe reader keeps the last 8 KB of output for error dialogs.
- `mihomo -t -f <file> -d <dir>` validates candidate configs.
- An already-running API is adopted by a startup probe and is not owned.

## Core update

```
GitHub latest release
  └─ asset: windows-<arch> zip, standard build preferred
      └─ downloadToFile → %TEMP%\imclash\core-...
          └─ tar.exe -xf
              └─ locate mihomo*.exe, run -v
                  └─ stop running core (owned or discovered external)
                      └─ move old aside → install → start core
```

The old executable is restored if the replacement move fails; the core is
restarted if it was running before the update.

## Window and tray

`window_frame.cpp` subclasses the GLFW window proc:

| Message | Purpose |
|---|---|
| `WM_NCCALCSIZE` | Remove the native frame |
| `WM_NCHITTEST` | Resize borders and title-bar drag band |
| `WM_NCLBUTTONDBLCLK` | Do not maximize |
| `WM_SYSCOMMAND` | Reject `SC_MAXIMIZE` |
| `WM_TIMER` | 1 s app tick, tray lookup, startup hide |

The window style keeps `WS_THICKFRAME` and `WS_CAPTION` but removes
`WS_SYSMENU`, `WS_MINIMIZEBOX` and `WS_MAXIMIZEBOX`, so Windows does not draw
caption buttons. `WS_EX_TOOLWINDOW` removes the taskbar button.

EUI's tray backend creates a window of class `TRAY`. imclash subclasses that
window:

- `WM_LBUTTONUP`: start a one-shot timer of the double-click interval.
- Timer fires: pass the original left-up to EUI, which opens `Show`/`Exit`.
- `WM_LBUTTONDBLCLK`: cancel the timer and send the `Show` menu command
  (`WM_COMMAND` id 1000) to the tray window.
- `WM_RBUTTONUP`: cancels a pending left-click timer and passes the event to
  EUI, which opens `Show`/`Exit` immediately.

The minimize control posts `WM_CLOSE`; EUI's close callback hides the window to
the tray and pauses the render loop.

A normal launch calls `centerWindow()`, which centers the window on the primary
monitor work area. `--autostart` first parks it at `-32000,-32000` so EUI's
initial show cannot flash, then hides it to the tray. Restores center it again.
Font paths are left empty so EUI uses the system UI font and its fallback.

## Dependencies

| Dependency | Provided by | Used for |
|---|---|---|
| EUI-NEO | `3rd/EUI-NEO` | Windowing, rendering, DSL, input, async, tray, JSON |
| yyjson | EUI-NEO tree | `/proxies` object iteration |
| WinHTTP | Windows SDK | Networking and downloads |
| Windows Shell / Registry | Windows SDK | File pickers, editor, startup entry |
| system `tar.exe` | Windows 10/11 | Extracting mihomo release zips |
