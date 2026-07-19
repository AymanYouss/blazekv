add_executable(blazekv-microbench microbench.cpp)
target_link_libraries(blazekv-microbench PRIVATE blazekv_core blazekv_flags benchmark::benchmark)
set_target_properties(blazekv-microbench PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
