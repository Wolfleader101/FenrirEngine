# The renderer from C#, through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an App
# with assets and the renderer on the null backend installed (or no renderer, for the no-op rules), its asset root
# FenrirRenderer's test fixtures, and scripting running a fixture script assembly that uses the Fenrir.Renderer contract
# (spec seam 7). What C# drew is read back through RendererProbe (seam 1).
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")
add_executable(FenrirRendererScriptingTests
    RendererScriptingApp.hpp
    Shapes.cpp
    Drawing.cpp
    Picking.cpp
    SpawnModel.cpp
    NoOps.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
# RendererProbe, from the renderer's own tests, and the renderer's sources it reads.
target_include_directories(FenrirRendererScriptingTests PRIVATE "${fenrir_scripting_tests_dir}" ../../Tests ../../src)
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
file(GLOB fenrir_renderer_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs")
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
add_test(NAME FenrirRendererScripting COMMAND FenrirRendererScriptingTests "~[noops]")
set_tests_properties(FenrirRendererScripting PROPERTIES TIMEOUT 300 ENVIRONMENT "${fenrir_renderer_fixture_environment}")
# The no-op rules count warnings given once per native call site per process, so they run in a process of their own,
# in declaration order.
add_test(NAME FenrirRendererScripting-NoOps COMMAND FenrirRendererScriptingTests "[noops]" --order decl)
set_tests_properties(FenrirRendererScripting-NoOps PROPERTIES TIMEOUT 300 ENVIRONMENT
    "${fenrir_renderer_fixture_environment}")
