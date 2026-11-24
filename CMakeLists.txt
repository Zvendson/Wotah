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

# Compile GLSL -> SPIR-V as a C array initializer (*.inc) that the code #includes,
# so there are no .spv files to locate at runtime.
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
foreach(stage vert frag)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cube.${stage})
    set(out ${SHADER_OUT}/cube.${stage}.inc)
    add_custom_command(
        OUTPUT  ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} -mfmt=num ${src} -o ${out}
        DEPENDS ${src}
        COMMENT "Compiling cube.${stage}")
    list(APPEND SHADERS ${out})
endforeach()

add_executable(square
    source/main.cpp
    source/app.hpp
    source/app.cpp
    source/core/utils.hpp
    source/core/utils.cpp
    source/core/logger.hpp
    source/core/logger.cpp
    source/platform/window.hpp
    source/platform/window.cpp
    source/render/camera.hpp
    source/render/camera.cpp
    source/render/device.hpp
    source/render/device.cpp
    source/render/pipeline.hpp
    source/render/pipeline.cpp
    source/render/renderer.hpp
    source/render/renderer.cpp
    source/render/swapchain.hpp
    source/render/swapchain.cpp
    source/render/vulkan_handles.hpp
    source/render/vulkan_utils.hpp
    source/render/vulkan_utils.cpp
    ${SHADERS})
# Project headers are included by their path from source/, e.g. "core/logger.hpp".
target_include_directories(square PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/source ${SHADER_OUT})
target_link_libraries(square PRIVATE Vulkan::Vulkan glfw spdlog::spdlog glm::glm)
# glm's projections produce Vulkan's 0..1 depth range instead of OpenGL's -1..1.
target_compile_definitions(square PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE)
