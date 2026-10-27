# fenrir_add_demo_app(<name> SOURCES <files>... [SCRIPTS <dir>] [ASSETS <dirs>...])
#
# Makes the demo app <name> (GLOSSARY.md: demo app): an executable from SOURCES linking FenrirDemoScaffold, with its
# asset root, the folder `assets` beside the executable, holding the shared examples/assets and then each ASSETS
# directory (relative to the calling CMakeLists.txt), copied over it after every build. With the tests on, it registers
# the app's smoke runs in CTest, one per backend: <name>-Smoke-null everywhere, and <name>-Smoke-opengl labelled gl,
# reported skipped without an OpenGL 4.6 context.
#
# SCRIPTS names the directory of the app's C# scripts project, `<name>.Scripts.csproj`. With scripting built
# (FENRIR_ENABLE_SCRIPTING) it is built against the scripting SDK and the contracts (Fenrir.Input, Fenrir.Window,
# Fenrir.Assets, Fenrir.Renderer, Fenrir.DebugDraw and the scaffold's Fenrir.Demo, whose folders it gets as -p:Fenrir<Name>ContractDir=,
# and Fenrir.Physics's too when physics's scripting extension is built, for an app that installs physics),
# and copied to `scripts/` beside the executable, with the contracts and what scripting needs at run time; the app's
# sources get FENRIR_DEMO_SCRIPT_ASSEMBLY, its path relative to the executable's directory, for
# DemoConfig::scriptAssembly. Without scripting, SCRIPTS is ignored.
include_guard(GLOBAL)

set(FENRIR_DEMO_SHARED_ASSETS "${CMAKE_CURRENT_LIST_DIR}/../../assets" CACHE INTERNAL "")

# How many frames each scene runs in a demo app's CTest smoke runs.
set(FENRIR_DEMO_SMOKE_FRAMES 60 CACHE STRING "Frames per scene in the demo apps' CTest smoke runs (FENRIR_DEMO_SMOKE)")

function(fenrir_add_demo_app name)
    cmake_parse_arguments(PARSE_ARGV 1 demo "" "SCRIPTS" "SOURCES;ASSETS")
    if(NOT demo_SOURCES)
        message(FATAL_ERROR "fenrir_add_demo_app(${name}): SOURCES is required")
    endif()
    if(demo_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "fenrir_add_demo_app(${name}): unknown arguments ${demo_UNPARSED_ARGUMENTS}")
    endif()

    add_executable(${name} ${demo_SOURCES})
    target_link_libraries(${name} PRIVATE FenrirDemoScaffold)

    # The asset root: the shared assets first, then the app's own, so an app's file replaces a shared one of its name.
    set(asset_root "$<TARGET_FILE_DIR:${name}>/assets")
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${FENRIR_DEMO_SHARED_ASSETS}" "${asset_root}"
        COMMENT "Copying the shared demo assets into ${name}'s asset root"
        VERBATIM
    )
    foreach(assets_dir IN LISTS demo_ASSETS)
        cmake_path(ABSOLUTE_PATH assets_dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_directory "${assets_dir}" "${asset_root}"
            COMMENT "Copying ${name}'s own assets into its asset root"
            VERBATIM
        )
    endforeach()

    if(demo_SCRIPTS AND FENRIR_ENABLE_SCRIPTING)
        fenrir_add_demo_scripts(${name} "${demo_SCRIPTS}")
    endif()

    # Every scene, whatever FENRIR_DEMO_SCENE and FENRIR_DEMO_KEYS the shell has: empty is unset.
    if(FENRIR_BUILD_TESTS)
        add_test(NAME ${name}-Smoke-null COMMAND ${name})
        set_tests_properties(${name}-Smoke-null PROPERTIES
            ENVIRONMENT "FENRIR_DEMO_SMOKE=${FENRIR_DEMO_SMOKE_FRAMES};FENRIR_DEMO_BACKEND=null;FENRIR_DEMO_SCENE=;FENRIR_DEMO_KEYS="
            TIMEOUT 300
            LABELS demo
        )
        # Skipped (exit code 4) without a display or an OpenGL 4.6 core context, as the renderer's GL tests are.
        add_test(NAME ${name}-Smoke-opengl COMMAND ${name})
        set_tests_properties(${name}-Smoke-opengl PROPERTIES
            ENVIRONMENT "FENRIR_DEMO_SMOKE=${FENRIR_DEMO_SMOKE_FRAMES};FENRIR_DEMO_BACKEND=opengl;FENRIR_DEMO_SCENE=;FENRIR_DEMO_KEYS="
            SKIP_RETURN_CODE 4
            TIMEOUT 300
            LABELS "demo;gl"
        )
    endif()
endfunction()

