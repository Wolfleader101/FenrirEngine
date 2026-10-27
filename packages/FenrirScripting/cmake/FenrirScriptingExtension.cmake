# A package's scripting extension, built by convention (ADR-0046). A package that gives C# something (Input, Window,
# Assets, Renderer, DebugDraw, Physics) carries a scripting extension: a C# contract assembly (Managed/Fenrir.<Pkg>), a
# Fenrir<Pkg>Scripting static library holding the native functions behind it and the generated registration, a fixture
# script assembly its tests load, and two test executables. The library is a library of its own so the package itself
# never links FenrirScripting; the root build adds each extension's directory only when the package and scripting are
# both on, after FenrirScripting (and after the extensions whose contracts it builds against).
#
# Every one of those is the same shape with the package name substituted, so each function here derives every path,
# target, cache variable, csproj property and environment variable from <Pkg> (as in `Input`) and the calling directory,
# and a package passes a keyword only where it deviates:
#
#   fenrir_scripting_contract(<Pkg> [USES <Pkg>...])
#       Builds ../Managed/Fenrir.<Pkg>/Fenrir.<Pkg>.csproj into ${CMAKE_BINARY_DIR}/fenrir-<pkg>/contract with the .NET
#       SDK Iris found, against the scripting SDK (Fenrir.Scripting and Fenrir.Math) and each used package's contract
#       (-p:Fenrir<Used>ContractDir=), rebuilt whenever any of them is. Sets the FENRIR_<PKG>_CONTRACT_DIR cache
#       variable, adds the Fenrir<Pkg>Contract target, and records the contract so fenrir_deploy_scripting(<target>
#       EXTENSIONS <Pkg>) can copy it next to an executable, beside the scripting contract, where Scripting::Install finds
#       the extension's relative path.
#   fenrir_scripting_extension(<Pkg> SOURCES <files...> LINK <targets...> [NO_SELECTION] [DESCRIPTIONS <api.json...>])
#       The Fenrir<Pkg>Scripting library: SOURCES are the hand-written files; the generated ones (the natives header,
#       the registration and the mappings header, ADR-0042 to ADR-0046) are listed here. `include` is public, `src`
#       private; it links FenrirScripting and LINK. Then the binding pipeline: fenrir_describe_api of the library's own
#       selection api/Fenrir<Pkg>Scripting.api.toml unless NO_SELECTION, fenrir_describe_api_natives of
#       api/Fenrir<Pkg>.binding.toml, and iris_generate_binding of that configuration over DESCRIPTIONS, by default the
#       package's ../api/Fenrir<Pkg>.api.json and (with a selection) the library's own api/Fenrir<Pkg>Scripting.api.json.
#       Adds Tests when FENRIR_BUILD_TESTS. After changing a shared type or a native, regenerate both stages:
#         cmake --build <dir> --target fenrir-describe-api
#         cmake --build <dir> --target fenrir-generate-bindings
#   fenrir_script_fixture(<Pkg> [USES <Pkg>...])
#       The fixture script assembly Fenrir.<Pkg>ScriptFixture.dll from Fixture/ and Generated/*.cs (the generated
#       defaults test's C# class), compiled against the scripting SDK, the package's contract and each used package's,
#       none copied (the host shares its own copies), into ${CMAKE_CURRENT_BINARY_DIR}/fixture; the
#       Fenrir<Pkg>ScriptFixture target.
#   fenrir_scripting_tests(<Pkg> SOURCES <files...> [TEST_ARGS <args...>])
#       Fenrir<Pkg>ScriptingTests: the package from C#, through the scripting fixture harness (FenrirScripting's
#       HeadlessApp, ExtensionTestApp and ScriptingProbe): an App with the package installed and scripting running the
#       fixture assembly. Its sources, the generated defaults test, and the harness headers; the calling directory and
#       FenrirScripting's Tests on its include path; links the library and Catch2; deploys scripting and the package's
#       contract beside it; depends on the fixture. add_test(Fenrir<Pkg>Scripting) with TEST_ARGS, a 300 s timeout and
#       FENRIR_TEST_<PKG>_FIXTURE in its environment. It leaves FENRIR_<PKG>_FIXTURE_ENVIRONMENT in the caller's scope
#       for a package that adds a test of its own on the same executable.
#   fenrir_scripting_rules_tests(<Pkg> SOURCES <files...> [TEST_ARGS <args...>])
#       Fenrir<Pkg>ScriptingRulesTests: the natives' rules (no package installed, a stale handle, each warning once under
#       the native's name), called from C++ through FenrirScripting's NativeCallHarness with an extension state the test
#       makes, so with no .NET and no fixture assembly. The harness includes FenrirScripting's private
#       src/ChangeBuffer.hpp, so the executable sees that package's src as FenrirScriptingRulesTests does (a leak the
#       harness's own change closes; until then it is encoded here once), and this package's src for the extension state
#       and the natives header. add_test(Fenrir<Pkg>ScriptingRules) with TEST_ARGS and a 60 s timeout.
#
# Timeouts are fixed here. FenrirScripting's own CMakeLists stay off these functions: its contract is the SDK, its
# fixture builds twice for the reload tests, and the built-in extension is Install's own, not a package's.

