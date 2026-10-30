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
    ASSETS assets
)
target_link_libraries(AnimationDemo PRIVATE FenrirSkeleton FenrirAnimation FenrirAssets FenrirDebugDraw)
# Scene 3's user shader without the SKINNED key, embedded in the app.
fenrir_add_shaders(AnimationDemo PROGRAM AnimationDemoFlat VERTEX shaders/flat.vert FRAGMENT shaders/flat.frag EMBED)
