cmake_minimum_required(VERSION 3.20)
project(DemoApp)


# Set the executable for the demo app
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

    src/Editor.hpp
    src/Editor.cpp

    src/GLRenderer.hpp
    src/GLRenderer.cpp

    src/Material.hpp
)

add_subdirectory(libs)

# Link against the modules from your engine
target_link_libraries(DemoApp PRIVATE FenrirApp FenrirCamera FenrirMath)

target_link_libraries(DemoApp PRIVATE
    imgui
    imguizmo
    glad
    glfw
    assimp
    glaze::glaze
    v8lib
)

target_compile_definitions(DemoApp PRIVATE 
V8_COMPRESS_POINTERS # WHAT A FUCKING STUPID FUCKING DUMB FUCKING FEATURE -- https://stackoverflow.com/questions/62921373/embedder-side-pointer-compression-is-disabled
V8_ENABLE_SANDBOX # SHITTY FUCKING DEFINITION NEEDED -- https://github.com/CauldronDevelopmentLLC/cbang/issues/117
V8_ENABLE_CHECKS  # FUCKING OBSECURE AS FUCK BUG CRASHING ON Isolate::Scope  destructor --- https://groups.google.com/g/v8-users/c/bt5QiMDIqzk/m/5x4wq4bUAAAJ?pli=1
)


add_custom_command(TARGET DemoApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    $<IF:$<CONFIG:Debug>,${CMAKE_CURRENT_SOURCE_DIR}/libs/v8/v8/lib/Debug,${CMAKE_CURRENT_SOURCE_DIR}/libs/v8/v8/lib/Release>
    $<TARGET_FILE_DIR:DemoApp>
    COMMENT "Copying V8 runtime DLLs to output directory"
)

# symlink the assets folder to the build directory
add_custom_command(TARGET DemoApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E create_symlink
    ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:DemoApp>/assets
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets
    COMMENT "symbolic link resources folder from ${CMAKE_SOURCE_DIR}/assets to $<TARGET_FILE_DIR:DemoApp>/assets"
)