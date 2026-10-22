# The headless scripting tests: an App with no window runs a few frames around a fixture script assembly.
add_executable(FenrirScriptingTests
    ScriptingTests.cpp
    HeadlessApp.hpp
    ScriptingProbe.hpp
)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
fenrir_add_catch2()
target_link_libraries(FenrirScriptingTests PRIVATE FenrirScripting Catch2::Catch2WithMain)
# Puts nethost, the Iris bridge and the contract next to the executable, which is where Scripting looks by default.
fenrir_deploy_scripting(FenrirScriptingTests)

# The rules tests: FenrirScripting's own modules (the change buffer, and the others as they are split out) tested through
# their interfaces, with no .NET runtime and no fixture assembly. The modules are private to the package, so this one
# executable is given the package's source directory; nothing else is.
add_executable(FenrirScriptingRulesTests
    Rules/ChangeBufferTests.cpp
    Rules/ReloadWatcherTests.cpp
    Rules/ReconcileTests.cpp
    HeadlessApp.hpp
)
target_link_libraries(FenrirScriptingRulesTests PRIVATE FenrirScripting Catch2::Catch2WithMain)
target_include_directories(FenrirScriptingRulesTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
add_test(NAME FenrirScriptingRules COMMAND FenrirScriptingRulesTests --order decl)
set_tests_properties(FenrirScriptingRules PROPERTIES TIMEOUT 60)

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
# A test-only scripting extension: its C# contract (shared by the host, as a package's would be), a script assembly that
# uses it, and a contract whose fields no extension satisfies, for the tests of what Install refuses.
set(fenrir_extension_dir "${CMAKE_CURRENT_BINARY_DIR}/extension")
set(fenrir_extension_script_dir "${CMAKE_CURRENT_BINARY_DIR}/extension-script")
set(fenrir_broken_extension_dir "${CMAKE_CURRENT_BINARY_DIR}/broken-extension")
file(GLOB fenrir_extension_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Extension/*.cs")
file(GLOB fenrir_extension_script_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ExtensionScript/*.cs")
file(GLOB fenrir_broken_extension_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/BrokenExtension/*.cs")
add_custom_command(
    OUTPUT "${fenrir_extension_dir}/Fenrir.TestExtension.dll" "${fenrir_extension_script_dir}/Fenrir.TestExtensionScript.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Extension/Fenrir.TestExtension.csproj"
        --configuration Release --output "${fenrir_extension_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/extension-obj/"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/ExtensionScript/Fenrir.TestExtensionScript.csproj"
        --configuration Release --output "${fenrir_extension_script_dir}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:TestExtensionDir=${fenrir_extension_dir}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/extension-script-obj/"
    DEPENDS ${fenrir_extension_sources} ${fenrir_extension_script_sources}
        "${CMAKE_CURRENT_SOURCE_DIR}/Extension/Fenrir.TestExtension.csproj"
        "${CMAKE_CURRENT_SOURCE_DIR}/ExtensionScript/Fenrir.TestExtensionScript.csproj" FenrirScriptingContract
    COMMENT "Building the test extension's contract and the script assembly that uses it"
    VERBATIM
)
add_custom_command(
    OUTPUT "${fenrir_broken_extension_dir}/Fenrir.BrokenExtension.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/BrokenExtension/Fenrir.BrokenExtension.csproj"
        --configuration Release --output "${fenrir_broken_extension_dir}" -nologo -v:q
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/broken-extension-obj/"
    DEPENDS ${fenrir_broken_extension_sources} "${CMAKE_CURRENT_SOURCE_DIR}/BrokenExtension/Fenrir.BrokenExtension.csproj"
    COMMENT "Building the broken test extension's contract"
    VERBATIM
)
add_custom_target(FenrirScriptFixture DEPENDS "${fenrir_fixture_dir}/Fenrir.ScriptFixture.dll"
    "${fenrir_fixture_v2_dir}/Fenrir.ScriptFixture.dll" "${fenrir_bad_reports_dir}/Fenrir.BadReportFixture.dll"
    "${fenrir_extension_dir}/Fenrir.TestExtension.dll" "${fenrir_extension_script_dir}/Fenrir.TestExtensionScript.dll"
    "${fenrir_broken_extension_dir}/Fenrir.BrokenExtension.dll")
add_dependencies(FenrirScriptFixture FenrirScriptingContract)
add_dependencies(FenrirScriptingTests FenrirScriptFixture)

# The ECS is wrapped by FenrirECS so it can change without breaking code built on it. FenrirScripting must use only the wrapper:
# this fails if any of its code mentions EnTT.
add_test(NAME ScriptingDoesNotUseEnTT
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/.." -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/NoEnTT.cmake")

# The tests find the assemblies they run against in the environment: to run one case by hand, set these (see
# `ctest -V -R FenrirScripting`) and give FenrirScriptingTests Catch2's options, for example a test name or "[scripting]".
add_test(NAME FenrirScripting COMMAND FenrirScriptingTests --order decl)
set_tests_properties(FenrirScripting PROPERTIES TIMEOUT 120 ENVIRONMENT
    "FENRIR_TEST_FIXTURE=${fenrir_fixture_dir}/Fenrir.ScriptFixture.dll;FENRIR_TEST_FIXTURE_V2=${fenrir_fixture_v2_dir}/Fenrir.ScriptFixture.dll;FENRIR_TEST_BAD_REPORTS=${fenrir_bad_reports_dir}/Fenrir.BadReportFixture.dll;FENRIR_BAD_REPORT_FILE=${CMAKE_CURRENT_BINARY_DIR}/bad-report-mode.txt;FENRIR_TEST_EXTENSION=${fenrir_extension_dir}/Fenrir.TestExtension.dll;FENRIR_TEST_EXTENSION_SCRIPT=${fenrir_extension_script_dir}/Fenrir.TestExtensionScript.dll;FENRIR_TEST_BROKEN_EXTENSION=${fenrir_broken_extension_dir}/Fenrir.BrokenExtension.dll;FENRIR_TEST_EXECUTABLE_DIR=$<TARGET_FILE_DIR:FenrirScriptingTests>")


# Ref<T> points into storage that can move, so the compiler must refuse every way of keeping it past the call that was
# given it. Each case is one small C# file built against the SDK: the control must compile (so a failure below is the
# rule and not a broken setup), and every misuse must fail with its own diagnostic and no other error (except one the compiler adds on its own, named as the third argument). The SDK is built
# by the FenrirScriptingContract target (part of ALL), so build everything before running these.
set(fenrir_compile_fail_work "${CMAKE_CURRENT_BINARY_DIR}/compile-fail")
# An optional third argument is one more error the case may also report (the compiler's own, beside the expected one).
function(fenrir_expect_compile case expect)
    set(also "")
    if(ARGC GREATER 2)
        set(also "${ARGV2}")
    endif()
    add_test(NAME "RefCompile-${case}"
        COMMAND "${CMAKE_COMMAND}"
            "-DDOTNET=${IRIS_DOTNET_EXECUTABLE}"
            "-DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/CompileFail/CompileFail.csproj"
            "-DSDK=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
            "-DCASE=${case}"
            "-DWORK=${fenrir_compile_fail_work}"
            "-DEXPECT=${expect}"
            "-DALSO=${also}"
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
fenrir_expect_compile(ReadOnlyFieldRead OK)     # a readonly field is read through Ref<T>, beside writable ones
fenrir_expect_compile(ReadOnlyFieldWrite CS8331)  # but never written through it
# The hierarchy is readable but never writable from C#.
fenrir_expect_compile(RelationshipRead OK)             # reads through a ReadOnlyRef work
fenrir_expect_compile(RelationshipRef CS0315)          # there is no Ref<Relationship>
fenrir_expect_compile(RelationshipAssign CS8331)       # r.Value = ... on a ReadOnlyRef
fenrir_expect_compile(RelationshipField CS8331)        # r.Parent = ... on a ReadOnlyRef
fenrir_expect_compile(ReadOnlyViewOfWritable CS8332)   # a writable component through the read-only view
# Native resources share Ref<T>'s rules, and a read-only one is readable but never writable from C#.
fenrir_expect_compile(ResourceControl OK)              # get, try-get, insert and remove; read a read-only one
fenrir_expect_compile(ReadOnlyResourceWrite CS8331)    # clock.Seconds = ... through its ReadOnlyRef
fenrir_expect_compile(ReadOnlyResourceRef CS0315)      # there is no Ref<T> of a read-only resource
fenrir_expect_compile(ReadOnlyResourceInsert CS0315)   # nor an insert of one
# Systems are found at build time, so a mistake is an error there and not a silent no-op when the game runs.
fenrir_expect_compile(SystemControl OK)
fenrir_expect_compile(SystemNotASystem FENRIR0010)
fenrir_expect_compile(SystemPrivateConstructor FENRIR0011)
fenrir_expect_compile(SystemBadPhase FENRIR0013)
fenrir_expect_compile(EventControl OK)
fenrir_expect_compile(EventManaged FENRIR0014)
fenrir_expect_compile(EventReadOnly CS8332)    # a read view cannot be written through
# Native functions are declared once in C#, and the generator refuses at build time what cannot cross or cannot be bound.
fenrir_expect_compile(NativeFunctionControl OK)
fenrir_expect_compile(NativeFunctionNotStatic FENRIR0020)
fenrir_expect_compile(NativeFunctionNotPartial FENRIR0021)
fenrir_expect_compile(NativeFunctionTypeNotPartial FENRIR0022 CS8795)  # the compiler adds: no implementation part
fenrir_expect_compile(NativeFunctionGeneric FENRIR0023)
fenrir_expect_compile(NativeFunctionGenericType FENRIR0023)    # a generic containing type, with no other error
fenrir_expect_compile(NativeFunctionTwoFiles OK)               # one type, two files, conflicting using aliases
fenrir_expect_compile(NativeFunctionTwoNamespaceBlocks OK)    # one type, one file, two namespace blocks with their own usings
fenrir_expect_compile(NativeFunctionOverload FENRIR0024)
fenrir_expect_compile(NativeFunctionStringResult FENRIR0025)
fenrir_expect_compile(NativeFunctionUnsupported FENRIR0026)
# A [TaggedUnion] struct gets the hand-written union's members from the generator.
fenrir_expect_compile(TaggedUnionControl OK)     # a byte tag and a noun, every generated member used
fenrir_expect_compile(TaggedUnionEmptyKind OK)   # a kind with no fields
fenrir_expect_compile(TaggedUnionNested OK)      # a union, its tag and kinds nested in a partial class, with no noun
fenrir_expect_compile(TaggedUnionIntTag OK)      # an int tag
# A [TaggedUnion] the generator cannot write for is refused with its own error, and gets nothing.
fenrir_expect_compile(TaggedUnionClass FENRIR0030 CS0592)       # a class is not a union (the compiler adds CS0592)
fenrir_expect_compile(TaggedUnionNotPartial FENRIR0030)         # the generator cannot add to a struct that is not partial
fenrir_expect_compile(TaggedUnionContainerNotPartial FENRIR0030) # nor to one inside a class that is not partial
fenrir_expect_compile(TaggedUnionReadOnly FENRIR0031)           # a readonly struct cannot hand out writable views
fenrir_expect_compile(TaggedUnionGeneric FENRIR0031)            # a generic union
fenrir_expect_compile(TaggedUnionRefStruct FENRIR0031)          # a ref struct cannot be stored in a component
fenrir_expect_compile(TaggedUnionNoTag FENRIR0032)              # the tag is not at offset 0
fenrir_expect_compile(TaggedUnionTwoTags FENRIR0032)            # two enums at offset 0: which is the tag?
fenrir_expect_compile(TaggedUnionPublicKind FENRIR0033)         # a kind field must be private
fenrir_expect_compile(TaggedUnionReadOnlyKind FENRIR0033)       # a view cannot be a writable reference to a readonly field
fenrir_expect_compile(TaggedUnionMisnamedKind FENRIR0033)       # Metal is _metal, not _steel
fenrir_expect_compile(TaggedUnionMissingKind FENRIR0034)        # CrateType.Metal has no field
fenrir_expect_compile(TaggedUnionMismatchedOffsets FENRIR0035)  # the kinds must overlap at one offset
fenrir_expect_compile(TaggedUnionOffsetInsideTag FENRIR0035)    # an int tag takes four bytes, so the kinds cannot start at 2
fenrir_expect_compile(TaggedUnionManagedKind FENRIR0036)        # a kind holding a reference cannot live in native storage
fenrir_expect_compile(TaggedUnionSelfKind FENRIR0036 CS0523)    # a union cannot be its own kind (the compiler adds CS0523)
fenrir_expect_compile(TaggedUnionDuplicateKind FENRIR0036)      # two kinds of one type would need two conversions from it
fenrir_expect_compile(TaggedUnionWrittenConstructor FENRIR0037) # the generator writes the parameterless constructor
fenrir_expect_compile(TaggedUnionWrittenType FENRIR0037)        # the generator writes Type
fenrir_expect_compile(TaggedUnionWrittenView FENRIR0037)        # the generator writes each view
fenrir_expect_compile(TaggedUnionWrittenConversion FENRIR0037)  # the generator writes each conversion
fenrir_expect_compile(TaggedUnionWrittenExpect FENRIR0037)      # the generator writes Expect
fenrir_expect_compile(TaggedUnionNoZero FENRIR0038)             # new() makes the kind with tag value 0, and there is none
fenrir_expect_compile(TaggedUnionDuplicateValue FENRIR0038)     # two tag members with one value: the message could not say which it is
