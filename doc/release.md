# Releasing

## Versioning

The version lives in `CMakeLists.txt` (`project(imclash VERSION ...)`) and is
compiled into the executable as a Windows version resource, so
`imclash.exe → Properties → Details` shows it. Bump it there; the resource
template reads `PROJECT_VERSION`.

`0.x.y` means preview: features may change without notice. `1.0.0` is the first
stable release and will be tagged; preview builds are not tagged.

## What a release consists of

The application is a **single file**: `imclash.exe`. The window icon and
version resource are embedded; no assets folder is read at startup and no app
settings file is created. The embedded multi-size icon is `res/imclash.ico`.

| Item | Ships? | Why |
|---|---|---|
| `imclash.exe` | yes | statically linked CRT and dependencies |
| `assets/` (EUI's fonts, icon, shaders) | no | unused; text uses the system UI font |
| notices and licence texts | yes | required by the licences below |
| `mihomo*` | no | the user installs the core separately |
| system `tar.exe` | no | invoked from Windows only when updating the core |

The executable needs no Visual C++ redistributable: the MSVC runtime and all
EUI-NEO dependencies are linked statically. Confirm with
`dumpbin /dependents imclash.exe` — only system DLLs should appear.

## Building a release

```sh
git clone --recursive https://github.com/zixiangro/imclash.git
cd imclash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
cd build && ctest --output-on-failure
```

The dependency is pinned by the `3rd/EUI-NEO` submodule; `git submodule status`
prints the exact commit that was built. Record it in the release notes:

```sh
git submodule status          # e.g. 1f62d33... 3rd/EUI-NEO (v0.6.2)
```

`doc/notes/` and `scripts/` are machine-local and are not part of a release.

## Packaging

`CMakeLists.txt` carries the install rules, so one command produces the release
folder (binary, notices, licence texts):

```sh
cmake --install build --prefix dist/imclash-0.1.0-win64
cd dist && zip -r imclash-0.1.0-win64.zip imclash-0.1.0-win64
```

```text
imclash-0.1.0-win64/
├─ imclash.exe
├─ LICENSE                     (Unlicense, applies to imclash itself)
├─ THIRD-PARTY-NOTICES.md
└─ licenses/
   ├─ EUI-NEO-Apache-2.0.txt
   ├─ GLFW-zlib.txt
   ├─ FreeType-FTL.txt
   ├─ libpng.txt
   ├─ zlib.txt
   ├─ md4c-MIT.txt
   ├─ yyjson-MIT.txt
   └─ tray-MIT.txt
```

Ship the zip, not the bare exe: the licence texts must travel with the binary.

The licence files are copied out of the pinned submodule at install time, so
they always match the code that was built. Adding a component means adding a
row to `3rd/THIRD-PARTY-NOTICES.md` and one `install(FILES ...)` line.

## Verification before publishing

| Check | Command |
|---|---|
| Logic tests | `ctest --output-on-failure` in `build/` passes |
| Version resource | right-click → Properties → Details shows `0.1.0` |
| Icon | the executable and tray show `res/imclash.ico` |
| Runs from an empty directory | copy only `imclash.exe` somewhere and start it |
| Window | normal launch is centered; `--autostart` stays hidden in the tray |
| Dependencies | `dumpbin /dependents imclash.exe` lists system DLLs only |

The subscription and core download paths need a live provider and the user's
own mihomo installation, so they are checked manually against
`doc/features.md`; they are not part of the automated tests.

## Not done yet

- No code signing: Windows SmartScreen warns on first run.
- No CI build: releases are built locally from a tagged or preview commit.
- No bundled mihomo: the application only manages the core the user installs or
  downloads from its own menu.
