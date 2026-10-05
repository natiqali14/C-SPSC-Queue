cmake_minimum_required(VERSION 3.20)

project(cold_lf_spsc_queue
    VERSION 1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(COLD_LF_BUILD_TESTING "Build tests" OFF)

option(COLD_LF_BUILD_BENCHMARKING "Build benchmarking" OFF)


# --------------------------------------------------
# GoogleTest
# --------------------------------------------------
if(COLD_LF_BUILD_TESTING)
include(FetchContent)
  FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
  )

  # Prevent GoogleTest from overriding your compiler/runtime settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)

endif(COLD_LF_BUILD_TESTING)

add_library(${PROJECT_NAME} INTERFACE
)

target_include_directories(${PROJECT_NAME} 
    INTERFACE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_sources(${PROJECT_NAME} INTERFACE
    src/spsc_bounded_queue.h
)

# --------------------------------------------------
# Tests
# --------------------------------------------------
if(COLD_LF_BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif(COLD_LF_BUILD_TESTING)

# --------------------------------------------------
# Benchmarking
# --------------------------------------------------
if(COLD_LF_BUILD_BENCHMARKING)
  enable_testing()
  add_subdirectory(benchmarks)
endif(COLD_LF_BUILD_BENCHMARKING)