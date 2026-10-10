if(FENRIR_DOTNET_EXECUTABLE)
    set(fenrir_checks_dotnet "${FENRIR_DOTNET_EXECUTABLE}")
else()
    find_program(fenrir_checks_dotnet NAMES dotnet REQUIRED)
endif()
# Single-configuration generators may have an empty CMAKE_BUILD_TYPE.
set(fenrir_checks_configuration "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Debug>")
set(fenrir_checks_managed "${CMAKE_CURRENT_BINARY_DIR}/managed/${fenrir_checks_configuration}")
set(fenrir_checks_runtimeconfig "${fenrir_checks_managed}/checks/ManagedChecks.runtimeconfig.json")
set(fenrir_checks_bridge "${fenrir_checks_managed}/checks/Fenrir.ManagedHost.dll")
set(fenrir_checks_fixture "${fenrir_checks_managed}/fixture/Fenrir.ManagedHost.dll")
set(fenrir_checks_component "${fenrir_checks_managed}/component/Fenrir.HostingComponent.dll")

add_custom_target(FenrirScriptingManagedBuild ALL
    # Publish a deployment, including its private dependency and .deps.json.
    # --output on build would also redirect referenced projects into this directory.
    COMMAND "${fenrir_checks_dotnet}" publish "${CMAKE_CURRENT_SOURCE_DIR}/../Examples/Hosting/ManagedComponent/Fenrir.HostingComponent.csproj"
        --configuration "${fenrir_checks_configuration}" --output "${fenrir_checks_managed}/component" -m:1
    COMMAND "${fenrir_checks_dotnet}" build "${CMAKE_CURRENT_SOURCE_DIR}/ManagedChecks/ManagedChecks.csproj"
        --configuration "${fenrir_checks_configuration}" --output "${fenrir_checks_managed}/checks"
    COMMAND "${fenrir_checks_dotnet}" build "${CMAKE_CURRENT_SOURCE_DIR}/FixtureBridge/FixtureBridge.csproj"
        --configuration "${fenrir_checks_configuration}" --output "${fenrir_checks_managed}/fixture"
    VERBATIM
)

function(fenrir_add_native_checks target source)
    add_executable(${target} ${source} TestHarness.hpp)
    target_link_libraries(${target} PRIVATE FenrirDotNetHost)
    add_dependencies(${target} FenrirScriptingManagedBuild)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:FenrirScriptingNethost>" "$<TARGET_FILE_DIR:${target}>"
        VERBATIM
    )
    if(APPLE)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "@loader_path")
    elseif(UNIX)
        set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN")
    endif()
endfunction()

# Managed checks call the bridge's exports directly, in-process.
add_test(NAME ManagedContracts
    COMMAND "${fenrir_checks_dotnet}" "${fenrir_checks_managed}/checks/ManagedChecks.dll" "${fenrir_checks_component}")
add_test(NAME ManagedUnload
    COMMAND "${fenrir_checks_dotnet}" "${fenrir_checks_managed}/checks/ManagedChecks.dll" --unload "${fenrir_checks_component}")
set_tests_properties(ManagedUnload PROPERTIES TIMEOUT 30)

# Bridge checks: each bridge variant runs in its own process, because the bridge lives in the default
# load context. The fixture bridge selects its handshake response from FENRIR_TEST_BRIDGE_MODE.
fenrir_add_native_checks(FenrirBridgeChecks bridge.cpp)
foreach(fenrir_checks_mode IN ITEMS real fixture legacy oversized missing notbridge)
    if(fenrir_checks_mode STREQUAL "real")
        set(fenrir_checks_variant "${fenrir_checks_bridge}")
    elseif(fenrir_checks_mode STREQUAL "notbridge")
        set(fenrir_checks_variant "${fenrir_checks_component}")
    else()
        set(fenrir_checks_variant "${fenrir_checks_fixture}")
    endif()
    add_test(NAME "Bridge-${fenrir_checks_mode}"
        COMMAND FenrirBridgeChecks "${fenrir_checks_mode}" "${fenrir_checks_runtimeconfig}"
            "${fenrir_checks_variant}" "${fenrir_checks_fixture}")
    set_tests_properties("Bridge-${fenrir_checks_mode}" PROPERTIES
        ENVIRONMENT "FENRIR_TEST_BRIDGE_MODE=${fenrir_checks_mode}")
endforeach()

fenrir_add_native_checks(FenrirComponentChecks components.cpp)
add_test(NAME Components
    COMMAND FenrirComponentChecks "${fenrir_checks_runtimeconfig}" "${fenrir_checks_bridge}" "${fenrir_checks_component}")
