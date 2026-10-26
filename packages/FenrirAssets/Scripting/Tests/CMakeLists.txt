# The asset store from C#, through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an
# App with assets installed (or not, for the no-op rule), its asset root FenrirAssets's test fixtures, and scripting
# running a fixture script assembly that uses the Fenrir.Assets contract (spec seam 7).
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")
add_executable(FenrirAssetsScriptingTests
    AssetsScriptingApp.hpp
    Loading.cpp
    Meshes.cpp
    Images.cpp
    Materials.cpp
    Shaders.cpp
    NoOps.cpp
    Shapes.cpp
    Generated/FenrirAssetsDefaultsTests.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
target_include_directories(FenrirAssetsScriptingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${fenrir_scripting_tests_dir}")
target_link_libraries(FenrirAssetsScriptingTests PRIVATE FenrirAssetsScripting Catch2::Catch2WithMain)
target_compile_definitions(FenrirAssetsScriptingTests PRIVATE
    FENRIR_ASSETS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/../../Tests/fixtures")
# The harness reads the fixture's path with std::getenv, which MSVC's runtime calls unsafe.
if(MSVC)
    target_compile_definitions(FenrirAssetsScriptingTests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
fenrir_deploy_scripting(FenrirAssetsScriptingTests)
fenrir_deploy_assets_scripting(FenrirAssetsScriptingTests)

# The fixture script assembly: compiled against the scripting SDK and the Fenrir.Assets contract, neither copied (the host
# shares its own copies), and rebuilt when either is.
set(fenrir_assets_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
# It also compiles the generated defaults test's C# class (Generated/), whose new() values the test reads.
file(GLOB fenrir_assets_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs"
    "${CMAKE_CURRENT_SOURCE_DIR}/Generated/*.cs")
add_custom_command(
    OUTPUT "${fenrir_assets_fixture_dir}/Fenrir.AssetsScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.AssetsScriptFixture.csproj"
        --configuration Release --output "${fenrir_assets_fixture_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:FenrirAssetsContractDir=${FENRIR_ASSETS_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/"
    DEPENDS ${fenrir_assets_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.AssetsScriptFixture.csproj"
        FenrirAssetsContract "${FENRIR_ASSETS_CONTRACT_DIR}/Fenrir.Assets.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll" "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMENT "Building the assets scripting test fixture assembly"
    VERBATIM
)
add_custom_target(FenrirAssetsScriptFixture DEPENDS "${fenrir_assets_fixture_dir}/Fenrir.AssetsScriptFixture.dll")
add_dependencies(FenrirAssetsScriptFixture FenrirAssetsContract)
add_dependencies(FenrirAssetsScriptingTests FenrirAssetsScriptFixture)

set(fenrir_assets_fixture_environment
    "FENRIR_TEST_ASSETS_FIXTURE=${fenrir_assets_fixture_dir}/Fenrir.AssetsScriptFixture.dll")
add_test(NAME FenrirAssetsScripting COMMAND FenrirAssetsScriptingTests "~[noops]")
set_tests_properties(FenrirAssetsScripting PROPERTIES TIMEOUT 300 ENVIRONMENT "${fenrir_assets_fixture_environment}")
# The no-op rules count warnings given once per native call site per process, so they run in a process of their own,
# in declaration order.
add_test(NAME FenrirAssetsScripting-NoOps COMMAND FenrirAssetsScriptingTests "[noops]" --order decl)
set_tests_properties(FenrirAssetsScripting-NoOps PROPERTIES TIMEOUT 300 ENVIRONMENT
    "${fenrir_assets_fixture_environment}")
