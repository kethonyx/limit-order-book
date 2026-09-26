cmake_minimum_required(VERSION 3.20)

project(order_book
    VERSION 0.1.0
    DESCRIPTION "Limit order book matching engine"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LOB_BUILD_TESTS "Build unit tests" ON)
option(LOB_BUILD_BENCH "Build benchmarks" OFF)
option(LOB_SANITIZE    "Enable AddressSanitizer + UndefinedBehaviorSanitizer" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

# --- Core library -----------------------------------------------------------
add_library(lob STATIC
    src/types.cpp
    src/order_book.cpp
    src/parser.cpp)
add_library(lob::lob ALIAS lob)

target_include_directories(lob PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
lob_set_warnings(lob)
lob_enable_sanitizers(lob)

# --- CLI demo ---------------------------------------------------------------
add_executable(lob_cli apps/cli/main.cpp)
target_link_libraries(lob_cli PRIVATE lob::lob)
lob_set_warnings(lob_cli)

# --- Tests ------------------------------------------------------------------
if(LOB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(LOB_BUILD_BENCH)
    add_subdirectory(bench)
endif()
