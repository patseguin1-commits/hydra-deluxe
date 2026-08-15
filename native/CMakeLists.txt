# Optional CMake build for Hydra's native scoring core.
#
# The supported build is `python native/build_native.py`, which drives the
# compiler directly and needs no CMake. This file exists so editors and IDEs
# that key off CMakeLists.txt can index the sources and resolve includes.
#
#   cmake -S native -B native/build
#   cmake --build native/build --config Release
#
# Output goes to native/build/, which is where hydra.hynative looks first.

cmake_minimum_required(VERSION 3.15)
project(hydra_score LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

add_library(hydra_score SHARED hydra_score.cpp hydra_search.cpp)

target_include_directories(hydra_score PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(MSVC)
    target_compile_options(hydra_score PRIVATE /O2 /W4 /EHsc)
else()
    target_compile_options(hydra_score PRIVATE -O2 -Wall -Wextra)
endif()

# hynative.py looks for an unprefixed name on every platform.
set_target_properties(hydra_score PROPERTIES
    OUTPUT_NAME "hydra_score"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/build"
)
