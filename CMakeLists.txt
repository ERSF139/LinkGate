cmake_minimum_required(VERSION 3.25)
project(LinkGate VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LG_IS_LINUX ON)
else()
  set(LG_IS_LINUX OFF)
endif()

option(LG_BUILD_NET "Build epoll-based modules (Linux only)" ${LG_IS_LINUX})
option(LG_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(LG_BUILD_FUZZ "Build libFuzzer targets (requires clang)" OFF)
option(LG_BUILD_BENCH "Build Google Benchmark micro-benchmarks" OFF)

if(LG_BUILD_NET AND NOT LG_IS_LINUX)
  message(FATAL_ERROR "LG_BUILD_NET requires Linux (epoll)")
endif()

include(cmake/Warnings.cmake)
include(cmake/Sanitizers.cmake)
include(cmake/Fuzzing.cmake)

add_subdirectory(devproto)

if(LG_BUILD_NET)
  find_package(Threads REQUIRED)
  add_subdirectory(net)
  add_subdirectory(gateway)
  add_subdirectory(tools)
endif()

if(LG_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(LG_BUILD_FUZZ)
  add_subdirectory(fuzz)
endif()

if(LG_BUILD_BENCH)
  add_subdirectory(bench)
endif()

message(STATUS "LinkGate: LG_BUILD_NET=${LG_BUILD_NET} LG_BUILD_TESTS=${LG_BUILD_TESTS} "
               "LG_BUILD_FUZZ=${LG_BUILD_FUZZ} LG_BUILD_BENCH=${LG_BUILD_BENCH} "
               "LG_SANITIZER=${LG_SANITIZER}")
