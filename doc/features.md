# Features

## Subscription model

- imclash manages exactly one subscription: `config.yaml` in the directory of
  the running mihomo, or `%USERPROFILE%\.config\mihomo\` when no mihomo is
  running.
- At startup, a running `mihomo*` process whose parent directory contains
  `config.yaml` determines both the working directory and the managed core
  executable. This lets an existing installation such as `~/.local/clash` be
  used without a setting.
- The update URL is the first line of that file:

```yaml
# url: https://example.com/sub?token=...
```

- A first line of `# http://...` or `# https://...` is also accepted when
  reading. The program writes the `# url:` form.
- When the first line has no URL, a clearly labelled URL in the first few
  comment lines (for example `## 链接：https://...`, `# url: ...`,
  `# subscription: ...`) is used as the update source. The next update writes
  the normal first-line form.
- There is no separate URL setting in the program. Editing the first line of
  `config.yaml` changes the source used by `Update now`.
- The rest of `config.yaml` is stored exactly as downloaded or imported. The
  program adds no profiles file, settings file or cache file.

## Add from URL

`Subscription → Add from URL...` opens a dialog with a URL field.

- The URL must start with `http://` or `https://`.
- The request uses the User-Agent `clash.meta`, follows redirects, and has a
  30-second timeout.
- The body must look like a Clash/Mihomo YAML configuration. Base64 node lists,
  HTML pages and empty responses are rejected.
- When a local `mihomo.exe` is available, the content is validated with
  `mihomo -t -f <temporary file> -d <mihomo directory>` before it replaces the
  current config.
- If a subscription already exists, the dialog explains that the current
  `config.yaml` will be replaced and a second confirmation is shown.
- On success the file is written atomically, the `# url:` line is added, and a
  running mihomo is asked to reload it.

## Import a file

`Subscription → Import file...` opens the system file picker for
`.yaml`, `.yml` and `.txt`.

- The selected content must look like Clash/Mihomo YAML.
- If the file's first line already has an update URL, the import dialog
  prefills it.
- The import dialog has an optional update URL field. The program writes that
  URL as the first line; leaving it empty removes any existing first-line URL.
- The imported file becomes `config.yaml`.
- The same Core validation and replacement confirmation as URL subscriptions
  apply.

## Update

`Subscription → Update now` is enabled only when a first-line URL exists and no
operation is already running.

- The content is downloaded and validated as in “Add from URL”.
- On success the first-line URL, file contents, mtime and size are refreshed.
- If a running mihomo rejects the reload, the previous content is written back
  and reloaded. The error is displayed in a dialog.
- If mihomo is not running, only `config.yaml` changes.

## Downloads through a proxy

The title-bar `Via proxy` chip controls subscription and mihomo downloads.

- Proxy mode uses mihomo's local HTTP/mixed port from the current
  `config.yaml`: `mixed-port` first, then `port`.
- Proxy mode is available only while mihomo is running and a proxy port is
  configured. Otherwise the chip shows `N/A` and is disabled.
- The chip defaults to On whenever a proxy is available, unless the user turned
  it off in this session.
- Off means a direct WinHTTP connection.
- The same setting is used for GitHub release downloads.

## Local mihomo

The program works in the discovered mihomo directory.

- A running `mihomo*` process whose parent contains `config.yaml` supplies the
  directory and the core executable. Without one, the fallback is
  `%USERPROFILE%\.config\mihomo` with `mihomo.exe`.
- At startup, the program probes the external controller from `config.yaml`.
  If it answers, that instance is adopted as an external, non-owned core.
- Otherwise, if the core executable and `config.yaml` exist, the program starts
  its own mihomo.
- If `config.yaml` has no `external-controller`, the program passes
  `-ext-ctl 127.0.0.1:9090`.
- `secret` is read from `config.yaml` and sent as a bearer token to the API.
- imclash leaves the mihomo process it started running when the app exits.
  Stop and Restart are explicit actions while the app is running.
- `Mihomo → Start`, `Stop` and `Restart` operate on the owned instance.
- `Stop` and `Restart` are disabled for an external instance.
- A startup failure or an unexpected exit shows a dialog containing the last
  mihomo output.

### Checking and updating mihomo

`Mihomo → Check for updates...` queries the latest MetaCubeX/mihomo release.

- The current version comes from the managed core executable's `-v` output or
  the running API.
- The Windows archive is selected by architecture (`amd64` or `arm64`),
  preferring the standard build over `compatible` and Go-version variants.
- Checking the GitHub release and downloading the archive both use the current
  `Via proxy` setting.
- The zip is extracted with the system `tar.exe` into `%TEMP%\imclash`.
- The extracted `mihomo*.exe` executable is validated with `-v` before
  installation. Release archives use names such as
  `mihomo-windows-<arch>-<version>.exe`.
- After the download succeeds the replacement sequence is automatic:
  stop the running core (owned or discovered external), replace the executable,
  then start the core again.
- The download has a 30 s network resolve/connect/receive timeout and a 120 s
  UI watchdog. On failure or timeout the running core is left untouched.
- The download happens before the stop so a running core's proxy can be used.
- A failed install restores the previous executable and restarts it if it was
  running before the update.

## Node list

The main area lists the proxy nodes returned by mihomo's `/proxies` API.

- Selector/URLTest/Fallback/LoadBalance/Relay groups, built-in rule targets and
  `DIRECT`/`REJECT` entries are hidden from the node rows.
- Group data is still read. A `Main group:` line above the list shows the main
  Selector group and its current selection. The main group is the first
  Selector whose name contains `节点选择`, then a literal `PROXY`/`Proxy`
  name, then the Selector with the most members.
- `Default` uses the order of the `proxies:` block in `config.yaml`; nodes from
  providers follow in API order.
- `Fastest` sorts known low latencies first; untested and timed-out nodes go
  last.
- The list scrolls with the mouse wheel. The scrollbar is only an indicator.
- Clicking a node row switches every Selector group whose `all` list contains
  that node to the node. The current node in the main group is highlighted.
  Each group receives `PUT /proxies/<group>` with `{"name":"<node>"}`.
- `Test all` requests a latency for every visible node. The per-row `Test`
  action tests one node; it does not switch groups. Row clicks switch.
- Latency tests use mihomo's `/proxies/<name>/delay` API with
  `https://www.gstatic.com/generate_204` and a 5000 ms timeout.
- The list shows the latest delay stored by mihomo at load time.

## Window and tray

- The window is borderless and uses the VS Code Dark Modern palette.
- A normal launch centers the window on the primary monitor work area.
- `--autostart` runs silently in the tray: the window is parked off-screen
  during startup and hidden before it can appear.
- The title bar contains `Mihomo`, `Subscription`, `Via proxy`, `Startup` and a
  single minimize control. `Mihomo` is left of `Subscription`; the toggles are
  on the right. Menu buttons have no dropdown arrows.
- There is no maximize or close control. Maximize is disabled.
- Minimize hides the window to the tray. The window is excluded from the
  taskbar and Alt+Tab.
- A left click on the tray icon opens the tray menu (`Show`, `Exit`). A tray
  double-click restores the main window.
- The tray menu's `Exit` quits imclash and leaves the mihomo process running.
- `Startup` writes or removes the current executable from
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` with `--autostart`.
- The tray's `Show` command, a tray double-click and a second app launch center
  the window when they restore it.
- Launching a second copy restores the existing window and exits the new
  process.

## Input policy

- All UI actions are left clicks.
- There are no context menus, global keyboard shortcuts or drag-to-reorder
  operations. Text input fields still use normal keyboard editing.
- The title bar can be dragged to move the window; the window edges resize it.
