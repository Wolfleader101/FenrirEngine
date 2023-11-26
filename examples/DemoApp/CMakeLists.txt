cmake_minimum_required(VERSION 3.20)
project(DemoApp)

# Set the executable for the demo app
add_executable(DemoApp
    src/main.cpp
)

add_subdirectory(libs)

# Link against the modules from your engine
target_link_libraries(DemoApp PRIVATE FenrirApp)

target_link_libraries(DemoApp PUBLIC
    glad
    glfw
)