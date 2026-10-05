add_library(devproto STATIC
  src/crc16.cpp
  src/sample.cpp
  src/frame.cpp
  src/parser.cpp
)
add_library(devproto::devproto ALIAS devproto)

target_include_directories(devproto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(devproto PRIVATE lg_warnings)
