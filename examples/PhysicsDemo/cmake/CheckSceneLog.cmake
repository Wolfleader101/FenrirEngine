# cmake -DDEMO=<PhysicsDemo executable> -DSCENE=<scene name> -P CheckSceneLog.cmake, with FENRIR_DEMO_SCENE and
# FENRIR_DEMO_BACKEND in the environment: smoke-runs the scene alone for a second (60 frames) per key and a second more,
# so its keys are pressed about a second apart, as FENRIR_DEMO_KEYS presses them, and fails unless the run exits with 0
# (no error logged) and logs each of the legacy scene's lines below for its keys and its autopilot. Exit code 4 (the
# renderer could not start) prints "PhysicsDemo skipped", which CTest reports as skipped.

# Per scene: its keys (as in its DemoScene) and the lines, as regular expressions, its log must hold.
if(SCENE STREQUAL "Playground (C++)")
    set(keys 9)
    set(expected
        # F2, the app's key, round every category and back.
        "Physics debug lines: colliders"
        "Physics debug lines: joints"
        "Physics debug lines: all"
        "Physics debug lines: off"
    )
elseif(SCENE STREQUAL "Playground (C#)")
    set(keys 1)
    set(expected
        "Physics \\(C#\\): C# built a ground, 10 boxes, 5 spheres, a paddle and a trigger zone\\."
        "ContactBegin: .* met at [0-9.]+ m/s"
        "J: kicked 15 bodies\\."
    )
elseif(SCENE STREQUAL "Joints (C++)")
    set(keys 26)
    set(expected
        "Door trigger entered by Post \\(hinge\\)"
        "Chain links collide with their neighbours"
        "Door pushed open"
        "Door motor opening at 1 rad/s \\(the door was (asleep|awake)\\)"
        "Door motor driving it shut"
        "Door limits off: it swings all the way round"
        "Door hinge: force"
        "Chain joint 1 breaks over 15 N"
        "Chain joint 1 broke at [0-9.]+ N"
        "Repaired 1 joint"
        "Door teleported 0\\.7 m out of its frame"
        "Door and frame teleported a metre together"
        "Link 5 scaled up by half"
        "Link 6 has a lump on its side"
        "Link 6's lump moved to its other side"
        "Chain link 3 destroyed"
    )
elseif(SCENE STREQUAL "Joints (C#)")
    set(keys 13)
    set(expected
        "Joints \\(C#\\): C# built a bridge, a lift, a spring rope and a limb, with 8 joints\\."
        "Bridge middle \\(Hinge\\): force"
        "K: the bridge's middle joint is fixed: the middle planks are one beam\\."
        "K: the bridge's middle joint is a hinge again\\."
        "B: dropped weight 1 \\(10 kg\\) on the bridge\\."
        "JointBroken: Bridge middle broke at [0-9.]+ N"
        "R: repaired [0-9]+ joints?\\."
        "1: lift motor up at 1 m/s"
        "1: lift motor off"
        "2: limb raised"
        "2: limb back to its rest pose\\."
    )
elseif(SCENE STREQUAL "Character (C++)")
    set(keys 113)
    set(expected
        "Autopilot: walking the route"
        "Tick [0-9]+: character in air -> on ground"
        "Tick [0-9]+: character [a-z ]+ -> on steep ground"
        "Crouch"
        "Tick [0-9]+: at \\(.*\\), on ground on Ground"
        "Autopilot off"
        "Camera free"
        "Grow by 1\\.5"
        "T: teleported to the start"
        "1: stopOnSlope off"
        "4: character disabled: nothing sees it"
        "M: mesh collider, the character is inactive"
        "B: ball thrown"
        "H: box dropped on the head"
        "5: stairs and ground snapping off"
        "6: second character stopped"
    )
elseif(SCENE STREQUAL "Character (C#)")
    set(keys 62)
    set(expected
        "Character \\(C#\\): C# built a slide ramp, an ice patch, a crouch tunnel, a trigger pad, a lift and a test wall"
        "Autopilot: walking the route"
        "Tick [0-9]+: character in air -> on ground"
        "Crouch"
        "TriggerEnter: the character \\(.*\\) entered the Trigger pad"
        "Autopilot off"
        "Camera free"
        "1: StopOnSlope on"
        "T: teleported to the start"
        "F: dash"
        "K: half into the test wall at .*: [0-9]+ penetrations?"
    )
else()
    message(FATAL_ERROR "CheckSceneLog.cmake: no lines for the scene \"${SCENE}\"")
endif()

math(EXPR frames "60 * (${keys} + 1)")
set(ENV{FENRIR_DEMO_SMOKE} "${frames}")
execute_process(COMMAND "${DEMO}" RESULT_VARIABLE result OUTPUT_VARIABLE log ERROR_VARIABLE log)
message("${log}")

if(result EQUAL 4)
    message("PhysicsDemo skipped: the renderer could not start")
    return()
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "PhysicsDemo's ${SCENE} exited with ${result}")
endif()
foreach(line IN LISTS expected)
    if(NOT log MATCHES "${line}")
        message(FATAL_ERROR "PhysicsDemo's ${SCENE} logged no line matching \"${line}\"")
    endif()
endforeach()
