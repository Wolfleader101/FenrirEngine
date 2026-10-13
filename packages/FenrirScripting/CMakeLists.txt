# C# scripting for FenrirEngine, built on Iris. Added to the build only when FENRIR_ENABLE_SCRIPTING is on.

# The scripting SDK, built with the .NET SDK Iris found, in one folder: the Fenrir.Scripting contract (the C# API scripts
# compile against, shared by the host and never reloaded), the Fenrir.Scripting.Generators source generator under
# analyzers/, and Fenrir.Scripting.props, which a script project imports to get both. fenrir_deploy_scripting copies the
# contract next to an executable; script projects use the folder as it is (-p:FenrirScriptingSdk=<folder>/).
# A cache variable, so it works from any directory of the build.
set(FENRIR_SCRIPTING_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-scripting/contract" CACHE INTERNAL "Where the Fenrir.Scripting SDK is built")
set(fenrir_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/Managed")
file(GLOB fenrir_contract_sources CONFIGURE_DEPENDS "${fenrir_managed_dir}/Fenrir.Scripting/*.cs")
file(GLOB fenrir_generator_sources CONFIGURE_DEPENDS "${fenrir_managed_dir}/Fenrir.Scripting.Generators/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_managed_dir}/Fenrir.Scripting/Fenrir.Scripting.csproj"
        --configuration Release --output "${FENRIR_SCRIPTING_CONTRACT_DIR}" -nologo -v:q
    # The contract builds the generator as a project reference, so the generator is built first by its own command and
    # the two builds never write the same files at once.
    DEPENDS ${fenrir_contract_sources} "${fenrir_managed_dir}/Fenrir.Scripting/Fenrir.Scripting.csproj"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/analyzers/Fenrir.Scripting.Generators.dll"
    COMMENT "Building the Fenrir.Scripting contract"
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
    "${FENRIR_SCRIPTING_CONTRACT_DIR}/analyzers/Fenrir.Scripting.Generators.dll"
    "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.props")
include(cmake/FenrirScriptingDeploy.cmake)

add_library(FenrirScripting STATIC
    src/Scripting.cpp
    src/Bindings.cpp
    src/ChangeBuffer.cpp
    src/ChangeBuffer.hpp
    src/EngineComponents.cpp
    src/ScriptComponents.cpp
    src/ScriptComponents.hpp
    src/ComponentRegistry.hpp
    include/FenrirScripting/ComponentRegistry.hpp
    src/EngineContext.hpp
    include/FenrirScripting/Scripting.hpp
)

target_include_directories(FenrirScripting PUBLIC include)
target_link_libraries(FenrirScripting PUBLIC FenrirApp FenrirLogger Iris::Iris)

add_subdirectory(Tests)
