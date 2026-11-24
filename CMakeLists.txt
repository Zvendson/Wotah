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

# Compile GLSL -> SPIR-V as a C array initializer (*.inc) that the code #includes, so there are no .spv files to locate
# at runtime. Puts the generated files' paths into out_var; each shader becomes <output_dir>/<file name>.inc.
function(wotah_compile_shaders out_var output_dir)
    set(outputs "")
    foreach(source ${ARGN})
        get_filename_component(name ${source} NAME)
        set(output ${output_dir}/${name}.inc)
        add_custom_command(
            OUTPUT  ${output}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${output_dir}
            COMMAND ${Vulkan_GLSLC_EXECUTABLE} -mfmt=num ${source} -o ${output}
            DEPENDS ${source}
            COMMENT "Compiling ${name}")
        list(APPEND outputs ${output})
    endforeach()
    set(${out_var} ${outputs} PARENT_SCOPE)
endfunction()

set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
wotah_compile_shaders(SHADERS ${SHADER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube.frag)

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
    source/platform/window.hpp
    source/platform/window.cpp
    source/render/buffer.hpp
    source/render/buffer.cpp
    source/render/camera.hpp
    source/render/camera.cpp
    source/render/cube_geometry.hpp
    source/render/device.hpp
    source/render/device.cpp
    source/render/pipeline.hpp
    source/render/pipeline.cpp
    source/render/renderer.hpp
    source/render/renderer.cpp
    source/render/swapchain.hpp
    source/render/swapchain.cpp
    source/render/vulkan_handles.hpp
    source/render/vulkan_memory_allocator.cpp
    source/render/vulkan_utils.hpp
    source/render/vulkan_utils.cpp
    ${SHADERS})
# Project headers are included by their path from source/, e.g. "core/logger.hpp".
target_include_directories(wotah PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/source ${SHADER_OUT})
target_link_libraries(wotah PUBLIC Vulkan::Vulkan glfw spdlog::spdlog glm::glm GPUOpen::VulkanMemoryAllocator)
# glm's projections produce Vulkan's 0..1 depth range instead of OpenGL's -1..1.
target_compile_definitions(wotah PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)

add_executable(square source/main.cpp)
target_link_libraries(square PRIVATE wotah)

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