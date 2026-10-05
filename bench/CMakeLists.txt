include(FetchContent)
FetchContent_Declare(
  benchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

add_executable(devproto_bench devproto_bench.cpp)
target_link_libraries(devproto_bench PRIVATE devproto lg_warnings benchmark::benchmark)
