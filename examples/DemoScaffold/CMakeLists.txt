# The demo scaffold (examples/DemoScaffold/README.md): the FenrirDemoScaffold static library every demo app links, and
# fenrir_add_demo_app, which makes a demo app. Built with the examples when the renderer is on.
add_library(FenrirDemoScaffold STATIC
    include/FenrirDemoScaffold/DemoScaffold.hpp
    include/FenrirDemoScaffold/DemoUI.hpp
    src/DemoScaffold.cpp
    src/DemoUI.cpp
)
target_include_directories(FenrirDemoScaffold PUBLIC include)
target_link_libraries(FenrirDemoScaffold
    PUBLIC FenrirApp FenrirRenderer FenrirInput FenrirMath FenrirECS FenrirUI
    PRIVATE FenrirWindow FenrirAssets FenrirDebugDraw FenrirLogger
)
# The demo UI kit's files (assets/ui, copied into every demo app's asset root by fenrir_add_demo_app) are read from this
# source tree while it is there, so an edit to them hot-reloads and a rebuild keeps it.
set(FENRIR_DEMO_SCAFFOLD_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets" CACHE INTERNAL "The demo UI kit's files")
target_compile_definitions(FenrirDemoScaffold PRIVATE FENRIR_DEMO_SOURCE_ASSETS="${FENRIR_DEMO_SCAFFOLD_ASSETS}")

# With scripting built, the scaffold installs Scripting for an app that has scripts, with every package's extension and
# its own, whose contract is the shared Fenrir.Demo assembly (Managed/Fenrir.Demo). FENRIR_DEMO_SCRIPTING tells the
# scaffold's sources, and each app's, that scripting is there.
if(FENRIR_ENABLE_SCRIPTING)
    target_sources(FenrirDemoScaffold PRIVATE src/DemoScripting.hpp src/DemoScripting.cpp)
    target_compile_definitions(FenrirDemoScaffold PUBLIC FENRIR_DEMO_SCRIPTING=1)
    # PUBLIC FenrirScripting: DemoConfig carries the app's own extensions.
    target_link_libraries(FenrirDemoScaffold PUBLIC FenrirScripting
                                             PRIVATE FenrirInputScripting FenrirWindowScripting FenrirAssetsScripting
                                                     FenrirRendererScripting FenrirDebugDrawScripting)
    # The scaffold installs FenrirUI, so its scripting extension and the text and painter ones it builds on are the
    # scaffold's too, wherever they are built.
    if(TARGET FenrirUIScripting)
        target_link_libraries(FenrirDemoScaffold PRIVATE FenrirTextScripting FenrirPainterScripting FenrirUIScripting)
        target_compile_definitions(FenrirDemoScaffold PRIVATE FENRIR_DEMO_UI_SCRIPTING=1)
    endif()

    set(FENRIR_DEMO_CONTRACT_DIR "${CMAKE_BINARY_DIR}/fenrir-demo/contract" CACHE INTERNAL "Where the Fenrir.Demo assembly is built")
    set(fenrir_demo_managed_dir "${CMAKE_CURRENT_SOURCE_DIR}/Managed/Fenrir.Demo")
    file(GLOB fenrir_demo_contract_sources CONFIGURE_DEPENDS "${fenrir_demo_managed_dir}/*.cs")
    # Its DemoUI finds the kit's root through FenrirUI's contract, which it is built against with the ones that
    # contract builds on, wherever FenrirUI's scripting is built; elsewhere it is left out (-p:FenrirDemoUI=false).
    set(fenrir_demo_ui_args "-p:FenrirDemoUI=false")
    set(fenrir_demo_ui_depends "")
    if(TARGET FenrirUIContract)
        set(fenrir_demo_ui_args "")
        foreach(contract IN ITEMS Input Assets Text Painter UI)
            string(TOUPPER "${contract}" CONTRACT_UPPER)
            fenrir_scripting_contract_dll(${contract} contract_dll)
            list(APPEND fenrir_demo_ui_args "-p:Fenrir${contract}ContractDir=${FENRIR_${CONTRACT_UPPER}_CONTRACT_DIR}/")
            list(APPEND fenrir_demo_ui_depends Fenrir${contract}Contract "${contract_dll}")
        endforeach()
    endif()
    add_custom_command(
        OUTPUT "${FENRIR_DEMO_CONTRACT_DIR}/Fenrir.Demo.dll"
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${fenrir_demo_managed_dir}/Fenrir.Demo.csproj"
            --configuration Release --output "${FENRIR_DEMO_CONTRACT_DIR}" -nologo -v:q
            "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
            ${fenrir_demo_ui_args}
            "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/contract-obj/"
        DEPENDS ${fenrir_demo_contract_sources} "${fenrir_demo_managed_dir}/Fenrir.Demo.csproj"
            FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
            "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
            ${fenrir_demo_ui_depends}
        COMMENT "Building the Fenrir.Demo assembly"
        VERBATIM
    )
    add_custom_target(FenrirDemoContract ALL DEPENDS "${FENRIR_DEMO_CONTRACT_DIR}/Fenrir.Demo.dll")
    add_dependencies(FenrirDemoContract FenrirScriptingContract)
    if(TARGET FenrirUIContract)
        add_dependencies(FenrirDemoContract FenrirUIContract)
    endif()
endif()

include(cmake/FenrirDemoApp.cmake)

# The scaffold's own tests, on the null backend: the scene list, the free-fly camera, the demo UI kit and the smoke
# runs.
if(FENRIR_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
