cmake_minimum_required(VERSION 3.24)
project(VulkanSquare CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release keeps full optimizations but also emits debug info (PDB on MSVC, DWARF on GCC/Clang).
if(MSVC)
    add_compile_options($<$<CONFIG:Release>:/Zi>)
    # /DEBUG turns off /OPT:REF and /OPT:ICF by default, so turn them back on.
    add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
else()
    add_compile_options($<$<CONFIG:Release>:-g>)
endif()

# Strict warnings, every one an error, for the project's own targets (wotah, square, wotah_tests). Third-party headers
# are not checked: MSVC skips every <...> include (/external), GCC and Clang skip system include folders
# (wotah_mark_system below).
function(wotah_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /external:anglebrackets /external:W0)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
            -Wdouble-promotion -Werror)
    endif()
endfunction()

# Makes a third-party target's include folders system folders for whoever links it, so GCC and Clang do not report
# warnings inside its headers. (CMake 3.25 has FetchContent's SYSTEM option for this; 3.24 is the minimum here.)
function(wotah_mark_system target)
    get_target_property(directories ${target} INTERFACE_INCLUDE_DIRECTORIES)
    if(directories)
        set_target_properties(${target} PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${directories}")
    endif()
endfunction()

# Vulkan headers/loader + glslc (shader compiler) from the Vulkan SDK
find_package(Vulkan REQUIRED COMPONENTS glslc)

# GLFW for a cross-platform window (Windows / Linux X11 + Wayland)
include(FetchContent)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4)
FetchContent_MakeAvailable(glfw)

# spdlog for logging, built as a static library (with its bundled fmt) so only logger.cpp compiles its headers.
# Wide file names on Windows, so the log file opens next to the executable whatever characters its path has.
if(WIN32)
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.17.0)
FetchContent_MakeAvailable(spdlog)

# glm for vectors and matrices, header-only.
set(GLM_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3)
FetchContent_MakeAvailable(glm)

# The Vulkan Memory Allocator (AMD GPUOpen) picks and suballocates GPU memory for buffers and images; header-only,
# compiled once in render/vulkan_memory_allocator.cpp.
FetchContent_Declare(VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.5.0)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

# Jolt Physics for gravity, collisions and (later) floating in water; only physics/*.cpp include it. Its CMake project
# lives in Build/. It builds with this project's runtime and flags (not its own /Wall /WX set), without its samples,
# tests or link-time optimisation.
set(TARGET_UNIT_TESTS                OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD               OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST          OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES                   OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER                    OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY  OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS               OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS              OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION     OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        v5.6.0
    SOURCE_SUBDIR  Build)
FetchContent_MakeAvailable(JoltPhysics)

# Dear ImGui for the windows and widgets drawn over the scene (ui/), with its Vulkan backend (render/ui_renderer.cpp).
# It has no CMake project of its own, so its files are built here. Its GLFW backend is left out: it calls GLFW on the
# drawing thread, which GLFW forbids; ui/user_interface.cpp hands ImGui the window's input events instead. The tests add
# its null backend (tests/CMakeLists.txt).
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.93.0)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC Vulkan::Vulkan)
# Only today's names: what ImGui marks obsolete is left out, so nothing here starts to depend on it.
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# Their headers are theirs: no warnings from inside them (Vulkan::Vulkan is imported, so already a system folder).
foreach(third_party glfw spdlog glm VulkanMemoryAllocator Jolt imgui)
    wotah_mark_system(${third_party})
endforeach()

# Compile GLSL -> SPIR-V as a C array initializer (*.inc) that the code #includes, so there are no .spv files to locate
# at runtime. Puts the generated files' paths into out_var; each shader becomes <output_dir>/<file name>.inc. Shaders
# may #include the shared files in shaders/ (WOTAH_SHADER_INCLUDES); every shader is rebuilt when one of them changes.
set(WOTAH_SHADER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
# The most directional lights at once: one number for the C++ code (render/lighting.hpp) and the shaders
# (shaders/lighting.glsl), so their light arrays always match.
set(WOTAH_MAX_DIRECTIONAL_LIGHTS 8)
# How many shadow maps (cascades) each light has, nearest stretch of the view first: for C++ (render/shadow_maps.hpp)
# and the shaders (shaders/lighting.glsl).
set(WOTAH_SHADOW_CASCADES 4)
set(WOTAH_SHADER_INCLUDES ${WOTAH_SHADER_INCLUDE_DIR}/atmosphere.glsl ${WOTAH_SHADER_INCLUDE_DIR}/lighting.glsl)
function(wotah_compile_shaders out_var output_dir)
    set(outputs "")
    foreach(source ${ARGN})
        get_filename_component(name ${source} NAME)
        set(output ${output_dir}/${name}.inc)
        add_custom_command(
            OUTPUT  ${output}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} -mfmt=num -I ${WOTAH_SHADER_INCLUDE_DIR}
                    -DWOTAH_MAX_DIRECTIONAL_LIGHTS=${WOTAH_MAX_DIRECTIONAL_LIGHTS}
                    -DWOTAH_SHADOW_CASCADES=${WOTAH_SHADOW_CASCADES} ${source} -o ${output}
            DEPENDS ${source} ${WOTAH_SHADER_INCLUDES}
            COMMENT "Compiling ${name}")
        list(APPEND outputs ${output})
    endforeach()
    set(${out_var} ${outputs} PARENT_SCOPE)
