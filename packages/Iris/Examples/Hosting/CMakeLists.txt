add_executable(IrisHostingExample main.cpp)
target_link_libraries(IrisHostingExample PRIVATE Iris::Iris)
set_target_properties(IrisHostingExample PROPERTIES CXX_EXTENSIONS OFF)
iris_deploy(IrisHostingExample)

# Publish the example component (with its private dependency and contracts) into a `component`
# directory next to the example, so the example runs without arguments. Needs the .NET SDK.
if(IRIS_BUILD_BRIDGE)
    set(iris_example_component_dir "${CMAKE_BINARY_DIR}/iris/examples/component")
    file(GLOB iris_example_component_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Component/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Component/*.csproj"
        "${CMAKE_CURRENT_SOURCE_DIR}/Dependency/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Dependency/*.csproj"
        "${CMAKE_CURRENT_SOURCE_DIR}/Contracts/*.cs" "${CMAKE_CURRENT_SOURCE_DIR}/Contracts/*.csproj")
    add_custom_command(
        OUTPUT "${iris_example_component_dir}/Iris.Examples.Component.dll"
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" publish "${CMAKE_CURRENT_SOURCE_DIR}/Component/Iris.Examples.Component.csproj"
            --configuration Release --output "${iris_example_component_dir}" -nologo -v:q
        DEPENDS ${iris_example_component_sources} IrisBridge
        COMMENT "Publishing the Iris example component"
        VERBATIM
    )
    add_custom_target(IrisExampleComponent DEPENDS "${iris_example_component_dir}/Iris.Examples.Component.dll")
    add_dependencies(IrisHostingExample IrisExampleComponent)
    add_custom_command(TARGET IrisHostingExample POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${iris_example_component_dir}" "$<TARGET_FILE_DIR:IrisHostingExample>/component"
        VERBATIM
    )
endif()
