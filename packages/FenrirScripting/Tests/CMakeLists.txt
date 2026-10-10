if(FENRIR_DOTNET_EXECUTABLE)
    set(fenrir_checks_dotnet "${FENRIR_DOTNET_EXECUTABLE}")
else()
    find_program(fenrir_checks_dotnet NAMES dotnet REQUIRED)
endif()
# Single-configuration generators may have an empty CMAKE_BUILD_TYPE.
set(fenrir_checks_configuration "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Debug>")
set(fenrir_checks_managed "${CMAKE_CURRENT_BINARY_DIR}/managed/${fenrir_checks_configuration}")
add_custom_target(FenrirManagedDiagnosticChecksBuild ALL
    # Publish a deployment, including its private dependency and .deps.json.
    # --output on build would also redirect referenced projects into this directory.
    COMMAND "${fenrir_checks_dotnet}" publish "${CMAKE_CURRENT_SOURCE_DIR}/../Examples/Hosting/ManagedComponent/Fenrir.HostingComponent.csproj"
        --configuration "${fenrir_checks_configuration}" --output "${fenrir_checks_managed}/component" -m:1
    COMMAND "${fenrir_checks_dotnet}" build "${CMAKE_CURRENT_SOURCE_DIR}/ManagedDiagnostics/ManagedDiagnostics.csproj"
        --configuration "${fenrir_checks_configuration}" --output "${fenrir_checks_managed}/checks"
    COMMAND "${fenrir_checks_dotnet}" build "${CMAKE_CURRENT_SOURCE_DIR}/DiagnosticBridge/DiagnosticBridge.csproj"
        --configuration "${fenrir_checks_configuration}" --output "${fenrir_checks_managed}/fixture"
    VERBATIM
)

add_executable(FenrirDiagnosticChecks diagnostics.cpp)
target_link_libraries(FenrirDiagnosticChecks PRIVATE FenrirDotNetHost)
add_dependencies(FenrirDiagnosticChecks FenrirManagedDiagnosticChecksBuild)
add_custom_command(TARGET FenrirDiagnosticChecks POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:FenrirScriptingNethost>" "$<TARGET_FILE_DIR:FenrirDiagnosticChecks>"
    VERBATIM
)
if(APPLE)
    set_target_properties(FenrirDiagnosticChecks PROPERTIES BUILD_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(FenrirDiagnosticChecks PROPERTIES BUILD_RPATH "$ORIGIN")
endif()

add_test(NAME ManagedDiagnosticContract
    COMMAND "${fenrir_checks_dotnet}" "${fenrir_checks_managed}/checks/ManagedDiagnostics.dll" "${fenrir_checks_managed}/component/Fenrir.HostingComponent.dll")
add_test(NAME ManagedUnloadContract
    COMMAND "${fenrir_checks_dotnet}" "${fenrir_checks_managed}/checks/ManagedDiagnostics.dll" --unload "${fenrir_checks_managed}/component/Fenrir.HostingComponent.dll")
set_tests_properties(ManagedUnloadContract PROPERTIES TIMEOUT 30)
# The bridge is loaded into the default load context, so each bridge variant needs its own process.
# The fixture bridge selects its handshake response from FENRIR_TEST_BRIDGE_MODE.
foreach(fenrir_checks_mode IN ITEMS real fixture legacy oversized missing notbridge)
    if(fenrir_checks_mode STREQUAL "real")
        set(fenrir_checks_bridge "${fenrir_checks_managed}/checks/Fenrir.ManagedHost.dll")
    elseif(fenrir_checks_mode STREQUAL "notbridge")
        set(fenrir_checks_bridge "${fenrir_checks_managed}/component/Fenrir.HostingComponent.dll")
    else()
        set(fenrir_checks_bridge "${fenrir_checks_managed}/fixture/Fenrir.ManagedHost.dll")
    endif()
    # Real mode also tries the fixture as a second bridge in the same process.
    add_test(NAME "NativeDiagnostics-${fenrir_checks_mode}"
        COMMAND FenrirDiagnosticChecks "${fenrir_checks_mode}"
            "${fenrir_checks_managed}/checks/ManagedDiagnostics.runtimeconfig.json" "${fenrir_checks_bridge}"
            "${fenrir_checks_managed}/fixture/Fenrir.ManagedHost.dll")
    set_tests_properties("NativeDiagnostics-${fenrir_checks_mode}" PROPERTIES
        ENVIRONMENT "FENRIR_TEST_BRIDGE_MODE=${fenrir_checks_mode}")
endforeach()

add_executable(FenrirComponentChecks components.cpp)
target_link_libraries(FenrirComponentChecks PRIVATE FenrirDotNetHost)
add_dependencies(FenrirComponentChecks FenrirManagedDiagnosticChecksBuild)
add_custom_command(TARGET FenrirComponentChecks POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:FenrirScriptingNethost>" "$<TARGET_FILE_DIR:FenrirComponentChecks>"
    VERBATIM
)
if(APPLE)
    set_target_properties(FenrirComponentChecks PROPERTIES BUILD_RPATH "@loader_path")
elseif(UNIX)
    set_target_properties(FenrirComponentChecks PROPERTIES BUILD_RPATH "$ORIGIN")
endif()
add_test(NAME NativeComponentOwnership COMMAND FenrirComponentChecks
    "${fenrir_checks_managed}/checks/ManagedDiagnostics.runtimeconfig.json"
    "${fenrir_checks_managed}/checks/Fenrir.ManagedHost.dll"
    "${fenrir_checks_managed}/component/Fenrir.HostingComponent.dll")
