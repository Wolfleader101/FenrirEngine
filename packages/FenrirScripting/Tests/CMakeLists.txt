# The headless scripting tests: an App with no window runs a few frames around a fixture script assembly.
add_executable(FenrirScriptingTests
    ScriptingTests.cpp
    TestHarness.hpp
    HeadlessApp.hpp
)
target_link_libraries(FenrirScriptingTests PRIVATE FenrirScripting)
# Puts nethost, the Iris bridge and the contract next to the executable, which is where Scripting looks by default.
fenrir_deploy_scripting(FenrirScriptingTests)

# The fixture script assembly, built with the same .NET SDK Iris uses.
set(fenrir_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
file(GLOB fenrir_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs")
add_custom_command(
    OUTPUT "${fenrir_fixture_dir}/Fenrir.ScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.ScriptFixture.csproj"
        --configuration Release --output "${fenrir_fixture_dir}" -nologo -v:q
    DEPENDS ${fenrir_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.ScriptFixture.csproj"
        FenrirScriptingContract
    COMMENT "Building the scripting test fixture assembly"
    VERBATIM
)
add_custom_target(FenrirScriptFixture DEPENDS "${fenrir_fixture_dir}/Fenrir.ScriptFixture.dll")
add_dependencies(FenrirScriptFixture FenrirScriptingContract)
add_dependencies(FenrirScriptingTests FenrirScriptFixture)

add_test(NAME FenrirScripting
    COMMAND FenrirScriptingTests "${fenrir_fixture_dir}/Fenrir.ScriptFixture.dll")
set_tests_properties(FenrirScripting PROPERTIES TIMEOUT 120)
