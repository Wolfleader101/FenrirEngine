# FenrirPhysics's tests: the App driven one frame at a time with a chosen delta, against real Jolt (ADR-0010: no fake
# backend). No window, no .NET.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirPhysicsTests PublicHeaders.cpp Bodies.cpp Shapes.cpp Worlds.cpp Materials.cpp CollisionLayers.cpp
    Threading.cpp Queries.cpp Interpolation.cpp)
target_link_libraries(FenrirPhysicsTests PRIVATE FenrirPhysics Catch2::Catch2WithMain)
add_test(NAME FenrirPhysics COMMAND FenrirPhysicsTests)
set_tests_properties(FenrirPhysics PROPERTIES TIMEOUT 600) # a deadlocked step fails rather than hangs
# The shape-sharing diagnostic is private to FenrirPhysics: only its tests reach src/ (never Jolt's headers).
target_include_directories(FenrirPhysicsTests PRIVATE ../src)
