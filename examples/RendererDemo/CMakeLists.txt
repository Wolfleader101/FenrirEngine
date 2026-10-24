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
    SCRIPTS scripts
)
# "Debug draw" draws into its scene's DebugDraw.
target_link_libraries(RendererDemo PRIVATE FenrirDebugDraw)
# The monitor screen's shader in "Cameras and render targets", a user shader embedded in the app.
fenrir_add_shaders(RendererDemo PROGRAM RendererDemoMonitor VERTEX shaders/monitor.vert FRAGMENT shaders/monitor.frag
                   EMBED)
# The stripes on a cube in "Meshes and materials": a user's own material shader (ADR-0024), embedded in the app.
fenrir_add_shaders(RendererDemo PROGRAM RendererDemoStripes VERTEX shaders/stripes.vert FRAGMENT shaders/stripes.frag
                   EMBED)
