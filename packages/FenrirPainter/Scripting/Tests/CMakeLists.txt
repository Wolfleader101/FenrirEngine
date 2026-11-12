# The painter from C#, through the scripting fixture harness: an App with assets, the renderer on the null backend,
# the painter's shader and the app's FontLibrary, and scripting running a fixture script assembly that uses the
# Fenrir.Painter contract (with the Input, Assets and Text contracts it builds on). What C# painted on the overlay is
# read back from the renderer's `Overlay 2D` pass through RendererProbe, beside the same Painter calls made in C++.
# The no-op rules (NoOps.cpp) are the rules tests, with no .NET.
fenrir_scripting_rules_tests(Painter SOURCES NoOps.cpp)
fenrir_scripting_tests(Painter SOURCES PainterScriptingApp.hpp Paints.cpp)
fenrir_script_fixture(Painter USES Input Assets Text)
# RendererProbe, from the renderer's own tests, and the renderer's sources it reads.
target_include_directories(FenrirPainterScriptingTests PRIVATE ../../../FenrirRenderer/Tests ../../../FenrirRenderer/src)
target_link_libraries(FenrirPainterScriptingTests PRIVATE FenrirRendererCore)
