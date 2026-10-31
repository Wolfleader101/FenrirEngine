cmake_minimum_required(VERSION 3.20)
project(FenrirExamples)

# The demo apps (GLOSSARY.md), on the demo scaffold. They draw, so they exist only with the renderer.
if(FENRIR_BUILD_EXAMPLES AND FENRIR_ENABLE_RENDERER)
    add_subdirectory(DemoScaffold)
    add_subdirectory(RendererDemo)
    # Skeletons, clips and skinning: FenrirSkeleton and FenrirAnimation come with FenrirAssets, as the renderer does.
    # With physics it installs FenrirPhysics too, for scene 5's tick-posed shield.
    add_subdirectory(AnimationDemo)
    # The lighting showcases; its large assets come with FENRIR_FETCH_DEMO_ASSETS, and without them those scenes say so.
    add_subdirectory(LightingDemo)
    # The scripting showcase is all scripting: without it there is nothing to show.
    if(FENRIR_ENABLE_SCRIPTING)
        add_subdirectory(ScriptingDemo)
    endif()
    # The physics scenes need physics; their C# twins come with scripting.
    if(FENRIR_ENABLE_PHYSICS)
        add_subdirectory(PhysicsDemo)
    endif()
endif()
