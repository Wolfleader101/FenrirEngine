cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(Iris LANGUAGES CXX)
endif()

option(IRIS_BUILD_EXAMPLES "Build the standalone hosting example" ON)
option(IRIS_BUILD_TESTS "Build hosting contract checks" OFF)

# Locates the .NET host pack; a parent project includes Iris only when it wants .NET.
include(cmake/Nethost.cmake)

add_subdirectory(Native)

if(IRIS_BUILD_EXAMPLES)
    add_subdirectory(Examples/Hosting)
endif()

if(IRIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