# fenrir_add_demo_scripts(<name> <dir>): builds the demo app <name>'s scripts project <dir>/<name>.Scripts.csproj and
# deploys it, the contracts and scripting's run-time files beside the executable (see fenrir_add_demo_app's SCRIPTS).
function(fenrir_add_demo_scripts name scripts_dir)
    cmake_path(ABSOLUTE_PATH scripts_dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    set(project "${scripts_dir}/${name}.Scripts.csproj")
    if(NOT EXISTS "${project}")
        message(FATAL_ERROR "fenrir_add_demo_app(${name}): SCRIPTS has no ${name}.Scripts.csproj in ${scripts_dir}")
    endif()
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${scripts_dir}/*.cs")
    list(FILTER sources EXCLUDE REGEX "/(obj|bin)/")

    # Built into a folder of its own, then copied beside the executable: the contracts are compile-time only (the host
    # shares its own copies), so the folder holds the scripts alone.
    set(out "${CMAKE_CURRENT_BINARY_DIR}/${name}-scripts")
    # Physics is the app's to install, not the scaffold's, but its contract is passed like the others when it is built.
    set(physics_args "")
    set(physics_depends "")
    if(TARGET FenrirPhysicsContract)
        set(physics_args "-p:FenrirPhysicsContractDir=${FENRIR_PHYSICS_CONTRACT_DIR}/")
        set(physics_depends FenrirPhysicsContract "${FENRIR_PHYSICS_CONTRACT_DIR}/Fenrir.Physics.dll")
    endif()
    add_custom_command(
        OUTPUT "${out}/${name}.Scripts.dll"
        COMMAND "${IRIS_DOTNET_EXECUTABLE}" build "${project}" --configuration Release --output "${out}" -nologo -v:q
            "-p:FenrirScriptingSdk=${FENRIR_SCRIPTING_CONTRACT_DIR}/"
            "-p:FenrirInputContractDir=${FENRIR_INPUT_CONTRACT_DIR}/"
            "-p:FenrirWindowContractDir=${FENRIR_WINDOW_CONTRACT_DIR}/"
            "-p:FenrirAssetsContractDir=${FENRIR_ASSETS_CONTRACT_DIR}/"
            "-p:FenrirRendererContractDir=${FENRIR_RENDERER_CONTRACT_DIR}/"
            "-p:FenrirDebugDrawContractDir=${FENRIR_DEBUGDRAW_CONTRACT_DIR}/"
            "-p:FenrirDemoContractDir=${FENRIR_DEMO_CONTRACT_DIR}/"
            ${physics_args}
            "-p:BaseIntermediateOutputPath=${CMAKE_CURRENT_BINARY_DIR}/${name}-scripts-obj/"
        DEPENDS ${sources} "${project}"
            FenrirScriptingContract "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Scripting.dll"
            "${FENRIR_SCRIPTING_CONTRACT_DIR}/Fenrir.Math.dll"
            FenrirInputContract "${FENRIR_INPUT_CONTRACT_DIR}/Fenrir.Input.dll"
            FenrirWindowContract "${FENRIR_WINDOW_CONTRACT_DIR}/Fenrir.Window.dll"
            FenrirAssetsContract "${FENRIR_ASSETS_CONTRACT_DIR}/Fenrir.Assets.dll"
            FenrirRendererContract "${FENRIR_RENDERER_CONTRACT_DIR}/Fenrir.Renderer.dll"
            FenrirDebugDrawContract "${FENRIR_DEBUGDRAW_CONTRACT_DIR}/Fenrir.DebugDraw.dll"
            FenrirDemoContract "${FENRIR_DEMO_CONTRACT_DIR}/Fenrir.Demo.dll"
            ${physics_depends}
        COMMENT "Building ${name}'s C# scripts"
        VERBATIM
    )
    add_custom_target(${name}Scripts DEPENDS "${out}/${name}.Scripts.dll")
    add_custom_target(${name}DeployScripts
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:${name}>/scripts"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${out}/${name}.Scripts.dll" "${out}/${name}.Scripts.deps.json"
            "$<TARGET_FILE_DIR:${name}>/scripts"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${FENRIR_DEMO_CONTRACT_DIR}/Fenrir.Demo.dll"
            "$<TARGET_FILE_DIR:${name}>"
        VERBATIM
    )
    add_dependencies(${name}DeployScripts ${name}Scripts FenrirDemoContract)
    add_dependencies(${name} ${name}DeployScripts)
    target_compile_definitions(${name} PRIVATE FENRIR_DEMO_SCRIPT_ASSEMBLY="scripts/${name}.Scripts.dll")

    # The scaffold installs every package's extension (DemoScripting.cpp), so their contracts go beside the executable.
    fenrir_deploy_scripting(${name} EXTENSIONS Input Window Assets Renderer DebugDraw)
endfunction()
