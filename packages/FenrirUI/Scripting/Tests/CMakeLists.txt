# The UI from C#, through the scripting fixture harness: an App with assets, input and the UI, and scripting running a
# fixture script assembly that uses the Fenrir.UI contract (with the Input, Assets, Text and Painter contracts it builds
# on). Roots draw into capturing targets: C# builds trees and reads rectangles beside the same calls made in C++, reads
# clicks fed through FenrirInput's test feed, makes elements in the Fenrir.UI.Build stage and paints into an element in
# the Fenrir.UI.Paint stage. The fixture imports Fenrir.UI.Razor.props and holds Razor components, which the tests
# mount and read back from C++, and see go when the script reloads: rebuilds, child components, handlers, paint
# overrides, placed documents and scoped component stylesheets among them.
fenrir_scripting_tests(UI SOURCES UIScriptingApp.hpp Roots.cpp BuildStage.cpp Razor.cpp RazorHandlers.cpp
    RazorPaint.cpp RazorSheets.cpp)
fenrir_script_fixture(UI USES Input Assets Text Painter UIRazor RazorComponents)
# Hot.razor's sheet, which the fixture records (its project file) and RazorSheets.cpp writes and rewrites: under the
# fixture's intermediate folder, which fenrir_script_fixture passes it.
target_compile_definitions(FenrirUIScriptingTests PRIVATE
    "FENRIR_TEST_UI_HOT_SHEET=\"${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/hot/Hot.razor.css\"")
