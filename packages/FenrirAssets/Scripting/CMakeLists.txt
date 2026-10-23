# FenrirAssets's scripting extension (ADR-0029): the Fenrir.Assets C# contract and the FenrirAssetsScripting library
# holding the native functions behind it. A library of its own so FenrirAssets never links FenrirScripting. The root
# build adds this directory only when FENRIR_ENABLE_ASSETS and FENRIR_ENABLE_SCRIPTING are both on.

# The contract, built against the scripting SDK with the .NET SDK Iris found, and rebuilt whenever the SDK's contracts
# (Fenrir.Scripting and Fenrir.Math) are. fenrir_deploy_assets_scripting copies it next to an executable, beside the
# scripting contract, where Scripting::Install finds the extension's relative path.
set(FENRIR_ASSETS_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-assets/contract" CACHE INTERNAL "Where the Fenrir.Assets contract is built")
set(fenrir_assets_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Managed/Fenrir.Assets")
file(GLOB fenrir_assets_contract_sources CONFIGURE_DEPENDS "${fenrir_assets_managed_dir}/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_ASSETS_CONTRACT_DIR}/Fenrir.Assets.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_assets_managed_dir}/Fenrir.Assets.csproj"
        --configuration Release --output "${FENRIR_ASSETS_CONTRACT_DIR}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
    DEPENDS ${fenrir_assets_contract_sources} "${fenrir_assets_managed_dir}/Fenrir.Assets.csproj"
        FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMENT "Building the Fenrir.Assets contract"
    VERBATIM
)
add_custom_target(FenrirAssetsContract ALL DEPENDS "${FENRIR_ASSETS_CONTRACT_DIR}/Fenrir.Assets.dll")
add_dependencies(FenrirAssetsContract FenrirScriptingContract)

# fenrir_deploy_assets_scripting(<target>): after <target> builds, copy the Fenrir.Assets contract next to it. Call it
# with fenrir_deploy_scripting(<target>) on an executable that adds Assets::ScriptingExtension() to its scripting config.
function(fenrir_deploy_assets_scripting target)
    add_custom_target(${target}DeployAssetsContract
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FENRIR_ASSETS_CONTRACT_DIR}/Fenrir.Assets.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_dependencies(${target}DeployAssetsContract FenrirAssetsContract)
    add_dependencies(${target} ${target}DeployAssetsContract)
endfunction()

add_library(FenrirAssetsScripting STATIC
    include/FenrirAssetsScripting/AssetMappings.hpp
    include/FenrirAssetsScripting/AssetsScripting.hpp
    src/AssetsScripting.cpp
)
target_include_directories(FenrirAssetsScripting PUBLIC include PRIVATE src)
target_link_libraries(FenrirAssetsScripting PUBLIC FenrirAssets FenrirScripting)

if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
