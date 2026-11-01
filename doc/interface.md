# Interface

All coordinates are logical units (96 dpi). The window scales with the monitor
DPI.

## Window

| Property | Value |
|---|---|
| Initial size | 960 x 680 |
| Initial position | Centered on the primary monitor work area |
| Minimum size | 760 x 520 |
| Fonts | System UI font (Segoe UI with the system CJK fallback) |
| Frame | Borderless Win32 hook in `src/window_frame.cpp` |
| Taskbar | Hidden (`WS_EX_TOOLWINDOW`) |
| Window controls | Minimize only; hides to tray |
| Maximize | Disabled |
| Autostart | Silent; parked off-screen and hidden to the tray |

The window can be dragged from the empty title-bar area and resized from its
edges.

## Title bar (36 px)

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ Mihomo  Subscription                       Via proxy: On  Startup: Off   [—] │
└──────────────────────────────────────────────────────────────────────────────┘
```

| Element | Position | Notes |
|---|---|---|
| `Mihomo` menu | x = 8, w = 72 | No dropdown arrow |
| `Subscription` menu | x = 84, w = 100 | No dropdown arrow |
| `Via proxy` chip | right of centre | 104 wide, on/off/unavailable |
| `Startup` chip | right of Via proxy | 104 wide, on/off |
| Minimize | right edge, 36 wide | Whole bar height; hides to tray |

The empty part of the bar is a native drag band. While a menu or dialog is open,
that band is disabled so clicks reach the UI.

## Content

Margins are 16 px. The status bar is 26 px at the bottom.

### Subscription card

| Property | Value |
|---|---|
| Height | 164 |
| Surface | `#212121` with 1 px `#2B2B2B` border |
| Corner radius | 5 |
| Label | `CURRENT SUBSCRIPTION`, 10 px secondary |
| Title | first-line URL, or “Local subscription file” |
| Meta | updated time, node count, file size |
| Actions | `Open config` and `Update now` buttons at the top right |
| Core row | status dot, mihomo version/state, proxy address |
| Core action | `Download mihomo` when no local core and no running core |

The title is clipped on one line. A warning line is shown when the current
`config.yaml` does not look like Clash YAML.

### Nodes

| Property | Value |
|---|---|
| Section header | 24 px |
| Main group line | 18 px below the header, 10.5 px secondary |
| Buttons | `Test all`, `Default`, `Fastest` |
| List card | starts 48 px below the section top; fills the rest, `#212121` panel |
| Header row | 28 px: Name, Type, Latency, Test |
| Node row | 28 px |
| Name | 12 px primary, clipped; white on the current row |
| Type | 11 px secondary |
| Latency | 11 px right-aligned, 60 px wide |
| Test | 10 px secondary, centred in a 58 px column |
| Current node | `#0B3B60` row background, accent dot, white name |
| Scrollbar | 4 px overlay indicator, not draggable |

Latency colours:

| State | Colour |
|---|---|
| < 200 ms | `#4EC9B0` |
| < 500 ms | `#CCA700` |
| >= 500 ms or timeout | `#F14C4C` |
| Untested | secondary |

`Default` and `Fastest` are mutually exclusive segmented buttons. The active
one uses the accent colour.

The `Main group:` line shows the selected main Selector group and its current
selection. Clicking a node row switches every Selector group containing that
node; the `Test` column starts a latency test without switching.

### Empty state

A 560 x 268 centred card:

- “No subscription” title.
- “Add a Clash YAML subscription from a URL, or import a local config file.”
- `Add from URL` and `Import file` buttons.
- When no local core and no running core exist: a warning line and
  `Download mihomo` button.

## Menus

Popup width 220, item height 26, separator 9. Surface `#282828`, 1 px
`#3C3C3C` border, 5 px radius, shadow.

`Mihomo`:

- Start
- Stop
- Restart
- Check for updates...
- Open core folder
- Download mihomo... appears instead of Start/Stop/Restart when no local core
  exists and no core is running
- Running externally is shown disabled for an adopted external core

`Subscription`:

- Add from URL...
- Import file...
- Update now
- Open config file
- Open data folder

Disabled items use secondary colour and have no hover state.

## Dialogs

Full-window 55% black backdrop. Panel width 480, background `#282828`, 1 px
border, 5 px radius, shadow.

| Dialog | Height | Contents |
|---|---|---|
| Add subscription | 260 | URL field, replace warning, Cancel/Add |
| Import subscription file | 300 | file name, optional URL field, replace warning, Cancel/Import |
| Replace current subscription? | 218 | warning, Cancel/Replace |
| mihomo update | 320 | current/latest/asset, update note, Close/Update now |
| Error | 300 | wrapped error text, OK |

Inputs use a `#212121` surface, `#3C3C3C` border, accent focus border, 4 px
radius and 12 px text.

## Tray

- Single left click: native tray menu (`Show`, `Exit`).
- Double-click: restore the main window.
- The tray menu is provided by EUI's Windows tray backend; imclash subclasses
  its message window to add the double-click and delay the single-click menu.
- There is no taskbar button.

## Colours

| Token | Value | Used for |
|---|---|---|
| Title bar | `#181818` | Title bar, status bar |
| Editor background | `#1F1F1F` | Window background |
| Panel | `#212121` | Subscription card, node card, inputs |
| Raised panel | `#282828` | Menus, dialogs |
| Border | `#2B2B2B` | Card outlines, separators |
| Border strong | `#3C3C3C` | Menu outline, input border |
| Text primary | `#CCCCCC` | Labels, node names |
| Text secondary | `#9D9D9D` | Metadata, types |
| Accent | `#0078D4` | Primary buttons, active toggles |
| Accent hover | `#026EC1` | |
| Accent pressed | `#0060A9` | |
| Success | `#4EC9B0` | Fast latency |
| Warning | `#CCA700` | Medium latency, warnings |
| Error | `#F14C4C` | Timeouts, errors |
