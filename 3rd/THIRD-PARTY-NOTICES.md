# Third-party notices

imclash is distributed as a single executable that statically links the
components below. Their licence texts must ship with every binary release. The
paths are relative to the repository root and point into the pinned EUI-NEO
submodule, so the texts cannot drift from the code that was built.

## Components linked into `imclash.exe`

| Component | Version | Licence | Licence text |
|---|---|---|---|
| [EUI-NEO](https://github.com/sudoevolve/EUI-NEO) | 0.6.2 (`3rd/EUI-NEO` submodule) | Apache-2.0 | `3rd/EUI-NEO/LICENSE` |
| [GLFW](https://www.glfw.org/) | 3.4.0 | zlib/libpng | `3rd/EUI-NEO/3rd/glfw/LICENSE.md` |
| [FreeType](https://freetype.org/) | 2.13.3 | FreeType Project Licence (FTL) | `3rd/EUI-NEO/3rd/freetype/LICENSE.TXT` |
| [libpng](http://www.libpng.org/) | 1.6.43 | libpng licence | `3rd/EUI-NEO/3rd/libpng-1.6.43/LICENSE` |
| [zlib](https://zlib.net/) | 1.3.1 | zlib licence | `3rd/EUI-NEO/3rd/zlib-1.3.1/LICENSE` |
| [md4c](https://github.com/mity/md4c) | bundled with EUI-NEO | MIT | `3rd/EUI-NEO/3rd/md4c/LICENSE.md` |
| [yyjson](https://github.com/ibireme/yyjson) | 0.12.0 | MIT | `3rd/EUI-NEO/3rd/yyjson-0.12.0/LICENSE` |
| [tray](https://github.com/zserge/tray) | bundled with EUI-NEO | MIT | `3rd/EUI-NEO/3rd/tray/LICENSE` |
| [glad](https://glad.dav1d.de/) | 0.1.34 (generated) | generated output; `khrplatform.h` is Khronos MIT-style | `3rd/EUI-NEO/3rd/glad/src/glad.c`, `3rd/EUI-NEO/3rd/glad/include/KHR/khrplatform.h` |
| [stb_image](https://github.com/nothings/stb) | 2.30 | public domain / MIT (dual) | header of `3rd/EUI-NEO/3rd/stb_image.h` |
| [miniaudio](https://miniaud.io/) | 0.11.21 | public domain / MIT-0 (dual) | header of `3rd/EUI-NEO/3rd/miniaudio.h` |

EUI-NEO is used unmodified; only configuration flags in `CMakeLists.txt` differ.

imclash itself, including everything in `src/`, is released into the public
domain under the [Unlicense](../LICENSE); it is not covered by the licences
above.

## Not redistributed

- **Fonts.** imclash ships no font file: the UI uses the system font (Segoe UI,
  with Microsoft YaHei and Segoe UI Emoji as glyph fallbacks). EUI-NEO's font
  assets are not part of a release.
- **EUI-NEO demo assets and its application icon** are not copied next to
  `imclash.exe`.
- **mihomo** is not bundled. The user installs or downloads it separately into
  the directory imclash discovers (or `%USERPROFILE%\.config\mihomo\`).
- **system `tar.exe`** is invoked from Windows for release-archive extraction;
  it is not redistributed.
