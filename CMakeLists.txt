cmake_minimum_required(VERSION 3.16)
project(window VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
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
elseif(UNIX)
    if(WINDOW_USE_WAYLAND)
        set(WINDOW_PLATFORM "WAYLAND")
        set(WINDOW_SOURCES window/window_wayland.cpp)
        list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_WAYLAND WINDOW_USE_WAYLAND)
    else()
        set(WINDOW_PLATFORM "X11")
        set(WINDOW_SOURCES window/window_x11.cpp)
        list(APPEND WINDOW_DEFINES WINDOW_PLATFORM_X11)
    endif()
endif()

message(STATUS "Window library platform: ${WINDOW_PLATFORM}")

# Add platform-independent source files
list(APPEND WINDOW_SOURCES window/window.cpp api/graphics_config.cpp input/input_keyboard.cpp input/input_mouse.cpp)

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
elseif(UNIX)
    list(APPEND WINDOW_SOURCES device/device_linux.cpp)
endif()

#=============================================================================
# Find Dependencies
#=============================================================================

# OpenGL
if(WINDOW_ENABLE_OPENGL)
    if(WINDOW_PLATFORM STREQUAL "WIN32")
        list(APPEND WINDOW_SOURCES api/api_opengl.cpp api/glad.c)
        list(APPEND WINDOW_LIBS opengl32)
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL)
    elseif(WINDOW_PLATFORM STREQUAL "X11")
        list(APPEND WINDOW_SOURCES api/api_opengl_glx.cpp)
        find_package(OpenGL REQUIRED)
        list(APPEND WINDOW_LIBS OpenGL::GL)
        list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_OPENGL)
    elseif(WINDOW_PLATFORM STREQUAL "WAYLAND")
        list(APPEND WINDOW_SOURCES api/api_opengl_egl.cpp)
        find_package(OpenGL REQUIRED COMPONENTS EGL)
        list(APPEND WINDOW_LIBS OpenGL::EGL OpenGL::GL)
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
    endif()
endif()

# Vulkan
if(WINDOW_ENABLE_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        if(WINDOW_PLATFORM STREQUAL "WIN32" OR
           WINDOW_PLATFORM STREQUAL "X11" OR
           WINDOW_PLATFORM STREQUAL "WAYLAND" OR
           WINDOW_PLATFORM STREQUAL "MACOS" OR
           WINDOW_PLATFORM STREQUAL "ANDROID")
            list(APPEND WINDOW_SOURCES api/api_vulkan.cpp)
            list(APPEND WINDOW_LIBS Vulkan::Vulkan)
            list(APPEND WINDOW_DEFINES WINDOW_SUPPORT_VULKAN)
            message(STATUS "Vulkan support enabled")
        endif()
    else()
        message(STATUS "Vulkan not found, Vulkan support disabled")
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
if(WINDOW_PLATFORM STREQUAL "WIN32")
    list(APPEND WINDOW_LIBS user32 gdi32)
elseif(WINDOW_PLATFORM STREQUAL "UWP")
    # UWP uses Windows Runtime
elseif(WINDOW_PLATFORM STREQUAL "X11")
    find_package(X11 REQUIRED)
    list(APPEND WINDOW_LIBS ${X11_LIBRARIES})
    # Xrandr for monitor enumeration
    if(X11_Xrandr_FOUND)
        list(APPEND WINDOW_LIBS ${X11_Xrandr_LIB})
    else()
        find_library(XRANDR_LIB Xrandr)
        if(XRANDR_LIB)
            list(APPEND WINDOW_LIBS ${XRANDR_LIB})
        endif()
    endif()
elseif(WINDOW_PLATFORM STREQUAL "WAYLAND")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND REQUIRED wayland-client wayland-egl)
    list(APPEND WINDOW_LIBS ${WAYLAND_LIBRARIES})
    include_directories(${WAYLAND_INCLUDE_DIRS})
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
endif()

#=============================================================================
# Examples
#=============================================================================

if(WINDOW_BUILD_EXAMPLES AND NOT ANDROID AND NOT IOS)
    # Basic window example
    add_executable(example_basic examples/basic.cpp)
    target_link_libraries(example_basic PRIVATE window)

    # OpenGL example
    if(WINDOW_ENABLE_OPENGL AND "WINDOW_SUPPORT_OPENGL" IN_LIST WINDOW_DEFINES)
        add_executable(example_opengl examples/opengl.cpp)
        target_link_libraries(example_opengl PRIVATE window)
    endif()

    # Vulkan example
    if(Vulkan_FOUND AND "WINDOW_SUPPORT_VULKAN" IN_LIST WINDOW_DEFINES)
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
    endif()

    # Metal example (macOS only)
    if(WINDOW_PLATFORM STREQUAL "MACOS" AND WINDOW_ENABLE_METAL)
        add_executable(example_metal examples/metal.mm)
        target_link_libraries(example_metal PRIVATE window)
    endif()
endif()

#=============================================================================
# Installation
#=============================================================================

include(GNUInstallDirs)

install(TARGETS window
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
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/input
)

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
message(STATUS "  Vulkan:      ${Vulkan_FOUND}")
if(WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
    message(STATUS "  D3D11:       ${WINDOW_ENABLE_D3D11}")
    message(STATUS "  D3D12:       ${WINDOW_ENABLE_D3D12}")
endif()
if(APPLE)
    message(STATUS "  Metal:       ${WINDOW_ENABLE_METAL}")
endif()
message(STATUS "  Examples:    ${WINDOW_BUILD_EXAMPLES}")
message(STATUS "")
