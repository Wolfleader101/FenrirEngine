cmake_minimum_required(VERSION 3.20)
project(DemoApp)

# Set the executable for the demo app
add_executable(DemoApp
    src/main.cpp
)

set(BUILD_FENRIR_APP ON)
set(BUILD_FENRIR_CORE OFF)
set(BUILD_FENRIR_ECS OFF)

# Link against the modules from your engine
target_link_libraries(DemoApp PRIVATE FenrirApp FenrirScripting)