cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(FenrirScripting LANGUAGES CXX)
    option(FENRIR_ENABLE_SCRIPTING "Build the optional native .NET hosting package" ON)
endif()

if(DEFINED FENRIR_ENABLE_SCRIPTING AND NOT FENRIR_ENABLE_SCRIPTING)
    return()
endif()

option(FENRIR_SCRIPTING_BUILD_EXAMPLES "Build the standalone hosting example" ON)
option(FENRIR_SCRIPTING_BUILD_TESTS "Build hosting contract checks" OFF)

# Hosting dependencies are inspected only after the scripting enablement check.
include(cmake/Nethost.cmake)

# The managed bridge is still built separately with dotnet build.
add_subdirectory(Native/DotNetHost)

if(FENRIR_SCRIPTING_BUILD_EXAMPLES)
    add_subdirectory(Examples/Hosting)
endif()

if(FENRIR_SCRIPTING_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
