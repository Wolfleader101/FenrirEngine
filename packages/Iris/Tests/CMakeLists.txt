if(NOT IRIS_BUILD_BRIDGE)
    message(FATAL_ERROR "IRIS_BUILD_TESTS requires IRIS_BUILD_BRIDGE.")
endif()
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
if(TARGET IrisBridge)
    # Serialize .NET builds that share the bridge project's obj directory.
    add_dependencies(IrisManagedBuild IrisBridge)
endif()

function(iris_add_native_checks target source)
    add_executable(${target} ${source} TestHarness.hpp)
    target_link_libraries(${target} PRIVATE Iris::Iris)
    add_dependencies(${target} IrisManagedBuild)
    # The checks pass bridge paths explicitly; this deploys nethost (and a bridge they do not use).
    iris_deploy(${target})
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

# The same checks' unbound mode: nothing is registered, so the component's internal calls are missing.
add_test(NAME Components-unbound
    COMMAND IrisComponentChecks "${iris_checks_runtimeconfig}" "${iris_checks_bridge}" "${iris_checks_component}" --unbound)

# Install this build and use it from the standalone consumer project, through find_package(Iris).
if(IRIS_INSTALL)
    add_test(NAME ConsumerPackage
        COMMAND "${CMAKE_COMMAND}"
            "-DIRIS_BUILD_DIR=${CMAKE_BINARY_DIR}"
            "-DIRIS_CONFIG=${iris_checks_configuration}"
            "-DCONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../Examples/Consumer"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer/${iris_checks_configuration}"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DCOMPONENT=${iris_checks_component}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ConsumerPackage.cmake")
    set_tests_properties(ConsumerPackage PROPERTIES TIMEOUT 300)
endif()

# Unsupported native types must not compile. The target is excluded from the normal build; this test
# builds it and passes only when the compiler output carries Iris's own static_assert message.
add_executable(IrisUnsupportedTypeShouldNotCompile EXCLUDE_FROM_ALL compile_fail.cpp)
target_link_libraries(IrisUnsupportedTypeShouldNotCompile PRIVATE Iris::Iris)
add_test(NAME UnsupportedTypeDoesNotCompile
    COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config $<CONFIG> --target IrisUnsupportedTypeShouldNotCompile)
set_tests_properties(UnsupportedTypeDoesNotCompile PROPERTIES
    PASS_REGULAR_EXPRESSION "Only bool, sized integers, float, double and Object cross the boundary by value")
