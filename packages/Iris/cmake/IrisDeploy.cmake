# iris_deploy(<target>): after <target> builds, copy what it needs at run time next to it: the nethost
# library and the bridge (Iris.Bridge.dll, its .runtimeconfig.json and .deps.json). The bridge's
# runtime configuration can be passed to Iris::Host::Initialize as-is. On Unix it also sets a run path
# so the executable finds nethost beside itself.
#
# Used both in the Iris build tree and by projects that find_package(Iris).
function(iris_deploy target)
    if(NOT IRIS_BRIDGE_DIR)
        message(FATAL_ERROR "iris_deploy: IRIS_BRIDGE_DIR is not set; the Iris bridge is unavailable.")
    endif()
    if(TARGET IrisBridge)
        add_dependencies(${target} IrisBridge)
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:Iris::Nethost>"
            "${IRIS_BRIDGE_DIR}/Iris.Bridge.dll"
            "${IRIS_BRIDGE_DIR}/Iris.Bridge.runtimeconfig.json"
            "${IRIS_BRIDGE_DIR}/Iris.Bridge.deps.json"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    if(APPLE)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
    endif()
endfunction()
