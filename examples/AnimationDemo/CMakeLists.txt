# AnimationDemo (examples/AnimationDemo/README.md): the animation spec's demo app, one scene per feature. It installs
# FenrirSkeleton and FenrirAnimation itself; the renderer reaches skeletons through the skinning source (ADR-0041).
fenrir_add_demo_app(AnimationDemo
    SOURCES
        src/main.cpp
        src/AnimationScenes.hpp
        src/Rigs.hpp
        src/Rigs.cpp
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
endif()
# Scene 5's kinematic shield pushes dynamic boxes with physics; without it the shield still follows its bone.
if(FENRIR_ENABLE_PHYSICS)
    target_link_libraries(AnimationDemo PRIVATE FenrirPhysics)
    target_compile_definitions(AnimationDemo PRIVATE ANIMATION_DEMO_PHYSICS)
endif()
# Scene 3's user shader without the SKINNED key, embedded in the app.
fenrir_add_shaders(AnimationDemo PROGRAM AnimationDemoFlat VERTEX shaders/flat.vert FRAGMENT shaders/flat.frag EMBED)
