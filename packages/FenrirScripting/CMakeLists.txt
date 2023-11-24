cmake_minimum_required(VERSION 3.20)
project(FenrirScripting)

add_library(FenrirScripting STATIC
    src/Host.cpp
    include/FenrirScripting/Host.hpp
)

add_subdirectory(libs)

target_include_directories(FenrirScripting PUBLIC include)

target_link_libraries(FenrirScripting PUBLIC FenrirApp)


set(CS_PROJECT_NAME "Fenrir.Managed")
set(CS_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Managed/${CS_PROJECT_NAME}")
set(CS_OUTPUT_DIR "${CS_PROJECT_DIR}/bin/Debug")

# Custom command to build the C# project
add_custom_target(BuildCsProj
    COMMAND dotnet build "${CS_PROJECT_DIR}"
    WORKING_DIRECTORY "${CS_PROJECT_DIR}"
    COMMENT "Building C# Project"
)

add_dependencies(FenrirScripting BuildCsProj)

# Custom command to copy the C# build output after building the C# project
add_custom_command(TARGET FenrirScripting POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CS_OUTPUT_DIR}/${CS_PROJECT_NAME}.dll"
        "$<TARGET_FILE_DIR:FenrirScripting>"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CS_OUTPUT_DIR}/${CS_PROJECT_NAME}.runtimeconfig.json"
        "$<TARGET_FILE_DIR:FenrirScripting>"
    DEPENDS BuildCsProj
    COMMENT "Copying C# build output"
)