cmake_minimum_required(VERSION 3.16)
project(window VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#=============================================================================
# Options
#=============================================================================

option(WINDOW_BUILD_EXAMPLES "Build example applications" ON)
option(WINDOW_USE_WAYLAND "Use Wayland instead of X11 on Linux" OFF)

# Graphics API options
option(WINDOW_ENABLE_OPENGL "Enable OpenGL support" ON)
option(WINDOW_ENABLE_VULKAN "Enable Vulkan support" ON)
option(WINDOW_ENABLE_D3D11 "Enable Direct3D 11 support (Windows only)" ON)
option(WINDOW_ENABLE_D3D12 "Enable Direct3D 12 support (Windows only)" ON)
option(WINDOW_ENABLE_METAL "Enable Metal support (Apple only)" ON)

# Gamepad API options (Windows only)
option(WINDOW_ENABLE_DINPUT "Use DirectInput for gamepad support instead of XInput (Windows only)" OFF)

# Audio options
option(WINDOW_ENABLE_AUDIO "Enable audio support" ON)
option(WINDOW_ENABLE_OPENAL "Enable OpenAL Soft audio backend (cross-platform)" OFF)

# Audio decoder options (each downloads its respective library via FetchContent)
option(WINDOW_ENABLE_MP3_DECODER "Enable MP3 decoder (minimp3)" OFF)
option(WINDOW_ENABLE_VORBIS_DECODER "Enable OGG Vorbis decoder (stb_vorbis)" OFF)
option(WINDOW_ENABLE_FLAC_DECODER "Enable FLAC decoder (dr_flac)" OFF)

# GUI options
option(WINDOW_ENABLE_GUI "Enable GUI interface support" ON)
option(WINDOW_ENABLE_RENDERER "Build the optional GPU GUI renderer (renderer/, uses the graphics abstraction)" ON)
option(WINDOW_ENABLE_CSS_THEME "Build the CSS-look GUI theme (renderer/css/, style presets only, no graphics backend needed)" ON)
option(WINDOW_ENABLE_SHADER_COMPILER "Build the built-in runtime shader compiler (renderer/shader_compiler/, HLSL->SPIR-V/GLSL/DXBC via glslang+SPIRV-Cross+d3dcompiler; fetched by CMake)" ON)

# Video renderer (renderer/video/). OFF by default: turning it on makes CMake fetch
# and BUILD GStreamer from source (Meson+Ninja), which is heavy and slow.
option(WINDOW_ENABLE_VIDEO "Build the GStreamer video renderer (renderer/video/); fetches+builds GStreamer from source via Meson" OFF)
option(WINDOW_VIDEO_ZERO_COPY "Enable GPU zero-copy frame import (GstGLMemory/GstD3D11Memory -> import_texture); CPU upload fallback otherwise" ON)

# Asset loading/management options (renderer/assets/)
option(WINDOW_ENABLE_ASSETS "Build the asset system (image + 3D model loading & GPU management)" ON)
option(WINDOW_ENABLE_MODEL_LOADER "Enable 3D model loading via Assimp (OBJ/FBX/glTF/...); fetched by CMake" ON)
option(WINDOW_ENABLE_EXR "Enable OpenEXR (.exr) HDR image loading via tinyexr; fetched by CMake" ON)
option(WINDOW_ENABLE_DDS "Enable DDS (.dds) texture loading via tinyddsloader + bcdec; fetched by CMake" ON)
option(WINDOW_ENABLE_ETC2 "Enable ETC2/EAC texture decoding (KTX containers) via detex; fetched by CMake" ON)
option(WINDOW_ENABLE_ASTC "Enable ASTC texture decoding (KTX / .astc) via astc_dec; fetched by CMake" ON)

# Virtual keyboard options
option(WINDOW_ENABLE_VIRTUAL_KEYBOARD "Enable virtual keyboard support" ON)

# Font options
option(WINDOW_ENABLE_FONT "Enable font rendering support" ON)
option(WINDOW_ENABLE_FREETYPE "Enable FreeType2 font backend" OFF)
option(WINDOW_ENABLE_HARFBUZZ "Enable HarfBuzz text shaping" ON)
option(WINDOW_ENABLE_LIBUNIBREAK "Enable libunibreak Unicode line breaking" ON)

# SIMD math acceleration options
option(WINDOW_MATH_SSE "Enable SSE SIMD acceleration for math (x86/x64)" ON)
option(WINDOW_MATH_NEON "Enable NEON SIMD acceleration for math (ARM)" ON)
option(WINDOW_MATH_SVE "Enable SVE SIMD acceleration for math (ARM)" OFF)

#=============================================================================
# FetchContent Configuration (git progress always enabled)
#=============================================================================

include(FetchContent)
set(FETCHCONTENT_QUIET OFF CACHE BOOL "" FORCE)
# Keep fetched deps under <build>/third when UGW is the top-level project, but
# do NOT hijack a parent project's FetchContent base when added via
# add_subdirectory (that would dump deps into the consumer's source tree).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/third" CACHE PATH "" FORCE)
endif()

# Helper function to declare FetchContent with git progress
# Usage: window_fetch_content(name GIT_REPOSITORY url GIT_TAG tag [other args...])
function(window_fetch_content name)
    set(options "")
    set(oneValueArgs GIT_REPOSITORY GIT_TAG)
    set(multiValueArgs "")
    cmake_parse_arguments(FETCH "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    FetchContent_Declare(${name}
        GIT_REPOSITORY ${FETCH_GIT_REPOSITORY}
        GIT_TAG ${FETCH_GIT_TAG}
        GIT_PROGRESS TRUE
        GIT_SHALLOW TRUE
        ${FETCH_UNPARSED_ARGUMENTS}
    )
endfunction()

# Compile-probe: can <src> (a .cpp) be compiled with <dirs> (a ;-list) on the include
# path? Sets <out_var> to TRUE/FALSE. Builds a throwaway static library (no link/main
# needed) via try_compile's project form (CMake 3.16-compatible), and forces a fresh test
# every configure so a re-fetched dependency is re-checked rather than reusing a cached
# result.
function(window_try_compile_cpp out_var src dirs)
    set(_probe "${CMAKE_BINARY_DIR}/compile_probe")
    file(MAKE_DIRECTORY "${_probe}")
    file(WRITE "${_probe}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.16)
project(compile_probe CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_library(compile_probe STATIC \"${src}\")
target_include_directories(compile_probe PRIVATE ${dirs})
")
    unset(_WINDOW_PROBE_RESULT CACHE)   # force a real re-test each configure
    try_compile(_WINDOW_PROBE_RESULT
        "${_probe}/build" "${_probe}" compile_probe
        OUTPUT_VARIABLE _WINDOW_PROBE_LOG)
    set(${out_var} ${_WINDOW_PROBE_RESULT} PARENT_SCOPE)
endfunction()

#=============================================================================
# Platform Detection
#=============================================================================

set(WINDOW_PLATFORM "")
set(WINDOW_SOURCES "")
set(WINDOW_LIBS "")
set(WINDOW_DEFINES "")

if(WIN32)
    if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
        set(WINDOW_PLATFORM "UWP")
        set(WINDOW_SOURCES window/window_uwp.cpp)
        list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_UWP)
    else()
        set(WINDOW_PLATFORM "WIN32")
        set(WINDOW_SOURCES window/window_win32.cpp)
        list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_WIN32)
    endif()
elseif(APPLE)
    if(IOS)
        set(WINDOW_PLATFORM "IOS")
        set(WINDOW_SOURCES window/window_ios.mm)
        list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_IOS)
    else()
        set(WINDOW_PLATFORM "MACOS")
        set(WINDOW_SOURCES window/window_macos.mm)
        list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_MACOS)
    endif()
elseif(ANDROID)
    set(WINDOW_PLATFORM "ANDROID")
    set(WINDOW_SOURCES window/window_android.cpp)
    list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_ANDROID)
elseif(EMSCRIPTEN)
    set(WINDOW_PLATFORM "WASM")
    set(WINDOW_SOURCES window/window_wasm.cpp)
    list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_WASM)
elseif(UNIX)
    if(WINDOW_USE_WAYLAND)
        set(WINDOW_PLATFORM "WAYLAND")
        set(WINDOW_SOURCES window/window_wayland.cpp)
        list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_WAYLAND WINDOW_USE_WAYLAND)
    else()
        set(WINDOW_PLATFORM "X11")
        set(WINDOW_SOURCES window/window_x11.cpp window/ibus_client.cpp)
        list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_X11)
    endif()
endif()

message(STATUS "Window library platform: ${WINDOW_PLATFORM}")

# Add platform-independent source files
list(APPEND WINDOW_SOURCES window/window.cpp api/graphics_config.cpp input/input_keyboard.cpp input/input_mouse.cpp input/input_gamepad.cpp input/input_wheel.cpp)
# Render device/commander abstraction (graphics_api.hpp). The dispatcher
# (api_render.cpp) is always present; each backend lives in its own api_render_*
# TU and self-guards with its WINDOW_SUPPORT_* macro (returning ErrorNotSupported
# when compiled out), so the factory always links regardless of enabled backends.
list(APPEND WINDOW_SOURCES
    api/api_render.cpp          # public factory → dispatches on Backend
    api/api_texture_format.cpp  # TextureFormat memory-layout helpers (block-aware upload/readback)
    api/api_render_opengl.cpp   # OpenGL (+ GLES via EGL, WebGL via WASM)
    api/api_render_vulkan.cpp
    api/api_render_d3d11.cpp
    api/api_render_d3d12.cpp    # + D3D12 Ultimate (WINDOW_SUPPORT_D3D12_ULTIMATE)
)
if(APPLE)
    list(APPEND WINDOW_SOURCES api/api_render_metal.mm)
endif()

# Add device enumeration files (platform-specific)
if(WIN32)
    if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
        list(APPEND WINDOW_SOURCES device/device_uwp.cpp)
    else()
        list(APPEND WINDOW_SOURCES device/device_win32.cpp)
    endif()
elseif(APPLE)
    list(APPEND WINDOW_SOURCES device/device_apple.mm)
elseif(ANDROID)
    list(APPEND WINDOW_SOURCES device/device_android.cpp)
elseif(EMSCRIPTEN)
    list(APPEND WINDOW_SOURCES device/device_wasm.cpp)
elseif(UNIX)
    list(APPEND WINDOW_SOURCES device/device_linux.cpp)
endif()

# Add platform-specific gamepad implementations
if(WIN32)
    if(CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
        # UWP: TODO - add gamepad_uwp.cpp when implemented
    else()
        if(WINDOW_ENABLE_DINPUT)
            # DirectInput - supports more controller types
            list(APPEND WINDOW_SOURCES input/gamepad_dinput.cpp)
            list(APPEND WINDOW_LIBS dinput8 dxguid)
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_DINPUT)
            message(STATUS "Gamepad support: DirectInput")
        else()
            # XInput - better for Xbox controllers (default)
            list(APPEND WINDOW_SOURCES input/gamepad_xinput.cpp)
            list(APPEND WINDOW_LIBS xinput)
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_XINPUT)
            message(STATUS "Gamepad support: XInput")
        endif()
    endif()
elseif(APPLE)
    list(APPEND WINDOW_SOURCES input/gamepad_gccontroller.mm)
    find_library(GAMECONTROLLER_FRAMEWORK GameController)
    if(GAMECONTROLLER_FRAMEWORK)
        list(APPEND WINDOW_LIBS ${GAMECONTROLLER_FRAMEWORK})
        message(STATUS "Gamepad support: GCController")
    endif()
elseif(ANDROID)
    list(APPEND WINDOW_SOURCES input/gamepad_android.cpp)
    message(STATUS "Gamepad support: Android InputDevice")
elseif(EMSCRIPTEN)
    list(APPEND WINDOW_SOURCES input/gamepad_wasm.cpp)
    message(STATUS "Gamepad support: HTML5 Gamepad API")
elseif(UNIX)
    list(APPEND WINDOW_SOURCES input/gamepad_evdev.cpp)
    # libudev for device enumeration (optional)
    find_library(UDEV_LIB udev)
    if(UDEV_LIB)
        list(APPEND WINDOW_LIBS ${UDEV_LIB})
    endif()
    message(STATUS "Gamepad support: evdev")
endif()

# Add platform-specific steering wheel implementations
if(WIN32)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
        # DirectInput for steering wheels (required for force feedback)
        list(APPEND WINDOW_SOURCES input/wheel_dinput.cpp)
        # dinput8 and dxguid may already be linked from gamepad, add if not using DINPUT
        if(NOT WINDOW_ENABLE_DINPUT)
            list(APPEND WINDOW_LIBS dinput8 dxguid)
        endif()
        message(STATUS "Steering wheel support: DirectInput")
    endif()
elseif(APPLE)
    list(APPEND WINDOW_SOURCES input/wheel_gccontroller.mm)
    message(STATUS "Steering wheel support: GCController (stub)")
