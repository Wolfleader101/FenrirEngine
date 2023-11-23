cmake_minimum_required(VERSION 3.20)
project(DemoApp)

# Set the executable for the demo app
add_executable(DemoApp
    src/main.cpp
    libs/dotnet/coreclr_delegates.h
    libs/dotnet/hostfxr.h
)

target_include_directories(DemoApp PUBLIC
    libs/
)

set(BUILD_FENRIR_APP ON)
set(BUILD_FENRIR_CORE OFF)
set(BUILD_FENRIR_ECS OFF)

# Link against the modules from your engine
target_link_libraries(DemoApp PRIVATE FenrirApp)


set(CS_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/managed/Fenrir.Managed")
set(CS_OUTPUT_DIR "${CS_PROJECT_DIR}/bin/Debug")



# Custom command to build the C# project
add_custom_target(BuildCsProj
    COMMAND dotnet build "${CS_PROJECT_DIR}"
    WORKING_DIRECTORY "${CS_PROJECT_DIR}"
    COMMENT "Building C# Project"
)

add_dependencies(DemoApp BuildCsProj)

# Custom command to copy the C# build output after building the C# project
add_custom_command(TARGET DemoApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CS_OUTPUT_DIR}/Fenrir.Managed.dll"
        "$<TARGET_FILE_DIR:DemoApp>"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CS_OUTPUT_DIR}/Fenrir.Managed.runtimeconfig.json"
        "$<TARGET_FILE_DIR:DemoApp>"
    DEPENDS BuildCsProj
    COMMENT "Copying C# build output"
)