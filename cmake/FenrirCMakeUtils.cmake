function(copy_dll_to_executable_target executable_target)

    get_property(CS_PROJECT_NAME GLOBAL PROPERTY CS_PROJECT_NAME)
    get_property(CS_PROJECT_DIR GLOBAL PROPERTY CS_PROJECT_DIR)
    get_property(CS_OUTPUT_DIR GLOBAL PROPERTY CS_OUTPUT_DIR)

    set(cs_dll "${CS_OUTPUT_DIR}/${CS_PROJECT_NAME}.dll")
    set(cs_runtime_config "${CS_OUTPUT_DIR}/${CS_PROJECT_NAME}.runtimeconfig.json")

    message(STATUS "Copying DLL: ${cs_dll}")
    message(STATUS "Copying runtime config: ${cs_runtime_config}")

    add_custom_command(TARGET ${executable_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${cs_dll}"
            "$<TARGET_FILE_DIR:${executable_target}>"
        COMMAND ${CMAKE_COMMAND} -E copy
            "${cs_runtime_config}"
            "$<TARGET_FILE_DIR:${executable_target}>"
        COMMENT "Copying C# build output to executable directory"
    )

    add_dependencies(${executable_target} BuildCsProj)
endfunction()