elseif(ANDROID)
    list(APPEND WINDOW_SOURCES input/wheel_android.cpp)
    message(STATUS "Steering wheel support: Android (stub)")
elseif(EMSCRIPTEN)
    list(APPEND WINDOW_SOURCES input/wheel_wasm.cpp)
    message(STATUS "Steering wheel support: WASM (stub)")
elseif(UNIX)
    list(APPEND WINDOW_SOURCES input/wheel_evdev.cpp)
    message(STATUS "Steering wheel support: evdev (stub)")
endif()

#=============================================================================
# Find Dependencies
#=============================================================================

# Boost (required for INI parsing and geometry)
find_package(Boost QUIET)
if(NOT Boost_FOUND)
    message(STATUS "Boost not found - fetching from GitHub...")
    FetchContent_Declare(
        Boost
        GIT_REPOSITORY https://github.com/boostorg/boost.git
        GIT_TAG boost-1.84.0
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
    )
    set(BOOST_INCLUDE_LIBRARIES geometry property_tree)
    set(BOOST_ENABLE_CMAKE ON)
    FetchContent_MakeAvailable(Boost)
    message(STATUS "Boost fetched: 1.84.0")
    set(BOOST_FETCHED TRUE)
    # Get the Boost include directory for header-only libraries
    FetchContent_GetProperties(Boost SOURCE_DIR BOOST_SOURCE_DIR)
    set(BOOST_INCLUDE_DIR "${BOOST_SOURCE_DIR}/libs")
else()
    message(STATUS "Boost found: ${Boost_VERSION}")
    set(BOOST_FETCHED FALSE)
    set(BOOST_INCLUDE_DIR "${Boost_INCLUDE_DIRS}")
endif()

# FreeType — fetched up-front (before HarfBuzz) so HarfBuzz can be built
# with HB_HAVE_FREETYPE=ON, which is required by font_harfbuzz.cpp's call
# to hb_ft_font_create.
# On Linux, FreeType is the only working font renderer (font_linux.cpp is a
# fontconfig-discovery wrapper, not a renderer), so auto-enable it.
if(WINDOW_PLATFORM STREQUAL "X11" OR WINDOW_PLATFORM STREQUAL "WAYLAND")
    set(WINDOW_ENABLE_FREETYPE ON CACHE BOOL "" FORCE)
endif()

set(FREETYPE_AVAILABLE FALSE)
if(WINDOW_ENABLE_FREETYPE AND WINDOW_ENABLE_FONT)
    find_package(Freetype QUIET)
    if(FREETYPE_FOUND)
        message(STATUS "FreeType: system package")
        set(FREETYPE_AVAILABLE TRUE)
    else()
        message(STATUS "FreeType not found - fetching from GitHub...")

        # zlib + libpng, built from source like the other deps, so FreeType can decode
        # PNG-compressed colour bitmaps (CBDT/sbix emoji such as Noto Color Emoji).
        # No system libpng/zlib dev packages are required.
        set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        window_fetch_content(zlib
            GIT_REPOSITORY https://github.com/madler/zlib.git
            GIT_TAG v1.3.1
        )
        FetchContent_MakeAvailable(zlib)   # → target zlibstatic (+ generated zconf.h)

        # libpng: build a minimal static target straight from its sources (skip its own
        # CMake to avoid find_package(ZLIB) friction — we link the fetched zlibstatic).
        window_fetch_content(libpng
            GIT_REPOSITORY https://github.com/pnggroup/libpng.git
            GIT_TAG v1.6.43
        )
        FetchContent_GetProperties(libpng)
        if(NOT libpng_POPULATED)
            FetchContent_Populate(libpng)
        endif()
        configure_file(${libpng_SOURCE_DIR}/scripts/pnglibconf.h.prebuilt
                       ${libpng_BINARY_DIR}/pnglibconf.h COPYONLY)
        add_library(png_static STATIC
            ${libpng_SOURCE_DIR}/png.c       ${libpng_SOURCE_DIR}/pngerror.c
            ${libpng_SOURCE_DIR}/pngget.c    ${libpng_SOURCE_DIR}/pngmem.c
            ${libpng_SOURCE_DIR}/pngpread.c  ${libpng_SOURCE_DIR}/pngread.c
            ${libpng_SOURCE_DIR}/pngrio.c    ${libpng_SOURCE_DIR}/pngrtran.c
            ${libpng_SOURCE_DIR}/pngrutil.c  ${libpng_SOURCE_DIR}/pngset.c
            ${libpng_SOURCE_DIR}/pngtrans.c  ${libpng_SOURCE_DIR}/pngwio.c
            ${libpng_SOURCE_DIR}/pngwrite.c  ${libpng_SOURCE_DIR}/pngwtran.c
            ${libpng_SOURCE_DIR}/pngwutil.c
        )
        target_include_directories(png_static PUBLIC
            ${libpng_SOURCE_DIR} ${libpng_BINARY_DIR}
            ${zlib_SOURCE_DIR}   ${zlib_BINARY_DIR}
        )
        target_link_libraries(png_static PUBLIC zlibstatic)
        # The prebuilt config has no hardware-accel sources, so disable those paths.
        target_compile_definitions(png_static PRIVATE
            PNG_ARM_NEON_OPT=0 PNG_INTEL_SSE_OPT=0 PNG_POWERPC_VSX_OPT=0 PNG_MIPS_MSA_OPT=0)

        # FreeType: keep FT_DISABLE_PNG ON so its CMake skips find_package(PNG); we wire
        # PNG support onto the target ourselves, linking the libpng we just built.
        set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
        window_fetch_content(freetype
            GIT_REPOSITORY https://github.com/freetype/freetype.git
            GIT_TAG VER-2-13-3
        )
        FetchContent_MakeAvailable(freetype)
        # Enable PNG colour-bitmap decoding in the fetched FreeType + link our libpng.
        # png_static is build-tree-only ($<BUILD_INTERFACE:>) so it stays out of
        # FreeType's (and window's) exported interface — install(EXPORT) would
        # otherwise fail; same treatment as the glslang/SPIRV-Cross static libs above.
        target_compile_definitions(freetype PRIVATE FT_CONFIG_OPTION_USE_PNG)
        target_include_directories(freetype PRIVATE
            ${libpng_SOURCE_DIR} ${libpng_BINARY_DIR}
            ${zlib_SOURCE_DIR}   ${zlib_BINARY_DIR})
        target_link_libraries(freetype PRIVATE $<BUILD_INTERFACE:png_static>)
        message(STATUS "FreeType fetched: 2.13.3 (+ libpng 1.6.43 / zlib 1.3.1 for colour emoji)")
        set(FREETYPE_AVAILABLE TRUE)
        set(FREETYPE_FETCHED TRUE)
    endif()
endif()

# HarfBuzz (text shaping)
if(WINDOW_ENABLE_HARFBUZZ AND WINDOW_ENABLE_FONT)
    find_package(harfbuzz QUIET)
    if(NOT harfbuzz_FOUND)
        message(STATUS "HarfBuzz not found - fetching from GitHub...")
        set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
        if(FREETYPE_AVAILABLE)
            set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
        else()
            set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
        endif()
        set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        window_fetch_content(harfbuzz
            GIT_REPOSITORY https://github.com/harfbuzz/harfbuzz.git
            GIT_TAG 9.0.0
        )
        FetchContent_MakeAvailable(harfbuzz)
        message(STATUS "HarfBuzz fetched: 9.0.0 (FreeType integration: ${FREETYPE_AVAILABLE})")
    else()
        message(STATUS "HarfBuzz found")
    endif()
    set(HARFBUZZ_AVAILABLE TRUE)
endif()

# libunibreak (Unicode line breaking, UAX #14)
if(WINDOW_ENABLE_LIBUNIBREAK AND WINDOW_ENABLE_FONT)
    message(STATUS "libunibreak - fetching from GitHub...")
    window_fetch_content(unibreak
        GIT_REPOSITORY https://github.com/adah1972/libunibreak.git
        GIT_TAG libunibreak_7_0
    )
    FetchContent_GetProperties(unibreak)
    if(NOT unibreak_POPULATED)
        FetchContent_Populate(unibreak)
    endif()
    # libunibreak ships autotools/Meson; build a minimal CMake static target.
    # Data tables are committed in the repo, so no code generation is needed.
    file(GLOB UNIBREAK_SOURCES "${unibreak_SOURCE_DIR}/src/*.c")
    # Exclude the bundled test harness (defines its own main()).
    list(FILTER UNIBREAK_SOURCES EXCLUDE REGEX "tests\\.c$")
    add_library(unibreak STATIC ${UNIBREAK_SOURCES})
    target_include_directories(unibreak PUBLIC
        $<BUILD_INTERFACE:${unibreak_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    if(MSVC)
        target_compile_options(unibreak PRIVATE /wd4244 /wd4267)
    endif()
    message(STATUS "libunibreak fetched: 7.0")
    set(LIBUNIBREAK_AVAILABLE TRUE)
endif()

# OpenGL
if(WINDOW_ENABLE_OPENGL)
    if(WINDOW_PLATFORM STREQUAL "WIN32")
        list(APPEND WINDOW_SOURCES api/api_opengl.cpp api/glad.c)
        list(APPEND WINDOW_LIBS opengl32)
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL)
    elseif(WINDOW_PLATFORM STREQUAL "X11")
        list(APPEND WINDOW_SOURCES api/api_opengl_glx.cpp api/glad.c)
        find_package(OpenGL REQUIRED)
        list(APPEND WINDOW_LIBS OpenGL::GL)
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL)
    elseif(WINDOW_PLATFORM STREQUAL "WAYLAND")
        list(APPEND WINDOW_SOURCES api/api_opengl_egl.cpp api/glad.c)
        # Try the proper CMake target first; fall back to bare libraries when
        # the dev headers/pkg-config files aren't installed (the Wayland
        # bootstrap above already wired the EGL libs into WINDOW_LIBS).
        find_package(OpenGL QUIET COMPONENTS EGL)
        if(TARGET OpenGL::EGL)
            list(APPEND WINDOW_LIBS OpenGL::EGL OpenGL::GL)
        else()
            find_library(WL_GL_LIB NAMES GL libGL.so.1)
            if(WL_GL_LIB)
                list(APPEND WINDOW_LIBS ${WL_GL_LIB})
            endif()
            # WL_EGL_RUNTIME_LIB is set in the Wayland bootstrap block.
        endif()
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL)
    elseif(WINDOW_PLATFORM STREQUAL "MACOS")
        list(APPEND WINDOW_SOURCES api/api_opengl_cocoa.mm)
        find_library(OPENGL_FRAMEWORK OpenGL)
        list(APPEND WINDOW_LIBS ${OPENGL_FRAMEWORK})
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL)
    elseif(WINDOW_PLATFORM STREQUAL "IOS")
        list(APPEND WINDOW_SOURCES api/api_opengl_cocoa.mm)
        find_library(OPENGLES_FRAMEWORK OpenGLES)
        list(APPEND WINDOW_LIBS ${OPENGLES_FRAMEWORK})
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL)
    elseif(WINDOW_PLATFORM STREQUAL "ANDROID")
        list(APPEND WINDOW_SOURCES api/api_opengl_egl.cpp)
        find_library(EGL_LIB EGL)
        find_library(GLES_LIB GLESv3)
        list(APPEND WINDOW_LIBS ${EGL_LIB} ${GLES_LIB})
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL)
    elseif(WINDOW_PLATFORM STREQUAL "WASM")
        list(APPEND WINDOW_SOURCES api/api_opengl_wasm.cpp)
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL WINDOW_SUPPORT_WEBGL)
        message(STATUS "WebGL support enabled")
    endif()
endif()

