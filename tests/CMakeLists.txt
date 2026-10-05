add_executable(cold_lf_spsc_tests
    test_spsc_bounded_lf.cpp
)

target_link_libraries(cold_lf_spsc_tests
    PRIVATE
        cold_lf_spsc_queue
        GTest::gtest_main
)

target_include_directories(cold_lf_spsc_tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

include(GoogleTest)

gtest_discover_tests(cold_lf_spsc_tests)