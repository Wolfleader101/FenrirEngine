# describe-api's fixture registers like a package: fenrir-generate-type-registry writes its registration into the
# FenrirTypes tests, which compile and exercise it, and the tool's tests compare it byte for byte. Until ticket 06 of the
# types spec generates visitors for owning types, the fixture's data assets, settings type and tagged unions are skipped.
fenrir_generate_type_registry(DescribeApiFixture ../describe-api/tests/Fixture/Fixture.api.toml SKIP_UNSUPPORTED)

# The tool's tests (xunit) generate the fixture's registration and the failing cases; they need the .NET SDK.
if(FENRIR_BUILD_TESTS AND FENRIR_DOTNET_EXECUTABLE)
    add_test(NAME GenerateTypeRegistryTests
        COMMAND "${FENRIR_DOTNET_EXECUTABLE}" test "${CMAKE_CURRENT_SOURCE_DIR}/tests/GenerateTypeRegistry.Tests.csproj"
            --nologo)
    set_tests_properties(GenerateTypeRegistryTests PROPERTIES TIMEOUT 600)
endif()
