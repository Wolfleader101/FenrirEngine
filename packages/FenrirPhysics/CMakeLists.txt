# FenrirPhysics: rigid-body physics behind a seam (ADR-0010). Only src/Backend/Jolt/ includes Jolt, and Jolt is linked
# PRIVATE, so neither its headers nor its compile definitions reach FenrirPhysics's users.
add_subdirectory(libs)

add_library(FenrirPhysics STATIC
    include/FenrirPhysics/Characters.hpp
    include/FenrirPhysics/CollisionLayers.hpp
    include/FenrirPhysics/Components.hpp
    include/FenrirPhysics/DebugLines.hpp
    include/FenrirPhysics/Events.hpp
    include/FenrirPhysics/Joints.hpp
    include/FenrirPhysics/Physics.hpp
    include/FenrirPhysics/PhysicsWorld.hpp
    include/FenrirPhysics/Queries.hpp
    include/FenrirPhysics/Vehicles.hpp

    src/Bodies.cpp
    src/Bodies.hpp
    src/BodyComponents.hpp
    src/Characters.cpp
    src/Characters.hpp
    src/CollisionLayers.cpp
    src/Contacts.cpp
    src/Contacts.hpp
    src/DebugCanvas.cpp
    src/DebugCanvas.hpp
    src/FilterOf.hpp
    src/Hierarchy.hpp
    src/InactiveTracker.hpp
    src/Joints.cpp
    src/Joints.hpp
    src/JointBreaking.cpp
    src/JointState.cpp
    src/LayerNames.hpp
    src/OutOfRange.hpp
    src/Physics.cpp
    src/PhysicsState.hpp
    src/PhysicsWorld.cpp
    src/Poses.cpp
    src/Poses.hpp
    src/Queries.cpp
    src/Same.hpp
    src/SortAndSend.hpp
    src/Vehicles.cpp
    src/Vehicles.hpp
    src/VehicleSettings.cpp
    src/VehicleSettings.hpp
    src/WarnOnce.hpp
    src/WorldImpl.hpp

    src/Backend/Backend.hpp
    src/Backend/Primitive.hpp
    src/Backend/Jolt/JoltBackend.cpp
)

target_link_libraries(FenrirPhysics PUBLIC FenrirApp FenrirScene FenrirECS FenrirMath PRIVATE Jolt)

target_include_directories(FenrirPhysics PUBLIC include PRIVATE src)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
