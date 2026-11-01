# imclash

A small Windows tray application for managing one Clash subscription with a
local [mihomo](https://github.com/MetaCubeX/mihomo) core. Built with C++17 on
[EUI-NEO](https://github.com/sudoevolve/EUI-NEO) in a borderless VS Code "Dark
Modern" window. Version 0.1.0 is a preview; the shipped artifact is a single
`imclash.exe` with no asset files.

The program has no configuration file of its own. The subscription is
`config.yaml` inside mihomo's directory, and the first line carries the update
URL as a comment.

## Features

- Add a subscription from a URL, or import a local Clash YAML file.
- For imported files, set an optional update URL; it is stored as
  `# url: https://...` on the first line of `config.yaml`.
- Update the subscription directly or through mihomo's local mixed-port proxy.
- Start, stop and restart the local mihomo core.
- Hot-reload a changed config through mihomo's external controller.
- Check and update the mihomo executable from GitHub releases. After the
download, the running core is stopped, the executable is replaced, and the core
is started again automatically.
- List the active proxy nodes, run latency tests, and sort by config order
  (`Default`) or measured latency (`Fastest`).
- Click a node to switch every Selector group that contains it to that node;
  the main group and its current selection are shown above the list. A per-row
  `Test` action keeps single-node latency tests separate from switching.
- Optional startup entry; window minimizes to the tray and is kept out of the
  taskbar. Tray double-click restores the window.
- Every action is a left click. There are no context menus, global shortcuts or
  drag operations.

## Data layout

```text
<directory of the running mihomo>\
  mihomo*.exe
  config.yaml
  cache.db / geo*      (mihomo itself)
```

If a mihomo process is already running, imclash uses that process's directory
(for example `%USERPROFILE%\.local\clash`). Otherwise it falls back to
`%USERPROFILE%\.config\mihomo`.

The first line of `config.yaml` can be:

```yaml
# url: https://example.com/sub?token=...
```

For an existing config whose first line is not `# url:`, a clearly labelled
source URL in the first few comment lines (for example `## 链接：https://...`)
is used as the update source. The next update rewrites the file with the normal
first-line form.

The rest is exactly the Clash/Mihomo YAML returned by the provider or imported
from the selected file. No profiles database or app settings file is created.

Temporary downloads go to `%TEMP%\imclash`. The startup toggle is stored in
`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.

## Build

Requires Windows 10/11, CMake 3.14+ and a C++17 compiler. EUI-NEO is a pinned
submodule:

```sh
git clone --recursive https://github.com/zixiangro/imclash.git
cd imclash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The result is `build/imclash.exe`. No assets folder is produced; the tray icon
is embedded in the executable.

## Run

```bat
build\imclash.exe
build\imclash.exe --autostart
```

`--autostart` starts silently in the tray: the window stays off-screen and is
hidden before it can appear. The Startup chip writes or removes the Windows Run
entry. A normal launch centers the window on the primary monitor.

## Menus

| Menu | Items |
|---|---|
| `Mihomo` | Start, Stop, Restart, Check for updates..., Open core folder; or Download mihomo... when no local core exists |
| `Subscription` | Add from URL..., Import file..., Update now, Open config file, Open data folder |

`Via proxy` chooses between direct downloads and mihomo's local mixed-port.
`Startup` toggles the Windows startup entry. The only window control is
minimize; it hides to the tray.

## Subscription format

Only Clash/Mihomo YAML is accepted. The download request uses the
`clash.meta` User-Agent, so providers that serve different formats can return a
Clash profile. Base64 node lists and HTML error pages are rejected.

## Tests

```sh
cd build
ctest --output-on-failure
```

The logic tests cover the first-line URL metadata, YAML/base64 detection,
top-level config parsing, proxy order, version comparison and URL encoding.

## Documentation

| Document | Contents |
|---|---|
| [doc/features.md](doc/features.md) | Exact behaviour of every feature |
| [doc/interface.md](doc/interface.md) | Layout, sizes and colours |
| [doc/architecture.md](doc/architecture.md) | Source map, data flow, threading |
| [doc/build.md](doc/build.md) | Build requirements and commands |
| [doc/release.md](doc/release.md) | Versioning, packaging, licence obligations |

## Scope

One Windows desktop application, one subscription and one local mihomo
installation. It downloads subscriptions and core releases; it does not edit
rules, keep multiple profiles, or collect traffic statistics.

## Licence

imclash is released into the public domain under the [Unlicense](LICENSE).
Third-party notices are in [3rd/THIRD-PARTY-NOTICES.md](3rd/THIRD-PARTY-NOTICES.md).
