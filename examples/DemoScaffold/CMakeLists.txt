# The demo scaffold (examples/DemoScaffold/README.md): the FenrirDemoScaffold static library every demo app links, and
# fenrir_add_demo_app, which makes a demo app. Built with the examples when the renderer is on.
add_library(FenrirDemoScaffold STATIC
    include/FenrirDemoScaffold/DemoScaffold.hpp
    src/DemoScaffold.cpp
)
target_include_directories(FenrirDemoScaffold PUBLIC include)
target_link_libraries(FenrirDemoScaffold
    PUBLIC FenrirApp FenrirRenderer FenrirInput FenrirMath FenrirECS
    PRIVATE FenrirWindow FenrirAssets FenrirDebugDraw FenrirLogger
)

# With scripting built, the scaffold installs Scripting for an app that has scripts, with every package's extension and
# its own, whose contract is the shared Fenrir.Demo assembly (Managed/Fenrir.Demo). FENRIR_DEMO_SCRIPTING tells the
# scaffold's sources, and each app's, that scripting is there.
if(FENRIR_ENABLE_SCRIPTING)
    target_sources(FenrirDemoScaffold PRIVATE src/DemoScripting.hpp src/DemoScripting.cpp)
    target_compile_definitions(FenrirDemoScaffold PUBLIC FENRIR_DEMO_SCRIPTING=1)
    target_link_libraries(FenrirDemoScaffold PRIVATE FenrirScripting FenrirInputScripting FenrirWindowScripting
                                                     FenrirAssetsScripting FenrirRendererScripting
                                                     FenrirDebugDrawScripting)

    set(FENRIR_DEMO_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-demo/contract" CACHE INTERNAL "Where the Fenrir.Demo assembly is built")
    set(fenrir_demo_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/Managed/Fenrir.Demo")
    file(GLOB fenrir_demo_contract_sources CONFIGURE_DEPENDS "${fenrir_demo_managed_dir}/*.cs")
    add_custom_command(
        OUTPUT "${FENRIR_DEMO_CONTRACT_DIR}/Fenrir.Demo.dll"
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_demo_managed_dir}/Fenrir.Demo.csproj"
            --configuration Release --output "${FENRIR_DEMO_CONTRACT_DIR}" -nologo -v:q
            "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
            "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
        DEPENDS ${fenrir_demo_contract_sources} "${fenrir_demo_managed_dir}/Fenrir.Demo.csproj"
            FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
            "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
        COMMENT "Building the Fenrir.Demo assembly"
        VERBATIM
    )
    add_custom_target(FenrirDemoContract ALL DEPENDS "${FENRIR_DEMO_CONTRACT_DIR}/Fenrir.Demo.dll")
    add_dependencies(FenrirDemoContract FenrirScriptingContract)
endif()

include(cmake/FenrirDemoApp.cmake)

# The scaffold's own tests, on the null backend: the scene list, the free-fly camera and the smoke runs.
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
