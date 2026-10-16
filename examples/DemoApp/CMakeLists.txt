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

if(FENRIR_ENABLE_PHYSICS)
    target_link_libraries(DemoApp PRIVATE FenrirPhysics)
    target_compile_definitions(DemoApp PRIVATE FENRIR_PHYSICS)
endif()

if(FENRIR_ENABLE_SCRIPTING)
    target_link_libraries(DemoApp PRIVATE FenrirScripting)
    target_compile_definitions(DemoApp PRIVATE FENRIR_SCRIPTING)
    # Puts nethost, the Iris bridge and the Fenrir.Scripting contract next to the executable.
    fenrir_deploy_scripting(DemoApp)

    # The demo's script assembly, built with the .NET SDK Iris found straight into <exe dir>/scripts, which is where
    # assets/demoApp.feproj ("scriptAssembly") points, relative to the working directory like the assets. It is its own
    # target and always runs (dotnet skips what has not changed), so building just DemoScripts while the demo is running
    # drops the new build where the demo's hot reload is watching for it.
    file(GLOB demo_scripts_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/*.cs")
    add_custom_target(DemoScripts
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/scripts/DemoScripts.csproj"
            --configuration Release --output "$<TARGET_FILE_DIR:DemoApp>/scripts" -nologo -v:q
            "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
        SOURCES ${demo_scripts_sources} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/DemoScripts.csproj"
        COMMENT "Building the demo script assembly"
        VERBATIM
    )
    add_dependencies(DemoScripts FenrirScriptingContract)
    add_dependencies(DemoApp DemoScripts)
endif()

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