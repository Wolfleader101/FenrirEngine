# The tool's own fixture is described like a package, and its natives like a binding's, so fenrir-describe-api and CI's dirty-tree check cover it too.
add_library(DescribeApiFixture INTERFACE)
target_include_directories(DescribeApiFixture INTERFACE tests/Fixture)
fenrir_describe_api(DescribeApiFixture tests/Fixture/Fixture.api.toml)
fenrir_describe_api_natives(DescribeApiFixture tests/Fixture/Fixture.binding.toml)

# The tool's tests (xunit) describe the fixture and the failing cases; they need the .NET SDK.
if(FENRIR_BUILD_TESTS AND FENRIR_DOTNET_EXECUTABLE)
    add_test(NAME DescribeApiTests
        COMMAND "${FENRIR_DOTNET_EXECUTABLE}" test "${CMAKE_CURRENT_SOURCE_DIR}/tests/DescribeApi.Tests.csproj" --nologo)
    set_tests_properties(DescribeApiTests PROPERTIES TIMEOUT 600)
endif()
