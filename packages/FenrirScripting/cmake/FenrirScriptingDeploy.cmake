# fenrir_deploy_scripting(<target>): after <target> builds, copy what scripting needs at run time next to it: nethost,
# the Iris bridge (iris_deploy) and the Fenrir.Scripting contract assembly that Scripting::Install shares.
function(fenrir_deploy_scripting target)
    if(NOT FENRIR_SCRIPTING_CONTRACT_DIR)
        message(FATAL_ERROR "fenrir_deploy_scripting: FenrirScripting has not been added to the build.")
    endif()
    iris_deploy(${target})
    add_dependencies(${target} FenrirScriptingContract)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
endfunction()
