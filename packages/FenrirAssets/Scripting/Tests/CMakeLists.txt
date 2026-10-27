# The asset store from C#, through the scripting fixture harness: an App with assets installed, its asset root
# FenrirAssets's test fixtures, and scripting running a fixture script assembly that uses the Fenrir.Assets contract
# (spec seam 7). The no-op rules (NoOps.cpp) count warnings given once per native call site per process, so they run in
# declaration order. Both executables load models and images from FenrirAssets's test fixtures.
fenrir_scripting_rules_tests(Assets SOURCES NoOps.cpp TEST_ARGS --order decl)
fenrir_scripting_tests(Assets
    SOURCES
        AssetsScriptingApp.hpp
        Loading.cpp
        Meshes.cpp
        Images.cpp
        Materials.cpp
        Shaders.cpp
        Shapes.cpp
)
fenrir_script_fixture(Assets)
target_compile_definitions(FenrirAssetsScriptingTests PRIVATE
    FENRIR_ASSETS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../Tests/fixtures")
target_compile_definitions(FenrirAssetsScriptingRulesTests PRIVATE
    FENRIR_ASSETS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../Tests/fixtures")
