cmake_minimum_required(VERSION 3.20)
project(DemoApp)

add_executable(DemoApp
    src/main.cpp

    src/TextureLibrary.hpp
    src/TextureLibrary.cpp

    src/ShaderLibrary.hpp
    src/ShaderLibrary.cpp

    src/ModelLibrary.hpp
    src/ModelLibrary.cpp

    src/Window.hpp
    src/Window.cpp

    src/Events.hpp

    src/CameraController.hpp
    src/CameraController.cpp
)

add_subdirectory(libs)

# Link against the modules from your engine
target_link_libraries(DemoApp PRIVATE FenrirApp FenrirCamera FenrirMath FenrirScripting)

target_link_libraries(DemoApp PUBLIC
    glad
    glfw
    assimp
)

include(${CMAKE_SOURCE_DIR}/cmake/FenrirCMakeUtils.cmake)

copy_dll_to_executable_target(DemoApp)

# symlink the assets folder to the build directory
add_custom_command(TARGET DemoApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:DemoApp>/assets
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets
    COMMENT "symbolic link resources folder from ${CMAKE_SOURCE_DIR}/assets to $<TARGET_FILE_DIR:DemoApp>/assets"
)