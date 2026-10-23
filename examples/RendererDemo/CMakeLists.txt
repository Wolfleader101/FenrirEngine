# RendererDemo (examples/RendererDemo/README.md): the renderer's demo app, one scene per renderer feature.
fenrir_add_demo_app(RendererDemo
    SOURCES
        src/main.cpp
        src/RendererScenes.hpp
        src/MeshesAndMaterials.cpp
        src/CamerasAndTargets.cpp
)
# The monitor screen's shader in "Cameras and render targets", a user shader embedded in the app.
fenrir_add_shaders(RendererDemo PROGRAM RendererDemoMonitor VERTEX shaders/monitor.vert FRAGMENT shaders/monitor.frag
                   EMBED)
