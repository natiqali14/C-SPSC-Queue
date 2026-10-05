add_executable(cold_lf_spsc_benchmark
    benchmark.cpp
)

target_link_libraries(cold_lf_spsc_benchmark
    PRIVATE
        cold_lf_spsc_queue
)

target_include_directories(cold_lf_spsc_benchmark
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)