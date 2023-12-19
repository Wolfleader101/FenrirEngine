cmake_minimum_required(VERSION 3.20)
project(FenrirScripting)

add_library(FenrirScripting STATIC
    src/Host.cpp
    include/FenrirScripting/Host.hpp
)

add_subdirectory(libs)

target_include_directories(FenrirScripting PUBLIC include)

target_link_libraries(FenrirScripting PUBLIC FenrirApp)

set_property(GLOBAL PROPERTY CS_PROJECT_NAME "Fenrir.Managed")
set_property(GLOBAL PROPERTY CS_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Managed/Fenrir.Managed")
set_property(GLOBAL PROPERTY CS_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Managed/Fenrir.Managed/bin/Debug")

get_property(CS_PROJECT_NAME GLOBAL PROPERTY CS_PROJECT_NAME)
get_property(CS_PROJECT_DIR GLOBAL PROPERTY CS_PROJECT_DIR)
get_property(CS_OUTPUT_DIR GLOBAL PROPERTY CS_OUTPUT_DIR)

add_custom_target(BuildCsProj
    COMMAND dotnet build "${CS_PROJECT_DIR}"
    WORKING_DIRECTORY "${CS_PROJECT_DIR}"
    COMMENT "Building C# Project"
)

# add_dependencies(FenrirScripting BuildCsProj)
include(${CMAKE_SOURCE_DIR}/cmake/FenrirCMakeUtils.cmake)
