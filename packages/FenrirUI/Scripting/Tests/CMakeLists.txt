# The UI from C#, through the scripting fixture harness: an App with assets, input and the UI, and scripting running a
# fixture script assembly that uses the Fenrir.UI contract (with the Input, Assets, Text and Painter contracts it builds
# on). Roots draw into capturing targets: C# builds trees and reads rectangles beside the same calls made in C++, reads
# clicks fed through FenrirInput's test feed, and paints into an element in the Fenrir.UI.Paint stage.
fenrir_scripting_tests(UI SOURCES UIScriptingApp.hpp Roots.cpp)
fenrir_script_fixture(UI USES Input Assets Text Painter)
