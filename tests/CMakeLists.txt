# The tests of the wotah library, in one doctest program.
# Run all:            ctest --preset windows-debug        (or build/Debug/wotah_tests)
# Skip the GPU ones:  wotah_tests --test-suite-exclude=gpu

# Shaders only the tests use: they exercise every pipeline option the example cube does not.
set(TEST_SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
wotah_compile_shaders(TEST_SHADERS ${TEST_SHADER_OUT}
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_test.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/water_test.frag)

add_executable(wotah_tests
    main.cpp
    test_utils.hpp
    buffer_tests.cpp
    camera_tests.cpp
    gpu_tests.cpp
    height_field_tests.cpp
    input_tests.cpp
    lighting_tests.cpp
    logger_tests.cpp
    mesh_data_tests.cpp
    mesh_tests.cpp
    physics_tests.cpp
    utils_tests.cpp
    vulkan_utils_tests.cpp
    ${TEST_SHADERS})
target_include_directories(wotah_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TEST_SHADER_OUT})
target_link_libraries(wotah_tests PRIVATE wotah doctest)
wotah_set_warnings(wotah_tests)

add_test(NAME wotah_tests COMMAND wotah_tests)
