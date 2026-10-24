# FenrirDebugDraw's scripting extension (ADR-0029): the Fenrir.DebugDraw C# contract and the FenrirDebugDrawScripting
# library holding the native functions behind it. A library of its own so FenrirDebugDraw never links FenrirScripting.
# The root build adds this directory only when FENRIR_ENABLE_SCRIPTING is on (FenrirDebugDraw itself is always built).

# The contract, built against the scripting SDK with the .NET SDK Iris found, and rebuilt whenever the SDK's contracts
# (Fenrir.Scripting and Fenrir.Math) are. fenrir_deploy_debugdraw_scripting copies it next to an executable, beside the
# scripting contract, where Scripting::Install finds the extension's relative path.
set(FENRIR_DEBUGDRAW_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-debugdraw/contract" CACHE INTERNAL "Where the Fenrir.DebugDraw contract is built")
set(fenrir_debugdraw_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/../Managed/Fenrir.DebugDraw")
file(GLOB fenrir_debugdraw_contract_sources CONFIGURE_DEPENDS "${fenrir_debugdraw_managed_dir}/*.cs")
add_custom_command(
    OUTPUT "${FENRIR_DEBUGDRAW_CONTRACT_DIR}/Fenrir.DebugDraw.dll"
    COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_debugdraw_managed_dir}/Fenrir.DebugDraw.csproj"
        --configuration Release --output "${FENRIR_DEBUGDRAW_CONTRACT_DIR}" -nologo -v:q
        "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
        "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
    DEPENDS ${fenrir_debugdraw_contract_sources} "${fenrir_debugdraw_managed_dir}/Fenrir.DebugDraw.csproj"
        FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
        "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
    COMMENT "Building the Fenrir.DebugDraw contract"
    VERBATIM
)
add_custom_target(FenrirDebugDrawContract ALL DEPENDS "${FENRIR_DEBUGDRAW_CONTRACT_DIR}/Fenrir.DebugDraw.dll")
add_dependencies(FenrirDebugDrawContract FenrirScriptingContract)

# fenrir_deploy_debugdraw_scripting(<target>): after <target> builds, copy the Fenrir.DebugDraw contract next to it. Call
# it with fenrir_deploy_scripting(<target>) on an executable that adds DebugDrawing::ScriptingExtension() to its
# scripting config.
function(fenrir_deploy_debugdraw_scripting target)
    add_custom_target(${target}DeployDebugDrawContract
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FENRIR_DEBUGDRAW_CONTRACT_DIR}/Fenrir.DebugDraw.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_dependencies(${target}DeployDebugDrawContract FenrirDebugDrawContract)
    add_dependencies(${target} ${target}DeployDebugDrawContract)
endfunction()

add_library(FenrirDebugDrawScripting STATIC
    include/FenrirDebugDrawScripting/DebugDrawScripting.hpp
    src/DebugDrawScripting.cpp
)
target_include_directories(FenrirDebugDrawScripting PUBLIC include PRIVATE src)
target_link_libraries(FenrirDebugDrawScripting PUBLIC FenrirDebugDraw FenrirScripting)

if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
