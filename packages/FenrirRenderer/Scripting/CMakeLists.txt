# FenrirRenderer's scripting extension (ADR-0029): the Fenrir.Renderer C# contract and the FenrirRendererScripting
# library holding the native functions behind it. A library of its own so FenrirRenderer never links FenrirScripting.
# The root build adds this directory only when FENRIR_ENABLE_RENDERER and FENRIR_ENABLE_SCRIPTING are both on, after
# FenrirInput's and FenrirAssets's extensions, whose contracts (WindowId, asset handles) the renderer's builds against.

# The contract, built against the scripting SDK and the Fenrir.Input and Fenrir.Assets contracts with the .NET SDK Iris
# found, and rebuilt whenever any is. fenrir_deploy_renderer_scripting copies it next to an executable, beside the
# scripting contract, where Scripting::Install finds the extension's relative path.
set(FENRIR_RENDERER_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-renderer/contract" CACHE INTERNAL "Where the Fenrir.Renderer contract is built")
set(fenrir_renderer_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Managed/Fenrir.Renderer")
file(GLOB fenrir_renderer_contract_sources CONFIGURE_DEPENDS "${fenrir_renderer_managed_dir}/*.cs"
    "${fenrir_renderer_managed_dir}/Generated/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_RENDERER_CONTRACT_DIR}/Fenrir.Renderer.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_renderer_managed_dir}/Fenrir.Renderer.csproj"
        --configuration Release --output "${FENRIR_RENDERER_CONTRACT_DIR}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:FenrirInputContractDir=${FENRIR_INPUT_CONTRACT_DIR}/"
        "-p:FenrirAssetsContractDir=${FENRIR_ASSETS_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
    DEPENDS ${fenrir_renderer_contract_sources} "${fenrir_renderer_managed_dir}/Fenrir.Renderer.csproj"
        FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
        FenrirInputContract "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll"
        FenrirAssetsContract "${FENRIR_ASSETS_CONTRACT_DIR}/Fenrir.Assets.dll"
    COMMENT "Building the Fenrir.Renderer contract"
    VERBATIM
)
add_custom_target(FenrirRendererContract ALL DEPENDS "${FENRIR_RENDERER_CONTRACT_DIR}/Fenrir.Renderer.dll")
add_dependencies(FenrirRendererContract FenrirScriptingContract FenrirInputContract FenrirAssetsContract)

# fenrir_deploy_renderer_scripting(<target>): after <target> builds, copy the Fenrir.Renderer contract next to it. Call
# it with fenrir_deploy_scripting(<target>), fenrir_deploy_input_scripting(<target>) and
# fenrir_deploy_assets_scripting(<target>) on an executable that adds Renderer::ScriptingExtension() to its scripting
# config.
function(fenrir_deploy_renderer_scripting target)
    add_custom_target(${target}DeployRendererContract
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FENRIR_RENDERER_CONTRACT_DIR}/Fenrir.Renderer.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_dependencies(${target}DeployRendererContract FenrirRendererContract)
    add_dependencies(${target} ${target}DeployRendererContract)
endfunction()

add_library(FenrirRendererScripting STATIC
    include/FenrirRendererScripting/RendererScripting.hpp
    include/FenrirRendererScripting/Generated/FenrirRendererMappings.hpp
    src/RendererScripting.cpp
    src/RendererScriptingState.hpp
    src/Generated/FenrirRendererRegistration.cpp
)
# Its generated sources, empty until the first generation on a new binding.
iris_binding_outputs(FenrirRendererScripting)
target_include_directories(FenrirRendererScripting PUBLIC include PRIVATE src)
target_link_libraries(FenrirRendererScripting PUBLIC FenrirRenderer FenrirAssetsScripting FenrirInputScripting
                                                     FenrirScripting)

# The binding (ADR-0043): the renderer's components, resources and stats are written once, in FenrirRenderer's headers
# (../api/FenrirRenderer.api.json), and every native function once, as its definition in src/RendererScripting.cpp, which
# describe-api's natives mode reads into api/FenrirRenderer.natives.json. Iris.Bindings generates the rest from those
# descriptions, api/FenrirRendererScripting.api.json (the entity value the natives take, described from FenrirScripting's
# header by this library) and api/FenrirRenderer.binding.toml, naming the math, input and assets bindings' types, all
# committed:
#   ../Managed/Fenrir.Renderer/Generated/       the C# mirrors and the natives' declarations, RendererNative;
#   include/FenrirRendererScripting/Generated/  the Iris mappings;
#   src/Generated/                              the extension's components, resources, sharedStructs and functions;
#   Tests/Generated/                            the defaults test and the natives test.
# After changing a shared type or a native, regenerate both stages:
#   cmake --build <dir> --target fenrir-describe-api
#   cmake --build <dir> --target fenrir-generate-bindings
fenrir_describe_api(FenrirRendererScripting api/FenrirRendererScripting.api.toml)
fenrir_describe_api_natives(FenrirRendererScripting api/FenrirRenderer.binding.toml)
iris_generate_binding(FenrirRenderer api/FenrirRenderer.binding.toml ../api/FenrirRenderer.api.json
    api/FenrirRendererScripting.api.json)

if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
