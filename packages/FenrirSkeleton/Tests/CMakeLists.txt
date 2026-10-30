# FenrirSkeleton's tests: the spec's seam 8, headless, with no animation, renderer or physics. The App is driven one
# frame at a time with a chosen delta, and skeletons and models are built in code.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirSkeletonTests PublicHeaders.cpp Instances.cpp Generations.cpp Pipeline.cpp Modifiers.cpp
                                   PoseOverride.cpp Attachments.cpp
                                   Spawning.cpp Install.cpp SkeletonTest.hpp)
target_link_libraries(FenrirSkeletonTests PRIVATE FenrirSkeleton Catch2::Catch2WithMain)
add_test(NAME FenrirSkeleton COMMAND FenrirSkeletonTests)
