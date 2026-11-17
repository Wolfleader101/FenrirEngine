# cmake -DDEMO=<AnimationDemo executable> -DEXPECTED=<regex> -P CheckRazorPanelsLog.cmake, with the smoke-run variables
# in the environment (FENRIR_DEMO_SCENE one C# twin): runs the demo and fails unless it exits with 0 and logs a line
# matching EXPECTED, the twin's Razor panel mounted in its demo slot (scripts/PanelSetup.cs). Exit code 4 (the renderer
# could not start) prints "AnimationDemo skipped", which CTest reports as skipped.
execute_process(COMMAND "${DEMO}" RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
message("${log}")

if(result EQUAL 4)
    message("AnimationDemo skipped: the renderer could not start")
    return()
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "AnimationDemo exited with ${result}")
endif()

if(NOT log MATCHES "${EXPECTED}")
    message(FATAL_ERROR "AnimationDemo's log has no line matching \"${EXPECTED}\"")
endif()
