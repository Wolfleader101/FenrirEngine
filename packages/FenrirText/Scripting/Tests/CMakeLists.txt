# Text from C#, through the scripting fixture harness: an App with assets and the app's FontLibrary, and scripting
# running a fixture script assembly that uses the Fenrir.Text contract; C++ lays the same text out in the same library
# and compares. The no-op rules (NoOps.cpp) are the rules tests, with no .NET.
fenrir_scripting_rules_tests(Text SOURCES NoOps.cpp)
fenrir_scripting_tests(Text SOURCES TextScriptingApp.hpp Paragraphs.cpp)
fenrir_script_fixture(Text)
# The asset root, HarfBuzz's test directory, which FenrirText's own tests use too, and the font the fixture registers by
# its asset path under it, one of HarfBuzz's test fonts.
target_compile_definitions(FenrirTextScriptingTests PRIVATE
    FENRIR_TEXT_HARFBUZZ_TEST="${CMAKE_CURRENT_SOURCE_DIR}/../../libs/harfbuzz/test")
set_property(TEST FenrirTextScripting APPEND PROPERTY ENVIRONMENT
    "FENRIR_TEST_TEXT_FONT=subset/data/fonts/Roboto-Regular.ttf")
