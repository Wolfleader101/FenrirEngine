# cmake -DDEMO=<RendererDemo executable> -P CheckRazorPanelLog.cmake, with the smoke-run variables in the environment
# (FENRIR_DEMO_SCENE="Debug draw (C#)", FENRIR_DEMO_KEYS=1): runs the demo and fails unless it exits with 0 and logs the
# Razor panel's mount into the scene's demo slot (scripts/DebugDrawPanelSetup.cs) and 1 hiding the Wire group, the group
# the panel's Wire toggle shows and hides. Exit code 4 (the renderer could not start) prints "RendererDemo skipped", which CTest reports as skipped.
execute_process(COMMAND "${DEMO}" RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
message("${log}")

if(result EQUAL 4)
    message("RendererDemo skipped: the renderer could not start")
    return()
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "RendererDemo exited with ${result}")
endif()

set(expected
    # DebugDrawPanelSetup.cs: the Razor panel, mounted into Debug draw (C#)'s demo slot.
    "RendererDemo: the Razor panel is in Debug draw \\(C#\\)'s demo slot"
    # DebugDrawScene.cs: 1 hides the Wire group, which the panel's Wire toggle reads.
    "1: the Wire group is hidden"
)
foreach(line IN LISTS expected)
    if(NOT log MATCHES "${line}")
        message(FATAL_ERROR "RendererDemo's log has no line matching \"${line}\"")
    endif()
endforeach()
# The panel reads the groups once the scene has its DebugDraw (DebugDrawPanelSetup.cs mounts it an update late). The
# warning is a debug build's only, so a release build checks nothing here.
if(log MATCHES "the active scene has no DebugDraw")
    message(FATAL_ERROR "RendererDemo's Razor panel read the groups before the scene had its DebugDraw")
endif()
