# Animation from C#, through the scripting fixture harness: an App with no window, assets, skeletons and animation
# installed, and scripting running a fixture script assembly that uses the Fenrir.Animation contract (spec seam 7). The
# rules tests (Rules.cpp) are the natives' rules with no .NET: no store, a stale handle, an event index out of range, each
# warning once under the native's name.
fenrir_scripting_rules_tests(Animation SOURCES Rules.cpp)
fenrir_scripting_tests(Animation SOURCES AnimationScriptingApp.hpp AnimationScriptingTests.cpp TEST_ARGS --order decl)
fenrir_script_fixture(Animation USES Assets Skeleton)
# The animation tests' rig, a skinned glTF with clips.
target_compile_definitions(FenrirAnimationScriptingTests PRIVATE
    FENRIR_ANIMATION_SCRIPTING_RIG="${CMAKE_CURRENT_SOURCE_DIR}/../../Tests/fixtures/rig.gltf")
