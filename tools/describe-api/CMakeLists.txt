# The tool's own fixture is described like a package, and its natives like a binding's, so fenrir-describe-api and CI's dirty-tree check cover it too.
add_library(DescribeApiFixture INTERFACE)
target_include_directories(DescribeApiFixture INTERFACE tests/Fixture)
fenrir_describe_api(DescribeApiFixture tests/Fixture/Fixture.api.toml)
fenrir_describe_api_natives(DescribeApiFixture tests/Fixture/Fixture.binding.toml)

# Iris.Bindings' fixture is a copy of this tool's fixture header, described here (Iris's own CMake stays free of Fenrir's
# module) so fenrir-describe-api regenerates its description and CI's dirty-tree check covers it. Its natives JSON is
# hand-maintained: the fixture has no natives source.
set(iris_bindings_fixture_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../packages/Iris/Tools/Iris.Bindings/tests/Fixture")
add_library(IrisBindingsFixtureHeader INTERFACE)
target_include_directories(IrisBindingsFixtureHeader INTERFACE "${iris_bindings_fixture_dir}")
fenrir_describe_api(IrisBindingsFixtureHeader "${iris_bindings_fixture_dir}/Fixture.api.toml")

# The tool's tests (xunit) describe the fixture and the failing cases; they need the .NET SDK.
if(FENRIR_BUILD_TESTS AND FENRIR_DOTNET_EXECUTABLE)
    add_test(NAME DescribeApiTests
        COMMAND "${FENRIR_DOTNET_EXECUTABLE}" test "${CMAKE_CURRENT_SOURCE_DIR}/tests/DescribeApi.Tests.csproj" --nologo)
    set_tests_properties(DescribeApiTests PROPERTIES TIMEOUT 600)
endif()
