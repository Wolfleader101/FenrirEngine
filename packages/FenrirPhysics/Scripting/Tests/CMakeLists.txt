# Physics from C#, through the scripting fixture harness (FenrirScripting's HeadlessApp and ScriptingProbe): an App with
# no window, physics installed, and scripting running a fixture script assembly that uses the Fenrir.Physics contract.
include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()

set(fenrir_scripting_tests_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/Tests")

# The natives' rules (no world, no body, no Joint, each warning once under the native's name), called from C++ through
# FenrirScripting's NativeCallHarness with no .NET and no fixture assembly. The harness includes FenrirScripting's
# private src/ChangeBuffer.hpp, so this executable is given that package's src directory, as FenrirScriptingRulesTests
# is, and this library's src for the state and the query types.
add_executable(FenrirPhysicsScriptingRulesTests
    Rules.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/NativeCallHarness.hpp"
)
target_include_directories(FenrirPhysicsScriptingRulesTests PRIVATE "${fenrir_scripting_tests_dir}"
    "${fenrir_scripting_tests_dir}/../src" ../src)
target_link_libraries(FenrirPhysicsScriptingRulesTests PRIVATE FenrirPhysicsScripting Catch2::Catch2WithMain)
add_test(NAME FenrirPhysicsScriptingRules COMMAND FenrirPhysicsScriptingRulesTests --order decl)
set_tests_properties(FenrirPhysicsScriptingRules PROPERTIES TIMEOUT 60)
add_executable(FenrirPhysicsScriptingTests
    PhysicsScriptingApp.hpp
    PhysicsScriptingTests.cpp
    Generated/FenrirPhysicsDefaultsTests.cpp
    "${fenrir_scripting_tests_dir}/HeadlessApp.hpp"
    "${fenrir_scripting_tests_dir}/ScriptingProbe.hpp"
)
# The library's own src too, for the generated mappings the defaults test walks and the bound types' mappings the
# drifted stand-ins use.
target_include_directories(FenrirPhysicsScriptingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${fenrir_scripting_tests_dir}" ../src)
target_link_libraries(FenrirPhysicsScriptingTests PRIVATE FenrirPhysicsScripting Catch2::Catch2WithMain)
fenrir_deploy_scripting(FenrirPhysicsScriptingTests)
fenrir_deploy_physics_scripting(FenrirPhysicsScriptingTests)

# The fixture script assembly: compiled against the scripting SDK and the Fenrir.Physics contract, neither copied (the
# host shares its own copies), and rebuilt when either is.
set(fenrir_physics_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
# It also compiles the generated defaults test's C# class (Generated/), whose new() values the test reads.
file(GLOB fenrir_physics_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs"
    "${CMAKE_CURRENT_SOURCE_DIR}/Generated/*.cs")
add_custom_command(
    OUTPUT "${fenrir_physics_fixture_dir}/Fenrir.PhysicsScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.PhysicsScriptFixture.csproj"
        --configuration Release --output "${fenrir_physics_fixture_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:FenrirPhysicsContractDir=${FENRIR_PHYSICS_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/"
    DEPENDS ${fenrir_physics_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.PhysicsScriptFixture.csproj"
        FenrirPhysicsContract "${FENRIR_PHYSICS_CONTRACT_DIR}/Fenrir.Physics.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll" "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
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

# What the compiler must refuse in a script using the Fenrir.Physics contract: each case is one small C# file built
# against the SDK and the contract, checked by FenrirScripting's ExpectCompile. The control must compile, so a failure
# below is the rule and not a broken setup. Build everything before running these.
set(fenrir_physics_compile_work "${CMAKE_CURRENT_BINARY_DIR}/compile-fail")
function(fenrir_physics_expect_compile case expect)
    add_test(NAME "PhysicsCompile-${case}"
        COMMAND "${CMAKE_COMMAND}"
            "-DDOTNET=${IRIS_DOTNET_EXECUTABLE}"
            "-DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/CompileFail/CompileFail.csproj"
            "-DSDK=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
            "-DPROPERTY=FenrirPhysicsContractDir=${FENRIR_PHYSICS_CONTRACT_DIR}/"
            "-DCASE=${case}"
            "-DWORK=${fenrir_physics_compile_work}"
            "-DEXPECT=${expect}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/cmake/ExpectCompile.cmake")
    set_tests_properties("PhysicsCompile-${case}" PROPERTIES TIMEOUT 120)
endfunction()
# A character's state is read through Ref<CharacterBody>, beside the Velocity it writes, but never written.
fenrir_physics_expect_compile(CharacterStateRead OK)
fenrir_physics_expect_compile(CharacterStateWrite CS8331)
fenrir_physics_expect_compile(CharacterStateFieldWrite CS0191)
# So are a vehicle's and a wheel's, beside the Input written and the kind edited through its view.
fenrir_physics_expect_compile(VehicleStateRead OK)
fenrir_physics_expect_compile(VehicleStateWrite CS8331)
fenrir_physics_expect_compile(VehicleStateFieldWrite CS0191)
fenrir_physics_expect_compile(WheelStateWrite CS8331)
fenrir_physics_expect_compile(WheelStateFieldWrite CS0191)
