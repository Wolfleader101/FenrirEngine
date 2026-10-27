# The natives' rules (no window, an unknown cursor mode, the clipboard written up to a capacity) run from C++ through
# NativeCallHarness, with no .NET. The window from C# runs through the scripting fixture harness: an App with scripting
# running a fixture script assembly that uses the Fenrir.Window contract, with a hidden window. Tests tagged [display]
# need SDL's video to start, so they are run apart, and without a display they report skipped (Catch2's SKIP, exit code
# 4), never passed.
fenrir_scripting_rules_tests(Window SOURCES Rules.cpp TEST_ARGS "~[display]")
fenrir_scripting_tests(Window SOURCES WindowScriptingTests.cpp WindowScriptingApp.hpp TEST_ARGS "~[display]")
fenrir_script_fixture(Window USES Input)
# FenrirWindow's test probe, to resize the hidden window as a user would.
target_include_directories(FenrirWindowScriptingTests PRIVATE ../../Tests)
target_link_libraries(FenrirWindowScriptingTests PRIVATE FenrirWindowProbe)
# Everything that needs a hidden window: skipped without a display.
add_test(NAME FenrirWindowScriptingRules-Display COMMAND FenrirWindowScriptingRulesTests "[display]")
set_tests_properties(FenrirWindowScriptingRules-Display PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 4)
add_test(NAME FenrirWindowScripting-Display COMMAND FenrirWindowScriptingTests "[display]")
set_tests_properties(FenrirWindowScripting-Display PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 4
    ENVIRONMENT "${FENRIR_WINDOW_FIXTURE_ENVIRONMENT}")
