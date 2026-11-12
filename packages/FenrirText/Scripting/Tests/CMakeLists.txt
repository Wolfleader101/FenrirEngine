# Text from C#, through the scripting fixture harness: an App with assets and the app's FontLibrary, and scripting
# running a fixture script assembly that uses the Fenrir.Text contract; C++ lays the same text out in the same library
# and compares. The no-op rules (NoOps.cpp) are the rules tests, with no .NET.
fenrir_scripting_rules_tests(Text SOURCES NoOps.cpp)
fenrir_scripting_tests(Text SOURCES TextScriptingApp.hpp Paragraphs.cpp)
fenrir_script_fixture(Text)
# The font the fixture registers by path: one of HarfBuzz's test fonts, which FenrirText's own tests use.
set_property(TEST FenrirTextScripting APPEND PROPERTY ENVIRONMENT
    "FENRIR_TEST_TEXT_FONT=${CMAKE_CURRENT_SOURCE_DIR}/../../libs/harfbuzz/test/subset/data/fonts/Roboto-Regular.ttf")
