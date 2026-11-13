# FenrirRagdoll's tests: the spec's seam 2, headless, against real Jolt and real FenrirSkeleton with no renderer. The
# App is driven one frame at a time with a chosen delta, and skeletons are built in code, never the downloaded demo
# assets (ADR-0052).
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirRagdollTests PublicHeaders.cpp Describe.cpp Build.cpp Following.cpp Simulating.cpp
                                  Powered.cpp Determinism.cpp RagdollTest.hpp)
target_link_libraries(FenrirRagdollTests PRIVATE FenrirRagdoll FenrirDebugDraw Catch2::Catch2WithMain)
add_test(NAME FenrirRagdoll COMMAND FenrirRagdollTests)
set_tests_properties(FenrirRagdoll PROPERTIES TIMEOUT 600) # a deadlocked step fails rather than hangs
