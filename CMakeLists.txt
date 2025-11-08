cmake_minimum_required(VERSION 3.25)
project(nybit LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/std:clatest /W4 /WX)
else()
    add_compile_options(-std=c23 -Wall -Wextra -Werror -g)
endif()

include_directories(include)

# Nybit Core sources (nygen.lib)
file(GLOB_RECURSE NYGEN_SOURCES "src/*.c")
list(FILTER NYGEN_SOURCES EXCLUDE REGEX "src/main\\.c$")
list(FILTER NYGEN_SOURCES EXCLUDE REGEX "src/nyjit/.*\\.c$")

add_library(nygen_core STATIC ${NYGEN_SOURCES})

# JIT sources (nyjit.lib)
file(GLOB_RECURSE NYJIT_SOURCES "src/nyjit/*.c")
add_library(nyjit STATIC ${NYJIT_SOURCES})
target_link_libraries(nyjit PUBLIC nygen_core)

# CLI Executable
add_executable(nybit src/main.c)
target_link_libraries(nybit PRIVATE nygen_core)

# Test Suite Executable
file(GLOB_RECURSE TEST_SOURCES "tests/*.c")
add_executable(test_runner ${TEST_SOURCES})
target_include_directories(test_runner PRIVATE tests)
target_link_libraries(test_runner PRIVATE nyjit nygen_core)

enable_testing()
add_test(NAME NybitTestSuite COMMAND test_runner)
