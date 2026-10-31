# FenrirAnimation's tests: the spec's seam 9, headless, with no renderer or physics. The App is driven one frame at a
# time with a chosen delta; skeletons and clips are built in code, and the bone check plays a small glTF rig that
# fixtures/make_fixtures.py wrote.
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

add_executable(FenrirAnimationTests PublicHeaders.cpp Install.cpp Playback.cpp PlaybackCursor.cpp Blending.cpp
                                    Sampling.cpp Assets.cpp BoneCheck.cpp TickPosed.cpp AnimationTest.hpp)
target_link_libraries(FenrirAnimationTests PRIVATE FenrirAnimation Catch2::Catch2WithMain)
# One agreed test seam below the public interface (CODING_STANDARDS.md): the playback cursor's private header, so
# ADR-0034's interval table runs with no App. Nothing else in src/ is for tests.
target_include_directories(FenrirAnimationTests PRIVATE ../src)
target_compile_definitions(FenrirAnimationTests PRIVATE FENRIR_ANIMATION_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME FenrirAnimation COMMAND FenrirAnimationTests)