# Vulkan
if(WINDOW_ENABLE_VULKAN)
    set(_vulkan_ok FALSE)
    set(_vulkan_target "")

    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        set(_vulkan_ok TRUE)
        set(_vulkan_target Vulkan::Vulkan)
        message(STATUS "Vulkan: system SDK")
    else()
        # No SDK installed — fetch Khronos headers and look for the loader
        # library (libvulkan.so.1 / vulkan-1.dll) that the OS already ships.
        # Distros without -dev installed only have the SO-version symlink
        # (e.g. libvulkan.so.1), not the unversioned libvulkan.so, so search
        # both names explicitly.
        find_library(VULKAN_LOADER_LIB NAMES vulkan vulkan-1 libvulkan.so.1)
        if(VULKAN_LOADER_LIB)
            message(STATUS "Vulkan loader found: ${VULKAN_LOADER_LIB}")
            message(STATUS "Vulkan headers not found - fetching from GitHub...")
            window_fetch_content(vulkan_headers
                GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
                GIT_TAG v1.3.290
            )
            FetchContent_MakeAvailable(vulkan_headers)
            # Don't link Vulkan::Headers as a library — it's not in our
            # install export set. Just pull its include directory directly
            # so `#include <vulkan/vulkan.h>` resolves.
            list(APPEND WINDOW_LIBS ${VULKAN_LOADER_LIB})
            include_directories("${vulkan_headers_SOURCE_DIR}/include")
            set(_vulkan_ok TRUE)
            message(STATUS "Vulkan: fetched headers + system loader")
        else()
            message(STATUS "Vulkan not found (no headers and no libvulkan), disabled")
        endif()
    endif()

    if(_vulkan_ok)
        if(WINDOW_PLATFORM STREQUAL "WIN32" OR
           WINDOW_PLATFORM STREQUAL "X11" OR
           WINDOW_PLATFORM STREQUAL "WAYLAND" OR
           WINDOW_PLATFORM STREQUAL "MACOS" OR
           WINDOW_PLATFORM STREQUAL "ANDROID")
            list(APPEND WINDOW_SOURCES api/api_vulkan.cpp)
            if(_vulkan_target)
                list(APPEND WINDOW_LIBS ${_vulkan_target})
            endif()
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_VULKAN)
            message(STATUS "Vulkan support enabled")
        endif()
    endif()
endif()

# Direct3D 11
if(WINDOW_ENABLE_D3D11 AND (WINDOW_PLATFORM STREQUAL "WIN32" OR WINDOW_PLATFORM STREQUAL "UWP"))
    list(APPEND WINDOW_SOURCES api/api_d3d11.cpp)
    list(APPEND WINDOW_LIBS d3d11 dxgi)
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_D3D11)
    message(STATUS "Direct3D 11 support enabled")
endif()

# Direct3D 12
if(WINDOW_ENABLE_D3D12 AND (WINDOW_PLATFORM STREQUAL "WIN32" OR WINDOW_PLATFORM STREQUAL "UWP"))
    list(APPEND WINDOW_SOURCES api/api_d3d12.cpp)
    list(APPEND WINDOW_LIBS d3d12 dxgi)
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_D3D12 WINDOW_SUPPORT_D3D12_ULTIMATE)
    message(STATUS "Direct3D 12 support enabled")
endif()

# Metal
if(WINDOW_ENABLE_METAL AND (WINDOW_PLATFORM STREQUAL "MACOS" OR WINDOW_PLATFORM STREQUAL "IOS"))
    list(APPEND WINDOW_SOURCES api/api_metal.mm)
    find_library(METAL_FRAMEWORK Metal)
    find_library(METALKIT_FRAMEWORK MetalKit)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore)
    list(APPEND WINDOW_LIBS ${METAL_FRAMEWORK} ${METALKIT_FRAMEWORK} ${QUARTZCORE_FRAMEWORK})
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_METAL)
    message(STATUS "Metal support enabled")
endif()

# Platform-specific dependencies
#=============================================================================
# Audio Backend Selection
#=============================================================================

if(WINDOW_ENABLE_AUDIO)
    list(APPEND WINDOW_SOURCES audio/audio.cpp)
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_AUDIO)
    message(STATUS "Audio support enabled")

    # External audio decoders (each fetched individually)
    set(WINDOW_AUDIO_DECODER_INCLUDES "")
    set(AUDIO_DECODERS_ENABLED "")

    # MP3 decoder (minimp3)
    if(WINDOW_ENABLE_MP3_DECODER)
        message(STATUS "Fetching MP3 decoder (minimp3)...")
        window_fetch_content(minimp3
            GIT_REPOSITORY https://github.com/lieff/minimp3.git
            GIT_TAG master
        )
        FetchContent_MakeAvailable(minimp3)
        list(APPEND WINDOW_AUDIO_DECODER_INCLUDES ${minimp3_SOURCE_DIR})
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_MP3_DECODER)
        list(APPEND AUDIO_DECODERS_ENABLED "MP3")
    endif()

    # OGG Vorbis decoder (stb_vorbis)
    if(WINDOW_ENABLE_VORBIS_DECODER)
        message(STATUS "Fetching OGG Vorbis decoder (stb_vorbis)...")
        window_fetch_content(stb
            GIT_REPOSITORY https://github.com/nothings/stb.git
            GIT_TAG master
        )
        FetchContent_MakeAvailable(stb)
        list(APPEND WINDOW_AUDIO_DECODER_INCLUDES ${stb_SOURCE_DIR})
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_VORBIS_DECODER)
        list(APPEND AUDIO_DECODERS_ENABLED "OGG")
    endif()

    # FLAC decoder (dr_flac)
    if(WINDOW_ENABLE_FLAC_DECODER)
        message(STATUS "Fetching FLAC decoder (dr_flac)...")
        window_fetch_content(dr_libs
            GIT_REPOSITORY https://github.com/mackron/dr_libs.git
            GIT_TAG master
        )
        FetchContent_MakeAvailable(dr_libs)
        list(APPEND WINDOW_AUDIO_DECODER_INCLUDES ${dr_libs_SOURCE_DIR})
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_FLAC_DECODER)
        list(APPEND AUDIO_DECODERS_ENABLED "FLAC")
    endif()

    if(AUDIO_DECODERS_ENABLED)
        string(REPLACE ";" ", " DECODERS_STR "${AUDIO_DECODERS_ENABLED}")
        message(STATUS "Audio decoders: ${DECODERS_STR}")
    endif()

    # Check for OpenAL Soft (cross-platform option)
    set(OPENAL_BACKEND_ENABLED FALSE)
    if(WINDOW_ENABLE_OPENAL)
        find_package(OpenAL QUIET)
        if(NOT OpenAL_FOUND)
            # Try pkg-config as fallback
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(OPENAL openal)
                if(OPENAL_FOUND)
                    set(OpenAL_FOUND TRUE)
                    set(OPENAL_INCLUDE_DIR ${OPENAL_INCLUDE_DIRS})
                    set(OPENAL_LIBRARY ${OPENAL_LIBRARIES})
                endif()
            endif()
        endif()

        if(OpenAL_FOUND OR OPENAL_FOUND)
            list(APPEND WINDOW_SOURCES audio/audio_openal.cpp)
            if(OPENAL_INCLUDE_DIR)
                include_directories(${OPENAL_INCLUDE_DIR})
            endif()
            if(OPENAL_LIBRARY)
                list(APPEND WINDOW_LIBS ${OPENAL_LIBRARY})
            elseif(TARGET OpenAL::OpenAL)
                list(APPEND WINDOW_LIBS OpenAL::OpenAL)
            endif()
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENAL)
            message(STATUS "Audio backend: OpenAL Soft (cross-platform)")
            set(OPENAL_BACKEND_ENABLED TRUE)
        else()
            message(STATUS "OpenAL not found, falling back to native backend")
        endif()
    endif()

    # Native platform backends (used if OpenAL not enabled or not found)
    if(NOT OPENAL_BACKEND_ENABLED)
        if(WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
            # Windows: WASAPI
            list(APPEND WINDOW_SOURCES audio/audio_wasapi.cpp)
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_WASAPI)
            message(STATUS "Audio backend: WASAPI")
        elseif(APPLE)
            # macOS/iOS: CoreAudio
            list(APPEND WINDOW_SOURCES audio/audio_coreaudio.mm)
            find_library(COREAUDIO_FRAMEWORK CoreAudio)
            find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
            list(APPEND WINDOW_LIBS ${COREAUDIO_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK})
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_COREAUDIO)
            message(STATUS "Audio backend: CoreAudio")
        elseif(ANDROID)
            # Android: AAudio
            list(APPEND WINDOW_SOURCES audio/audio_aaudio.cpp)
            list(APPEND WINDOW_LIBS aaudio)
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_AAUDIO)
            message(STATUS "Audio backend: AAudio")
        elseif(EMSCRIPTEN)
            # WebAssembly: Web Audio API
            list(APPEND WINDOW_SOURCES audio/audio_webaudio.cpp)
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_WEBAUDIO)
            message(STATUS "Audio backend: Web Audio")
        elseif(UNIX)
            # Linux: PulseAudio preferred, ALSA fallback
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(PULSEAUDIO libpulse)
            endif()
            if(PULSEAUDIO_FOUND)
                list(APPEND WINDOW_SOURCES audio/audio_pulseaudio.cpp)
                list(APPEND WINDOW_LIBS ${PULSEAUDIO_LIBRARIES})
                include_directories(${PULSEAUDIO_INCLUDE_DIRS})
                list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_PULSEAUDIO)
                message(STATUS "Audio backend: PulseAudio")
            else()
                if(PkgConfig_FOUND)
                    pkg_check_modules(ALSA alsa)
                endif()
                if(ALSA_FOUND)
                    list(APPEND WINDOW_SOURCES audio/audio_alsa.cpp)
                    list(APPEND WINDOW_LIBS ${ALSA_LIBRARIES})
                    include_directories(${ALSA_INCLUDE_DIRS})
                    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_ALSA)
                    message(STATUS "Audio backend: ALSA")
                else()
                    message(STATUS "Audio: No backend found (PulseAudio or ALSA required)")
                endif()
            endif()
        endif()
    endif()
else()
    message(STATUS "Audio support disabled")
endif()

#=============================================================================
# GUI Support
#=============================================================================

if(WINDOW_ENABLE_GUI)
    list(APPEND WINDOW_SOURCES
        gui/gui.cpp
        gui/gui_context.cpp
        gui/gui_label.cpp
        gui/gui_controls.cpp
        gui/gui_scroll.cpp
        gui/gui_list.cpp
        gui/gui_tree.cpp
        gui/gui_tab.cpp
        gui/gui_property.cpp
        gui/gui_dialog.cpp
        gui/gui_menu.cpp
        gui/gui_toolbar.cpp
        gui/gui_page.cpp
        gui/gui_panel.cpp
        gui/gui_sizer.cpp
        gui/gui_animation.cpp
        gui/gui_serialization.cpp
    )
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_GUI)
    message(STATUS "GUI support enabled")
else()
    message(STATUS "GUI support disabled")
endif()

#=============================================================================
# GPU Renderer (optional, opt-in)
#
# Optional, self-contained renderers built on the backend-neutral graphics
# abstraction (GraphicDevice/Commander). The gfx-level pieces (Material, the
# VectorRenderer for 2D/3D lines, gizmos and wireframes) need only a graphics
# backend. The GUI renderer additionally draws the GUI's WidgetRenderInfo and
# so requires the GUI. Consumers who supply their own renderer can switch the
# whole feature off.
#=============================================================================

if(WINDOW_ENABLE_RENDERER)
    list(APPEND WINDOW_SOURCES
        renderer/material.cpp
        renderer/vector_renderer.cpp
        renderer/buffer/gpu_buffer.cpp
        renderer/buffer/gpu_texture.cpp
        renderer/light/light.cpp
        renderer/space_partition/space_partition.cpp
        renderer/space_partition/uniform_grid.cpp
        renderer/space_partition/bvh.cpp
        renderer/space_partition/octree.cpp
        renderer/space_partition/quadtree.cpp
        renderer/space_partition/bsp.cpp
        renderer/space_partition/gpu_uniform_grid.cpp
        renderer/space_partition/gpu_leaf_cull.cpp
        renderer/space_partition/gpu_lbvh.cpp
        renderer/video/video_renderer.cpp   # self-guards: real impl only when WINDOW_SUPPORT_VIDEO, else a stub
    )
    if(WINDOW_ENABLE_GUI)
        list(APPEND WINDOW_SOURCES
            renderer/gui_renderer.cpp
            renderer/gui_text_rasterizer.cpp
        )
        message(STATUS "GPU renderer enabled (gfx + GUI)")
    else()
        message(STATUS "GPU renderer enabled (gfx only; GUI renderer needs WINDOW_ENABLE_GUI)")
    endif()
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_RENDERER)
else()
    message(STATUS "GPU renderer disabled")
endif()

#=============================================================================
# Video renderer dependency: build GStreamer FROM SOURCE (renderer/video/)
#
# Per project policy we do not depend on a system GStreamer install: when
# WINDOW_ENABLE_VIDEO is ON, CMake fetches the GStreamer monorepo and builds it
# with its native build system (Meson + Ninja) via ExternalProject, installing
# into the build tree. The monorepo's Meson subproject "wraps" auto-fetch GLib
# and the other deps at build time (network required). This is heavy and slow,
# which is why the feature is opt-in.
#
# video_renderer.cpp is always part of the renderer sources (it self-guards), so
# without this block it still links as an ErrorUnsupported stub. This block adds
# the real GStreamer include dirs / libs and defines WINDOW_SUPPORT_VIDEO (+ the
# per-backend WINDOW_SUPPORT_VIDEO_* zero-copy paths).
#=============================================================================