endfunction()

set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
wotah_compile_shaders(SHADERS ${SHADER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ball.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert)

# Everything but the entry point, so the program and the tests run the same code.
add_library(wotah STATIC
    source/app.hpp
    source/app.cpp
    source/core/utils.hpp
    source/core/utils.cpp
    source/core/logger.hpp
    source/core/logger.cpp
    source/input/camera_controller.hpp
    source/input/camera_controller.cpp
    source/input/input_dispatcher.hpp
    source/input/input_dispatcher.cpp
    source/input/input_event.hpp
    source/input/input_handler.hpp
    source/input/scene_shortcuts.hpp
    source/input/scene_shortcuts.cpp
    source/physics/physics_world.hpp
    source/physics/physics_world.cpp
    source/platform/window.hpp
    source/platform/window.cpp
    source/render/buffer.hpp
    source/render/buffer.cpp
    source/render/camera.hpp
    source/render/camera.cpp
    source/render/device.hpp
    source/render/device.cpp
    source/render/height_field.hpp
    source/render/height_field.cpp
    source/render/image.hpp
    source/render/image.cpp
    source/render/lighting.hpp
    source/render/lighting.cpp
    source/render/mesh.hpp
    source/render/mesh.cpp
    source/render/mesh_data.hpp
    source/render/mesh_data.cpp
    source/render/pipeline.hpp
    source/render/pipeline.cpp
    source/render/renderer.hpp
    source/render/renderer.cpp
    source/render/shadow_maps.hpp
    source/render/shadow_maps.cpp
    source/render/sky.hpp
    source/render/sky.cpp
    source/render/sky_clock.hpp
    source/render/sky_clock.cpp
    source/render/sky_lights.hpp
    source/render/sky_lights.cpp
    source/render/swapchain.hpp
    source/render/swapchain.cpp
    source/render/ui_renderer.hpp
    source/render/ui_renderer.cpp
    source/render/vulkan_handles.hpp
    source/render/vulkan_memory_allocator.cpp
    source/render/vulkan_utils.hpp
    source/render/vulkan_utils.cpp
    source/ui/card.hpp
    source/ui/card.cpp
    source/ui/control_bar.hpp
    source/ui/control_bar.cpp
    source/ui/hints_window.hpp
    source/ui/hints_window.cpp
    source/ui/settings_window.hpp
    source/ui/settings_window.cpp
    source/ui/stats_window.hpp
    source/ui/stats_window.cpp
    source/ui/theme.hpp
    source/ui/theme.cpp
    source/ui/user_interface.hpp
    source/ui/user_interface.cpp
    ${SHADERS})
# Project headers are included by their path from source/, e.g. "core/logger.hpp".
target_include_directories(wotah PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source ${SHADER_OUT})
target_link_libraries(wotah PUBLIC Vulkan::Vulkan glfw spdlog::spdlog glm::glm GPUOpen::VulkanMemoryAllocator)
target_link_libraries(wotah PRIVATE Jolt) # only physics/*.cpp see it
target_link_libraries(wotah PRIVATE imgui) # only ui/*.cpp and render/ui_renderer.cpp see it
# glm's projections produce Vulkan's 0..1 depth range instead of OpenGL's -1..1.
target_compile_definitions(wotah PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE
    WOTAH_MAX_DIRECTIONAL_LIGHTS=${WOTAH_MAX_DIRECTIONAL_LIGHTS} WOTAH_SHADOW_CASCADES=${WOTAH_SHADOW_CASCADES})

wotah_set_warnings(wotah)

add_executable(square source/main.cpp)
target_link_libraries(square PRIVATE wotah)
wotah_set_warnings(square)

# Tests: doctest, in tests/; run them with "ctest --preset <preset>" or the wotah_tests program.
# doctest is one header; its own CMake file is skipped (SOURCE_SUBDIR has none) because it targets old CMake versions.
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.12
    SOURCE_SUBDIR  doctest)
FetchContent_MakeAvailable(doctest)
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE ${doctest_SOURCE_DIR})
enable_testing()
add_subdirectory(tests)