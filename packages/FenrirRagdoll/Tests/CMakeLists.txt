# FenrirRagdoll's spike checks (ticket 55): headless, with real Jolt and FenrirSkeleton, a humanoid-ish skeleton built
# in code and an Animate item standing in for a clip.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirRagdollTests RagdollSpike.cpp)
target_link_libraries(FenrirRagdollTests PRIVATE FenrirRagdoll FenrirAnimation FenrirDebugDraw Catch2::Catch2WithMain)
# SPIKE: the Quaternius human and its walk clip (AnimationDemo's committed assets), for the demo-rig check.
target_compile_definitions(FenrirRagdollTests PRIVATE FENRIR_RAGDOLL_DEMO_ASSETS="${CMAKE_SOURCE_DIR}/examples/AnimationDemo/assets")
add_test(NAME FenrirRagdoll COMMAND FenrirRagdollTests)
set_tests_properties(FenrirRagdoll PROPERTIES TIMEOUT 600)
