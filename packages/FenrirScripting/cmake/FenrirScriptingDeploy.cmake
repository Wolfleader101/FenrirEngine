# fenrir_deploy_scripting(<target> [EXTENSIONS <Pkg>...]): after <target> builds, copy what scripting needs at run time
# next to it: nethost, the Iris bridge (iris_deploy), the contract assemblies the built-in extension shares (Fenrir.Math
# and Fenrir.Scripting), and each named extension's contract (Fenrir.<Pkg>.dll, built by fenrir_scripting_contract in
# FenrirScriptingExtension.cmake) with the contracts it builds against, beside the scripting contract, where
# Scripting::Install finds the extension's relative path. Call it on an executable that adds the extensions to its
# scripting config. A second call on the same target adds its extensions and copies nothing twice.
function(fenrir_deploy_scripting target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "EXTENSIONS")
    if(NOT FENRIR_SCRIPTING_CONTRACT_DIR)
        message(FATAL_ERROR "fenrir_deploy_scripting: FenrirScripting has not been added to the build.")
    endif()

    if(NOT TARGET ${target}DeployContract)
        iris_deploy(${target})

        # The contract is refreshed whenever it is rebuilt, not only when <target> relinks: a script compiled against a
        # newer contract than the copy beside the executable fails to load, which is the right behaviour but a poor
        # surprise.
        add_custom_target(${target}DeployContract
            COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
                "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
                "$<TARGET_FILE_DIR:${target}>"
            VERBATIM
        )
        add_dependencies(${target}DeployContract FenrirScriptingContract)
        add_dependencies(${target} ${target}DeployContract)
    endif()

    foreach(pkg IN LISTS arg_EXTENSIONS)
        _fenrir_deploy_extension_contract(${target} ${pkg})
    endforeach()
endfunction()

# One extension's contract beside <target>, then the contracts it builds against. Fails when no extension <pkg> is in the
# build.
function(_fenrir_deploy_extension_contract target pkg)
    if(TARGET ${target}Deploy${pkg}Contract)
        return()
    endif()
    fenrir_scripting_contract_dir(${pkg} contract_dir)
    add_custom_target(${target}Deploy${pkg}Contract
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${contract_dir}/Fenrir.${pkg}.dll" "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    add_dependencies(${target}Deploy${pkg}Contract Fenrir${pkg}Contract)
    add_dependencies(${target} ${target}Deploy${pkg}Contract)

    get_property(uses GLOBAL PROPERTY FENRIR_SCRIPTING_CONTRACT_USES_${pkg})
    foreach(used IN LISTS uses)
        _fenrir_deploy_extension_contract(${target} ${used})
    endforeach()
endfunction()
