cmake_minimum_required(VERSION 3.20)
project(lob LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LOB_WERROR "Treat warnings as errors" OFF)
option(LOB_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

# Warning and sanitizer flags shared by target. GCC and Clang only for now.
add_library(lob_warnings INTERFACE)
target_compile_options(lob_warnings INTERFACE -Wall -Wextra -Wpedantic)
if(LOB_WERROR)
  target_compile_options(lob_warnings INTERFACE -Werror)
endif()
if(LOB_SANITIZE)
  target_compile_options(lob_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(lob_warnings INTERFACE -fsanitize=address,undefined)
endif()

# Public headers and the naive reference book (the differential-testing oracle).
add_library(lob_ref INTERFACE)
target_include_directories(lob_ref INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/reference)
target_link_libraries(lob_ref INTERFACE lob_warnings)

# GoogleTest: prefer a system install (offline or restricted networks), else fetch it.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()
include(GoogleTest)

add_executable(ref_tests tests/reference_book_test.cpp)
target_link_libraries(ref_tests PRIVATE lob_ref GTest::gtest_main)
gtest_discover_tests(ref_tests)

add_executable(pool_tests tests/order_pool_test.cpp)
target_link_libraries(pool_tests PRIVATE lob_warnings lob_ref GTest::gtest_main)
gtest_discover_tests(pool_tests)

add_executable(idmap_tests tests/id_map_test.cpp)
target_link_libraries(idmap_tests PRIVATE lob_warnings lob_ref GTest::gtest_main)
gtest_discover_tests(idmap_tests)

add_executable(price_bitmap_tests tests/price_bitmap_test.cpp)
target_link_libraries(price_bitmap_tests PRIVATE lob_warnings lob_ref GTest::gtest_main)
gtest_discover_tests(price_bitmap_tests)

add_executable(price_levels_tests tests/price_levels_test.cpp)
target_link_libraries(price_levels_tests PRIVATE lob_warnings lob_ref GTest::gtest_main)
gtest_discover_tests(price_levels_tests)

add_executable(book_add_cancel_tests tests/book_add_cancel_test.cpp)
target_link_libraries(book_add_cancel_tests PRIVATE lob_warnings lob_ref GTest::gtest_main)
gtest_discover_tests(book_add_cancel_tests)

add_executable(differential_tests tests/differential_test.cpp)
target_link_libraries(differential_tests PRIVATE lob_warnings lob_ref GTest::gtest_main)
gtest_discover_tests(differential_tests)

add_executable(invariant_tests tests/invariants_test.cpp)
target_link_libraries(invariant_tests PRIVATE lob_warnings lob_ref GTest::gtest_main)
gtest_discover_tests(invariant_tests)

add_executable(book_match_tests tests/book_match_test.cpp)
target_link_libraries(book_match_tests PRIVATE lob_warnings lob_ref GTest::gtest_main)
gtest_discover_tests(book_match_tests)
