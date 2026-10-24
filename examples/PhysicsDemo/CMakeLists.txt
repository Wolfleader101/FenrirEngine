# PhysicsDemo (examples/PhysicsDemo/README.md): the legacy DemoApp's physics scenes on the demo scaffold, each C++ scene
# followed by its C# twin when scripting is built. It exists only with physics (examples/CMakeLists.txt).
fenrir_add_demo_app(PhysicsDemo
    SOURCES
        src/main.cpp
        src/PhysicsScenes.hpp
        src/SceneLooks.hpp
        src/SceneLooks.cpp
        src/Playground.cpp
        src/Joints.cpp
        src/Character.cpp
    SCRIPTS scripts
)
target_link_libraries(PhysicsDemo PRIVATE FenrirPhysics FenrirAssets FenrirDebugDraw)
# Physics's scripting extension, which the C# scenes use, and its contract beside the executable.
if(FENRIR_ENABLE_SCRIPTING)
    target_link_libraries(PhysicsDemo PRIVATE FenrirPhysicsScripting)
    fenrir_deploy_physics_scripting(PhysicsDemo)
endif()

# Each scene's log, checked: a smoke run of the scene alone, long enough for its keys to be pressed about a second apart
# (as FENRIR_DEMO_KEYS presses them), must log the legacy scene's lines for its keys and autopilot
# (cmake/CheckSceneLog.cmake).
if(FENRIR_BUILD_TESTS)
    set(physics_demo_scenes "Playground (C++)" "Joints (C++)" "Character (C++)")
    if(FENRIR_ENABLE_SCRIPTING)
        list(APPEND physics_demo_scenes "Playground (C#)" "Joints (C#)" "Character (C#)")
    endif()
    foreach(scene IN LISTS physics_demo_scenes)
        # "Joints (C++)" is PhysicsDemo-JointsCpp-<backend>, "Joints (C#)" PhysicsDemo-JointsCSharp-<backend>.
        string(REPLACE "(C++)" "Cpp" test_scene "${scene}")
        string(REPLACE "(C#)" "CSharp" test_scene "${test_scene}")
        string(REGEX REPLACE "[^A-Za-z]+" "" test_scene "${test_scene}")
        foreach(backend IN ITEMS null opengl)
            set(test PhysicsDemo-${test_scene}-${backend})
            add_test(NAME ${test}
                COMMAND "${CMAKE_COMMAND}" "-DDEMO=$<TARGET_FILE:PhysicsDemo>" "-DSCENE=${scene}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckSceneLog.cmake")
            set_tests_properties(${test} PROPERTIES
                ENVIRONMENT "FENRIR_DEMO_BACKEND=${backend};FENRIR_DEMO_SCENE=${scene};FENRIR_DEMO_KEYS="
                SKIP_REGULAR_EXPRESSION "PhysicsDemo skipped"
                TIMEOUT 600
                LABELS demo
            )
        endforeach()
        set_property(TEST PhysicsDemo-${test_scene}-opengl APPEND PROPERTY LABELS gl)
    endforeach()
endif()
