#pragma once

#include <cstdint>

#include "FenrirDemoScaffold/DemoScaffold.hpp"

namespace Fenrir
{
    class App;
} // namespace Fenrir

// AnimationDemo's scenes, in the order of its scene list (README.md). Each animation ticket adds its scene here, in the
// spec's order.
namespace AnimationDemo
{
    /**
     * @brief Playback: the mannequin's Animator under the keys (clip, speed, reverse, Loop or Once, pause, seek), its
     * clip events as world text, and a second mannequin playing a short fast clip with more events than the 64-event
     * budget, its AnimationEventsDropped counts on screen.
     */
    [[nodiscard]] FenrirDemo::DemoScene Playback();

    /**
     * @brief Skeleton: the Quaternius human, the mannequin, CesiumMan, the Fox and the Kenney FBX character, each
     * playing a clip with its bones through FenrirDebugDraw (lines to their parents, axes, names as world text); keys
     * toggle the meshes, the bones and the names.
     */
    [[nodiscard]] FenrirDemo::DemoScene SkeletonScene();

    /**
     * @brief Skin diagnostics: a working skinned mesh, then one labelled entity per skip case (no SkinnedMesh, an
     * Unavailable skeleton, a failed bind signature, a shader without SKINNED drawn magenta), each logged once as a
     * warning.
     */
    [[nodiscard]] FenrirDemo::DemoScene SkinDiagnostics();

    /**
     * @brief Blending: three Quaternius humans. The left crossfades through idle, walk, run and sprint by either curve,
     * and can be interrupted mid-fade; the middle walks with the call played on its upper body by an override layer
     * masked to the spine's subtree; the right idles with an additive lean measured from its start or from rest, its
     * weight ramping to 0 and back with its clock running and its events silent at 0.
     */
    [[nodiscard]] FenrirDemo::DemoScene Blending();

    /**
     * @brief Bone attachments: the Quaternius human cycling its clips with a rifle in its right hand (a skeleton entity
     * of its own), a scope attached to the rifle's rail (an attachment on an attachment) and a hat on its head, each
     * labelled with its AttachmentState; beside it one labelled attachment per failure case. Keys detach the rifle
     * keeping its world transform and the hat keeping its local one, and attach them again.
     *
     * To the left, a second human sprints with a kinematic shield on its forearm, moved by demo glue in Tick from the
     * simulation pose, pushing dynamic boxes off a table when the app is built with physics (ADR-0040). Both humans
     * start tick-posed, and a key switches them to frame timing and back, so the shield's lag comes and goes. Adds the
     * glue's Tick system to `app`.
     */
    [[nodiscard]] FenrirDemo::DemoScene BoneAttachments(Fenrir::App& app);

    /**
     * @brief Going limp: the Quaternius human walking while demo code, standing in for ragdoll glue, writes world
     * targets that let its left arm hang and sway from the shoulder through the pose override. The arm's override
     * weight steps to 1 to go limp and ramps back to 0 to blend into the walk, its targets cleared at 0.
     */
    [[nodiscard]] FenrirDemo::DemoScene GoingLimp();

    /**
     * @brief Crowd: a grid of Kenney's character, 64 at first, doubled and halved between 1 and 1024 by the keys, the
     * members taking the idle and run in turn from staggered starts (Seek), with pause and reset. The screen text
     * shows the instances, their bones, the frame time and the renderer's palette stats.
     */
    [[nodiscard]] FenrirDemo::DemoScene Crowd();

    /** @brief Playback (C#)'s number, which its C# systems know it by (scripts/Playback.cs). */
    inline constexpr std::uint32_t PlaybackCSharpScene = 1;

    /**
     * @brief Playback (C#), scene 1's twin, made and run by C# (scripts/Playback.cs): the Quaternius human's Animator
     * under the keys (crossfades between its clips, speed, reverse, Loop or Once, pause, seeks), its clip events read
     * through an EventReader and shown above it, and one AnimationLayer playing the call on its upper body through a
     * BoneMask, ramped in and out. The floor and sun are C++'s. Built only with scripting.
     */
    [[nodiscard]] FenrirDemo::DemoScene PlaybackCSharp();

    /** @brief Bone attachments (C#)'s number, which its C# systems know it by (scripts/BoneAttachments.cs). */
    inline constexpr std::uint32_t BoneAttachmentsCSharpScene = 2;

    /**
     * @brief Bone attachments (C#), scene 5's twin, made and run by C# (scripts/BoneAttachments.cs): a blade, a hat and
     * a tag naming no bone, attached to the walking human with BoneAttachments C# writes, each labelled with its
     * AttachmentState; keys detach them both ways, attach them again and move the blade along its bone. A C# Modify
     * stage system nods the head through a PoseEdit, and a C# Late stage system marks the hand where this frame's pose
     * puts it. The floor and sun are C++'s. Built only with scripting.
     */
    [[nodiscard]] FenrirDemo::DemoScene BoneAttachmentsCSharp();
} // namespace AnimationDemo