if(WINDOW_ENABLE_VIDEO AND WINDOW_ENABLE_RENDERER)
    # GStreamer's only build system is Meson (+Ninja); GLib and GStreamer have no
    # CMake build, and hand-porting them is impractical (thousands of files with
    # Meson-generated headers). So instead of REQUIRING meson/ninja preinstalled, we
    # bootstrap both through CMake:
    #   * Ninja  -- use a system one if present, else build it from source (it is a
    #     small CMake project) so no manual install is needed.
    #   * Meson  -- fetch its source and run it via a Python 3 interpreter. Meson is
    #     pure Python and runs straight from a checkout (no install step).
    # The single remaining prerequisite is Python 3 (Meson cannot run without it).
    include(ExternalProject)

    find_package(Python3 COMPONENTS Interpreter REQUIRED)

    # Python 3.12+ removed distutils (PEP 632), but GStreamer 1.24's bundled GLib
    # (gdbus-codegen) still does `import distutils.version`. Ensure it resolves by
    # installing setuptools (ships a distutils shim) into the meson interpreter when
    # missing -- otherwise the GLib codegen step fails on modern Pythons.
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import distutils.version"
                    RESULT_VARIABLE _distutils_missing OUTPUT_QUIET ERROR_QUIET)
    if(_distutils_missing)
        message(STATUS "Video: distutils missing on ${Python3_EXECUTABLE} (Python 3.12+); installing setuptools shim")
        execute_process(COMMAND ${Python3_EXECUTABLE} -m pip install --user "setuptools>=69"
                        RESULT_VARIABLE _pip_rc OUTPUT_QUIET ERROR_QUIET)
        if(_pip_rc)
            message(WARNING "Video: failed to install setuptools; the GLib codegen step may fail "
                            "with 'No module named distutils'. Install it manually: "
                            "${Python3_EXECUTABLE} -m pip install setuptools")
        endif()
    endif()

    # --- Ninja: reuse a system binary, else build from source via CMake ---
    set(_video_dep_targets "")
    find_program(NINJA_EXECUTABLE NAMES ninja ninja-build)
    if(NINJA_EXECUTABLE)
        set(GST_NINJA "${NINJA_EXECUTABLE}")
        message(STATUS "Video: ninja found at ${NINJA_EXECUTABLE}")
    else()
        message(STATUS "Video: ninja not found - building it from source (ninja-build)")
        window_fetch_content(ninja_src
            GIT_REPOSITORY https://github.com/ninja-build/ninja.git
            GIT_TAG v1.12.1)
        FetchContent_MakeAvailable(ninja_src)   # builds the `ninja` executable target
        set(GST_NINJA "$<TARGET_FILE:ninja>")
        list(APPEND _video_dep_targets ninja)
    endif()

    # --- Meson: fetch source, invoke via Python (no install) ---
    window_fetch_content(meson_src
        GIT_REPOSITORY https://github.com/mesonbuild/meson.git
        GIT_TAG 1.6.1)
    FetchContent_GetProperties(meson_src)
    if(NOT meson_src_POPULATED)
        FetchContent_Populate(meson_src)
    endif()
    set(GST_MESON ${Python3_EXECUTABLE} "${meson_src_SOURCE_DIR}/meson.py")
    message(STATUS "Video: meson via ${Python3_EXECUTABLE} ${meson_src_SOURCE_DIR}/meson.py")

    set(GST_VERSION "1.24.10")
    set(GST_PREFIX  "${CMAKE_CURRENT_BINARY_DIR}/third/gstreamer")
    set(GST_INSTALL "${GST_PREFIX}/install")

    # Enable only what the video renderer needs: core + app/video/audio (appsink,
    # video frame map, audio caps) + playback (playbin) + typefind/codecs, and the
    # Enable only what the video renderer needs. The GPU zero-copy plugin differs per
    # platform: D3D11 on Windows (no extra winsys deps), GstGL on Linux (uses the EGL/
    # GLX that ship there). We deliberately do NOT force GStreamer's GL integration on
    # Windows -- with auto_features=disabled meson can't find a GL window-system
    # (WGL/EGL) and `gl=enabled` then hard-errors. D3D11 covers Windows zero-copy;
    # everything else is the CPU-upload fallback. auto_features=disabled keeps it minimal.
    set(GST_MESON_ARGS
        --prefix=${GST_INSTALL}
        --buildtype=release
        -Ddefault_library=shared
        -Dauto_features=disabled
        -Dgst-full=disabled
        -Dexamples=disabled
        -Dtests=disabled
        -Dtools=disabled
        -Dintrospection=disabled
        -Ddoc=disabled
        -Dgstreamer:check=disabled
        -Dbase=enabled
        -Dgood=enabled
        -Dbad=enabled
        -Dgst-plugins-base:app=enabled
        -Dgst-plugins-base:playback=enabled
        -Dgst-plugins-base:typefind=enabled
        -Dgst-plugins-base:videoconvertscale=enabled
        -Dgst-plugins-base:audioconvert=enabled
        -Dgst-plugins-base:audioresample=enabled
        -Dgst-plugins-base:videotestsrc=enabled   # synthetic source for the no-file demo/test
        -Dgst-plugins-base:audiotestsrc=enabled
        -Dglib:tests=false)               # GLib's test suite isn't needed and fails to build (gresource helper)
    if(WINDOW_VIDEO_ZERO_COPY)
        if(WIN32)
            list(APPEND GST_MESON_ARGS -Dgst-plugins-bad:d3d11=enabled)   # ID3D11Texture2D zero-copy
        elseif(UNIX AND NOT APPLE)
            list(APPEND GST_MESON_ARGS -Dgst-plugins-base:gl=enabled -Dgst-plugins-bad:gl=enabled)  # GstGLMemory zero-copy
        endif()
    endif()

    # Drive the build entirely through Meson (which invokes Ninja for us). The
    # bootstrapped Ninja is handed to Meson via the NINJA env var so it is used even
    # when not on PATH; `cmake -E env` sets it for the meson invocations.
    ExternalProject_Add(gstreamer_ext
        GIT_REPOSITORY https://github.com/GStreamer/gstreamer.git   # GitHub mirror (faster/more reliable than gitlab.freedesktop.org)
        GIT_TAG        ${GST_VERSION}
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
        GIT_SUBMODULES ""                       # skip ALL submodules: the only ones are the
                                                # gst-integration-testsuites media (git-LFS,
                                                # gigabytes) which we don't need. Real build
                                                # deps (GLib, ...) come from Meson wrap fetches.
        UPDATE_DISCONNECTED TRUE                 # pinned tag -> skip the post-clone update step
                                                # (redundant, and it fails on a re-run).
        PREFIX         ${GST_PREFIX}
        DEPENDS        ${_video_dep_targets}
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env "NINJA=${GST_NINJA}"
                          ${GST_MESON} setup <BINARY_DIR> <SOURCE_DIR> --backend=ninja ${GST_MESON_ARGS}
        BUILD_COMMAND     ${CMAKE_COMMAND} -E env "NINJA=${GST_NINJA}"
                          ${GST_MESON} compile -C <BINARY_DIR>
        INSTALL_COMMAND   ${GST_MESON} install -C <BINARY_DIR> --no-rebuild
        BUILD_ALWAYS      FALSE
        LOG_CONFIGURE     TRUE
        LOG_BUILD         TRUE
        LOG_INSTALL       TRUE)

    # The ExternalProject installs at build time; reference the known install layout
    # and order it before `window` compiles video_renderer.cpp.
    set(GST_INCLUDE_DIRS
        ${GST_INSTALL}/include/gstreamer-1.0
        ${GST_INSTALL}/include/glib-2.0
        ${GST_INSTALL}/lib/glib-2.0/include
        ${GST_INSTALL}/lib/gstreamer-1.0/include)   # generated config headers (gstconfig.h, gstd3d11config.h, ...)
    if(WIN32)
        set(GST_LIB_DIR "${GST_INSTALL}/lib")
        set(GST_LIBS
            ${GST_LIB_DIR}/gstreamer-1.0.lib
            ${GST_LIB_DIR}/gstapp-1.0.lib
            ${GST_LIB_DIR}/gstvideo-1.0.lib
            ${GST_LIB_DIR}/gstaudio-1.0.lib
            ${GST_LIB_DIR}/gobject-2.0.lib
            ${GST_LIB_DIR}/glib-2.0.lib)
    else()
        set(GST_LIB_DIR "${GST_INSTALL}/lib")
        set(GST_LIBS
            ${GST_LIB_DIR}/libgstreamer-1.0.so
            ${GST_LIB_DIR}/libgstapp-1.0.so
            ${GST_LIB_DIR}/libgstvideo-1.0.so
            ${GST_LIB_DIR}/libgstaudio-1.0.so
            ${GST_LIB_DIR}/libgobject-2.0.so
            ${GST_LIB_DIR}/libglib-2.0.so)
    endif()

    list(APPEND WINDOW_VIDEO_INCLUDES ${GST_INCLUDE_DIRS})
    list(APPEND WINDOW_VIDEO_LIBS     ${GST_LIBS})
    list(APPEND WINDOW_DEFINES        WINDOW_SUPPORT_VIDEO)

    # Per-backend zero-copy interop, matched to the plugin actually built above:
    # D3D11 (gstd3d11) on Windows, GstGL (gstgl) on Linux. Defining the matching
    # WINDOW_SUPPORT_VIDEO_* gates the corresponding code path in video_renderer.cpp.
    if(WINDOW_VIDEO_ZERO_COPY)
        if(WIN32)
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_VIDEO_D3D11)
            list(APPEND WINDOW_VIDEO_LIBS ${GST_LIB_DIR}/gstd3d11-1.0.lib)
        elseif(UNIX AND NOT APPLE)
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_VIDEO_GL)
            list(APPEND WINDOW_VIDEO_LIBS ${GST_LIB_DIR}/libgstgl-1.0.so)
        endif()
    endif()

    set(WINDOW_VIDEO_ENABLED TRUE)
    message(STATUS "Video renderer enabled: GStreamer ${GST_VERSION} built from source (Meson+Ninja); "
                   "zero-copy=${WINDOW_VIDEO_ZERO_COPY}")
else()
    message(STATUS "Video renderer disabled (renderer/video stub returns ErrorUnsupported)")
endif()

#=============================================================================
# Built-in shader compiler (renderer/shader_compiler/, optional)
#
# Compiles HLSL source into each backend's blob at runtime (and offline via a
# CLI), so consumers don't have to pre-bake shaders by hand:
#   * HLSL -> SPIR-V  (Vulkan, and OpenGL 4.6 via ARB_gl_spirv)  -- glslang
#   * SPIR-V -> GLSL  (OpenGL)                                    -- SPIRV-Cross
#   * HLSL -> DXBC    (D3D11 and D3D12)                           -- d3dcompiler
# glslang + SPIRV-Cross are fetched and built from source; d3dcompiler is the
# Windows system component (the only producer of DXBC -- FXC has no source form).
# DXC/DXIL is intentionally unused: both D3D backends consume DXBC here, so the
# heavy LLVM-based DXC build is avoided entirely.
#=============================================================================

if(WINDOW_ENABLE_SHADER_COMPILER AND WINDOW_ENABLE_RENDERER)
    # --- glslang (HLSL/GLSL -> SPIR-V), built from source, minimal footprint ---
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)              # no SPIRV-Tools optimizer dep
    set(ENABLE_HLSL ON CACHE BOOL "" FORCE)              # need the HLSL frontend
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE) # no standalone exe
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    window_fetch_content(glslang
        GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
        GIT_TAG 14.3.0
    )
    FetchContent_MakeAvailable(glslang)

    # --- SPIRV-Cross (SPIR-V -> GLSL / MSL), built from source ---
    set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)   # we emit GLSL/MSL, not HLSL
    set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
    set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
    window_fetch_content(spirv_cross
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Cross.git
        GIT_TAG vulkan-sdk-1.3.296.0
    )
    FetchContent_MakeAvailable(spirv_cross)

    list(APPEND WINDOW_SOURCES renderer/shader_compiler/shader_compiler.cpp)
    # glslang + SPIRV-Cross are fetched static libs with no install export, so link them
    # build-only ($<BUILD_INTERFACE:>) to keep them out of window's exported interface
    # (install(EXPORT) would otherwise fail) -- same treatment as Assimp.
    list(APPEND WINDOW_SHADER_COMPILER_PRIVATE_LIBS
        glslang SPIRV glslang-default-resource-limits
        spirv-cross-core spirv-cross-glsl spirv-cross-msl)
    if(WIN32)
        list(APPEND WINDOW_LIBS d3dcompiler)   # HLSL -> DXBC (D3D11/D3D12); system lib
    endif()
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_SHADER_COMPILER)
    message(STATUS "Shader compiler enabled (glslang + SPIRV-Cross from source; DXBC via d3dcompiler=${WIN32})")
else()
    message(STATUS "Shader compiler disabled")
endif()

