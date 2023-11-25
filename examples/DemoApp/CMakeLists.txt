cmake_minimum_required(VERSION 3.20)
project(DemoApp)

add_executable(DemoApp
    src/main.cpp
)

target_link_libraries(DemoApp PRIVATE FenrirApp FenrirScripting)

include(${CMAKE_SOURCE_DIR}/cmake/FenrirCMakeUtils.cmake)

copy_dll_to_executable_target(DemoApp)