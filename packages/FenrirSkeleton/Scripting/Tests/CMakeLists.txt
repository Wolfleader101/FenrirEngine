# Skeletons from C#, through the scripting fixture harness: an App with no window, assets and skeletons installed, and
# scripting running a fixture script assembly that uses the Fenrir.Skeleton contract, its stage systems in each pose
# stage (spec seam 7). The rules tests (Rules.cpp) are the natives' rules with no .NET: no skeleton installed, an entity
# that is not Ready, a stage native outside its stage, each warning once under the native's name.
fenrir_scripting_rules_tests(Skeleton SOURCES Rules.cpp)
fenrir_scripting_tests(Skeleton SOURCES SkeletonScriptingApp.hpp SkeletonScriptingTests.cpp)
fenrir_script_fixture(Skeleton USES Assets)
# The animation tests' rig, a skinned glTF the SpawnModel case loads.
target_compile_definitions(FenrirSkeletonScriptingTests PRIVATE
    FENRIR_SKELETON_SCRIPTING_RIG="${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirAnimation/Tests/fixtures/rig.gltf")
