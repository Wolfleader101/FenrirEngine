# Physics from C#, through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an App with
# no window, physics installed, and scripting running a fixture script assembly that uses the Fenrir.Physics contract.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")
add_executable(FenrirPhysicsScriptingTests
    PhysicsScriptingTests.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
target_include_directories(FenrirPhysicsScriptingTests PRIVATE "${fenrir_scripting_tests_dir}")
target_link_libraries(FenrirPhysicsScriptingTests PRIVATE FenrirPhysicsScripting Catch2::Catch2WithMain)
fenrir_deploy_scripting(FenrirPhysicsScriptingTests)
fenrir_deploy_physics_scripting(FenrirPhysicsScriptingTests)

# The fixture script assembly: compiled against the scripting SDK and the Fenrir.Physics contract, neither copied (the
# host shares its own copies).
set(fenrir_physics_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
file(GLOB fenrir_physics_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs")
add_custom_command(
    OUTPUT "${fenrir_physics_fixture_dir}/Fenrir.PhysicsScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.PhysicsScriptFixture.csproj"
        --configuration Release --output "${fenrir_physics_fixture_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:FenrirPhysicsContractDir=${FENRIR_PHYSICS_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/"
    DEPENDS ${fenrir_physics_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.PhysicsScriptFixture.csproj"
        FenrirPhysicsContract
    COMMENT "Building the physics scripting test fixture assembly"
    VERBATIM
)
add_custom_target(FenrirPhysicsScriptFixture DEPENDS "${fenrir_physics_fixture_dir}/Fenrir.PhysicsScriptFixture.dll")
add_dependencies(FenrirPhysicsScriptFixture FenrirPhysicsContract)
add_dependencies(FenrirPhysicsScriptingTests FenrirPhysicsScriptFixture)

# Some cases install scripting once and rely on running in the order they are declared.
add_test(NAME FenrirPhysicsScripting COMMAND FenrirPhysicsScriptingTests --order decl)
set_tests_properties(FenrirPhysicsScripting PROPERTIES TIMEOUT 300 ENVIRONMENT
    "FENRIR_TEST_PHYSICS_FIXTURE=${fenrir_physics_fixture_dir}/Fenrir.PhysicsScriptFixture.dll")