# The <Pkg>=<dir> entries fenrir_scripting_contract records, read by fenrir_scripting_contract_dir.
define_property(GLOBAL PROPERTY FENRIR_SCRIPTING_CONTRACTS
    BRIEF_DOCS "Each scripting extension's contract directory, as <Pkg>=<dir>"
    FULL_DOCS "Appended by fenrir_scripting_contract; fenrir_deploy_scripting copies the contracts it names from here.")

# Sets <out> to the directory fenrir_scripting_contract(<pkg>) built its contract into, or fails naming the caller.
function(fenrir_scripting_contract_dir pkg out)
    get_property(contracts GLOBAL PROPERTY FENRIR_SCRIPTING_CONTRACTS)
    foreach(entry IN LISTS contracts)
        if(entry MATCHES "^${pkg}=(.*)$")
            set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "No scripting extension ${pkg} is in the build: fenrir_scripting_contract(${pkg}) was never called. "
                        "Its directory is added by the root CMakeLists when its package and FENRIR_ENABLE_SCRIPTING are on, "
                        "and must come before what uses it.")
endfunction()

function(fenrir_scripting_contract pkg)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "USES")
    string(TOUPPER "${pkg}" PKG)
    string(TOLOWER "${pkg}" pkg_lower)
    set(contract_dir "${CMAKE_BINARY_DIR}/fenrir-${pkg_lower}/contract")
    set(FENRIR_${PKG}_CONTRACT_DIR "${contract_dir}" CACHE INTERNAL "Where the Fenrir.${pkg} contract is built")
    set(managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Managed/Fenrir.${pkg}")
    file(GLOB sources CONFIGURE_DEPENDS "${managed_dir}/*.cs" "${managed_dir}/Generated/*.cs")

    set(properties "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/")
    set(depends FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll")
    set(targets FenrirScriptingContract)
    foreach(used IN LISTS arg_USES)
        fenrir_scripting_contract_dir(${used} used_dir)
        list(APPEND properties "-p:Fenrir${used}ContractDir=${used_dir}/")
        list(APPEND depends Fenrir${used}Contract "${used_dir}/Fenrir.${used}.dll")
        list(APPEND targets Fenrir${used}Contract)
    endforeach()

    add_custom_command(
        OUTPUT "${contract_dir}/Fenrir.${pkg}.dll"
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${managed_dir}/Fenrir.${pkg}.csproj"
            --configuration Release --output "${contract_dir}" -nologo -v:q ${properties}
            "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
        DEPENDS ${sources} "${managed_dir}/Fenrir.${pkg}.csproj" ${depends}
        COMMENT "Building the Fenrir.${pkg} contract"
        VERBATIM
    )
    add_custom_target(Fenrir${pkg}Contract ALL DEPENDS "${contract_dir}/Fenrir.${pkg}.dll")
    add_dependencies(Fenrir${pkg}Contract ${targets})
    set_property(GLOBAL APPEND PROPERTY FENRIR_SCRIPTING_CONTRACTS "${pkg}=${contract_dir}")
    # Deploying this extension deploys the contracts it builds against too.
    set_property(GLOBAL PROPERTY FENRIR_SCRIPTING_CONTRACT_USES_${pkg} "${arg_USES}")
endfunction()

function(fenrir_scripting_extension pkg)
    cmake_parse_arguments(PARSE_ARGV 1 arg "NO_SELECTION" "" "SOURCES;LINK;DESCRIPTIONS")
    # The mappings header is public when another package's binding uses this one, private otherwise.
    set(mappings "include/Fenrir${pkg}Scripting/Generated/Fenrir${pkg}Mappings.hpp")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${mappings}")
        set(mappings "src/Generated/Fenrir${pkg}Mappings.hpp")
    endif()
    add_library(Fenrir${pkg}Scripting STATIC
        ${arg_SOURCES}
        ${mappings}
        src/Generated/Fenrir${pkg}Natives.hpp
        src/Generated/Fenrir${pkg}Registration.cpp
    )
    target_include_directories(Fenrir${pkg}Scripting PUBLIC include PRIVATE src)
    target_link_libraries(Fenrir${pkg}Scripting PUBLIC ${arg_LINK} FenrirScripting)

    if(NOT arg_NO_SELECTION)
        fenrir_describe_api(Fenrir${pkg}Scripting api/Fenrir${pkg}Scripting.api.toml)
    endif()
    fenrir_describe_api_natives(Fenrir${pkg}Scripting api/Fenrir${pkg}.binding.toml)
    if(NOT arg_DESCRIPTIONS)
        set(arg_DESCRIPTIONS ../api/Fenrir${pkg}.api.json)
        if(NOT arg_NO_SELECTION)
            list(APPEND arg_DESCRIPTIONS api/Fenrir${pkg}Scripting.api.json)
        endif()
    endif()
    iris_generate_binding(Fenrir${pkg} api/Fenrir${pkg}.binding.toml ${arg_DESCRIPTIONS})

    if(FENRIR_BUILD_TESTS)
        add_subdirectory(Tests)
    endif()
endfunction()

function(fenrir_script_fixture pkg)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "USES")
    string(TOUPPER "${pkg}" PKG)
    string(TOLOWER "${pkg}" pkg_lower)
    set(fixture_dir "${CMAKE_CURRENT_BINARY_DIR}/fixture")
    file(GLOB sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Generated/*.cs")

    set(properties "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/")
    set(depends "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll" "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll")
    foreach(contract IN ITEMS ${pkg} ${arg_USES})
        fenrir_scripting_contract_dir(${contract} contract_dir)
        list(APPEND properties "-p:Fenrir${contract}ContractDir=${contract_dir}/")
        list(APPEND depends Fenrir${contract}Contract "${contract_dir}/Fenrir.${contract}.dll")
    endforeach()

    add_custom_command(
        OUTPUT "${fixture_dir}/Fenrir.${pkg}ScriptFixture.dll"
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.${pkg}ScriptFixture.csproj"
            --configuration Release --output "${fixture_dir}" -nologo -v:q ${properties}
            "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/fixture-obj/"
        DEPENDS ${sources} "${CMAKE_CURRENT_SOURCE_DIR}/Fixture/Fenrir.${pkg}ScriptFixture.csproj" ${depends}
        COMMENT "Building the ${pkg_lower} scripting test fixture assembly"
        VERBATIM
    )
    add_custom_target(Fenrir${pkg}ScriptFixture DEPENDS "${fixture_dir}/Fenrir.${pkg}ScriptFixture.dll")
    add_dependencies(Fenrir${pkg}ScriptFixture Fenrir${pkg}Contract)
endfunction()

function(fenrir_scripting_tests pkg)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;TEST_ARGS")
    string(TOUPPER "${pkg}" PKG)
    set(tests_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Tests")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
    fenrir_add_catch2()

    # A binding with no [tests] (nothing registered) has no generated defaults test.
    set(defaults "Generated/Fenrir${pkg}DefaultsTests.cpp")
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${defaults}")
        set(defaults "")
    endif()
    add_executable(Fenrir${pkg}ScriptingTests
        ${arg_SOURCES}
        ${defaults}
        "${tests_dir}/ExtensionTestApp.hpp"
        "${tests_dir}/HeadlessApp.hpp"
        "${tests_dir}/ScriptingProbe.hpp"
    )
    target_include_directories(Fenrir${pkg}ScriptingTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${tests_dir}")
    target_link_libraries(Fenrir${pkg}ScriptingTests PRIVATE Fenrir${pkg}Scripting Catch2::Catch2WithMain)
    # The harness reads the fixture's path with std::getenv, which MSVC's runtime calls unsafe.
    if(MSVC)
        target_compile_definitions(Fenrir${pkg}ScriptingTests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    fenrir_deploy_scripting(Fenrir${pkg}ScriptingTests EXTENSIONS ${pkg})
    add_dependencies(Fenrir${pkg}ScriptingTests Fenrir${pkg}ScriptFixture)

    set(environment "FENRIR_TEST_${PKG}_FIXTURE=${CMAKE_CURRENT_BINARY_DIR}/fixture/Fenrir.${pkg}ScriptFixture.dll")
    add_test(NAME Fenrir${pkg}Scripting COMMAND Fenrir${pkg}ScriptingTests ${arg_TEST_ARGS})
    set_tests_properties(Fenrir${pkg}Scripting PROPERTIES TIMEOUT 300 ENVIRONMENT "${environment}")
    set(FENRIR_${PKG}_FIXTURE_ENVIRONMENT "${environment}" PARENT_SCOPE)
endfunction()

function(fenrir_scripting_rules_tests pkg)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;TEST_ARGS")
    set(tests_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../Tests")
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../../cmake/FenrirCatch2.cmake")
    fenrir_add_catch2()

    add_executable(Fenrir${pkg}ScriptingRulesTests
        ${arg_SOURCES}
        "${tests_dir}/HeadlessApp.hpp"
        "${tests_dir}/NativeCallHarness.hpp"
    )
    target_include_directories(Fenrir${pkg}ScriptingRulesTests PRIVATE "${tests_dir}" "${tests_dir}/../src"
        "${CMAKE_CURRENT_SOURCE_DIR}/../src")
    target_link_libraries(Fenrir${pkg}ScriptingRulesTests PRIVATE Fenrir${pkg}Scripting Catch2::Catch2WithMain)
    add_test(NAME Fenrir${pkg}ScriptingRules COMMAND Fenrir${pkg}ScriptingRulesTests ${arg_TEST_ARGS})
    set_tests_properties(Fenrir${pkg}ScriptingRules PROPERTIES TIMEOUT 60)
endfunction()
