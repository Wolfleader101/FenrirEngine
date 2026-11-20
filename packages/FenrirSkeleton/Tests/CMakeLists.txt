# FenrirSkeleton's tests: the spec's seam 8, headless, with no animation, renderer or physics. The App is driven one
# frame at a time with a chosen delta, and skeletons and models are built in code.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirSkeletonTests PublicHeaders.cpp Instances.cpp Generations.cpp Pipeline.cpp Modifiers.cpp
                                   PoseOverride.cpp Attachments.cpp TickPosed.cpp
                                   Spawning.cpp Install.cpp SkeletonTest.hpp)
target_link_libraries(FenrirSkeletonTests PRIVATE FenrirSkeleton Catch2::Catch2WithMain)
add_test(NAME FenrirSkeleton COMMAND FenrirSkeletonTests)

# Its registration against its description: each described type, default-constructed through its registered
# description, holds the defaults api/FenrirSkeleton.api.json records (tools/generate-type-registry writes the test).
target_sources(FenrirSkeletonTests PRIVATE Generated/FenrirSkeletonTypesDefaultsTests.cpp)
target_link_libraries(FenrirSkeletonTests PRIVATE FenrirTypesTesting)
