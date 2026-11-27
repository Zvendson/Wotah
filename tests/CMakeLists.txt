# The tests of the wotah library, in one doctest program.
# Run all:            ctest --preset windows-debug        (or build/Debug/wotah_tests)
# Skip the GPU ones:  wotah_tests --test-suite-exclude=gpu

# Shaders only the tests use: they exercise every pipeline option the example cube does not.
set(TEST_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
wotah_compile_shaders(TEST_SHADERS ${TEST_SHADER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ground_test.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/terrain_probe.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sea_probe.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_test.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_test.frag)

# ImGui's null backend: takes the UI's font pictures without drawing them, so the UI runs in tests without a GPU. It is
# ImGui's code, so it gets a library of its own, without the project's warnings; the tests reach ImGui through it.
add_library(imgui_null STATIC ${imgui_SOURCE_DIR}/backends/imgui_impl_null.cpp)
target_link_libraries(imgui_null PUBLIC imgui)

add_executable(wotah_tests
    main.cpp
    test_utils.hpp
    beach_tests.cpp
    buffer_tests.cpp
    camera_tests.cpp
    clipmap_tests.cpp
    gpu_tests.cpp
    height_field_tests.cpp
    input_tests.cpp
    land_collider_tests.cpp
    lighting_tests.cpp
    logger_tests.cpp
    mesh_data_tests.cpp
    mesh_tests.cpp
    mirror_view_tests.cpp
    physics_tests.cpp
    sea_tests.cpp
    sky_clock_tests.cpp
    sky_lights_tests.cpp
    terrain_shape_tests.cpp
    ui_tests.cpp
    utils_tests.cpp
    vulkan_utils_tests.cpp
    ${TEST_SHADERS})
target_include_directories(wotah_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TEST_SHADER_OUT})
target_link_libraries(wotah_tests PRIVATE wotah doctest imgui_null imguizmo) # ImGuizmo: to find the gizmo's arrows
wotah_set_warnings(wotah_tests)

add_test(NAME wotah_tests COMMAND wotah_tests)
