# Debug draw from C#, through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an App
# with debug drawing installed (or not, for the no-op rules) and scripting running a fixture script assembly that uses
# the Fenrir.DebugDraw contract (spec seam 7). What C# drew is read back through the scene's DebugDraw (seam 6).
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")
add_executable(FenrirDebugDrawScriptingTests
    DebugDrawScriptingApp.hpp
    Shapes.cpp
    Items.cpp
    NoOps.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
# Its generated sources, empty until the first generation on a new binding.
iris_binding_outputs(FenrirDebugDrawScriptingTests)
target_include_directories(FenrirDebugDrawScriptingTests PRIVATE "${fenrir_scripting_tests_dir}")
target_link_libraries(FenrirDebugDrawScriptingTests PRIVATE FenrirDebugDrawScripting Catch2::Catch2WithMain)
# The harness reads the fixture's path with std::getenv, which MSVC's runtime calls unsafe.
if(MSVC)
    target_compile_definitions(FenrirDebugDrawScriptingTests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
fenrir_deploy_scripting(FenrirDebugDrawScriptingTests)
fenrir_deploy_debugdraw_scripting(FenrirDebugDrawScriptingTests)

# The fixture script assembly: compiled against the scripting SDK and the Fenrir.DebugDraw contract, neither copied (the
# host shares its own copies), and rebuilt when either is.
set(fenrir_debugdraw_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
file(GLOB fenrir_debugdraw_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs")
add_custom_command(
    OUTPUT "${fenrir_debugdraw_fixture_dir}/Fenrir.DebugDrawScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.DebugDrawScriptFixture.csproj"
        --configuration Release --output "${fenrir_debugdraw_fixture_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
        "-p:FenrirDebugDrawContractDir=${FENRIR_DEBUGDRAW_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/"
    DEPENDS ${fenrir_debugdraw_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.DebugDrawScriptFixture.csproj"
        FenrirDebugDrawContract "${FENRIR_DEBUGDRAW_CONTRACT_DIR}/Fenrir.DebugDraw.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll" "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMENT "Building the debug draw scripting test fixture assembly"
    VERBATIM
)
add_custom_target(FenrirDebugDrawScriptFixture DEPENDS "${fenrir_debugdraw_fixture_dir}/Fenrir.DebugDrawScriptFixture.dll")
add_dependencies(FenrirDebugDrawScriptFixture FenrirDebugDrawContract)
add_dependencies(FenrirDebugDrawScriptingTests FenrirDebugDrawScriptFixture)

set(fenrir_debugdraw_fixture_environment
    "FENRIR_TEST_DEBUGDRAW_FIXTURE=${fenrir_debugdraw_fixture_dir}/Fenrir.DebugDrawScriptFixture.dll")
add_test(NAME FenrirDebugDrawScripting COMMAND FenrirDebugDrawScriptingTests "~[noops]")
set_tests_properties(FenrirDebugDrawScripting PROPERTIES TIMEOUT 300 ENVIRONMENT
    "${fenrir_debugdraw_fixture_environment}")
# The no-op rules count warnings given once per native call site per process, so they run in a process of their own,
# in declaration order.
add_test(NAME FenrirDebugDrawScripting-NoOps COMMAND FenrirDebugDrawScriptingTests "[noops]" --order decl)
set_tests_properties(FenrirDebugDrawScripting-NoOps PROPERTIES TIMEOUT 300 ENVIRONMENT
    "${fenrir_debugdraw_fixture_environment}")
