# Ragdolls from C#, through the scripting fixture harness (the ragdolls spec, seam 3): an App with no window, assets,
# skeletons, physics and the ragdoll glue installed, and scripting running a fixture script assembly that uses the
# Fenrir.Ragdoll contract on FenrirRagdoll's code-built test humanoid (../../Tests/RagdollTest.hpp). The rules tests
# (Rules.cpp) are the natives' rules with no .NET: an entity that is not alive or not a Ready skeleton entity, a refused
# bone map, a stale handle, a buffer too small, each warning once under the native's name.
fenrir_scripting_rules_tests(Ragdoll SOURCES Rules.cpp)
fenrir_scripting_tests(Ragdoll SOURCES RagdollScriptingApp.hpp RagdollScriptingTests.cpp)
fenrir_script_fixture(Ragdoll USES Assets Skeleton)
# FenrirRagdoll's test rig, and the library's own src for the generated mappings the defaults test walks and the
# description store the scene-end case reads (private here, since no other binding uses this one).
foreach(target FenrirRagdollScriptingTests FenrirRagdollScriptingRulesTests)
    target_include_directories(${target} PRIVATE ../src ../../Tests)
    target_link_libraries(${target} PRIVATE FenrirPhysicsScripting FenrirAssetsScripting)
endforeach()
# The fixture loads with the physics, skeleton and assets extensions beside this one: their contracts go beside the
# executable too.
fenrir_deploy_scripting(FenrirRagdollScriptingTests EXTENSIONS Assets Skeleton Physics)

# What the compiler must refuse in a script using the Fenrir.Ragdoll contract: each case is one small C# file built
# against the SDK and the contracts, checked by FenrirScripting's ExpectCompile. The control must compile, so a failure
# below is the rule and not a broken setup. Build everything before running these.
set(fenrir_ragdoll_compile_work "${CMAKE_CURRENT_BINARY_DIR}/compile-fail")
function(fenrir_ragdoll_expect_compile case expect)
    add_test(NAME "RagdollCompile-${case}"
        COMMAND "${CMAKE_COMMAND}"
            "-DDOTNET=${IRIS_DOTNET_EXECUTABLE}"
            "-DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/CompileFail/CompileFail.csproj"
            "-DSDK=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
            "-DPROPERTY=FenrirRagdollContractDir=${FENRIR_RAGDOLL_CONTRACT_DIR}/|FenrirSkeletonContractDir=${FENRIR_SKELETON_CONTRACT_DIR}/|FenrirAssetsContractDir=${FENRIR_ASSETS_CONTRACT_DIR}/"
            "-DCASE=${case}"
            "-DWORK=${fenrir_ragdoll_compile_work}"
            "-DEXPECT=${expect}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../FenrirScripting/cmake/ExpectCompile.cmake")
    set_tests_properties("RagdollCompile-${case}" PROPERTIES TIMEOUT 120)
endfunction()
# A ragdoll's and a body's states are read through Ref<T>, beside the mode written, but never written.
fenrir_ragdoll_expect_compile(StateRead OK)
fenrir_ragdoll_expect_compile(RagdollStateWrite CS8331)
fenrir_ragdoll_expect_compile(RagdollStateFieldWrite CS0191)
fenrir_ragdoll_expect_compile(BodyStateWrite CS8331)
fenrir_ragdoll_expect_compile(BodyStateFieldWrite CS0191)
