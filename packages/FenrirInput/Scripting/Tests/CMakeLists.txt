# Input from C#, through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an App with no
# window, input fed by hand through the input feed, and scripting running a fixture script assembly that uses the
# Fenrir.Input contract (spec seam 7: fed keys per phase and in an EventReader).
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")
add_executable(FenrirInputScriptingTests
    InputScriptingApp.hpp
    Keys.cpp
    Devices.cpp
    Shapes.cpp
    NoInput.cpp
    Generated/FenrirInputDefaultsTests.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
target_include_directories(FenrirInputScriptingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${fenrir_scripting_tests_dir}")
target_link_libraries(FenrirInputScriptingTests PRIVATE FenrirInputScripting Catch2::Catch2WithMain)
fenrir_deploy_scripting(FenrirInputScriptingTests)
fenrir_deploy_input_scripting(FenrirInputScriptingTests)

# The fixture script assembly: compiled against the scripting SDK and the Fenrir.Input contract, neither copied (the host
# shares its own copies), and rebuilt when either is.
set(fenrir_input_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
# It also compiles the generated defaults test's C# class (Generated/), whose new() values the test reads.
file(GLOB fenrir_input_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs"
    "${CMAKE_CURRENT_SOURCE_DIR}/Generated/*.cs")
add_custom_command(
    OUTPUT "${fenrir_input_fixture_dir}/Fenrir.InputScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.InputScriptFixture.csproj"
        --configuration Release --output "${fenrir_input_fixture_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:FenrirInputContractDir=${FENRIR_INPUT_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/"
    DEPENDS ${fenrir_input_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.InputScriptFixture.csproj"
        FenrirInputContract "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll" "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMENT "Building the input scripting test fixture assembly"
    VERBATIM
)
add_custom_target(FenrirInputScriptFixture DEPENDS "${fenrir_input_fixture_dir}/Fenrir.InputScriptFixture.dll")
add_dependencies(FenrirInputScriptFixture FenrirInputContract)
add_dependencies(FenrirInputScriptingTests FenrirInputScriptFixture)

add_test(NAME FenrirInputScripting COMMAND FenrirInputScriptingTests)
set_tests_properties(FenrirInputScripting PROPERTIES TIMEOUT 300 ENVIRONMENT
    "FENRIR_TEST_INPUT_FIXTURE=${fenrir_input_fixture_dir}/Fenrir.InputScriptFixture.dll")
