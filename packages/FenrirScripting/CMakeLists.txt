# C# scripting for FenrirEngine, built on Iris. Added to the build only when FENRIR_ENABLE_SCRIPTING is on.

# The Fenrir.Scripting contract: the C# API scripts compile against, shared by the host (never reloaded). Built with
# the .NET SDK Iris found; fenrir_deploy_scripting copies it next to an executable.
# A cache variable, so fenrir_deploy_scripting works from any directory of the build.
set(FENRIR_SCRIPTING_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-scripting/contract" CACHE INTERNAL "Where the Fenrir.Scripting contract is built")
set(FENRIR_SCRIPTING_CONTRACT_PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Managed/Fenrir.Scripting/Fenrir.Scripting.csproj")
file(GLOB fenrir_contract_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Managed/Fenrir.Scripting/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${FENRIR_SCRIPTING_CONTRACT_PROJECT}"
        --configuration Release --output "${FENRIR_SCRIPTING_CONTRACT_DIR}" -nologo -v:q
    DEPENDS ${fenrir_contract_sources} "${FENRIR_SCRIPTING_CONTRACT_PROJECT}"
    COMMENT "Building the Fenrir.Scripting contract"
    VERBATIM
)
add_custom_target(FenrirScriptingContract ALL DEPENDS "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll")
include(cmake/FenrirScriptingDeploy.cmake)

add_library(FenrirScripting STATIC
    src/Scripting.cpp
    src/Bindings.cpp
    src/EngineContext.hpp
    include/FenrirScripting/Scripting.hpp
)

target_include_directories(FenrirScripting PUBLIC include)
target_link_libraries(FenrirScripting PUBLIC FenrirApp FenrirLogger Iris::Iris)

add_subdirectory(Tests)
