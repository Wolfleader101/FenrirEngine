# FenrirInput's scripting extension (ADR-0029): the Fenrir.Input C# contract and the FenrirInputScripting library
# holding the native functions behind it. A library of its own so FenrirInput never links FenrirScripting. The root
# build adds this directory only when FENRIR_ENABLE_SCRIPTING is on (FenrirInput itself is always built).

# The contract, built against the scripting SDK with the .NET SDK Iris found, and rebuilt whenever the SDK's contracts
# (Fenrir.Scripting and Fenrir.Math) are. fenrir_deploy_input_scripting copies it next to an executable, beside the
# scripting contract, where Scripting::Install finds the extension's relative path.
set(FENRIR_INPUT_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-input/contract" CACHE INTERNAL "Where the Fenrir.Input contract is built")
set(fenrir_input_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Managed/Fenrir.Input")
file(GLOB fenrir_input_contract_sources CONFIGURE_DEPENDS "${fenrir_input_managed_dir}/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_input_managed_dir}/Fenrir.Input.csproj"
        --configuration Release --output "${FENRIR_INPUT_CONTRACT_DIR}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
    DEPENDS ${fenrir_input_contract_sources} "${fenrir_input_managed_dir}/Fenrir.Input.csproj"
        FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMENT "Building the Fenrir.Input contract"
    VERBATIM
)
add_custom_target(FenrirInputContract ALL DEPENDS "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll")
add_dependencies(FenrirInputContract FenrirScriptingContract)

# fenrir_deploy_input_scripting(<target>): after <target> builds, copy the Fenrir.Input contract next to it. Call it with
# fenrir_deploy_scripting(<target>) on an executable that adds Input::ScriptingExtension() to its scripting config.
function(fenrir_deploy_input_scripting target)
    add_custom_target(${target}DeployInputContract
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_dependencies(${target}DeployInputContract FenrirInputContract)
    add_dependencies(${target} ${target}DeployInputContract)
endfunction()

add_library(FenrirInputScripting STATIC
    include/FenrirInputScripting/InputMappings.hpp
    include/FenrirInputScripting/InputScripting.hpp
    src/ScriptInputClaims.hpp
    src/InputScripting.cpp
)
target_include_directories(FenrirInputScripting PUBLIC include PRIVATE src)
target_link_libraries(FenrirInputScripting PUBLIC FenrirInput FenrirScripting)

if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
