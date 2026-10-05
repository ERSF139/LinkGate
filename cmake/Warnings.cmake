add_library(lg_warnings INTERFACE)

if(MSVC)
  target_compile_options(lg_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(lg_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wsign-conversion -Wshadow)
endif()
