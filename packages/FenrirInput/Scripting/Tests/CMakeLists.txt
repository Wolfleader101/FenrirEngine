# Input from C#, through the scripting fixture harness: an App with no window, input fed by hand through the input
# feed, and scripting running a fixture script assembly that uses the Fenrir.Input contract (spec seam 7: fed keys per
# phase and in an EventReader). The rules tests are the natives' no-op rule (NoInput.cpp): no input installed, each
# reads false or zero and warns once under its name.
fenrir_scripting_rules_tests(Input SOURCES NoInput.cpp)
fenrir_scripting_tests(Input SOURCES InputScriptingApp.hpp Keys.cpp Devices.cpp Shapes.cpp)
fenrir_script_fixture(Input)
