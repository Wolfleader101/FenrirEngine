cmake_minimum_required(VERSION 3.20)
project(DemoApp)

# Set the executable for the demo app
add_executable(DemoApp
    src/main.cpp
)

# Link against the modules from your engine
target_link_libraries(DemoApp PRIVATE FenrirApp FenrirScripting)

include(${CMAKE_SOURCE_DIR}/cmake/FenrirCMakeUtils.cmake)


copy_dll_to_executable_target(DemoApp)