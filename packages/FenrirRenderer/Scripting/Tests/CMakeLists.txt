# The renderer from C#, through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an App
# with assets and the renderer on the null backend installed, its asset root FenrirRenderer's test fixtures, and
# scripting running a fixture script assembly that uses the Fenrir.Renderer contract (spec seam 7). What C# drew is read
# back through RendererProbe (seam 1). The no-op rules are the rules tests below, with no .NET.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")
add_executable(FenrirRendererScriptingTests
    RendererScriptingApp.hpp
    Shapes.cpp
    Drawing.cpp
    Picking.cpp
    SpawnModel.cpp
    Generated/FenrirRendererDefaultsTests.cpp
    "${fenrir_scripting_tests_dir}/ExtensionTestApp.hpp"
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
# RendererProbe, from the renderer's own tests, and the renderer's sources it reads.
target_include_directories(FenrirRendererScriptingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${fenrir_scripting_tests_dir}"
    ../../Tests ../../src)
target_link_libraries(FenrirRendererScriptingTests PRIVATE FenrirRendererScripting FenrirRendererCore
                                                           Catch2::Catch2WithMain)
target_compile_definitions(FenrirRendererScriptingTests PRIVATE
    FENRIR_RENDERER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../Tests/fixtures")
# The harness reads the fixture's path with std::getenv, which MSVC's runtime calls unsafe.
if(MSVC)
    target_compile_definitions(FenrirRendererScriptingTests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
fenrir_deploy_scripting(FenrirRendererScriptingTests)
fenrir_deploy_input_scripting(FenrirRendererScriptingTests)
fenrir_deploy_assets_scripting(FenrirRendererScriptingTests)
fenrir_deploy_renderer_scripting(FenrirRendererScriptingTests)

# The fixture script assembly: compiled against the scripting SDK and the three contracts, none copied (the host shares
# its own copies), and rebuilt when any is.
set(fenrir_renderer_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
# It also compiles the generated defaults test's C# class (Generated/), whose new() values the test reads.
file(GLOB fenrir_renderer_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs"
    "${CMAKE_CURRENT_SOURCE_DIR}/Generated/*.cs")
add_custom_command(
    OUTPUT "${fenrir_renderer_fixture_dir}/Fenrir.RendererScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.RendererScriptFixture.csproj"
        --configuration Release --output "${fenrir_renderer_fixture_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:FenrirInputContractDir=${FENRIR_INPUT_CONTRACT_DIR}/"
        "-p:FenrirAssetsContractDir=${FENRIR_ASSETS_CONTRACT_DIR}/"
        "-p:FenrirRendererContractDir=${FENRIR_RENDERER_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/"
    DEPENDS ${fenrir_renderer_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.RendererScriptFixture.csproj"
        FenrirRendererContract "${FENRIR_RENDERER_CONTRACT_DIR}/Fenrir.Renderer.dll"
        FenrirAssetsContract "${FENRIR_ASSETS_CONTRACT_DIR}/Fenrir.Assets.dll"
        FenrirInputContract "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll" "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMENT "Building the renderer scripting test fixture assembly"
    VERBATIM
)
add_custom_target(FenrirRendererScriptFixture DEPENDS "${fenrir_renderer_fixture_dir}/Fenrir.RendererScriptFixture.dll")
add_dependencies(FenrirRendererScriptFixture FenrirRendererContract)
add_dependencies(FenrirRendererScriptingTests FenrirRendererScriptFixture)

set(fenrir_renderer_fixture_environment
    "FENRIR_TEST_RENDERER_FIXTURE=${fenrir_renderer_fixture_dir}/Fenrir.RendererScriptFixture.dll")
add_test(NAME FenrirRendererScripting COMMAND FenrirRendererScriptingTests)
set_tests_properties(FenrirRendererScripting PROPERTIES TIMEOUT 300 ENVIRONMENT "${fenrir_renderer_fixture_environment}")

# The rules tests: the no-op rules of the natives, called from C++ through FenrirScripting's NativeCallHarness with an
# extension state the test makes, so they need no .NET runtime and no fixture assembly. The harness includes
# FenrirScripting's private ChangeBuffer.hpp, so this executable is given that package's source directory, as
# FenrirScriptingRulesTests is; the natives' state header is private to this package's src. The model SpawnModel is
# given is loaded from FenrirRenderer's test fixtures.
add_executable(FenrirRendererScriptingRulesTests
    NoOps.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/NativeCallHarness.hpp"
)
target_include_directories(FenrirRendererScriptingRulesTests PRIVATE "${fenrir_scripting_tests_dir}"
    "${fenrir_scripting_tests_dir}/../src" "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(FenrirRendererScriptingRulesTests PRIVATE FenrirRendererScripting Catch2::Catch2WithMain)
target_compile_definitions(FenrirRendererScriptingRulesTests PRIVATE
    FENRIR_RENDERER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../Tests/fixtures")
add_test(NAME FenrirRendererScriptingRules COMMAND FenrirRendererScriptingRulesTests)
set_tests_properties(FenrirRendererScriptingRules PROPERTIES TIMEOUT 60)
