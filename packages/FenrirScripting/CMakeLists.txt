# C# scripting for FenrirEngine, built on Iris. Added to the build only when FENRIR_ENABLE_SCRIPTING is on.

# The scripting SDK, built with the .NET SDK Iris found, in one folder: the Fenrir.Scripting contract (the C# API scripts
# compile against, shared by the host and never reloaded), the Fenrir.Math contract it is built on (FenrirMath's, built
# here as the scripting contract's project reference), the Fenrir.Scripting.Generators source generator under
# analyzers/, and Fenrir.Scripting.props, which a script project imports to get them. fenrir_deploy_scripting copies the
# two contracts next to an executable; script projects use the folder as it is (-p:FenrirScriptingSdk=<folder>/).
# A cache variable, so it works from any directory of the build.
set(FENRIR_SCRIPTING_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-scripting/contract" CACHE INTERNAL "Where the Fenrir.Scripting SDK is built")
set(fenrir_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/Managed")
file(GLOB fenrir_contract_sources CONFIGURE_DEPENDS "${fenrir_managed_dir}/Fenrir.Scripting/*.cs"
    "${fenrir_managed_dir}/Fenrir.Scripting/Generated/*.cs")
set(fenrir_math_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../FenrirMath/Managed/Fenrir.Math")
file(GLOB fenrir_math_sources CONFIGURE_DEPENDS "${fenrir_math_managed_dir}/*.cs")
file(GLOB fenrir_generator_sources CONFIGURE_DEPENDS "${fenrir_managed_dir}/Fenrir.Scripting.Generators/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll" "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_managed_dir}/Fenrir.Scripting/Fenrir.Scripting.csproj"
        --configuration Release --output "${FENRIR_SCRIPTING_CONTRACT_DIR}" -nologo -v:q
    # The contract builds the generator as a project reference, so the generator is built first by its own command and
    # the two builds never write the same files at once.
    DEPENDS ${fenrir_contract_sources} "${fenrir_managed_dir}/Fenrir.Scripting/Fenrir.Scripting.csproj"
        ${fenrir_math_sources} "${fenrir_math_managed_dir}/Fenrir.Math.csproj"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/analyzers/Fenrir.Scripting.Generators.dll"
    COMMENT "Building the Fenrir.Scripting and Fenrir.Math contracts"
    VERBATIM
)
add_custom_command(
    OUTPUT "${FENRIR_SCRIPTING_CONTRACT_DIR}/analyzers/Fenrir.Scripting.Generators.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_managed_dir}/Fenrir.Scripting.Generators/Fenrir.Scripting.Generators.csproj"
        --configuration Release --output "${FENRIR_SCRIPTING_CONTRACT_DIR}/analyzers" -nologo -v:q
    DEPENDS ${fenrir_generator_sources} "${fenrir_managed_dir}/Fenrir.Scripting.Generators/Fenrir.Scripting.Generators.csproj"
    COMMENT "Building the Fenrir.Scripting source generator"
    VERBATIM
)
add_custom_command(
    OUTPUT "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.props"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${fenrir_managed_dir}/Fenrir.Scripting.props"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.props"
    DEPENDS "${fenrir_managed_dir}/Fenrir.Scripting.props"
    VERBATIM
)
add_custom_target(FenrirScriptingContract ALL DEPENDS
    "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
    "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    "${FENRIR_SCRIPTING_CONTRACT_DIR}/analyzers/Fenrir.Scripting.Generators.dll"
    "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.props")
include(cmake/FenrirScriptingDeploy.cmake)
# The functions each package's extension directory calls; the root adds those after this one.
include(cmake/FenrirScriptingExtension.cmake)

add_library(FenrirScripting STATIC
    src/Scripting.cpp
    src/Bindings.cpp
    src/ExtensionSet.cpp
    src/ExtensionSet.hpp
    src/NativeRegistrar.hpp
    src/ChangeBuffer.cpp
    src/Queries.cpp
    src/Queries.hpp
    src/ChangeBuffer.hpp
    src/ScriptData.cpp
    src/ScriptSystems.cpp
    src/ScriptSystems.hpp
    src/SystemHost.hpp
    src/SystemHostNames.hpp
    src/GenerationLifecycle.cpp
    src/ReloadWatcher.cpp
    src/ReloadWatcher.hpp
    src/ScriptAssemblyLoader.hpp
    src/GenerationLifecycle.hpp
    src/ScriptData.hpp
    src/ScriptComponents.hpp
    src/ComponentRegistry.hpp
    include/FenrirScripting/BoundMappings.hpp
    include/FenrirScripting/ComponentRegistry.hpp
    include/FenrirScripting/Generated/FenrirMathMappings.hpp
    include/FenrirScripting/Generated/FenrirScriptingMappings.hpp
    include/FenrirScripting/NativeGuard.hpp
    include/FenrirScripting/NativeMarshalling.hpp
    include/FenrirScripting/ZeroingOutputs.hpp
    src/EngineContext.hpp
    src/Generated/FenrirScriptingNatives.hpp
    src/Generated/FenrirScriptingRegistration.cpp
    include/FenrirScripting/Scripting.hpp
    include/FenrirScripting/ScriptingExtension.hpp
    include/FenrirScripting/WarnOnceRecord.hpp
)

target_include_directories(FenrirScripting PUBLIC include)
target_link_libraries(FenrirScripting PUBLIC FenrirApp FenrirLogger Iris::Iris)

# The built-in extension's binding (ADR-0042): the engine's components (Transform, Name, Relationship) and the entity
# link they hold are written once, in FenrirECS's headers (../FenrirECS/api/FenrirECS.api.json, with FenrirMath's vector
# and quaternion as bound types), and every native function once, as its definition in
# src/Bindings.cpp, which describe-api's natives mode reads into api/FenrirScripting.natives.json. Iris.Bindings
# generates the rest from those descriptions and api/FenrirScripting.binding.toml, all committed:
#   Managed/Fenrir.Scripting/Generated/         the C# mirrors, beside which the contract's own .cs files add behaviour
#                                               only, and the natives' declarations, Native;
#   include/FenrirScripting/Generated/          the Iris mappings, which an extension whose types hold the engine's
#                                               includes;
#   src/Generated/                              the natives' declarations, and the extension's components, sharedStructs
#                                               and functions;
#   Tests/Generated/                            the defaults test.
# After changing a shared type or a native, regenerate both stages:
#   cmake --build <dir> --target fenrir-describe-api
#   cmake --build <dir> --target fenrir-generate-bindings
fenrir_describe_api(FenrirScripting api/FenrirScripting.api.toml)
fenrir_describe_api_natives(FenrirScripting api/FenrirScripting.binding.toml)
iris_generate_binding(FenrirScripting api/FenrirScripting.binding.toml ../FenrirECS/api/FenrirECS.api.json
    api/FenrirScripting.api.json)
# FenrirMath's binding: the Fenrir.Math contract's colour, ray and box, which every other binding uses (ADR-0043).
iris_generate_binding(FenrirMath api/FenrirMath.binding.toml ../FenrirMath/api/FenrirMath.api.json)

if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
