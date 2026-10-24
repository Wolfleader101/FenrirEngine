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
elseif(SCENE STREQUAL "Vehicles (C++)")
    set(keys 332)
    set(expected
        "Physics debug lines: vehicles"
        "P: autopilot: driving the track"
        "Autopilot: waypoint [0-9]+ reached"
        "Autopilot: parked with the handbrake for 5 s"
        "L \\(tick [0-9]+\\): car .* m/s, .* RPM, gear"
        "  trailer .* m/s, [0-9] wheels on ground, brake"
        "  wheel 3: on ground"
        "P: autopilot off"
        "C: camera free: it stays where it is \\(F3 flies the free-fly camera\\), the arrows drive"
        "C: the camera chases the car"
        "1: soft springs, 1\\.15 Hz"
        "1: stock springs, 1\\.5 Hz"
        "2: big wheels, 0\\.45 m"
        "2: stock wheels, 0\\.3 m"
        "4: the car is off: a plain rolling box"
        "4: the car is on again, built fresh"
        "5: the car is not interpolated"
        "5: the car is interpolated, its wheels with it"
        "7: tilt limited to 60 degrees"
        "7: tilt limit off"
        "9: the chassis is kinematic: the car is inactive"
        "9: the chassis is dynamic: the car starts again at idle in neutral"
        # Physics's one warning for 9, and its one for 8.
        "The Vehicle on entity [0-9]+ is inactive: its RigidBody is kinematic"
        "The VehicleDifferential on entity [0-9]+ names entity [0-9]+, whose wheel has died"
        "T: the car and its trailer are back at the start"
        "8: the rear-left wheel is gone"
        "8: the rear-left wheel is back, driven again"
        "M: Manual gearbox: E/Q shift, Shift holds the clutch"
        "E: gear [0-9]"
        "Q: gear [0-9]"
        "M: Automatic gearbox"
        "K: the car is kicked over"
        "R: the car is righted, 1 m up, its trailer behind it"
        "F: the driver gets out beside the door"
        "F: the driver gets in and drives the car"
    )
elseif(SCENE STREQUAL "Motorcycle (C++)")
    set(keys 53)
    set(expected
        "Physics debug lines: vehicles"
        "P: autopilot: riding the loop"
        "Autopilot: point [0-9]+ reached"
        "L: motorcycle .* m/s, .* RPM, gear .*, lean"
        "  front wheel: on ground"
        "3: max lean 45 -> 20 degrees, a rebuild"
        "3: max lean 20 -> 45 degrees, a rebuild"
        "P: autopilot off"
        "6: the lean controller is off: the motorcycle falls over"
        "6: the lean controller is on"
        "T: the motorcycle stands at the start"
        "C: camera free: it stays where it is \\(F3 flies the free-fly camera\\), the arrows ride"
        "C: the camera chases the motorcycle"
        "5: the motorcycle is not interpolated"
        "5: the motorcycle is interpolated, its lean and wheels with it"
    )
elseif(SCENE STREQUAL "Tank (C++)")
    set(keys 51)
    set(expected
        "Physics debug lines: vehicles"
        "P: autopilot: driving the loop"
        "Autopilot: point [0-9]+ reached"
        "L: tank .* m/s, .* RPM, gear .*, tracks"
        "  left driven wheel: on ground"
        "3: the tracks' grip is 1, a live edit: they slip, and it crawls"
        "3: the tracks' grip is 4, a live edit"
        "P: autopilot off"
        "T: the tank is back at the start"
        "C: camera free: it stays where it is \\(F3 flies the free-fly camera\\), the arrows drive"
        "C: the camera chases the tank"
        "5: the tank is not interpolated"
        "5: the tank is interpolated, its wheels with it"
    )
elseif(SCENE STREQUAL "Vehicles (C#)")
    set(keys 63)
    set(expected
        "Vehicles \\(C#\\): C# built a track of bumps, a ramp and a grass patch, a car .* and its trailer .* on a ball joint\\."
        "P: autopilot: driving the track"
        "Autopilot: waypoint [0-9]+ reached"
        "L \\(tick [0-9]+\\): car .* m/s, .* RPM, gear"
        "  wheel 3: on ground"
        "P: autopilot off: the car brakes to a stop until you drive"
        "C: camera free: it stays where it is \\(F3 flies the free-fly camera\\), the arrows drive"
        "C: the camera chases the car"
        "V: the vehicles' debug lines are on"
        "V: debug lines off"
        "U: the trailer is a wheeled car now, and pushes"
        "U: the trailer is a trailer again"
        "M: Manual gearbox: E/Q shift, Shift holds the clutch"
        "E: gear [0-9]"
        "Q: gear [0-9]"
        "M: Automatic gearbox"
        "K: the car is kicked over"
        "R: the car is upright again at"
        "T: the car and its trailer are back at the start"
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
