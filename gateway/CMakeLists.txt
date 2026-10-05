add_library(lg_gateway STATIC
  src/config.cpp
  src/device_registry.cpp
  src/gateway.cpp
  src/hub.cpp
  src/signal_watcher.cpp
  src/stats.cpp
)
add_library(lg::gateway ALIAS lg_gateway)

target_include_directories(lg_gateway PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(lg_gateway PUBLIC lg_net devproto PRIVATE lg_warnings)

add_executable(linkgate main.cpp)
target_link_libraries(linkgate PRIVATE lg_gateway lg_warnings)
