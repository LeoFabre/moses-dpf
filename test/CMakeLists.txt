# Helper macro: each test is a standalone exe registered with CTest.
function(moses_add_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/dsp
        ${CMAKE_SOURCE_DIR}/plugin)
    target_compile_features(${name} PRIVATE cxx_std_17)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

moses_add_test(test_dsp_math)
moses_add_test(test_lr_filter)
moses_add_test(test_band_splitter)
moses_add_test(test_envelope)
moses_add_test(test_compressor)
moses_add_test(test_gain_approx)
moses_add_test(test_multiband)

moses_add_test(test_anti_overlap)

add_executable(test_plugin_offline test_plugin_offline.cpp)
target_include_directories(test_plugin_offline PRIVATE
    ${CMAKE_SOURCE_DIR}/dsp
    ${CMAKE_SOURCE_DIR}/plugin)
target_compile_features(test_plugin_offline PRIVATE cxx_std_17)
add_test(NAME test_plugin_offline COMMAND test_plugin_offline)
