add_executable(env_check env_check.cpp)
target_link_libraries(env_check PRIVATE lg_warnings)

add_executable(echo_blocking echo_blocking.cpp)
target_link_libraries(echo_blocking PRIVATE lg_warnings)

add_executable(loadgen loadgen/main.cpp)
target_link_libraries(loadgen PRIVATE lg_net devproto lg_warnings)
