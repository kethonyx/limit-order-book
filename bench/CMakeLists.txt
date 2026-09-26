add_executable(lob_bench bench_main.cpp)
target_link_libraries(lob_bench PRIVATE lob::lob)
lob_set_warnings(lob_bench)
