# FenrirUI's Razor components (ADR-0067, ADR-0068), added by the scripting extension's CMakeLists after the Fenrir.UI
# contract and before the extension, which shares both contracts after Fenrir.UI (its binding's [registration] shares).
#
#   * Microsoft.AspNetCore.Components, the engine's stand-in for the names the stock Razor generator emits calls to.
#   * Fenrir.UI.Razor, the runtime over Fenrir.UI. Its folder is the one a script project imports Fenrir.UI.Razor.props
#     from: the props, RazorDispatcher.cs and _Imports.razor are copied beside both contracts, and FenrirUI's Razor
#     generator (Fenrir.UI.Razor.Generators) is built under analyzers/, so the build passes
#     -p:FenrirUIRazorContractDir=<it>/ to a script project with Razor components (the UI scripting fixture, the demos).
#   * Fenrir.UI.Razor.Tests, the builder and mounting against a recording fake of the element seam, run by CTest, and
#     FenrirUIRazorNoRazor: a script project with no .razor file that imports the props generates the same sources as
#     one that does not.
fenrir_scripting_contract(RazorComponents ASSEMBLY Microsoft.AspNetCore.Components
    USES UI Input Assets Text Painter
    SHARED_BY UI
)
# FenrirUI's Razor generator (ADR-0070), under analyzers/ in the Fenrir.UI.Razor folder, where the props find it; built
# by its own command first, so it and the contract never write the same files at once.
set(razor_generator_dir "${CMAKE_CURRENT_SOURCE_DIR}/Fenrir.UI.Razor.Generators")
set(razor_generator_dll "${PROJECT_BINARY_DIR}/fenrir-uirazor/contract/analyzers/Fenrir.UI.Razor.Generators.dll")
file(GLOB razor_generator_sources CONFIGURE_DEPENDS "${razor_generator_dir}/*.cs")
add_custom_command(
    OUTPUT "${razor_generator_dll}"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${razor_generator_dir}/Fenrir.UI.Razor.Generators.csproj"
        --configuration Release --output "${PROJECT_BINARY_DIR}/fenrir-uirazor/contract/analyzers" -nologo -v:q
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/generator-obj/"
    DEPENDS ${razor_generator_sources} "${razor_generator_dir}/Fenrir.UI.Razor.Generators.csproj"
    COMMENT "Building FenrirUI's Razor generator"
    VERBATIM
)
fenrir_scripting_contract(UIRazor ASSEMBLY Fenrir.UI.Razor
    USES UI RazorComponents Input Assets Text Painter
    SHARED_BY UI
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Fenrir.UI.Razor.props"
        "${CMAKE_CURRENT_SOURCE_DIR}/RazorDispatcher.cs"
        "${CMAKE_CURRENT_SOURCE_DIR}/_Imports.razor"
        "${razor_generator_dll}"
)

if(NOT FENRIR_BUILD_TESTS)
    return()
endif()

set(razor_contracts
    "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
    "-p:FenrirUIRazorContractDir=${FENRIR_UIRAZOR_CONTRACT_DIR}/"
    "-p:FenrirUIContractDir=${FENRIR_UI_CONTRACT_DIR}/"
    "-p:FenrirInputContractDir=${FENRIR_INPUT_CONTRACT_DIR}/"
    "-p:FenrirAssetsContractDir=${FENRIR_ASSETS_CONTRACT_DIR}/"
    "-p:FenrirTextContractDir=${FENRIR_TEXT_CONTRACT_DIR}/"
    "-p:FenrirPainterContractDir=${FENRIR_PAINTER_CONTRACT_DIR}/"
)
# xunit through dotnet test, which builds the project first; its build lives under the build tree.
add_test(NAME FenrirUIRazorTests
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" test "${CMAKE_CURRENT_SOURCE_DIR}/Fenrir.UI.Razor.Tests/Fenrir.UI.Razor.Tests.csproj"
        --nologo --artifacts-path "${CMAKE_CURRENT_BINARY_DIR}/razor-tests" ${razor_contracts})
set_tests_properties(FenrirUIRazorTests PROPERTIES TIMEOUT 600)

add_test(NAME FenrirUIRazorNoRazor
    COMMAND "${CMAKE_COMMAND}" "-DDOTNET=${IRIS_DOTNET_EXECUTABLE}"
        "-DPROJECT=${CMAKE_CURRENT_SOURCE_DIR}/Fenrir.UI.Razor.Tests/NoRazor/NoRazor.csproj"
        "-DSDK=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-DRAZOR=${FENRIR_UIRAZOR_CONTRACT_DIR}/"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/no-razor"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/Fenrir.UI.Razor.Tests/NoRazor/SameSources.cmake")
set_tests_properties(FenrirUIRazorNoRazor PROPERTIES TIMEOUT 300)
