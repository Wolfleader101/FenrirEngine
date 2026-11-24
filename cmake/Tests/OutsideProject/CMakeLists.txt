# A project outside the engine, built against it as a game is: it adds the engine with add_subdirectory, the demo apps
# off and scripting on, compiles one material shader of its own, links every package and deploys scripting's run-time
# files beside its executable. It sets nothing the engine should give it (no C++ standard, no engine paths): if it needs
# a workaround to build, the engine is wrong. Run by hand, not by CTest; README.md says how and what to check.
cmake_minimum_required(VERSION 3.20)
project(OutsideProject LANGUAGES C CXX)

# The engine's options, set before it is added (they are option()s, so a normal variable set here wins under CMP0077).
set(FENRIR_BUILD_EXAMPLES OFF)
set(FENRIR_ENABLE_SCRIPTING ON)

# `f`: a short binary directory for the engine keeps MSVC's deepest object path (glslang's) under 260 characters.
add_subdirectory(../../.. f)

# Links only the app package and sets no C++ standard: it compiles only if FenrirApp gives its consumers C++20.
add_executable(outside_app_only src/AppOnly.cpp)
target_link_libraries(outside_app_only PRIVATE FenrirApp)

add_executable(outside_project src/main.cpp)
target_link_libraries(outside_project PRIVATE
    FenrirTypes FenrirMath FenrirECS FenrirTime FenrirScheduler FenrirLogger FenrirScene FenrirCore FenrirApp
    FenrirInput FenrirAssets FenrirDraw2D FenrirSkeleton FenrirAnimation FenrirText FenrirDebugDraw FenrirWindow
    FenrirRenderer FenrirPainter FenrirUI FenrirPhysics FenrirScripting
    FenrirInputScripting FenrirDebugDrawScripting FenrirWindowScripting FenrirAssetsScripting FenrirSkeletonScripting
    FenrirAnimationScripting FenrirRendererScripting FenrirTextScripting FenrirPainterScripting FenrirUIScripting
    FenrirPhysicsScripting
)

# Called from this directory: the shader includes the engine's shared headers, so it builds only if the engine's
# shader include directory reaches here. The container goes to <this binary dir>/shaders.
fenrir_add_shaders(outside_project PROGRAM OutsideFlat VERTEX shaders/Flat.vert FRAGMENT shaders/Flat.frag)

fenrir_deploy_scripting(outside_project EXTENSIONS Physics)
