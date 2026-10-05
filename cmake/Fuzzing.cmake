if(LG_BUILD_FUZZ)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "LG_BUILD_FUZZ requires clang (libFuzzer); use the 'fuzz' preset")
  endif()
  # Libraries need coverage instrumentation too, otherwise libFuzzer cannot see
  # which branches of the parser an input reached.
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()
