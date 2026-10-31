# cmake -DDEMO=<ScriptingDemo executable> -P CheckShowcaseLog.cmake, with the smoke-run variables in the environment:
# runs the demo and fails unless it exits with 0 and logs each line of the legacy scripting demo below. Exit code 4 (the
# renderer could not start) prints "ScriptingDemo skipped", which CTest reports as skipped.
execute_process(COMMAND "${DEMO}" RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
message("${log}")

if(result EQUAL 4)
    message("ScriptingDemo skipped: the renderer could not start")
    return()
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "ScriptingDemo exited with ${result}")
endif()

set(expected
    # Spin.cs: the C#-defined component, given to both boxes.
    "Spinning 2 entities from C#\\. Press R to reverse them, Space for sparks\\."
    # Bobbing.cs and Showcase.cpp: the component C++ declares, which C# writes and C++ reads back.
    "C\\+\\+ read Bob\\.Phase = [0-9.]+, which C# wrote into the same component\\."
    # Controls.cs: FenrirInput's C++ KeyEvent, read from C#.
    "R: reversed the spin\\."
    # Controls.cs, Sparks.cs and Showcase.cpp: Burst, sent from C# and read by C# and C++.
    "C\\+\\+ saw a burst of 5 sparks sent from C#\\."
    # Sparks.cs: Expired, the event only C# defines.
    "Spark [0-9]+ burnt out\\."
)
foreach(line IN LISTS expected)
    if(NOT log MATCHES "${line}")
        message(FATAL_ERROR "ScriptingDemo's log has no line matching \"${line}\"")
    endif()
endforeach()
