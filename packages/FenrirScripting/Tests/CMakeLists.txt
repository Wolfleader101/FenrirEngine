# The headless scripting tests: an App with no window runs a few frames around a fixture script assembly.
add_executable(FenrirScriptingTests
    ScriptingTests.cpp
    TestHarness.hpp
    HeadlessApp.hpp
    ScriptingProbe.hpp
)
target_link_libraries(FenrirScriptingTests PRIVATE FenrirScripting)
# Puts nethost, the Iris bridge and the contract next to the executable, which is where Scripting looks by default.
fenrir_deploy_scripting(FenrirScriptingTests)

# The fixture script assembly, built with the same .NET SDK Iris uses.
set(fenrir_fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
file(GLOB fenrir_fixture_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs")
# Built twice, in one command so the two builds never run side by side: the fixture, and "version 2" of it (the same sources
# with VARIANT2 defined), which the reload tests copy over the first while an app is running.
set(fenrir_fixture_v2_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture-v2")
add_custom_command(
    OUTPUT "${fenrir_fixture_dir}/Fenrir.ScriptFixture.dll" "${fenrir_fixture_v2_dir}/Fenrir.ScriptFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.ScriptFixture.csproj"
        --configuration Release --output "${fenrir_fixture_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.ScriptFixture.csproj"
        --configuration Release --output "${fenrir_fixture_v2_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" -p:FixtureVariant=2
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-v2-obj/"
    DEPENDS ${fenrir_fixture_sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.ScriptFixture.csproj"
        FenrirScriptingContract
    COMMENT "Building the scripting test fixture assembly (and its version 2)"
    VERBATIM
)
# A second, hand-written assembly whose component and event tables report what the SDK's generator never would (a name
# twice, a layout no C# struct has), for the tests of what the engine refuses when a script assembly loads.
set(fenrir_bad_reports_dir "${CMAKE_CURRENT_BINARY_DIR}/bad-reports")
file(GLOB fenrir_bad_reports_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/BadReports/*.cs")
add_custom_command(
    OUTPUT "${fenrir_bad_reports_dir}/Fenrir.BadReportFixture.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/BadReports/Fenrir.BadReportFixture.csproj"
        --configuration Release --output "${fenrir_bad_reports_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/bad-reports-obj/"
    DEPENDS ${fenrir_bad_reports_sources} "${CMAKE_CURRENT_SOURCE_DIR}/BadReports/Fenrir.BadReportFixture.csproj"
        FenrirScriptingContract
    COMMENT "Building the bad-report test fixture assembly"
    VERBATIM
)
add_custom_target(FenrirScriptFixture DEPENDS "${fenrir_fixture_dir}/Fenrir.ScriptFixture.dll"
    "${fenrir_fixture_v2_dir}/Fenrir.ScriptFixture.dll" "${fenrir_bad_reports_dir}/Fenrir.BadReportFixture.dll")
add_dependencies(FenrirScriptFixture FenrirScriptingContract)
add_dependencies(FenrirScriptingTests FenrirScriptFixture)

# The ECS is wrapped by FenrirECS so it can change without breaking code built on it. FenrirScripting must use only the wrapper:
# this fails if any of its code mentions EnTT.
add_test(NAME ScriptingDoesNotUseEnTT
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.." -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/NoEnTT.cmake")

add_test(NAME FenrirScripting
    COMMAND FenrirScriptingTests "${fenrir_fixture_dir}/Fenrir.ScriptFixture.dll" "${fenrir_fixture_v2_dir}/Fenrir.ScriptFixture.dll"
            "${fenrir_bad_reports_dir}/Fenrir.BadReportFixture.dll")
set_tests_properties(FenrirScripting PROPERTIES TIMEOUT 120)


# Ref<T> points into storage that can move, so the compiler must refuse every way of keeping it past the call that was
# given it. Each case is one small C# file built against the SDK: the control must compile (so a failure below is the
# rule and not a broken setup), and every misuse must fail with its own diagnostic and no other error. The SDK is built
# by the FenrirScriptingContract target (part of ALL), so build everything before running these.
set(fenrir_compile_fail_work "${CMAKE_CURRENT_BINARY_DIR}/compile-fail")
function(fenrir_expect_compile case expect)
    add_test(NAME "RefCompile-${case}"
        COMMAND "${CMAKE_COMMAND}"
            "-DDOTNET=${IRIS_DOTNET_EXECUTABLE}"
            "-DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/CompileFail/CompileFail.csproj"
            "-DSDK=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
            "-DCASE=${case}"
            "-DWORK=${fenrir_compile_fail_work}"
            "-DEXPECT=${expect}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/ExpectCompile.cmake")
    set_tests_properties("RefCompile-${case}" PROPERTIES TIMEOUT 120)
endfunction()
fenrir_expect_compile(Control OK)
fenrir_expect_compile(StoreInField CS8345)      # a field of a class
fenrir_expect_compile(CaptureInLambda CS9108)   # a lambda capturing it
fenrir_expect_compile(InCollection CS9244)      # List<Ref<T>>
fenrir_expect_compile(AcrossAwait CS4012)       # an async method taking it
fenrir_expect_compile(AcrossAwaitLocal CS4007)  # a local still alive after an await
# What the generator must get right, and what it must refuse clearly.
fenrir_expect_compile(InternalField OK)         # an internal field of an internal type on a public component
fenrir_expect_compile(PointerField OK)          # a pointer field makes the accessors class unsafe
fenrir_expect_compile(LatePart OK)              # IComponent named only on a later part of a partial struct
fenrir_expect_compile(ManagedComponent FENRIR0004)  # a component holding a reference: a clear error
fenrir_expect_compile(ReadOnlyStruct CS1929)    # a readonly component has no writable accessors
# The hierarchy is readable but never writable from C#.
fenrir_expect_compile(RelationshipRead OK)             # reads through a ReadOnlyRef work
fenrir_expect_compile(RelationshipRef CS0315)          # there is no Ref<Relationship>
fenrir_expect_compile(RelationshipAssign CS8331)       # r.Value = ... on a ReadOnlyRef
fenrir_expect_compile(RelationshipField CS8331)        # r.Parent = ... on a ReadOnlyRef
fenrir_expect_compile(ReadOnlyViewOfWritable CS8332)   # a writable component through the read-only view
# Systems are found at build time, so a mistake is an error there and not a silent no-op when the game runs.
fenrir_expect_compile(SystemControl OK)
fenrir_expect_compile(SystemNotASystem FENRIR0010)
fenrir_expect_compile(SystemPrivateConstructor FENRIR0011)
fenrir_expect_compile(SystemBadPhase FENRIR0013)
fenrir_expect_compile(EventControl OK)
fenrir_expect_compile(EventManaged FENRIR0014)
fenrir_expect_compile(EventReadOnly CS8332)    # a read view cannot be written through
