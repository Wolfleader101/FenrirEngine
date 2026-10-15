# FenrirPhysics: rigid-body physics behind a seam (ADR-0010). Only src/Backend/Jolt/ includes Jolt, and Jolt is linked
# PRIVATE, so neither its headers nor its compile definitions reach FenrirPhysics's users.
add_subdirectory(libs)

add_library(FenrirPhysics STATIC
    include/FenrirPhysics/CollisionLayers.hpp
    include/FenrirPhysics/Components.hpp
    include/FenrirPhysics/Physics.hpp
    include/FenrirPhysics/PhysicsWorld.hpp

    src/CollisionLayers.cpp
    src/Physics.cpp
    src/PhysicsWorld.cpp
    src/WorldImpl.hpp

    src/Backend/Backend.hpp
    src/Backend/Jolt/JoltBackend.cpp
)

target_link_libraries(FenrirPhysics PUBLIC FenrirApp FenrirScene FenrirECS FenrirMath PRIVATE Jolt)

target_include_directories(FenrirPhysics PUBLIC include PRIVATE src)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
