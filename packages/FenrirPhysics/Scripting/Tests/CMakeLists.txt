# Physics from C#, through the scripting fixture harness: an App with no window, physics installed, and scripting
# running a fixture script assembly that uses the Fenrir.Physics contract. The rules tests (Rules.cpp) are the natives'
# rules: no world, no body, no Joint, each warning once under the native's name. Some cases in each install scripting
# once and rely on running in the order they are declared.
fenrir_scripting_rules_tests(Physics SOURCES Rules.cpp TEST_ARGS --order decl)
fenrir_scripting_tests(Physics SOURCES PhysicsScriptingApp.hpp PhysicsScriptingTests.cpp TEST_ARGS --order decl)
fenrir_script_fixture(Physics)
# The library's own src too, for the generated mappings the defaults test walks (private here, since no other binding
# uses this one).
target_include_directories(FenrirPhysicsScriptingTests PRIVATE ../src)

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
