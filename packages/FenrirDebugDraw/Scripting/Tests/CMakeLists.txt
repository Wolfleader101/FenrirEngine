# Debug draw from C#, through the scripting fixture harness: an App with debug drawing installed and scripting running a
# fixture script assembly that uses the Fenrir.DebugDraw contract (spec seam 7). What C# drew is read back through the
# scene's DebugDraw (seam 6). The no-op rules (NoOps.cpp) are the rules tests, with no .NET.
fenrir_scripting_rules_tests(DebugDraw SOURCES NoOps.cpp)
fenrir_scripting_tests(DebugDraw SOURCES DebugDrawScriptingApp.hpp Shapes.cpp Items.cpp)
fenrir_script_fixture(DebugDraw)
