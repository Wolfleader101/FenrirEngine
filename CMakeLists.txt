cmake_minimum_required(VERSION 3.20)
project(FenrirEngine)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
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


add_subdirectory(examples)


option(BUILD_FENRIR_APP "Build FenrirApp module" ON)
option(BUILD_FENRIR_CORE "Build FenrirCore module" ON)
option(BUILD_FENRIR_ECS "Build FenrirECS module" ON)

add_subdirectory(packages/FenrirTime)
add_subdirectory(packages/FenrirScheduler)
add_subdirectory(packages/FenrirLogger)

if(BUILD_FENRIR_APP)
    add_subdirectory(packages/FenrirApp)
endif()

if(BUILD_FENRIR_CORE)
    add_subdirectory(packages/FenrirCore)
endif()

if(BUILD_FENRIR_ECS)
    add_subdirectory(packages/FenrirECS)
endif()