#=============================================================================
# CSS-look GUI Theme (optional, opt-in)
#
# A "looks like a web page" skin for the GUI: style presets (GuiStyle,
# ButtonStyle, …) coloured/sized to match HTML+CSS defaults, plus helpers to
# apply them onto a widget subtree. It only sets widget styles, so unlike the
# GPU renderer it needs the GUI but no graphics backend, and the ordinary
# renderer draws the result.
#=============================================================================

if(WINDOW_ENABLE_CSS_THEME AND WINDOW_ENABLE_GUI)
    list(APPEND WINDOW_SOURCES
        renderer/css/css_theme.cpp
    )
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_CSS_THEME)
    message(STATUS "CSS GUI theme enabled")
else()
    message(STATUS "CSS GUI theme disabled")
endif()

#=============================================================================
# Asset System (renderer/assets/) - image + 3D model loading & GPU management
#
# Images decode through stb_image (PNG/JPEG/BMP/TGA/GIF/HDR/PSD/PIC/PNM); 3D models
# decode through Assimp (OBJ/FBX/glTF/GLB/Collada/STL/PLY/3DS/...). Both libraries are
# fetched by CMake. Assimp is optional (WINDOW_ENABLE_MODEL_LOADER); without it image
# loading and AssetManager still build and model loads return an error at runtime.
#=============================================================================

set(WINDOW_ASSET_INCLUDES "")
set(WINDOW_ASSET_PRIVATE_LIBS "")
if(WINDOW_ENABLE_ASSETS)
    list(APPEND WINDOW_SOURCES
        renderer/assets/image.cpp
        renderer/assets/mesh.cpp
        renderer/assets/model.cpp
        renderer/assets/asset_manager.cpp
    )
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_ASSETS)

    # stb_image (single-header image decoder). Shares the stb repo with stb_vorbis.
    message(STATUS "Assets: fetching image decoder (stb_image)...")
    window_fetch_content(stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(stb)
    list(APPEND WINDOW_ASSET_INCLUDES ${stb_SOURCE_DIR})

    # tinyexr (OpenEXR HDR decoder) - optional. Populate-only (don't configure its
    # own CMake project / examples); use the header + its bundled miniz for zlib.
    if(WINDOW_ENABLE_EXR)
        message(STATUS "Assets: fetching EXR decoder (tinyexr)...")
        window_fetch_content(tinyexr
            GIT_REPOSITORY https://github.com/syoyo/tinyexr.git
            GIT_TAG v1.0.8
        )
        FetchContent_GetProperties(tinyexr)
        if(NOT tinyexr_POPULATED)
            FetchContent_Populate(tinyexr)
        endif()
        list(APPEND WINDOW_ASSET_INCLUDES ${tinyexr_SOURCE_DIR} ${tinyexr_SOURCE_DIR}/deps/miniz)
        list(APPEND WINDOW_SOURCES ${tinyexr_SOURCE_DIR}/deps/miniz/miniz.c)
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_EXR)
        message(STATUS "Assets: EXR loading enabled (tinyexr)")
    endif()

    # DDS textures: tinyddsloader (container parsing) + bcdec (BCn block decode) -
    # optional. Both header-only; populate-only (skip their example/test CMake).
    if(WINDOW_ENABLE_DDS)
        message(STATUS "Assets: fetching DDS decoders (tinyddsloader + bcdec)...")
        window_fetch_content(tinyddsloader
            GIT_REPOSITORY https://github.com/benikabocha/tinyddsloader.git
            GIT_TAG master
        )
        FetchContent_GetProperties(tinyddsloader)
        if(NOT tinyddsloader_POPULATED)
            FetchContent_Populate(tinyddsloader)
        endif()
        list(APPEND WINDOW_ASSET_INCLUDES ${tinyddsloader_SOURCE_DIR})
        window_fetch_content(bcdec
            GIT_REPOSITORY https://github.com/iOrange/bcdec.git
            GIT_TAG main
        )
        FetchContent_GetProperties(bcdec)
        if(NOT bcdec_POPULATED)
            FetchContent_Populate(bcdec)
        endif()
        list(APPEND WINDOW_ASSET_INCLUDES ${bcdec_SOURCE_DIR})
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_DDS)
        message(STATUS "Assets: DDS loading enabled (tinyddsloader + bcdec)")
    endif()

    # ETC2/EAC block decoder (detex) - optional. Carried by KTX/KTX2 containers.
    if(WINDOW_ENABLE_ETC2)
        message(STATUS "Assets: fetching ETC2/EAC decoder (detex)...")
        window_fetch_content(detex
            GIT_REPOSITORY https://github.com/hglm/detex.git
            GIT_TAG master
        )
        FetchContent_GetProperties(detex)
        if(NOT detex_POPULATED)
            FetchContent_Populate(detex)
        endif()
        list(APPEND WINDOW_ASSET_INCLUDES ${detex_SOURCE_DIR})
        # Only the ETC/EAC block decoders are needed and they include just detex.h
        # (its inline helpers cover the rest). The other detex sources pull POSIX-only
        # headers (pthread.h/strings.h) and tool main()s, so they are deliberately skipped.
        set(DETEX_SOURCES
            ${detex_SOURCE_DIR}/decompress-etc.c
            ${detex_SOURCE_DIR}/decompress-eac.c
            ${detex_SOURCE_DIR}/clamp.c           # detex_clamp0to255_table (used by detex.h's inline clamp)
        )
        # MSVC can't parse detex's __attribute__((always_inline)); force-include a shim
        # that neutralizes it (no-op on GCC/Clang, which compile detex natively).
        if(MSVC)
            set_source_files_properties(${DETEX_SOURCES} PROPERTIES
                COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/renderer/assets/detex_compat.h")
        endif()
        list(APPEND WINDOW_SOURCES ${DETEX_SOURCES})
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_ETC2)
        message(STATUS "Assets: ETC2/EAC decoding enabled (detex)")
    endif()

    # ASTC block decoder (astc_dec, single source). KTX / raw .astc.
    #
    # ASTC can't simply be turned off here — renderer/assets/image.cpp uses the decoder
    # unconditionally — so the job is to always make astc_dec *build*, not to disable it.
    # astc_dec is fetched from a moving branch (master) by default, and upstream has
    # shipped sources that don't build as-is (e.g. astc_decomp.cpp #include's
    # "basisu_astc_decomp.h" while the matching declarations ship as "astc_decomp.h").
    # So rather than trust the latest blindly:
    #   1. always drop in a forwarding shim header (basisu_astc_decomp.h -> astc_decomp.h)
    #      so the misnamed include resolves, then compile-probe astc_decomp.cpp;
    #   2. if it still won't compile, re-fetch a pinned known commit and probe again.
    # ASTC stays enabled either way; the probe re-runs every configure so a fixed upstream
    # (or a better pinned commit) is picked up automatically.
    if(WINDOW_ENABLE_ASTC)
        set(WINDOW_ASTC_DEC_TAG      "master" CACHE STRING "astc_dec git tag/branch tried first")
        set(WINDOW_ASTC_DEC_FALLBACK "0b45f6c772e34878ebc7f989ba83f753034b49fe"
            CACHE STRING "Known astc_dec commit used if the tag fails the compile probe")

        # Forwarding shim for the misnamed include (works for any astc_dec checkout, since
        # it resolves astc_decomp.h from whichever source dir is on the include path).
        set(_astc_compat "${CMAKE_CURRENT_BINARY_DIR}/astc_compat")
        file(MAKE_DIRECTORY "${_astc_compat}")
        file(WRITE "${_astc_compat}/basisu_astc_decomp.h" "#pragma once\n#include \"astc_decomp.h\"\n")

        message(STATUS "Assets: fetching ASTC decoder (astc_dec @ ${WINDOW_ASTC_DEC_TAG})...")
        window_fetch_content(astc_dec
            GIT_REPOSITORY https://github.com/richgel999/astc_dec.git
            GIT_TAG ${WINDOW_ASTC_DEC_TAG})
        FetchContent_GetProperties(astc_dec)
        if(NOT astc_dec_POPULATED)
            FetchContent_Populate(astc_dec)
        endif()
        set(ASTC_DIR "${astc_dec_SOURCE_DIR}")
        window_try_compile_cpp(ASTC_OK "${ASTC_DIR}/astc_decomp.cpp" "${ASTC_DIR};${_astc_compat}")

        # (2) Fall back to the pinned commit if the moving tag won't compile.
        if(NOT ASTC_OK)
            message(STATUS "Assets: astc_dec '${WINDOW_ASTC_DEC_TAG}' failed the compile probe; "
                           "pinning to ${WINDOW_ASTC_DEC_FALLBACK}")
            FetchContent_Declare(astc_dec_pinned
                GIT_REPOSITORY https://github.com/richgel999/astc_dec.git
                GIT_TAG ${WINDOW_ASTC_DEC_FALLBACK}
                GIT_PROGRESS TRUE)   # full clone (not shallow): an arbitrary SHA must be checkout-able
            FetchContent_GetProperties(astc_dec_pinned)
            if(NOT astc_dec_pinned_POPULATED)
                FetchContent_Populate(astc_dec_pinned)
            endif()
            set(ASTC_DIR "${astc_dec_pinned_SOURCE_DIR}")
            window_try_compile_cpp(ASTC_OK "${ASTC_DIR}/astc_decomp.cpp" "${ASTC_DIR};${_astc_compat}")
        endif()

        # (3) Wire it in. If even the pinned commit failed the probe we still enable it
        # (image.cpp needs the decoder) and warn — so the build surfaces the real astc_dec
        # compile error rather than an unrelated downstream one.
        if(NOT ASTC_OK)
            message(WARNING "Assets: astc_dec failed the compile probe even at the pinned "
                            "commit ${WINDOW_ASTC_DEC_FALLBACK}; enabling anyway so the actual "
                            "error is visible. Update WINDOW_ASTC_DEC_FALLBACK to a buildable commit.")
        endif()
        list(APPEND WINDOW_ASSET_INCLUDES ${ASTC_DIR} ${_astc_compat})
        list(APPEND WINDOW_SOURCES ${ASTC_DIR}/astc_decomp.cpp)
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_ASTC)
        message(STATUS "Assets: ASTC decoding enabled (astc_dec)")
    endif()

    # Assimp (3D model importer) - optional.
    if(WINDOW_ENABLE_MODEL_LOADER)
        message(STATUS "Assets: fetching 3D model importer (Assimp)...")
        # Trim Assimp to an import-only static lib (no tools/tests/exports/install).
        set(ASSIMP_BUILD_TESTS              OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_ASSIMP_TOOLS       OFF CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_SAMPLES            OFF CACHE BOOL "" FORCE)
        set(ASSIMP_INSTALL                  OFF CACHE BOOL "" FORCE)
        set(ASSIMP_NO_EXPORT                ON  CACHE BOOL "" FORCE)
        set(ASSIMP_BUILD_ZLIB               ON  CACHE BOOL "" FORCE)
        set(ASSIMP_WARNINGS_AS_ERRORS       OFF CACHE BOOL "" FORCE)
        set(ASSIMP_INJECT_DEBUG_POSTFIX     OFF CACHE BOOL "" FORCE)
        set(ASSIMP_INSTALL_PDB              OFF CACHE BOOL "" FORCE)
        # Build a static Assimp regardless of the parent's BUILD_SHARED_LIBS.
        set(_WINDOW_SAVED_BUILD_SHARED_LIBS "${BUILD_SHARED_LIBS}")
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

        window_fetch_content(assimp
            GIT_REPOSITORY https://github.com/assimp/assimp.git
            GIT_TAG v5.4.3
        )
        FetchContent_MakeAvailable(assimp)

        set(BUILD_SHARED_LIBS "${_WINDOW_SAVED_BUILD_SHARED_LIBS}" CACHE BOOL "" FORCE)

        # Assimp is an internal implementation detail (no public header includes it)
        # and ASSIMP_INSTALL is OFF, so link it build-only to keep it out of the
        # exported `window` interface (install(EXPORT) would otherwise fail).
        list(APPEND WINDOW_ASSET_PRIVATE_LIBS assimp)
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_MODEL_LOADER)
        message(STATUS "Assets: 3D model loader enabled (Assimp)")
    else()
        message(STATUS "Assets: 3D model loader disabled (image loading only)")
    endif()

    message(STATUS "Asset system enabled")
else()
    message(STATUS "Asset system disabled")
endif()

#=============================================================================
# Virtual Keyboard Support
#=============================================================================

