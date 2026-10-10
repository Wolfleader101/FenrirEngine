if(IRIS_DOTNET_EXECUTABLE)
    set(iris_checks_dotnet "${IRIS_DOTNET_EXECUTABLE}")
else()
    find_program(iris_checks_dotnet NAMES dotnet REQUIRED)
endif()
# Single-configuration generators may have an empty CMAKE_BUILD_TYPE.
set(iris_checks_configuration "$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Debug>")
set(iris_checks_managed "${CMAKE_CURRENT_BINARY_DIR}/managed/${iris_checks_configuration}")
set(iris_checks_runtimeconfig "${iris_checks_managed}/checks/ManagedChecks.runtimeconfig.json")
set(iris_checks_bridge "${iris_checks_managed}/checks/Iris.Bridge.dll")
set(iris_checks_fixture "${iris_checks_managed}/fixture/Iris.Bridge.dll")
set(iris_checks_component "${iris_checks_managed}/component/Iris.Examples.Component.dll")

add_custom_target(IrisManagedBuild ALL
    # Publish a deployment, including its private dependency and .deps.json.
    # --output on build would also redirect referenced projects into this directory.
    COMMAND "${iris_checks_dotnet}" publish "${CMAKE_CURRENT_SOURCE_DIR}/../Examples/Hosting/Component/Iris.Examples.Component.csproj"
        --configuration "${iris_checks_configuration}" --output "${iris_checks_managed}/component" -m:1
    COMMAND "${iris_checks_dotnet}" build "${CMAKE_CURRENT_SOURCE_DIR}/ManagedChecks/ManagedChecks.csproj"
        --configuration "${iris_checks_configuration}" --output "${iris_checks_managed}/checks"
    COMMAND "${iris_checks_dotnet}" build "${CMAKE_CURRENT_SOURCE_DIR}/FixtureBridge/FixtureBridge.csproj"
        --configuration "${iris_checks_configuration}" --output "${iris_checks_managed}/fixture"
    VERBATIM
)

function(iris_add_native_checks target source)
    add_executable(${target} ${source} TestHarness.hpp)
    target_link_libraries(${target} PRIVATE Iris)
    add_dependencies(${target} IrisManagedBuild)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:IrisNethost>" "$<TARGET_FILE_DIR:${target}>"
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
    COMMAND "${iris_checks_dotnet}" "${iris_checks_managed}/checks/ManagedChecks.dll" "${iris_checks_component}")
add_test(NAME ManagedUnload
    COMMAND "${iris_checks_dotnet}" "${iris_checks_managed}/checks/ManagedChecks.dll" --unload "${iris_checks_component}")
set_tests_properties(ManagedUnload PROPERTIES TIMEOUT 30)

# Bridge checks: each bridge variant runs in its own process, because the bridge lives in the default
# load context. The fixture bridge selects its handshake response from IRIS_TEST_BRIDGE_MODE.
iris_add_native_checks(IrisBridgeChecks bridge.cpp)
foreach(iris_checks_mode IN ITEMS real fixture legacy oversized missing notbridge)
    if(iris_checks_mode STREQUAL "real")
        set(iris_checks_variant "${iris_checks_bridge}")
    elseif(iris_checks_mode STREQUAL "notbridge")
        set(iris_checks_variant "${iris_checks_component}")
    else()
        set(iris_checks_variant "${iris_checks_fixture}")
    endif()
    add_test(NAME "Bridge-${iris_checks_mode}"
        COMMAND IrisBridgeChecks "${iris_checks_mode}" "${iris_checks_runtimeconfig}"
            "${iris_checks_variant}" "${iris_checks_fixture}")
    set_tests_properties("Bridge-${iris_checks_mode}" PROPERTIES
        ENVIRONMENT "IRIS_TEST_BRIDGE_MODE=${iris_checks_mode}")
endforeach()

iris_add_native_checks(IrisComponentChecks components.cpp)
add_test(NAME Components
    COMMAND IrisComponentChecks "${iris_checks_runtimeconfig}" "${iris_checks_bridge}" "${iris_checks_component}")
