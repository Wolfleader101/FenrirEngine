# The tool's own fixture is registered like a binding, so the bindings target and a parent project's dirty-tree check
# cover its committed outputs too.
iris_generate_binding(IrisBindingsFixture tests/Fixture/Fixture.binding.toml tests/Fixture/Fixture.api.json)
# The binding the fixture uses: its mirror of Spring, which the fixture names instead of generating its own.
iris_generate_binding(IrisBindingsFixtureCommon tests/Fixture/Uses/Common.binding.toml tests/Fixture/Uses/Common.api.json)

if(NOT IRIS_BUILD_TESTS)
    return()
endif()

# The tool's tests (xunit): the fixture's committed outputs, the diagnostics, and the mirrors compiled against stubs of the
# scripting SDK and, when it is beside Iris, the SDK itself.
add_test(NAME IrisBindingsTests
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" test "${CMAKE_CURRENT_SOURCE_DIR}/tests/Iris.Bindings.Tests.csproj" --nologo)
set_tests_properties(IrisBindingsTests PROPERTIES TIMEOUT 600)

# The fixture's generated defaults test, run: its C# assembly (the mirrors, the generated Defaults class and the SDK
# stand-ins) is loaded as a component, and each registered type's new() is compared with C++'s T{} through the generated
# mappings.
set(iris_bindings_fixture "${CMAKE_CURRENT_BINARY_DIR}/fixture/$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Debug>")
add_custom_target(IrisBindingsFixtureAssembly ALL
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/tests/Checks/Runtime/Iris.Bindings.Fixture.csproj"
        --output "${iris_bindings_fixture}" --nologo -v:q
    VERBATIM
)
if(TARGET IrisManagedBuild)
    # .NET builds in one directory tree run one at a time.
    add_dependencies(IrisBindingsFixtureAssembly IrisManagedBuild)
endif()

add_executable(IrisBindingsDefaults tests/Fixture/Tests/Generated/FixtureDefaultsTests.cpp)
target_include_directories(IrisBindingsDefaults PRIVATE tests/Fixture tests/Fixture/Native tests/Fixture/Tests)
target_link_libraries(IrisBindingsDefaults PRIVATE Iris::Iris Catch2::Catch2WithMain)
add_dependencies(IrisBindingsDefaults IrisBindingsFixtureAssembly)
iris_deploy(IrisBindingsDefaults)
add_test(NAME BindingDefaults COMMAND IrisBindingsDefaults "[bindings]")
set_tests_properties(BindingDefaults PROPERTIES TIMEOUT 120 ENVIRONMENT
    "IRIS_TEST_RUNTIMECONFIG=${iris_bindings_fixture}/Iris.Bindings.Fixture.runtimeconfig.json;IRIS_TEST_BRIDGE=${IRIS_BRIDGE_DIR}/Iris.Bridge.dll;IRIS_TEST_COMPONENT=${iris_bindings_fixture}/Iris.Bindings.Fixture.dll")
