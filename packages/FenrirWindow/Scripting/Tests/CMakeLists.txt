# Two test executables. The natives' rules (no window, an unknown cursor mode, the clipboard written up to a capacity)
# run from C++ through FenrirScripting's NativeCallHarness, with no .NET and no fixture assembly. The window from C#
# runs through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an App with scripting
# running a fixture script assembly that uses the Fenrir.Window contract, with a hidden window. Tests tagged [display]
# need SDL's video to start, so without a display they report skipped (Catch2's SKIP, exit code 4), never passed.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")

# The rules, with no .NET. NativeCallHarness includes FenrirScripting's private src/ChangeBuffer.hpp, so the package's
# src is an include too, as FenrirScriptingRulesTests has it; the library's own src gives the natives' state header.
add_executable(FenrirWindowScriptingRulesTests
    Rules.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/NativeCallHarness.hpp"
)
target_include_directories(FenrirWindowScriptingRulesTests PRIVATE "${fenrir_scripting_tests_dir}"
    "${fenrir_scripting_tests_dir}/../src" ../src)
target_link_libraries(FenrirWindowScriptingRulesTests PRIVATE FenrirWindowScripting Catch2::Catch2WithMain)
add_test(NAME FenrirWindowScriptingRules COMMAND FenrirWindowScriptingRulesTests "~[display]")
set_tests_properties(FenrirWindowScriptingRules PROPERTIES TIMEOUT 60)
add_test(NAME FenrirWindowScriptingRules-Display COMMAND FenrirWindowScriptingRulesTests "[display]")
set_tests_properties(FenrirWindowScriptingRules-Display PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 4)

add_executable(FenrirWindowScriptingTests
    WindowScriptingTests.cpp
    WindowScriptingApp.hpp
    Generated/FenrirWindowDefaultsTests.cpp
    "${fenrir_scripting_tests_dir}/ExtensionTestApp.hpp"
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
# FenrirWindow's test probe, to resize the hidden window as a user would.
target_include_directories(FenrirWindowScriptingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${fenrir_scripting_tests_dir}"
    ../../Tests)
target_link_libraries(FenrirWindowScriptingTests PRIVATE FenrirWindowScripting FenrirWindowProbe Catch2::Catch2WithMain)
fenrir_deploy_scripting(FenrirWindowScriptingTests)
fenrir_deploy_input_scripting(FenrirWindowScriptingTests)
fenrir_deploy_window_scripting(FenrirWindowScriptingTests)

# The fixture script assembly: compiled against the scripting SDK and both contracts, none copied (the host shares its
# own copies), and rebuilt when any is.
set(fenrir_window_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
# It also compiles the generated defaults test's C# class (Generated/), whose new() values the test reads.
file(GLOB fenrir_window_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs"
    "${CMAKE_CURRENT_SOURCE_DIR}/Generated/*.cs")
add_custom_command(
    OUTPUT "${fenrir_window_fixture_dir}/Fenrir.WindowScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.WindowScriptFixture.csproj"
        --configuration Release --output "${fenrir_window_fixture_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:FenrirInputContractDir=${FENRIR_INPUT_CONTRACT_DIR}/"
        "-p:FenrirWindowContractDir=${FENRIR_WINDOW_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/"
    DEPENDS ${fenrir_window_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.WindowScriptFixture.csproj"
        FenrirWindowContract "${FENRIR_WINDOW_CONTRACT_DIR}/Fenrir.Window.dll"
        FenrirInputContract "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll" "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMENT "Building the window scripting test fixture assembly"
    VERBATIM
)
add_custom_target(FenrirWindowScriptFixture DEPENDS "${fenrir_window_fixture_dir}/Fenrir.WindowScriptFixture.dll")
add_dependencies(FenrirWindowScriptFixture FenrirWindowContract)
add_dependencies(FenrirWindowScriptingTests FenrirWindowScriptFixture)

set(fenrir_window_fixture_environment
    "FENRIR_TEST_WINDOW_FIXTURE=${fenrir_window_fixture_dir}/Fenrir.WindowScriptFixture.dll")
# Everything from C# that needs no display.
add_test(NAME FenrirWindowScripting COMMAND FenrirWindowScriptingTests "~[display]")
set_tests_properties(FenrirWindowScripting PROPERTIES TIMEOUT 300 ENVIRONMENT "${fenrir_window_fixture_environment}")
# Everything that needs a hidden window: skipped without a display.
add_test(NAME FenrirWindowScripting-Display COMMAND FenrirWindowScriptingTests "[display]")
set_tests_properties(FenrirWindowScripting-Display PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 4
    ENVIRONMENT "${fenrir_window_fixture_environment}")
