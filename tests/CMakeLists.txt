include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(lg_tests
  support/golden.cpp
  support/parser_check.cpp
  devproto/crc16_test.cpp
  devproto/byte_order_test.cpp
  devproto/sample_test.cpp
  devproto/frame_test.cpp
  devproto/parser_test.cpp
  devproto/parser_property_test.cpp
)
target_include_directories(lg_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_compile_definitions(lg_tests PRIVATE
  LG_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/data/golden/frames.txt")
target_link_libraries(lg_tests PRIVATE devproto lg_warnings GTest::gtest_main)

if(LG_BUILD_NET)
  target_sources(lg_tests PRIVATE
    support/net_util.cpp
    support/quiet_logs.cpp
    net/unique_fd_test.cpp
    net/buffer_test.cpp
    net/event_loop_test.cpp
    net/timing_wheel_test.cpp
    net/tcp_server_test.cpp
    gateway/config_test.cpp
    gateway/gateway_test.cpp
  )
  target_link_libraries(lg_tests PRIVATE lg_net lg_gateway)
endif()

include(GoogleTest)
gtest_discover_tests(lg_tests DISCOVERY_TIMEOUT 30)
