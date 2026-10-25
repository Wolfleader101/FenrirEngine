# FenrirWindow's scripting extension (ADR-0029): the Fenrir.Window C# contract and the FenrirWindowScripting library
# holding the native functions behind it. A library of its own so FenrirWindow never links FenrirScripting. The root
# build adds this directory only when FENRIR_ENABLE_RENDERER and FENRIR_ENABLE_SCRIPTING are both on, after
# FenrirInput's extension, whose contract (WindowId, the focus event) the window's builds against.

# The contract, built against the scripting SDK and the Fenrir.Input contract with the .NET SDK Iris found, and rebuilt
# whenever either is. fenrir_deploy_window_scripting copies it next to an executable, beside the scripting contract, where
# Scripting::Install finds the extension's relative path.
set(FENRIR_WINDOW_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-window/contract" CACHE INTERNAL "Where the Fenrir.Window contract is built")
set(fenrir_window_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Managed/Fenrir.Window")
file(GLOB fenrir_window_contract_sources CONFIGURE_DEPENDS "${fenrir_window_managed_dir}/*.cs"
    "${fenrir_window_managed_dir}/Generated/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_WINDOW_CONTRACT_DIR}/Fenrir.Window.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_window_managed_dir}/Fenrir.Window.csproj"
        --configuration Release --output "${FENRIR_WINDOW_CONTRACT_DIR}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/" "-p:FenrirInputContractDir=${FENRIR_INPUT_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
    DEPENDS ${fenrir_window_contract_sources} "${fenrir_window_managed_dir}/Fenrir.Window.csproj"
        FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
        FenrirInputContract "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll"
    COMMENT "Building the Fenrir.Window contract"
    VERBATIM
)
add_custom_target(FenrirWindowContract ALL DEPENDS "${FENRIR_WINDOW_CONTRACT_DIR}/Fenrir.Window.dll")
add_dependencies(FenrirWindowContract FenrirScriptingContract FenrirInputContract)

# fenrir_deploy_window_scripting(<target>): after <target> builds, copy the Fenrir.Window contract next to it. Call it
# with fenrir_deploy_scripting(<target>) and fenrir_deploy_input_scripting(<target>) on an executable that adds
# Window::ScriptingExtension() to its scripting config.
function(fenrir_deploy_window_scripting target)
    add_custom_target(${target}DeployWindowContract
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FENRIR_WINDOW_CONTRACT_DIR}/Fenrir.Window.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_dependencies(${target}DeployWindowContract FenrirWindowContract)
    add_dependencies(${target} ${target}DeployWindowContract)
endfunction()

add_library(FenrirWindowScripting STATIC
    include/FenrirWindowScripting/WindowScripting.hpp
    src/Utf16.hpp
    src/WindowScripting.cpp
    src/WindowScriptingState.hpp
    src/Generated/FenrirWindowMappings.hpp
    src/Generated/FenrirWindowRegistration.cpp
)
target_include_directories(FenrirWindowScripting PUBLIC include PRIVATE src)
target_link_libraries(FenrirWindowScripting PUBLIC FenrirWindow FenrirInputScripting FenrirScripting)

# The binding (ADR-0043): the window's events and size are written once, in FenrirWindow's headers
# (../api/FenrirWindow.api.json), and every native function once, as its definition in src/WindowScripting.cpp, which
# describe-api's natives mode reads into api/FenrirWindow.natives.json. Iris.Bindings generates the rest from those
# descriptions and api/FenrirWindow.binding.toml, naming FenrirInput's WindowId through FenrirInput's binding, all
# committed:
#   ../Managed/Fenrir.Window/Generated/   the C# mirrors, beside which the contract's own .cs files add behaviour only,
#                                         and the natives' declarations, WindowNative;
#   src/Generated/                        the Iris mappings and the extension's events, sharedStructs and functions;
#   Tests/Generated/                      the defaults test and the natives test.
# After changing a shared type or a native, regenerate both stages:
#   cmake --build <dir> --target fenrir-describe-api
#   cmake --build <dir> --target fenrir-generate-bindings
fenrir_describe_api_natives(FenrirWindowScripting api/FenrirWindow.binding.toml)
iris_generate_binding(FenrirWindow api/FenrirWindow.binding.toml ../api/FenrirWindow.api.json)

if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
