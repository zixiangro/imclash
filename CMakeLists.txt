cmake_minimum_required(VERSION 3.14)
project(imclash VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(IMCLASH_BUILD_TESTS "Build the logic tests" ON)

# Keep the vendored EUI-NEO lean.
set(EUI_BUILD_APPS OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_USER_APPS OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_TEST_FIXTURES OFF CACHE BOOL "" FORCE)
set(EUI_ENABLE_MODULES OFF CACHE BOOL "" FORCE)
set(EUI_ENABLE_TRAY ON CACHE BOOL "" FORCE)
set(EUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_FFMPEG_VIDEO_EXAMPLE OFF CACHE BOOL "" FORCE)
set(EUI_BUILD_SHARED OFF CACHE BOOL "" FORCE)

add_subdirectory(3rd/EUI-NEO)

# imclash ships as one executable. EUI's asset folder is not copied; text uses
# the system UI font, and the tray/window icon comes from the embedded resource.
set_property(GLOBAL PROPERTY EUI_NEO_APP_ASSETS_DIR "")
set_property(GLOBAL PROPERTY EUI_NEO_APP_ICON_RESOURCE "")

configure_file(res/imclash.rc.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/imclash.rc" @ONLY)

add_executable(imclash
    src/app.cpp
    src/config.cpp
    src/fsutil.cpp
    src/http.cpp
    src/mihomo_api.cpp
    src/mihomo_process.cpp
    src/core_update.cpp
    src/shell_actions.cpp
    src/util.cpp
    src/window_frame.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/generated/imclash.rc"
)
target_include_directories(imclash PRIVATE
    src
    3rd/EUI-NEO/3rd/yyjson-0.12.0/src
)
target_compile_definitions(imclash PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(imclash PRIVATE
    winhttp
    dwmapi
    shell32
    advapi32
    ole32
    uuid
)
eui_neo_configure_app(imclash)

# Logic tests: no window and no renderer, just the pure helpers.
if(IMCLASH_BUILD_TESTS)
    enable_testing()
    add_executable(imclash_tests
        tests/tests.cpp
        src/config.cpp
        src/fsutil.cpp
        src/util.cpp
    )
    target_include_directories(imclash_tests PRIVATE src)
    target_compile_definitions(imclash_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(imclash_tests PRIVATE shell32 advapi32 ole32 uuid)
    add_test(NAME logic COMMAND imclash_tests)
endif()

# `cmake --install` produces a release-ready folder: the binary plus the licence
# texts that must travel with it (see doc/release.md).
install(TARGETS imclash RUNTIME DESTINATION .)
install(FILES LICENSE 3rd/THIRD-PARTY-NOTICES.md DESTINATION .)
install(FILES 3rd/EUI-NEO/LICENSE
        DESTINATION licenses RENAME EUI-NEO-Apache-2.0.txt)
install(FILES 3rd/EUI-NEO/3rd/glfw/LICENSE.md
        DESTINATION licenses RENAME GLFW-zlib.txt)
install(FILES 3rd/EUI-NEO/3rd/freetype/LICENSE.TXT
        DESTINATION licenses RENAME FreeType-FTL.txt)
install(FILES 3rd/EUI-NEO/3rd/libpng-1.6.43/LICENSE
        DESTINATION licenses RENAME libpng.txt)
install(FILES 3rd/EUI-NEO/3rd/zlib-1.3.1/LICENSE
        DESTINATION licenses RENAME zlib.txt)
install(FILES 3rd/EUI-NEO/3rd/md4c/LICENSE.md
        DESTINATION licenses RENAME md4c-MIT.txt)
install(FILES 3rd/EUI-NEO/3rd/yyjson-0.12.0/LICENSE
        DESTINATION licenses RENAME yyjson-MIT.txt)
install(FILES 3rd/EUI-NEO/3rd/tray/LICENSE
        DESTINATION licenses RENAME tray-MIT.txt)
