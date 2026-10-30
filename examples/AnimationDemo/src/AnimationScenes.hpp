#pragma once

#include "FenrirDemoScaffold/DemoScaffold.hpp"

// AnimationDemo's scenes, in the order of its scene list (README.md). Each animation ticket adds its scene here, in the
// spec's order: Blending, Bone attachments and Going limp (tickets 08-10) go before Crowd.
namespace AnimationDemo
{
    /**
     * @brief Playback: the mannequin's Animator under the keys (clip, speed, reverse, Loop or Once, pause, seek), its
     * clip events as world text, and a second mannequin playing a short fast clip with more events than the 64-event
     * budget, its AnimationEventsDropped counts on screen.
     */
    [[nodiscard]] FenrirDemo::DemoScene Playback();

    /**
     * @brief Skeleton: the walking mannequin and the critter with their bones through FenrirDebugDraw (lines to their
     * parents, axes, names as world text); keys toggle the meshes, the bones and the names.
     */
    [[nodiscard]] FenrirDemo::DemoScene SkeletonScene();

    /**
     * @brief Skin diagnostics: a working skinned mesh, then one labelled entity per skip case (no SkinnedMesh, an
     * Unavailable skeleton, a failed bind signature, a shader without SKINNED drawn magenta), each logged once as a
     * warning.
     */
    [[nodiscard]] FenrirDemo::DemoScene SkinDiagnostics();

    /**
     * @brief Crowd: a grid of Kenney's character, 64 at first, doubled and halved between 1 and 1024 by the keys, the
     * members taking the idle and run in turn from staggered starts (Seek), with pause and reset. The screen text
     * shows the instances, their bones, the frame time and the renderer's palette stats.
     */
    [[nodiscard]] FenrirDemo::DemoScene Crowd();
} // namespace AnimationDemo
