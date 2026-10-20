# FenrirPhysics's tests: the App driven one frame at a time with a chosen delta, against real Jolt (ADR-0010: no fake
# backend). No window, no .NET.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirPhysicsTests PublicHeaders.cpp Bodies.cpp Shapes.cpp Worlds.cpp Materials.cpp CollisionLayers.cpp
                                  Threading.cpp Queries.cpp Forces.cpp Interpolation.cpp Events.cpp Hierarchy.cpp
                                  DebugLines.cpp Joints.cpp JointCollisions.cpp JointSettings.cpp JointState.cpp
                                  JointBreaking.cpp JointEdges.cpp JointMoves.cpp MotionTests.cpp Characters.cpp
                                  CharacterShapes.cpp CharacterPoses.cpp CharacterSwitching.cpp CharacterPlatforms.cpp
                                  CharacterPushes.cpp CharacterDebugLines.cpp CharacterSlopes.cpp CharacterAmongBodies.cpp
                                  CharacterContacts.cpp VehicleSpike.cpp
                                  CharacterTest.hpp
                                  PhysicsProbe.hpp)
target_link_libraries(FenrirPhysicsTests PRIVATE FenrirPhysics Catch2::Catch2WithMain)
add_test(NAME FenrirPhysics COMMAND FenrirPhysicsTests)
set_tests_properties(FenrirPhysics PROPERTIES TIMEOUT 600) # a deadlocked step fails rather than hangs

# Reading a JointKind as the wrong kind asserts, which ends the process: a program of its own, checked by the assert's
# message. Only where asserts are compiled in.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_executable(JointKindWrongView JointKindWrongView.cpp)
    target_link_libraries(JointKindWrongView PRIVATE FenrirPhysics)
    add_test(NAME FenrirPhysics-JointKindWrongViewAsserts COMMAND JointKindWrongView)
    set_tests_properties(FenrirPhysics-JointKindWrongViewAsserts PROPERTIES
        PASS_REGULAR_EXPRESSION "JointKind::As: the joint is a different kind" TIMEOUT 60)
endif()
