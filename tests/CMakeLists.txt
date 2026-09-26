include(FetchContent)

# Pinned release tarball: reproducible, and faster than a git clone.
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(lob_tests
    types_test.cpp)
target_link_libraries(lob_tests PRIVATE lob::lob GTest::gtest_main)
lob_set_warnings(lob_tests)

include(GoogleTest)
gtest_discover_tests(lob_tests)
