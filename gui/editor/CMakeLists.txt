cmake_minimum_required(VERSION 3.16)
project(gui_editor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

#=============================================================================
# Editor library (reusable editor logic)
#=============================================================================

add_library(gui_editor_lib STATIC
    editor.cpp
    editor_canvas.cpp
    editor_commands.cpp
    editor_inspector.cpp
    editor_palette.cpp
)

target_include_directories(gui_editor_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(gui_editor_lib PUBLIC window)

if(MSVC)
    target_compile_options(gui_editor_lib PRIVATE /FS)
endif()

#=============================================================================
# Editor executable
#=============================================================================

add_executable(gui_editor main.cpp)
target_link_libraries(gui_editor PRIVATE gui_editor_lib)

if(WIN32)
    target_link_libraries(gui_editor PRIVATE opengl32)
endif()

if(MSVC)
    target_compile_options(gui_editor PRIVATE /FS)
endif()
