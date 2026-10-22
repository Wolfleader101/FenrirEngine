cmake_minimum_required(VERSION 3.20)
project(FenrirEngine)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile_commands.json in the build directory, for clangd and other tools (see .clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FENRIR_ENABLE_SCRIPTING "Build the Iris .NET hosting library (packages/Iris)" OFF)
option(FENRIR_ENABLE_PHYSICS "Build FenrirPhysics and JoltPhysics (the packages/FenrirPhysics/libs/JoltPhysics submodule)" ON)
option(FENRIR_BUILD_TESTS "Build the package tests (Catch2: the libs/catch2 submodule, or an installed Catch2 v3)" OFF)
option(FENRIR_BUILD_EXAMPLES "Build the DemoApp (needs its third-party libraries in examples/DemoApp/libs, which are not submodules)" ON)

# One CTest run for every package that has tests.
if(FENRIR_BUILD_TESTS)
    enable_testing()
endif()


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

# fenrir_describe_api() and the fenrir-describe-api target, which packages that share types with bindings use (ADR-0016).
include(cmake/FenrirDescribeApi.cmake)
add_subdirectory(tools/describe-api)

if(FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/Iris)
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

if(FENRIR_ENABLE_PHYSICS)
    add_subdirectory(packages/FenrirPhysics)
endif()

if(FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirScripting)
endif()

# Physics's scripting extension needs both, and comes after FenrirScripting (it builds against the scripting SDK).
if(FENRIR_ENABLE_PHYSICS AND FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirPhysics/Scripting)
endif()

if(FENRIR_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
