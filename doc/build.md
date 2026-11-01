# Build and run

## Requirements

- Windows 10/11.
- A C++17 compiler.
- CMake 3.15 or newer.
- The system `tar.exe` (included with Windows 10/11) for mihomo release
  archives.

EUI-NEO and its bundled dependencies are compiled from
`3rd/EUI-NEO`, so no external package manager is required.

## Dependency

EUI-NEO is a pinned git submodule at `3rd/EUI-NEO`. Clone recursively:

```sh
git clone --recursive https://github.com/zixiangro/imclash.git
```

If the repository was cloned without submodules:

```sh
git submodule update --init --depth 1
```

Do not download a GitHub archive ZIP: it leaves out the submodule.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The executable is `build/imclash.exe`. It needs no assets folder: the
window/tray icon is embedded and text uses the system font. The MSVC runtime is
linked statically, so no Visual C++ redistributable is required. An incremental
rebuild only needs the second command.

## Tests

```sh
cd build
ctest --output-on-failure
```

`imclash_tests.exe` covers the logic that needs no window: first-line URL
metadata, Clash YAML detection, Base64 rejection, top-level config parsing,
proxy order, version comparison, byte/time formatting and percent encoding.
The window and tray are verified by running the application.

Configure with `-DIMCLASH_BUILD_TESTS=OFF` to skip the test target. See
[release.md](release.md) for packaging.

## Run

```bat
build\imclash.exe
build\imclash.exe --autostart
```

`--autostart` is used by the Startup entry and runs silently in the tray: the
window is parked off-screen and hidden before it can appear.

## Repository layout

```text
CMakeLists.txt          project, EUI-NEO integration, install rules
src/                    application sources
tests/                  logic tests
res/                    Clash_Logo.png source, generated imclash.ico and version resource
doc/                    descriptive documentation
3rd/EUI-NEO/            EUI-NEO submodule
3rd/THIRD-PARTY-NOTICES.md
```

`scripts/`, `datas/` and `doc/notes/` are machine-local and are not tracked.

## Install

`cmake --install build --prefix <dir>` installs `imclash.exe`, the licence and
the third-party notices. The program itself reads only mihomo's directory at
runtime. It uses the directory of a running `mihomo*` process first; without
one it falls back to:

```text
%USERPROFILE%\.config\mihomo\
```