if(WINDOW_ENABLE_VIRTUAL_KEYBOARD)
    list(APPEND WINDOW_SOURCES gui/vk/virtual_keyboard.cpp)
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_VIRTUAL_KEYBOARD)

    # Platform-specific virtual keyboard implementation
    if(WINDOW_PLATFORM STREQUAL "WIN32")
        list(APPEND WINDOW_SOURCES gui/vk/virtual_keyboard_win32.cpp)
        list(APPEND WINDOW_LIBS ole32 shell32)
    elseif(WINDOW_PLATFORM STREQUAL "MACOS")
        list(APPEND WINDOW_SOURCES gui/vk/virtual_keyboard_apple.mm)
        list(APPEND WINDOW_LIBS "-framework Carbon")
    elseif(WINDOW_PLATFORM STREQUAL "IOS")
        list(APPEND WINDOW_SOURCES gui/vk/virtual_keyboard_apple.mm)
    elseif(WINDOW_PLATFORM STREQUAL "ANDROID")
        list(APPEND WINDOW_SOURCES gui/vk/virtual_keyboard_android.cpp)
    elseif(WINDOW_PLATFORM STREQUAL "WASM")
        list(APPEND WINDOW_SOURCES gui/vk/virtual_keyboard_stub.cpp)
    elseif(WINDOW_PLATFORM STREQUAL "X11" OR WINDOW_PLATFORM STREQUAL "WAYLAND")
        list(APPEND WINDOW_SOURCES gui/vk/virtual_keyboard_linux.cpp)
    else()
        list(APPEND WINDOW_SOURCES gui/vk/virtual_keyboard_stub.cpp)
    endif()

    message(STATUS "Virtual keyboard support enabled")
else()
    message(STATUS "Virtual keyboard support disabled")
endif()

#=============================================================================
# Font Rendering Support
#=============================================================================

if(WINDOW_ENABLE_FONT)
    list(APPEND WINDOW_SOURCES gui/font/font.cpp)
    list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_FONT)

    # FreeType is fetched/located up-front (above) so HarfBuzz can link
    # against it. Just hook it into the source list here.
    if(FREETYPE_AVAILABLE)
        list(APPEND WINDOW_SOURCES gui/font/font_freetype.cpp)
        if(FREETYPE_FETCHED)
            list(APPEND WINDOW_LIBS freetype)
        else()
            list(APPEND WINDOW_LIBS ${FREETYPE_LIBRARIES})
            include_directories(${FREETYPE_INCLUDE_DIRS})
        endif()
        list(APPEND WINDOW_DEFINES FONT_SUPPORT_FREETYPE)
    endif()

    # Platform-specific native font implementation
    if(WINDOW_PLATFORM STREQUAL "WIN32")
        list(APPEND WINDOW_SOURCES gui/font/font_win32.cpp)
        list(APPEND WINDOW_LIBS dwrite d2d1 windowscodecs)
        message(STATUS "Font backend: DirectWrite")
    elseif(WINDOW_PLATFORM STREQUAL "MACOS")
        list(APPEND WINDOW_SOURCES gui/font/font_apple.mm)
        list(APPEND WINDOW_LIBS "-framework CoreText" "-framework CoreGraphics")
        message(STATUS "Font backend: Core Text")
    elseif(WINDOW_PLATFORM STREQUAL "IOS")
        list(APPEND WINDOW_SOURCES gui/font/font_apple.mm)
        list(APPEND WINDOW_LIBS "-framework CoreText" "-framework CoreGraphics")
        message(STATUS "Font backend: Core Text")
    elseif(WINDOW_PLATFORM STREQUAL "X11" OR WINDOW_PLATFORM STREQUAL "WAYLAND")
        # Try to find fontconfig — purely for richer font discovery. Without
        # it, FreeType's built-in path-walk in find_system_font is used.
        find_package(PkgConfig QUIET)
        set(_fc_found FALSE)
        if(PkgConfig_FOUND)
            pkg_check_modules(FONTCONFIG fontconfig)
            if(FONTCONFIG_FOUND)
                list(APPEND WINDOW_SOURCES gui/font/font_linux.cpp)
                list(APPEND WINDOW_LIBS ${FONTCONFIG_LIBRARIES})
                include_directories(${FONTCONFIG_INCLUDE_DIRS})
                list(APPEND WINDOW_DEFINES FONT_SUPPORT_FONTCONFIG)
                message(STATUS "Font discovery: Fontconfig + FreeType")
                set(_fc_found TRUE)
            endif()
        endif()
        if(NOT _fc_found)
            message(STATUS "Font discovery: FreeType built-in path search (libfontconfig-dev not installed)")
        endif()
    endif()

    # HarfBuzz text shaper
    if(HARFBUZZ_AVAILABLE)
        list(APPEND WINDOW_SOURCES gui/font/font_harfbuzz.cpp)
        list(APPEND WINDOW_LIBS harfbuzz)
        list(APPEND WINDOW_DEFINES FONT_SUPPORT_HARFBUZZ)
        message(STATUS "Text shaping: HarfBuzz")
    endif()

    # libunibreak line breaking
    if(LIBUNIBREAK_AVAILABLE)
        list(APPEND WINDOW_LIBS unibreak)
        list(APPEND WINDOW_DEFINES FONT_SUPPORT_LIBUNIBREAK)
        message(STATUS "Line breaking: libunibreak")
    endif()

    message(STATUS "Font support enabled")
else()
    message(STATUS "Font support disabled")
endif()

# Platform-specific dependencies
if(WINDOW_PLATFORM STREQUAL "WIN32")
    list(APPEND WINDOW_LIBS user32 gdi32)
    # Prevent Windows min/max macros from conflicting with std::min/std::max
    list(APPEND WINDOW_DEFINES NOMINMAX)
elseif(WINDOW_PLATFORM STREQUAL "UWP")
    # UWP uses Windows Runtime
elseif(WINDOW_PLATFORM STREQUAL "X11")
    find_package(X11 REQUIRED)
    list(APPEND WINDOW_LIBS ${X11_LIBRARIES})
    # Xrandr for monitor enumeration — optional. Without it we fall back
    # to single-monitor enumeration via DisplayWidth/Height.
    if(X11_Xrandr_FOUND AND X11_Xrandr_INCLUDE_PATH)
        list(APPEND WINDOW_LIBS ${X11_Xrandr_LIB})
        list(APPEND WINDOW_DEFINES WINDOW_HAS_XRANDR)
        message(STATUS "Xrandr: enabled (multi-monitor support)")
    else()
        message(STATUS "Xrandr: disabled (libxrandr-dev not installed; single-monitor fallback)")
    endif()
