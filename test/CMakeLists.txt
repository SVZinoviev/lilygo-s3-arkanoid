# Host-side build for the Arkanoid game-logic tests.
#
# The game module is pure C with no ESP-IDF dependency, so it compiles and
# runs natively on the host. This is a standalone CMake project (NOT part of
# the firmware build).
#
# Usage:
#     cmake -S . -B build
#     cmake --build build
#     ctest --test-dir build --output-on-failure
#   or just run the binary directly:
#     ./build/test_arkanoid

cmake_minimum_required(VERSION 3.16)
project(arkanoid_game_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(test_arkanoid
    test_arkanoid_game.c
    ../main/arkanoid_game.c
)

target_include_directories(test_arkanoid PRIVATE ../main)

if(NOT MSVC)
    target_compile_options(test_arkanoid PRIVATE -Wall -Wextra -Werror -g)
endif()

enable_testing()
add_test(NAME arkanoid_logic COMMAND test_arkanoid)
