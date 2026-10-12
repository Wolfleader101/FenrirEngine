# fenrir_deploy_scripting(<target>): after <target> builds, copy what scripting needs at run time next to it: nethost,
# the Iris bridge (iris_deploy) and the Fenrir.Scripting contract assembly that Scripting::Install shares.
function(fenrir_deploy_scripting target)
    if(NOT FENRIR_SCRIPTING_CONTRACT_DIR)
        message(FATAL_ERROR "fenrir_deploy_scripting: FenrirScripting has not been added to the build.")
    endif()
    iris_deploy(${target})

    # The contract is refreshed whenever it is rebuilt, not only when <target> relinks: a script compiled against a newer
    # contract than the copy beside the executable fails to load, which is the right behaviour but a poor surprise.
    add_custom_target(${target}DeployContract
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_dependencies(${target}DeployContract FenrirScriptingContract)
    add_dependencies(${target} ${target}DeployContract)
endfunction()