elseif(WINDOW_PLATFORM STREQUAL "WAYLAND")
    # Try the system pkg-config first (fast path on a properly-set-up dev box).
    # If not found, fetch everything we need: libexpat (for the scanner),
    # wayland (for headers + scanner + libwayland sources),
    # wayland-protocols (for xdg-shell.xml), and xkbcommon headers. Runtime
    # libraries (libwayland-client.so.0, libwayland-egl.so.1, libxkbcommon.so.0)
    # are taken from the system since we'd otherwise be vendoring an entire
    # display protocol implementation.
    find_package(PkgConfig QUIET)
    set(_wl_system_ok FALSE)
    if(PkgConfig_FOUND)
        pkg_check_modules(WAYLAND wayland-client wayland-egl xkbcommon)
        if(WAYLAND_FOUND)
            list(APPEND WINDOW_LIBS ${WAYLAND_LIBRARIES})
            include_directories(${WAYLAND_INCLUDE_DIRS})
            set(_wl_system_ok TRUE)
            message(STATUS "Wayland: system packages")
        endif()
    endif()

    if(NOT _wl_system_ok)
        message(STATUS "Wayland dev packages not found - bootstrapping from sources")

        # ----- libexpat (used by wayland-scanner to parse protocol XML) -----
        find_library(WL_EXPAT_LIB NAMES expat libexpat.so.1)
        if(NOT WL_EXPAT_LIB)
            message(FATAL_ERROR "libexpat runtime not found — install libexpat1 or libexpat-dev")
        endif()
        message(STATUS "Wayland: expat runtime: ${WL_EXPAT_LIB}")

        set(EXPAT_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
        set(EXPAT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(EXPAT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(EXPAT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(EXPAT_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        window_fetch_content(expat_src
            GIT_REPOSITORY https://github.com/libexpat/libexpat.git
            GIT_TAG R_2_6_2
            SOURCE_SUBDIR expat
        )
        FetchContent_GetProperties(expat_src)
        if(NOT expat_src_POPULATED)
            FetchContent_Populate(expat_src)
        endif()
        # Just need expat headers for the scanner — link to system libexpat.so.1
        set(WL_EXPAT_INCLUDE "${expat_src_SOURCE_DIR}/expat/lib")

        # ----- wayland source tree (for src/scanner.c and protocol/wayland.xml) -----
        window_fetch_content(wayland_src
            GIT_REPOSITORY https://gitlab.freedesktop.org/wayland/wayland.git
            GIT_TAG 1.23.0
        )
        FetchContent_GetProperties(wayland_src)
        if(NOT wayland_src_POPULATED)
            FetchContent_Populate(wayland_src)
        endif()

        # ----- wayland-protocols (xdg-shell.xml etc.) -----
        window_fetch_content(wayland_protocols_src
            GIT_REPOSITORY https://gitlab.freedesktop.org/wayland/wayland-protocols.git
            GIT_TAG 1.36
        )
        FetchContent_GetProperties(wayland_protocols_src)
        if(NOT wayland_protocols_src_POPULATED)
            FetchContent_Populate(wayland_protocols_src)
        endif()

        # ----- Build wayland-scanner from its single C file -----
        # wayland-version.h is normally generated by Meson at configure time;
        # write it ourselves so the scanner compiles.
        set(WL_VERSION_H_DIR "${CMAKE_BINARY_DIR}/wayland-bootstrap-include")
        file(MAKE_DIRECTORY "${WL_VERSION_H_DIR}")
        configure_file(
            "${wayland_src_SOURCE_DIR}/src/wayland-version.h.in"
            "${WL_VERSION_H_DIR}/wayland-version.h"
            @ONLY
        )
        # Match the FetchContent tag.
        set(WAYLAND_VERSION_MAJOR 1)
        set(WAYLAND_VERSION_MINOR 23)
        set(WAYLAND_VERSION_MICRO 0)
        set(WAYLAND_VERSION "1.23.0")
        configure_file(
            "${wayland_src_SOURCE_DIR}/src/wayland-version.h.in"
            "${WL_VERSION_H_DIR}/wayland-version.h"
            @ONLY
        )

        set(WL_SCANNER_BIN "${CMAKE_BINARY_DIR}/wayland-scanner-bootstrap")
        add_executable(wayland-scanner-bootstrap
            ${wayland_src_SOURCE_DIR}/src/scanner.c
            ${wayland_src_SOURCE_DIR}/src/wayland-util.c
        )
        target_include_directories(wayland-scanner-bootstrap PRIVATE
            ${WL_EXPAT_INCLUDE}
            ${wayland_src_SOURCE_DIR}/src
            ${WL_VERSION_H_DIR}
        )
        target_link_libraries(wayland-scanner-bootstrap PRIVATE ${WL_EXPAT_LIB})
        # scanner.c uses some POSIX features; suppress fcommon warnings on GCC
        target_compile_definitions(wayland-scanner-bootstrap PRIVATE
            _POSIX_C_SOURCE=200809L
            HAVE_STRNDUP=1
        )
        set_target_properties(wayland-scanner-bootstrap PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        )

        # Helper: run scanner to generate {client-header,private-code} for an XML protocol.
        # Outputs go into ${WL_GEN_DIR} as <name>-client-protocol.h and <name>-protocol.c.
        set(WL_GEN_DIR "${CMAKE_BINARY_DIR}/wayland-generated")
        file(MAKE_DIRECTORY "${WL_GEN_DIR}")
        function(window_wl_scan name xml_path out_header_var out_source_var)
            set(_h "${WL_GEN_DIR}/${name}-client-protocol.h")
            set(_c "${WL_GEN_DIR}/${name}-protocol.c")
            add_custom_command(
                OUTPUT "${_h}"
                COMMAND wayland-scanner-bootstrap client-header "${xml_path}" "${_h}"
                DEPENDS wayland-scanner-bootstrap "${xml_path}"
                VERBATIM
            )
            add_custom_command(
                OUTPUT "${_c}"
                COMMAND wayland-scanner-bootstrap private-code "${xml_path}" "${_c}"
                DEPENDS wayland-scanner-bootstrap "${xml_path}"
                VERBATIM
            )
            set(${out_header_var} "${_h}" PARENT_SCOPE)
            set(${out_source_var} "${_c}" PARENT_SCOPE)
        endfunction()

        # Generate the core wayland protocol — its .h is what `<wayland-client.h>`
        # ultimately includes via wayland-client-protocol.h. The matching .c is
        # already in libwayland-client.so (we link to the system runtime), so we
        # only consume the header here.
        window_wl_scan(wayland
            "${wayland_src_SOURCE_DIR}/protocol/wayland.xml"
            WL_CORE_HEADER WL_CORE_SOURCE)
        window_wl_scan(xdg-shell
            "${wayland_protocols_src_SOURCE_DIR}/stable/xdg-shell/xdg-shell.xml"
            WL_XDG_HEADER WL_XDG_SOURCE)

        # ----- xkbcommon headers (we vendor only the public subset) -----
        # Fetch the xkbcommon repo just to get include/xkbcommon/*.h. Runtime
        # links against system libxkbcommon.so.0.
        window_fetch_content(xkbcommon_src
            GIT_REPOSITORY https://github.com/xkbcommon/libxkbcommon.git
            GIT_TAG xkbcommon-1.7.0
        )
        FetchContent_GetProperties(xkbcommon_src)
        if(NOT xkbcommon_src_POPULATED)
            FetchContent_Populate(xkbcommon_src)
        endif()
        find_library(WL_XKB_LIB NAMES xkbcommon libxkbcommon.so.0)
        if(NOT WL_XKB_LIB)
            message(FATAL_ERROR "libxkbcommon runtime not found")
        endif()

        # ----- EGL / Khronos registry headers (for api_opengl_egl.cpp) -----
        window_fetch_content(egl_registry
            GIT_REPOSITORY https://github.com/KhronosGroup/EGL-Registry.git
            GIT_TAG main
        )
        FetchContent_GetProperties(egl_registry)
        if(NOT egl_registry_POPULATED)
            FetchContent_Populate(egl_registry)
        endif()
        window_fetch_content(opengl_registry
            GIT_REPOSITORY https://github.com/KhronosGroup/OpenGL-Registry.git
            GIT_TAG main
        )
        FetchContent_GetProperties(opengl_registry)
        if(NOT opengl_registry_POPULATED)
            FetchContent_Populate(opengl_registry)
        endif()

        # ----- Runtime libraries we link against -----
        find_library(WL_CLIENT_LIB NAMES wayland-client libwayland-client.so.0)
        find_library(WL_EGL_LIB NAMES wayland-egl libwayland-egl.so.1)
        find_library(WL_EGL_RUNTIME_LIB NAMES EGL libEGL.so.1)
        if(NOT WL_CLIENT_LIB OR NOT WL_EGL_LIB OR NOT WL_EGL_RUNTIME_LIB)
            message(FATAL_ERROR
                "Wayland runtime not complete: client=${WL_CLIENT_LIB}, "
                "wayland-egl=${WL_EGL_LIB}, EGL=${WL_EGL_RUNTIME_LIB}")
        endif()

        # ----- Hook everything into the build -----
        list(APPEND WINDOW_SOURCES
            ${WL_CORE_HEADER}    # generated, ensures the header is built before window/
            ${WL_XDG_HEADER}
            ${WL_XDG_SOURCE})    # contains xdg_wm_base_interface etc. used by window_wayland.cpp

        # Headers: scanner output, xkbcommon public, EGL/OpenGL registry,
        # wayland source tree (for wayland-egl.h, wayland-client-core.h, etc.)
        include_directories(
            ${WL_GEN_DIR}
            ${WL_VERSION_H_DIR}
            ${xkbcommon_src_SOURCE_DIR}/include
            ${egl_registry_SOURCE_DIR}/api
            ${opengl_registry_SOURCE_DIR}/api
            ${wayland_src_SOURCE_DIR}/src       # wayland-client-core.h, wayland-util.h
            ${wayland_src_SOURCE_DIR}/egl       # wayland-egl-core.h
        )

        list(APPEND WINDOW_LIBS
            ${WL_CLIENT_LIB} ${WL_EGL_LIB} ${WL_EGL_RUNTIME_LIB} ${WL_XKB_LIB})

        message(STATUS "Wayland: bootstrapped (scanner+xdg-shell+xkbcommon headers fetched)")
    endif()
elseif(WINDOW_PLATFORM STREQUAL "MACOS")
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    list(APPEND WINDOW_LIBS ${COCOA_FRAMEWORK} ${IOKIT_FRAMEWORK} ${COREGRAPHICS_FRAMEWORK})
elseif(WINDOW_PLATFORM STREQUAL "IOS")
    find_library(UIKIT_FRAMEWORK UIKit)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    list(APPEND WINDOW_LIBS ${UIKIT_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
elseif(WINDOW_PLATFORM STREQUAL "ANDROID")
    find_library(ANDROID_LIB android)
    find_library(LOG_LIB log)
    list(APPEND WINDOW_LIBS ${ANDROID_LIB} ${LOG_LIB})
elseif(WINDOW_PLATFORM STREQUAL "WASM")
    # Emscripten provides WebGL via its own headers
    # Enable USE_WEBGL2 for WebGL 2.0 support
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s USE_WEBGL2=1 -s FULL_ES3=1")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -s USE_WEBGL2=1 -s FULL_ES3=1")
    message(STATUS "WASM/Emscripten platform configured")
endif()

#=============================================================================
# Library Target
#=============================================================================

add_library(window STATIC
    window.hpp
    ${WINDOW_SOURCES}
)

target_include_directories(window PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/input>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(window PUBLIC ${WINDOW_DEFINES})
target_link_libraries(window PUBLIC ${WINDOW_LIBS})

# Fix parallel PDB write contention on MSVC debug builds
if(MSVC)
    target_compile_options(window PRIVATE /FS)
endif()

# SIMD math acceleration
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86|x86_64|AMD64|i[3-6]86")
    if(WINDOW_MATH_SSE)
        target_compile_definitions(window PUBLIC WINDOW_MATH_SSE=1)
        if(NOT MSVC)
            target_compile_options(window PUBLIC -msse4.1)
        endif()
        message(STATUS "Math SIMD: SSE4.1")
    endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64|armv[78]")
    if(WINDOW_MATH_NEON)
        target_compile_definitions(window PUBLIC WINDOW_MATH_NEON=1)
        if(NOT MSVC AND CMAKE_SYSTEM_PROCESSOR MATCHES "armv7")
            target_compile_options(window PUBLIC -mfpu=neon)
        endif()
        message(STATUS "Math SIMD: NEON")
    endif()
    if(WINDOW_MATH_SVE)
        target_compile_definitions(window PUBLIC WINDOW_MATH_SVE=1)
        if(NOT MSVC)
            target_compile_options(window PUBLIC -march=armv8.2-a+sve)
        endif()
        message(STATUS "Math SIMD: SVE")
    endif()
else()
    message(STATUS "Math SIMD: None (scalar fallback)")
endif()

# Add Boost include directories (geometry and property_tree are header-only)
if(BOOST_FETCHED)
    # When fetched via FetchContent, add all Boost library include paths
    # Each library has headers in libs/<name>/include/
    file(GLOB BOOST_LIB_DIRS "${boost_SOURCE_DIR}/libs/*")
    foreach(lib_dir ${BOOST_LIB_DIRS})
        if(IS_DIRECTORY "${lib_dir}/include")
            target_include_directories(window PUBLIC
                $<BUILD_INTERFACE:${lib_dir}/include>
            )
        endif()
    endforeach()
    # Also add numeric/conversion which has a nested structure
    if(IS_DIRECTORY "${boost_SOURCE_DIR}/libs/numeric/conversion/include")
        target_include_directories(window PUBLIC
            $<BUILD_INTERFACE:${boost_SOURCE_DIR}/libs/numeric/conversion/include>
        )
    endif()
else()
    # When system Boost is found, use its include directories
    target_include_directories(window PUBLIC ${Boost_INCLUDE_DIRS})
endif()

# Add external audio decoder include directories
if(WINDOW_AUDIO_DECODER_INCLUDES)
    target_include_directories(window PRIVATE ${WINDOW_AUDIO_DECODER_INCLUDES})
endif()

# Add asset system include directories (stb_image; Assimp comes via its target)
if(WINDOW_ASSET_INCLUDES)
    target_include_directories(window PRIVATE ${WINDOW_ASSET_INCLUDES})
endif()

# Link the asset system's private (build-only) libraries. Assimp is wrapped in
# $<BUILD_INTERFACE:> so it is not part of window's exported/installed interface.
foreach(_lib ${WINDOW_ASSET_PRIVATE_LIBS})
    target_link_libraries(window PRIVATE $<BUILD_INTERFACE:${_lib}>)
endforeach()

# Link the shader compiler's fetched libs (glslang + SPIRV-Cross) build-only, for the same
# reason as the asset libs above: they have no install export of their own.
foreach(_lib ${WINDOW_SHADER_COMPILER_PRIVATE_LIBS})
    target_link_libraries(window PRIVATE $<BUILD_INTERFACE:${_lib}>)
endforeach()

# Video renderer: hook in the from-source GStreamer build. The ExternalProject must
# finish (headers + import libs installed) before window compiles video_renderer.cpp,
# hence add_dependencies. Includes/libs are kept build-only (out of window's exported
# interface) like the other fetched deps.
if(WINDOW_VIDEO_ENABLED)
    add_dependencies(window gstreamer_ext)
    target_include_directories(window PRIVATE ${WINDOW_VIDEO_INCLUDES})
    foreach(_lib ${WINDOW_VIDEO_LIBS})
        target_link_libraries(window PRIVATE $<BUILD_INTERFACE:${_lib}>)
    endforeach()
endif()

#=============================================================================
# Offline shader-compiler tool + helper (the "build-time" half of the compiler)
#=============================================================================
if("WINDOW_SUPPORT_SHADER_COMPILER" IN_LIST WINDOW_DEFINES)
    add_executable(shaderc tools/shader_compiler_cli.cpp)
    target_link_libraries(shaderc PRIVATE window)

    # window_compile_shader(<input.hlsl>
    #     STAGE   <vs|ps|cs|gs|hs|ds>          (required)
    #     ENTRY   <name>                       (default: main)
    #     TARGETS <spirv glsl dxbc msl>        (default: all four)
    #     OUTPUT_DIR <dir>                     (default: alongside the input)
    #     INCLUDE <dir>                        (#include root)
    #     DEFINES <NAME=VAL ...>
    #     OUTPUT_VAR <var>)                    (set to the list of generated blob paths)
    # Emits "<OUTPUT_DIR>/<basename>.<entry>.<suffix>" per target via the shaderc tool, so a
    # single HLSL source is pre-baked for every backend at build time.
    function(window_compile_shader input)
        cmake_parse_arguments(SC "" "STAGE;ENTRY;OUTPUT_DIR;INCLUDE;OUTPUT_VAR" "TARGETS;DEFINES" ${ARGN})
        if(NOT SC_STAGE)
            message(FATAL_ERROR "window_compile_shader: STAGE is required")
        endif()
        if(NOT SC_ENTRY)
            set(SC_ENTRY "main")
        endif()
        if(NOT SC_TARGETS)
            set(SC_TARGETS spirv glsl dxbc msl)
        endif()
        get_filename_component(_abs "${input}" ABSOLUTE)
        get_filename_component(_name "${input}" NAME_WE)
        if(NOT SC_OUTPUT_DIR)
            get_filename_component(SC_OUTPUT_DIR "${_abs}" DIRECTORY)
        endif()
        file(MAKE_DIRECTORY "${SC_OUTPUT_DIR}")

        set(_define_args "")
        foreach(_d ${SC_DEFINES})
            list(APPEND _define_args -D ${_d})
        endforeach()
        set(_inc_args "")
        if(SC_INCLUDE)
            set(_inc_args --include "${SC_INCLUDE}")
        endif()

        set(_outputs "")
        foreach(_t ${SC_TARGETS})
            if(_t STREQUAL "spirv")
                set(_suffix ".spv")
            elseif(_t STREQUAL "glsl")
                set(_suffix ".glsl")
            elseif(_t STREQUAL "dxbc")
                set(_suffix ".dxbc")
            elseif(_t STREQUAL "msl")
                set(_suffix ".msl")
            else()
                message(FATAL_ERROR "window_compile_shader: bad TARGET '${_t}'")
            endif()
            set(_out "${SC_OUTPUT_DIR}/${_name}.${SC_ENTRY}${_suffix}")
            add_custom_command(
                OUTPUT  "${_out}"
                COMMAND shaderc "${_abs}" --stage ${SC_STAGE} --entry ${SC_ENTRY}
                                --target ${_t} --out "${_out}" ${_inc_args} ${_define_args}
                DEPENDS shaderc "${_abs}"
                COMMENT "shaderc ${_name}.${SC_ENTRY} -> ${_t}"
                VERBATIM)
            list(APPEND _outputs "${_out}")
        endforeach()
        if(SC_OUTPUT_VAR)
            set(${SC_OUTPUT_VAR} "${_outputs}" PARENT_SCOPE)
        endif()
    endfunction()
endif()

# Enable Objective-C++ for Apple platforms
if(APPLE)
    set_source_files_properties(${WINDOW_SOURCES} PROPERTIES
        COMPILE_FLAGS "-x objective-c++"
    )
endif()

#=============================================================================
# Tests
#=============================================================================

option(WINDOW_BUILD_TESTS "Build unit tests" ON)

if(WINDOW_BUILD_TESTS)
    enable_testing()

    # Basic unit tests (headless, no display required)
    add_executable(test_window tests/test_window.cpp)
    target_link_libraries(test_window PRIVATE window)

    add_test(NAME test_window COMMAND test_window)

    # Font fallback unit tests (headless, no display required)
    add_executable(test_font tests/test_font.cpp)
    target_link_libraries(test_font PRIVATE window)
    add_test(NAME test_font COMMAND test_font)

    # RAM glyph-atlas manager unit tests (headless, no display required)
    if(WINDOW_ENABLE_FONT)
        add_executable(test_glyph_atlas tests/test_glyph_atlas.cpp)
        target_link_libraries(test_glyph_atlas PRIVATE window)
        add_test(NAME test_glyph_atlas COMMAND test_glyph_atlas)
    endif()

    # GUI unit tests (headless, no display required)
    if(WINDOW_ENABLE_GUI)
        add_executable(test_gui tests/test_gui.cpp)
        target_link_libraries(test_gui PRIVATE window)
        add_test(NAME test_gui COMMAND test_gui)
    endif()

    # Asset system unit tests (headless: image decode, model data, layout, cache keys)
    if(WINDOW_ENABLE_ASSETS)
        add_executable(test_assets tests/test_assets.cpp)
        target_link_libraries(test_assets PRIVATE window)
        add_test(NAME test_assets COMMAND test_assets)
    endif()

    # Scene-partition CPU unit tests (headless: grid/BVH/octree/quadtree/BSP queries vs brute force)
    if("WINDOW_SUPPORT_RENDERER" IN_LIST WINDOW_DEFINES)
        add_executable(test_space_partition tests/test_space_partition.cpp)
        target_link_libraries(test_space_partition PRIVATE window)
        add_test(NAME test_space_partition COMMAND test_space_partition)

        # VideoPlayer API tests (headless: result strings + open() argument validation;
        # works whether or not GStreamer was built in -- the stub reports ErrorUnsupported)
        add_executable(test_video tests/test_video.cpp)
        target_link_libraries(test_video PRIVATE window)
        add_test(NAME test_video COMMAND test_video)
    endif()
endif()

#=============================================================================
# Examples
#=============================================================================

if(WINDOW_BUILD_EXAMPLES AND NOT ANDROID AND NOT IOS)
    # Basic window example
    add_executable(example_basic examples/basic.cpp)
    target_link_libraries(example_basic PRIVATE window)

    # Windowed swapchain present smoke test (set_render_target_backbuffer + present per backend)
    add_executable(example_present examples/present.cpp)
    target_link_libraries(example_present PRIVATE window)

    # Present-mode + custom backbuffer-count matrix (every SwapMode x back_buffers per backend)
    add_executable(example_present_modes examples/present_modes.cpp)
    target_link_libraries(example_present_modes PRIVATE window)

    # Graphics pipeline conformance test (offscreen render + pixel readback over the RHI)
    add_executable(example_render_pipeline examples/render_pipeline.cpp)
    target_link_libraries(example_render_pipeline PRIVATE window)

    # Common dialog demo (file open/save, folder, color, font)
    add_executable(example_dialogs examples/dialogs.cpp)
    target_link_libraries(example_dialogs PRIVATE window)

    # VectorRenderer demo / cross-API smoke test (2D overlay + 3D debug geometry)
    if("WINDOW_SUPPORT_RENDERER" IN_LIST WINDOW_DEFINES)
        add_executable(example_vector_renderer examples/vector_renderer.cpp)
        target_link_libraries(example_vector_renderer PRIVATE window)

        # Buffer/texture wrapper demo + cross-API smoke test (typed buffers + views)
        add_executable(example_buffer examples/buffer.cpp)
        target_link_libraries(example_buffer PRIVATE window)

        # Scene-partition demo + cross-API GPU/CPU verification (grid/BVH/octree/quadtree/BSP)
        add_executable(example_space_partition examples/space_partition.cpp)
        target_link_libraries(example_space_partition PRIVATE window)

        # Light buffer demo + cross-API smoke test (pack all light types, upload, read back)
        add_executable(example_light examples/light.cpp)
        target_link_libraries(example_light PRIVATE window)

        # Built-in shader compiler demo: HLSL -> all backends at runtime, then draw with it
        if("WINDOW_SUPPORT_SHADER_COMPILER" IN_LIST WINDOW_DEFINES)
            add_executable(example_shader_compiler examples/shader_compiler.cpp)
            target_link_libraries(example_shader_compiler PRIVATE window)

            # Slot-based binding cross-API check (UBO by slot, no explicit pipeline layout)
            add_executable(example_slot_binding examples/slot_binding.cpp)
            target_link_libraries(example_slot_binding PRIVATE window)

            # GStreamer video playback demo (needs the video renderer + the shader compiler
            # for the blit pipeline). Only built when WINDOW_ENABLE_VIDEO built GStreamer.
            if("WINDOW_SUPPORT_VIDEO" IN_LIST WINDOW_DEFINES)
                add_executable(example_video examples/video.cpp)
                target_link_libraries(example_video PRIVATE window)
            endif()
        endif()

        # GUI renderer offscreen pixel test (verification harness for the GUI shader migration)
        if(WINDOW_ENABLE_GUI)
            add_executable(example_gui_renderer examples/gui_renderer.cpp)
            target_link_libraries(example_gui_renderer PRIVATE window)
        endif()
    endif()

    # Asset loading & management demo (image + 3D model via AssetManager)
    if(WINDOW_ENABLE_ASSETS)
        add_executable(example_assets examples/assets.cpp)
        target_link_libraries(example_assets PRIVATE window)
    endif()

    # PBR-via-Material demo (renderer/shaders/pbr.material + pbr.hlsl; no PBR C++ class)
    if(WINDOW_ENABLE_RENDERER AND WINDOW_ENABLE_ASSETS)
        add_executable(example_pbr examples/pbr.cpp)
        target_link_libraries(example_pbr PRIVATE window)
        target_compile_definitions(example_pbr PRIVATE
            WINDOW_PBR_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/renderer/shaders")
    endif()

    # Material binding/creation/editing demo (storage+uniform binders, Material-owned
    # buffers, typed CPU-mirror editing, multi-descriptor-set layouts; runtime-compiled)
    if(WINDOW_ENABLE_RENDERER)
        add_executable(example_material_resources examples/material_resources.cpp)
        target_link_libraries(example_material_resources PRIVATE window)
    endif()

    # OpenGL example
    if(WINDOW_ENABLE_OPENGL AND "WINDOW_SUPPORT_OPENGL" IN_LIST WINDOW_DEFINES)
        add_executable(example_opengl examples/opengl.cpp)
        target_link_libraries(example_opengl PRIVATE window)

    endif()

    # Vulkan example
    if("WINDOW_SUPPORT_VULKAN" IN_LIST WINDOW_DEFINES)
        add_executable(example_vulkan examples/vulkan.cpp)
        target_link_libraries(example_vulkan PRIVATE window)
    endif()

    # D3D11 example (Windows only)
    if(WINDOW_PLATFORM STREQUAL "WIN32" AND WINDOW_ENABLE_D3D11)
        add_executable(example_d3d11 examples/d3d11.cpp)
        target_link_libraries(example_d3d11 PRIVATE window)
    endif()

    # D3D12 example (Windows only)
    if(WINDOW_PLATFORM STREQUAL "WIN32" AND WINDOW_ENABLE_D3D12)
        add_executable(example_d3d12 examples/d3d12.cpp)
        target_link_libraries(example_d3d12 PRIVATE window)
    endif()

    # External window example (Windows only for now)
    if(WINDOW_PLATFORM STREQUAL "WIN32")
        add_executable(example_external_window examples/external_window.cpp)
        target_link_libraries(example_external_window PRIVATE window)

        # Graphics config example
        add_executable(example_graphics_config examples/graphics_config.cpp)
        target_link_libraries(example_graphics_config PRIVATE window)

        # Events example
        add_executable(example_events examples/events.cpp)
        target_link_libraries(example_events PRIVATE window)

        # Gamepad example
        add_executable(example_gamepad examples/gamepad.cpp)
        target_link_libraries(example_gamepad PRIVATE window)

        # Steering wheel example
        add_executable(example_wheel examples/wheel.cpp)
        target_link_libraries(example_wheel PRIVATE window)

        # Audio example
        if(WINDOW_ENABLE_AUDIO AND "WINDOW_SUPPORT_AUDIO" IN_LIST WINDOW_DEFINES)
            add_executable(example_audio examples/audio.cpp)
            target_link_libraries(example_audio PRIVATE window)
        endif()

        # Font rendering example
        if(WINDOW_ENABLE_FONT AND "WINDOW_SUPPORT_FONT" IN_LIST WINDOW_DEFINES)
            add_executable(example_font examples/font.cpp)
            target_link_libraries(example_font PRIVATE window)
        endif()

        # Virtual keyboard example
        if(WINDOW_ENABLE_FONT AND "WINDOW_SUPPORT_FONT" IN_LIST WINDOW_DEFINES)
            add_executable(example_virtual_keyboard examples/virtual_keyboard.cpp)
            target_link_libraries(example_virtual_keyboard PRIVATE window)
        endif()
    endif()

    # GUI example (headless, no display required)
    if(WINDOW_ENABLE_GUI)
        add_executable(example_gui examples/gui.cpp)
        target_link_libraries(example_gui PRIVATE window)

        add_executable(example_gui_serialization examples/gui_serialization.cpp)
        target_link_libraries(example_gui_serialization PRIVATE window)

        # CSS-look theme showcase (needs the CssTheme presets compiled into the lib)
        if("WINDOW_SUPPORT_CSS_THEME" IN_LIST WINDOW_DEFINES)
            add_executable(example_css_theme examples/css_theme.cpp)
            target_link_libraries(example_css_theme PRIVATE window)
        endif()
    endif()

    # Metal example (macOS only)
    if(WINDOW_PLATFORM STREQUAL "MACOS" AND WINDOW_ENABLE_METAL)
        add_executable(example_metal examples/metal.mm)
        target_link_libraries(example_metal PRIVATE window)
    endif()
endif()

#=============================================================================
# GUI Editor Sub-project
#=============================================================================

option(WINDOW_BUILD_GUI_EDITOR "Build GUI editor application" ON)

if(WINDOW_BUILD_GUI_EDITOR AND WINDOW_ENABLE_GUI AND WINDOW_ENABLE_FONT)
    add_subdirectory(gui/editor)
    message(STATUS "GUI Editor: enabled")
else()
    message(STATUS "GUI Editor: disabled (requires GUI and Font support)")
endif()

#=============================================================================
# Installation
#=============================================================================

include(GNUInstallDirs)

set(_WINDOW_INSTALL_TARGETS window)
if(LIBUNIBREAK_AVAILABLE AND TARGET unibreak)
    list(APPEND _WINDOW_INSTALL_TARGETS unibreak)
endif()
install(TARGETS ${_WINDOW_INSTALL_TARGETS}
    EXPORT windowTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES window.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
    input/input_keyboard.hpp
    input/input_mouse.hpp
    input/input_gamepad.hpp
    input/input_wheel.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/input
)

if(WINDOW_ENABLE_AUDIO)
    install(FILES
        audio/audio.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/audio
    )
endif()

install(EXPORT windowTargets
    FILE windowTargets.cmake
    NAMESPACE window::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/window
)

# Create package config
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/windowConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/windowConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/window
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/windowConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/windowConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/windowConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/window
)

#=============================================================================
# Summary
#=============================================================================

message(STATUS "")
message(STATUS "Window Library Configuration:")
message(STATUS "  Platform:    ${WINDOW_PLATFORM}")
message(STATUS "  OpenGL:      ${WINDOW_ENABLE_OPENGL}")
if("WINDOW_SUPPORT_VULKAN" IN_LIST WINDOW_DEFINES)
    message(STATUS "  Vulkan:      ON")
else()
    message(STATUS "  Vulkan:      OFF")
endif()
if(WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(STATUS "  D3D11:       ${WINDOW_ENABLE_D3D11}")
    message(STATUS "  D3D12:       ${WINDOW_ENABLE_D3D12}")
    if(WINDOW_ENABLE_DINPUT)
        message(STATUS "  Gamepad:     DirectInput")
    else()
        message(STATUS "  Gamepad:     XInput")
    endif()
endif()
if(APPLE)
    message(STATUS "  Metal:       ${WINDOW_ENABLE_METAL}")
    message(STATUS "  Gamepad:     GCController")
endif()
message(STATUS "  Audio:       ${WINDOW_ENABLE_AUDIO}")
message(STATUS "  Examples:    ${WINDOW_BUILD_EXAMPLES}")
message(STATUS "")
