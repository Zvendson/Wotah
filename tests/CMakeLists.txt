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
    camera_tests.cpp
    gpu_tests.cpp
    input_tests.cpp
    logger_tests.cpp
    utils_tests.cpp
    ${TEST_SHADERS})
target_include_directories(wotah_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${TEST_SHADER_OUT})
target_link_libraries(wotah_tests PRIVATE wotah doctest)

add_test(NAME wotah_tests COMMAND wotah_tests)
