cmake_minimum_required(VERSION 3.24)
project(VulkanSquare CXX)

set(CMAKE_CXX_STANDARD 17)
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

# Compile GLSL -> SPIR-V as a C array initializer (*.inc) that main.cpp #includes,
# so there are no .spv files to locate at runtime.
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
foreach(stage vert frag)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/shaders/square.${stage})
    set(out ${SHADER_OUT}/square.${stage}.inc)
    add_custom_command(
        OUTPUT  ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE} -mfmt=num ${src} -o ${out}
        DEPENDS ${src}
        COMMENT "Compiling square.${stage}")
    list(APPEND SHADERS ${out})
endforeach()

add_executable(square main.cpp ${SHADERS})
target_include_directories(square PRIVATE ${SHADER_OUT})
target_link_libraries(square PRIVATE Vulkan::Vulkan glfw)
