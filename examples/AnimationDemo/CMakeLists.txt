# AnimationDemo (examples/AnimationDemo/README.md): the animation spec's demo app, one scene per feature. It installs
# FenrirSkeleton and FenrirAnimation itself; the renderer reaches skeletons through the skinning source (ADR-0041).
fenrir_add_demo_app(AnimationDemo
    SOURCES
        src/main.cpp
        src/AnimationScenes.hpp
        src/SkinnedModels.hpp
        src/SkinnedModels.cpp
        src/Playback.cpp
        src/SkeletonScene.cpp
        src/SkinDiagnostics.cpp
        src/Blending.cpp
        src/BoneAttachments.cpp
        src/GoingLimp.cpp
        src/Crowd.cpp
        src/CSharpScenes.cpp
    ASSETS assets
    SCRIPTS scripts
)
target_link_libraries(AnimationDemo PRIVATE FenrirSkeleton FenrirAnimation FenrirAssets FenrirDebugDraw)
# Skeleton's and animation's scripting extensions, which scenes 8 and 9 (C#) use, and their contracts beside the
# executable.
if(FENRIR_ENABLE_SCRIPTING)
    target_link_libraries(AnimationDemo PRIVATE FenrirSkeletonScripting FenrirAnimationScripting)
    fenrir_deploy_scripting(AnimationDemo EXTENSIONS Skeleton Animation)
    # The C# twins' Razor panels, checked: a smoke run of each twin alone must log its panel mounted in its demo slot
    # (scripts/PanelSetup.cs). The test names have no "(C#)": a CTest name with parentheses is awkward to -R.
    if(FENRIR_BUILD_TESTS)
        foreach(twin IN ITEMS "Playback" "Bone attachments")
            string(REPLACE " " "" name "${twin}")
            add_test(NAME AnimationDemo-${name}Razor-null
                COMMAND "${CMAKE_COMMAND}" "-DDEMO=$<TARGET_FILE:AnimationDemo>"
                    "-DEXPECTED=AnimationDemo: the Razor panel is in ${twin} .C#.'s demo slot"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckRazorPanelsLog.cmake")
            set_tests_properties(AnimationDemo-${name}Razor-null PROPERTIES
                ENVIRONMENT "FENRIR_DEMO_SMOKE=120;FENRIR_DEMO_BACKEND=null;FENRIR_DEMO_SCENE=${twin} (C#)"
                SKIP_REGULAR_EXPRESSION "AnimationDemo skipped"
                TIMEOUT 300
                LABELS demo
            )
        endforeach()
    endif()
endif()
# Scene 5's kinematic shield pushes dynamic boxes with physics; without it the shield still follows its bone.
if(FENRIR_ENABLE_PHYSICS)
    target_link_libraries(AnimationDemo PRIVATE FenrirPhysics)
    target_compile_definitions(AnimationDemo PRIVATE ANIMATION_DEMO_PHYSICS)
endif()
# Scene 3's user shader without the SKINNED key, embedded in the app.
fenrir_add_shaders(AnimationDemo PROGRAM AnimationDemoFlat VERTEX shaders/flat.vert FRAGMENT shaders/flat.frag EMBED)
