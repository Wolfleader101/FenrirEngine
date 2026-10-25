# The window from C#, through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an App
# with scripting running a fixture script assembly that uses the Fenrir.Window contract, with no window (the no-op rule)
# or a hidden one. Tests tagged [display] need SDL's video to start, so without a display they report skipped (Catch2's
# SKIP, exit code 4), never passed.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")
add_executable(FenrirWindowScriptingTests
    WindowScriptingTests.cpp
    WindowScriptingApp.hpp
    Generated/FenrirWindowDefaultsTests.cpp
    Generated/FenrirWindowNativesTests.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
# FenrirWindow's test probe, to resize the hidden window as a user would; and the library's own src, for its UTF-16
# conversion.
target_include_directories(FenrirWindowScriptingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${fenrir_scripting_tests_dir}"
    ../../Tests ../src)
target_link_libraries(FenrirWindowScriptingTests PRIVATE FenrirWindowScripting FenrirWindowProbe Catch2::Catch2WithMain)
fenrir_deploy_scripting(FenrirWindowScriptingTests)
fenrir_deploy_input_scripting(FenrirWindowScriptingTests)
fenrir_deploy_window_scripting(FenrirWindowScriptingTests)

# The fixture script assembly: compiled against the scripting SDK and both contracts, none copied (the host shares its
# own copies), and rebuilt when any is.
set(fenrir_window_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
# It also compiles the generated tests' C# classes (Generated/): the defaults test reads their new() values, and the
# natives test calls the natives through them.
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
# Everything that needs no display.
add_test(NAME FenrirWindowScripting COMMAND FenrirWindowScriptingTests "~[display]")
set_tests_properties(FenrirWindowScripting PROPERTIES TIMEOUT 300 ENVIRONMENT "${fenrir_window_fixture_environment}")
# Everything that needs a hidden window: skipped without a display.
add_test(NAME FenrirWindowScripting-Display COMMAND FenrirWindowScriptingTests "[display]")
set_tests_properties(FenrirWindowScripting-Display PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 4
    ENVIRONMENT "${fenrir_window_fixture_environment}")
