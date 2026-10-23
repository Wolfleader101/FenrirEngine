# fenrir_add_demo_app(<name> SOURCES <files>... [SCRIPTS <dir>] [ASSETS <dirs>...])
#
# Makes the demo app <name> (GLOSSARY.md: demo app): an executable from SOURCES linking FenrirDemoScaffold, with its
# asset root, the folder `assets` beside the executable, holding the shared examples/assets and then each ASSETS
# directory (relative to the calling CMakeLists.txt), copied over it after every build. With the tests on, it registers
# the app's smoke runs in CTest, one per backend: <name>-Smoke-null everywhere, and <name>-Smoke-opengl labelled gl,
# reported skipped without an OpenGL 4.6 context.
#
# SCRIPTS names the app's C# script project. It is accepted and not yet used: building and copying it, and the
# Fenrir.Demo assembly, come with scripting in the demo apps (renderer ticket 25).
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
