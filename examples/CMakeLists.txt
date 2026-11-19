cmake_minimum_required(VERSION 3.20)
project(FenrirExamples)

# The demo apps (GLOSSARY.md), on the demo scaffold. They draw, so they exist only with the renderer, and the scaffold's
# demo UI kit is FenrirUI's, so only with the UI.
if(FENRIR_BUILD_EXAMPLES AND FENRIR_ENABLE_RENDERER AND NOT FENRIR_ENABLE_UI)
    message(STATUS "FENRIR_ENABLE_UI is off: the demo apps, whose demo UI kit is FenrirUI's, are not built.")
endif()
if(FENRIR_BUILD_EXAMPLES AND FENRIR_ENABLE_RENDERER AND FENRIR_ENABLE_UI)
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
    # The UI showcase.
    add_subdirectory(UIDemo)
    # The physics scenes need physics; their C# twins come with scripting.
    if(FENRIR_ENABLE_PHYSICS)
        add_subdirectory(PhysicsDemo)
        # PROTOTYPE, throwaway: Flightless Frenzy's movement grey box, on the prototype branch only.
        add_subdirectory(KikiMovePrototype)
    endif()
endif()
