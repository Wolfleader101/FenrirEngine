# ScriptingDemo (examples/ScriptingDemo/README.md): the scripting showcase, C++ and C# sharing one scene. It exists only
# with scripting built (examples/CMakeLists.txt).
fenrir_add_demo_app(ScriptingDemo
    SOURCES
        src/main.cpp
        src/Showcase.hpp
        src/Showcase.cpp
    SCRIPTS scripts
)
# Bob and Burst, shared with C#, are the app's own scripting extension; the hierarchy's stand-in draws world text.
target_link_libraries(ScriptingDemo PRIVATE FenrirScripting FenrirAssets FenrirDebugDraw)

# The showcase's log, checked: a smoke run long enough for the sparks to burn out, with R and Space pressed, must log the
# C++ -> C# key event, the C# -> C++ Burst and the C#-only Expired (scripts/Controls.cs, scripts/Sparks.cs).
if(FENRIR_BUILD_TESTS)
    foreach(backend IN ITEMS null opengl)
        add_test(NAME ScriptingDemo-Showcase-${backend}
            COMMAND "${CMAKE_COMMAND}" "-DDEMO=$<TARGET_FILE:ScriptingDemo>"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckShowcaseLog.cmake")
        set_tests_properties(ScriptingDemo-Showcase-${backend} PROPERTIES
            ENVIRONMENT "FENRIR_DEMO_SMOKE=900;FENRIR_DEMO_KEYS=1;FENRIR_DEMO_BACKEND=${backend};FENRIR_DEMO_SCENE=Showcase"
            SKIP_REGULAR_EXPRESSION "ScriptingDemo skipped"
            TIMEOUT 300
            LABELS demo
        )
    endforeach()
    set_property(TEST ScriptingDemo-Showcase-opengl APPEND PROPERTY LABELS gl)
endif()
