# Host unit tests for the Pulse-Eight core. They build with the host compiler, not the ESP-IDF toolchain.
cmake_minimum_required(VERSION 3.24)
project(PlumCecHostTests LANGUAGES CXX)

# ESPHome builds esp32 targets with -std=gnu++20. The tests use the same standard.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

include(FetchContent)
# doctest v2.4.12, pinned by tag and by the SHA-256 of the release archive.
FetchContent_Declare(
  doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
  URL_HASH SHA256=73381c7aa4dee704bd935609668cf41880ea7f19fa0504a200e13b74999c2d70
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(doctest)

set(COMPONENTS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../components)

add_executable(pulse8_core_test
  pulse8_core_test.cpp
  ${COMPONENTS_DIR}/pulse8/pulse8_core.cpp
)
target_include_directories(pulse8_core_test PRIVATE ${COMPONENTS_DIR})
target_link_libraries(pulse8_core_test PRIVATE doctest::doctest_with_main)
target_compile_options(pulse8_core_test PRIVATE -Wall -Wextra -Werror)

add_executable(ack_rules_test ack_rules_test.cpp)
target_include_directories(ack_rules_test PRIVATE ${COMPONENTS_DIR})
target_link_libraries(ack_rules_test PRIVATE doctest::doctest_with_main)
target_compile_options(ack_rules_test PRIVATE -Wall -Wextra -Werror)

enable_testing()
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pulse8_core_test)
doctest_discover_tests(ack_rules_test)
