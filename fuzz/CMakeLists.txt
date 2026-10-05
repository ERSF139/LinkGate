add_executable(parser_fuzz
  parser_fuzz.cpp
  ${PROJECT_SOURCE_DIR}/tests/support/parser_check.cpp
)
target_include_directories(parser_fuzz PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
target_link_libraries(parser_fuzz PRIVATE devproto lg_warnings)
target_link_options(parser_fuzz PRIVATE -fsanitize=fuzzer)
