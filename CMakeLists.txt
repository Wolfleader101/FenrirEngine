cmake_minimum_required(VERSION 3.20)
project(FenrirEngine)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# compile_commands.json in the build directory, for clangd and other tools (see .clangd).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FENRIR_ENABLE_SCRIPTING "Build the Iris .NET hosting library (packages/Iris)" OFF)
option(FENRIR_ENABLE_PHYSICS "Build FenrirPhysics and JoltPhysics (the packages/FenrirPhysics/libs/JoltPhysics submodule)" ON)
option(FENRIR_ENABLE_ASSETS "Build FenrirAssets with cgltf, ufbx and stb (the packages/FenrirAssets/libs submodules)" ON)
option(FENRIR_ENABLE_RENDERER "Build FenrirWindow and SDL3 (the packages/FenrirWindow/libs/SDL submodule), and FenrirRenderer's FenrirShaderCompiler with glslang and SPIRV-Reflect, the renderer core with its graphics device, null backend and OpenGL backend (with the checked-in glad loader), and FenrirRenderer's ECS facade" ON)
option(FENRIR_BUILD_TESTS "Build the package tests (Catch2: the libs/catch2 submodule, or an installed Catch2 v3)" OFF)
option(FENRIR_BUILD_EXAMPLES "Build the demo apps on the demo scaffold (examples/; with the renderer only)" ON)
option(FENRIR_BUILD_LEGACY_DEMOAPP "Build the legacy DemoApp (needs its third-party libraries in examples/DemoApp/libs, which are not submodules)" OFF)

# The renderer draws assets, so it cannot be built without them.
if(FENRIR_ENABLE_RENDERER AND NOT FENRIR_ENABLE_ASSETS)
    message(FATAL_ERROR "FENRIR_ENABLE_RENDERER needs FENRIR_ENABLE_ASSETS: turn assets on, or the renderer off.")
endif()

# fenrir_add_shaders(), for the engine's shaders and users' alike. Defined before any package, so a package's tests can
# build shader fixtures whatever order the packages are added in.
if(FENRIR_ENABLE_RENDERER)
    include(cmake/FenrirShaders.cmake)
endif()

# One CTest run for every package that has tests.
if(FENRIR_BUILD_TESTS)
    enable_testing()
endif()


# Compiler-specific flags
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR
    "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    add_compile_options(
        -Weverything -fcolor-diagnostics
        -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded
        -Wno-deprecated-declarations -Wno-exit-time-destructors
        -Wno-switch-enum -Wno-weak-vtables -Wno-global-constructors
        -Wno-newline-eof
    )
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -fdiagnostics-color=always)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    add_compile_options(/W4)
endif()

if(FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/Iris)
endif()

add_subdirectory(packages/FenrirMath)
add_subdirectory(packages/FenrirECS)
add_subdirectory(packages/FenrirCamera)
add_subdirectory(packages/FenrirTime)
add_subdirectory(packages/FenrirScheduler)
add_subdirectory(packages/FenrirLogger)
add_subdirectory(packages/FenrirScene)

add_subdirectory(packages/FenrirCore)
add_subdirectory(packages/FenrirApp)
add_subdirectory(packages/FenrirInput)

if(FENRIR_ENABLE_ASSETS)
    add_subdirectory(packages/FenrirAssets)
    add_subdirectory(packages/FenrirDraw2D)
endif()
# Always built: it links no library (ADR-0025).
add_subdirectory(packages/FenrirDebugDraw)
if(FENRIR_ENABLE_RENDERER)
    add_subdirectory(packages/FenrirWindow)
    add_subdirectory(packages/FenrirRenderer)
endif()

if(FENRIR_ENABLE_PHYSICS)
    add_subdirectory(packages/FenrirPhysics)
endif()

if(FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirScripting)
endif()

# Each package's scripting extension builds against the scripting SDK, so comes after FenrirScripting. Input's needs only
# scripting (FenrirInput is always built); Window's builds with the renderer, and after Input's, whose contract it uses.
if(FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirInput/Scripting)
endif()
# DebugDraw's needs only scripting too (FenrirDebugDraw is always built).
if(FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirDebugDraw/Scripting)
endif()
if(FENRIR_ENABLE_RENDERER AND FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirWindow/Scripting)
endif()
# Assets's builds with assets on.
if(FENRIR_ENABLE_ASSETS AND FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirAssets/Scripting)
endif()
# Renderer's builds with the renderer, after Input's and Assets's, whose contracts it uses.
if(FENRIR_ENABLE_RENDERER AND FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirRenderer/Scripting)
endif()

# Physics's scripting extension needs both, and comes after FenrirScripting (it builds against the scripting SDK).
if(FENRIR_ENABLE_PHYSICS AND FENRIR_ENABLE_SCRIPTING)
    add_subdirectory(packages/FenrirPhysics/Scripting)
endif()

if(FENRIR_BUILD_EXAMPLES OR FENRIR_BUILD_LEGACY_DEMOAPP)
    add_subdirectory(examples)
endif()
