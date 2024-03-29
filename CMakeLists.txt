cmake_minimum_required(VERSION 3.20)
project(FenrirEngine)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# Compiler-specific flags
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    add_compile_options(
        -Weverything -fcolor-diagnostics
        -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded
        -Wno-deprecated-declarations -Wno-exit-time-destructors
        -Wno-switch-enum -Wno-weak-vtables -Wno-global-constructors
        -Wno-newline-eof
    )
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -fdiagnostics-color=always)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_compile_options(/W4)
endif()

add_subdirectory(packages/FenrirMath)
add_subdirectory(packages/FenrirECS)
add_subdirectory(packages/FenrirCamera)
add_subdirectory(packages/FenrirTime)
add_subdirectory(packages/FenrirScheduler)
add_subdirectory(packages/FenrirLogger)
add_subdirectory(packages/FenrirScene)

add_subdirectory(packages/FenrirCore)
add_subdirectory(packages/FenrirApp)

add_subdirectory(examples)
