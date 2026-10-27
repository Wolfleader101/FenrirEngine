# The renderer from C#, through the scripting fixture harness: an App with assets and the renderer on the null backend
# installed, its asset root FenrirRenderer's test fixtures, and scripting running a fixture script assembly that uses the
# Fenrir.Renderer contract (spec seam 7). What C# drew is read back through RendererProbe (seam 1), from the renderer's
# own tests. The no-op rules (NoOps.cpp) are the rules tests, with no .NET; the model SpawnModel is given is loaded from
# the same fixtures.
fenrir_scripting_rules_tests(Renderer SOURCES NoOps.cpp)
fenrir_scripting_tests(Renderer SOURCES RendererScriptingApp.hpp Shapes.cpp Drawing.cpp Picking.cpp SpawnModel.cpp)
fenrir_script_fixture(Renderer USES Input Assets)
# RendererProbe, from the renderer's own tests, and the renderer's sources it reads.
target_include_directories(FenrirRendererScriptingTests PRIVATE ../../Tests ../../src)
target_link_libraries(FenrirRendererScriptingTests PRIVATE FenrirRendererCore)
target_compile_definitions(FenrirRendererScriptingTests PRIVATE
    FENRIR_RENDERER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../Tests/fixtures")
target_compile_definitions(FenrirRendererScriptingRulesTests PRIVATE
    FENRIR_RENDERER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../Tests/fixtures")
