# FenrirPhysics: rigid-body physics behind a seam (ADR-0010). Only src/Backend/Jolt/ includes Jolt, and Jolt is linked
# PRIVATE, so neither its headers nor its compile definitions reach FenrirPhysics's users.
# It registers its described types with FenrirTypes' type registry (ADR-0074), through the generated registration
# it compiles, so it links FenrirTypes PUBLIC: the generated header, public, gives a game Describe<T>.
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

target_link_libraries(FenrirPhysics PUBLIC FenrirApp FenrirScene FenrirECS FenrirMath FenrirDebugDraw PRIVATE Jolt)

target_include_directories(FenrirPhysics PUBLIC include PRIVATE src)

# The package's API description, api/FenrirPhysics.api.json (ADR-0042): its artefact whether or not scripting is built.
fenrir_describe_api(FenrirPhysics api/FenrirPhysics.api.toml)
# Its type registration (ADR-0074), from that description: `cmake --build <dir> --target
# fenrir-generate-type-registry` regenerates include/FenrirPhysics/Generated/ and src/Generated/.
fenrir_generate_type_registry(FenrirPhysics api/FenrirPhysics.api.toml)
target_sources(FenrirPhysics PRIVATE include/FenrirPhysics/Generated/FenrirPhysicsTypes.hpp
                                     src/Generated/FenrirPhysicsTypes.cpp)
target_link_libraries(FenrirPhysics PUBLIC FenrirTypes)

# Tests that use Catch2: only with -DFENRIR_BUILD_TESTS=ON (see the root CMakeLists.txt).
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
