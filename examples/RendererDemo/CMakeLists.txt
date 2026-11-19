# RendererDemo (examples/RendererDemo/README.md): the renderer's demo app, one scene per renderer feature.
fenrir_add_demo_app(RendererDemo
    SOURCES
        src/main.cpp
        src/RendererScenes.hpp
        src/MeshesAndMaterials.cpp
        src/CamerasAndTargets.cpp
        src/Sky.cpp
        src/DebugDrawScene.cpp
        src/OverlayScene.cpp
        src/PainterScene.cpp
        src/SceneLabels.hpp
        src/SceneLabels.cpp
    SCRIPTS scripts
)
# "Debug draw" draws into its scene's DebugDraw; "Painter" paints with FenrirPainter, as do Overlay's and Painter's
# labels (SceneLabels), at the main window's content scale.
target_link_libraries(RendererDemo PRIVATE FenrirDebugDraw FenrirPainter FenrirWindow)
# The monitor screen's shader in "Cameras and render targets", a user shader embedded in the app.
fenrir_add_shaders(RendererDemo PROGRAM RendererDemoMonitor VERTEX shaders/monitor.vert FRAGMENT shaders/monitor.frag
                   EMBED)
# The stripes on a cube in "Meshes and materials": a user's own material shader (ADR-0024), embedded in the app.
fenrir_add_shaders(RendererDemo PROGRAM RendererDemoStripes VERTEX shaders/stripes.vert FRAGMENT shaders/stripes.frag
                   EMBED)

# Debug draw (C#)'s Razor panel, checked: a smoke run of the scene with its keys pressed must log the panel's mount into
# the scene's demo slot and 1 hiding the Wire group, the group its Wire toggle shows and hides
# (cmake/CheckRazorPanelLog.cmake). Only where C# can
# mount a Razor panel: scripting, with FenrirUI's scripting extension.
if(FENRIR_BUILD_TESTS AND TARGET FenrirUIScripting)
    foreach(backend IN ITEMS null opengl)
        add_test(NAME RendererDemo-RazorPanel-${backend}
            COMMAND "${CMAKE_COMMAND}" "-DDEMO=$<TARGET_FILE:RendererDemo>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckRazorPanelLog.cmake")
        set_tests_properties(RendererDemo-RazorPanel-${backend} PROPERTIES
            ENVIRONMENT "FENRIR_DEMO_SMOKE=600;FENRIR_DEMO_KEYS=1;FENRIR_DEMO_BACKEND=${backend};FENRIR_DEMO_SCENE=Debug draw (C#)"
            SKIP_REGULAR_EXPRESSION "RendererDemo skipped"
            TIMEOUT 300
            LABELS demo
        )
    endforeach()
    set_property(TEST RendererDemo-RazorPanel-opengl APPEND PROPERTY LABELS gl)
endif()
