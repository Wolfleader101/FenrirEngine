#pragma once

#include <memory>

#include "FenrirECS/Entity.hpp"
#include "FenrirRagdoll/Builder.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "FenrirRagdoll/Description.hpp"

namespace Fenrir
{
    class App;
    class Scene;
    // Only named here: FenrirRagdollScripting defines Ragdolls::ScriptingExtension().
    struct ScriptingExtension;

    namespace Detail
    {
        struct RagdollsState;
    } // namespace Detail

    /**
     * @brief The owner of FenrirRagdoll's glue (ADR-0053, ADR-0054): the systems that join each ragdoll's bodies to
     * its skeleton, on the active scene only (an inactive scene's ragdolls keep their state). Move-only. Destroy it
     * before the App; the systems then do nothing.
     *
     * **Each tick**, in `Tick` (after FenrirSkeleton's tick pass in `PreTick`, before physics steps in `PostTick`: a
     * phase boundary, so install order does not matter), for every `Ragdoll` on a `Ready`, tick-posed skeleton, its
     * bodies in the order they were spawned:
     *
     * 1. **Resolve:** each `RagdollBody`'s bone, by engine name. A ragdoll on a frame-posed or not-ready skeleton is
     *    inactive (`state.inactive`), writes nothing and warns once each time it becomes inactive; a body whose name
     *    matches no bone, whose entity has no `RigidBody`, or whose `ragdoll` is not a live `Ragdoll` entity of the
     *    scene is skipped, warned about once each time it becomes so, with `state.bone = NoBone`. Each resumes by
     *    itself once the cause is gone. Names are resolved every tick, so a skeleton asset rebuilt with its bones
     *    reordered is followed at once. Removing `Ragdoll` (or destroying the skeleton entity) leaves its bodies so:
     *    skipped, each warned about once, where they are, and turns each one's joint to its parent body off once
     *    (its motor's mode `Off`, its other fields left), after which the joints are game code's. A powered body made
     *    so stops being driven; its bones keep their override entries, which `ClearOverrides` still clears.
     * 2. **Modes:** each body's effective mode (`Inherit` takes the ragdoll's), turned into its
     *    `RigidBody::motionType`: kinematic while following, dynamic otherwise. Switching moves no body: one made
     *    dynamic keeps the velocity its `RigidBody` holds, the last following move's, so a running character tumbles
     *    forward; one made kinematic is taken back onto its bone by the follow below over the next step. Each joint
     *    between two of the ragdoll's bodies (`bodyA` the parent, `bodyB` the child: as the builder joins them) gets
     *    its motor's mode: `Position` while its child body is powered, `Off` otherwise (a powered body skipped
     *    included), its other fields left. Joints game code made to anything else are left alone.
     * 3. **Follow:** each following body's `Transform` is written so its world transform is
     *    `GetBoneWorldMatrix(skeleton, bone, PoseKind::Simulation) × offset`, so physics moves it there over the step,
     *    with no lag at any number of ticks per frame. Its `RigidBody::linearVelocity` and `angularVelocity` are
     *    written from this and its previous following pose over `Time::tickRate` (0 on its first following tick), so
     *    a body that becomes dynamic keeps moving as it did.
     * 4. **Motor targets:** each powered child's joint is aimed at the child bone's base-pose rotation relative to
     *    its parent bone (`GetBaseLocal` with `PoseKind::Simulation`, or both bones' `GetBaseModel` where unmapped
     *    bones lie between them), through both bodies' offsets and the joint's frames: a swing-twist joint's
     *    `SwingTwistMotor::targetRotation`, a hinge's `JointMotor::targetPosition` (the turn about its axis). No target
     *    velocity: it is written 0. The strength gives the drive: `maxTorque` (a hinge's `maxForce`) is `strength ×
     *    strengthScale × 30 N m` per kilogram of the child body, and the spring `lerp(1 Hz, 10 Hz, strength)`,
     *    damping 1, with `strength` taken within [0, 1]. The root body has no joint to a parent, so it is undriven: a
     *    powered ragdoll holds its own shape but not its balance, and one standing up falls over. Joints are written
     *    one by one, so their order does not matter.
     * 5. **State:** `Ragdoll::state` and each `RagdollBody::state`; `state.anyAwake` is false once every body sleeps.
     *
     * **Each frame**, in `Update` (after physics interpolates the bodies in `AfterTicks`, before the pose pipeline in
     * `PostUpdate`), for every active ragdoll, from the modes the last tick applied:
     *
     * 1. **Follow root:** while the root body is not following and `followRoot` is true, the skeleton entity is moved
     *    so the hips bone's base pose (`GetBaseModel`, `PoseKind::Simulation`) under it lands on the root body's bone
     *    horizontally and in yaw (the twist about +Y of the hips' turn from their base pose, so a body tipped over
     *    still faces the way it faced); it keeps its height and scale and stays upright.
     * 2. **Override targets:** each simulating or powered body's bone is set where the body (interpolated) puts it,
     *    body × `offset`⁻¹, as `SetOverride(skeleton, bone, Target { World, trs, Translation | Rotation, weight 1 })`,
     *    each frame, asleep or awake. The glue never writes an override weight: gameplay sets them to 1 when it drops
     *    the ragdoll (on the subtree, for a partial ragdoll) and ramps them to 0 as it gets up (ADR-0038). A body
     *    destroyed while simulating stops being written, and its bone keeps its last entry.
     * 3. **Clearing:** while every body follows, the entries hold the last simulated pose, until every override
     *    weight of the bones written reads 0; then the glue clears them. `ClearOverrides` clears them at once. A body
     *    of a partial ragdoll that follows again while others still simulate keeps its held entry until they all
     *    follow: set its subtree's weight to 0 to let the animation through. A skeleton whose `Ragdoll` is removed
     *    keeps its entries; `ClearOverrides` (or `Skeletons::ClearOverrides`, every entry) clears them.
     * 4. **Debug lines:** with `PhysicsDebugCategories::Ragdolls` on in the scene's physics world, each resolved body's
     *    bone frame and line to the body, and each driven joint's motor target, drawn after the steps above
     *    (FenrirRagdoll/DebugLines.hpp).
     *
     * A paused scene's ragdolls (the scene inactive) change nothing. Destroying the scene, its physics world (the
     * Physics owner) or this owner under a live ragdoll is clean; without a world the glue still writes the
     * components, and `anyAwake` reads false.
     */
    class Ragdolls
    {
      public:
        /** @brief Adds the glue's systems to `app`. */
        [[nodiscard]] static Ragdolls Install(App& app);

        /**
         * @brief Clears the pose override entries of `skeleton`'s ragdoll at once: those on its bodies' bones and on
         * the bones of bodies destroyed since the glue wrote them. Game code's entries on other bones, and every
         * override weight, stay. It works after the `Ragdoll` is removed, while its bodies still name `skeleton` or the
         * glue's record of the bones it wrote is there. False, clearing nothing, when `skeleton` is not a live entity
         * of `scene`, or has no `Ragdoll`, no body naming it and no record. While a body simulates the glue writes its
         * entry again next frame.
         */
        static bool ClearOverrides(Scene& scene, Entity skeleton);

        /**
         * @brief FenrirRagdoll's scripting extension (the ragdolls spec, ticket 56): the Fenrir.Ragdoll contract's
         * mirrors and its natives `Ragdoll.DescribeHumanoid`, `Build`, `Destroy` and `ClearOverrides`. Only named here:
         * FenrirRagdollScripting defines it, built with FENRIR_ENABLE_PHYSICS, FENRIR_ENABLE_ASSETS and
         * FENRIR_ENABLE_SCRIPTING. Give Scripting the skeleton extension too, whose Fenrir.Skeleton contract (the bone
         * names) this one builds against.
         */
        [[nodiscard]] static Fenrir::ScriptingExtension ScriptingExtension();

        Ragdolls(Ragdolls&& other) noexcept;
        Ragdolls& operator=(Ragdolls&& other) noexcept;
        Ragdolls(const Ragdolls&) = delete;
        Ragdolls& operator=(const Ragdolls&) = delete;
        /** @brief Leaves the systems doing nothing. */
        ~Ragdolls();

      private:
        explicit Ragdolls(std::shared_ptr<Detail::RagdollsState> state) noexcept;

        // Shared with the installed systems, which hold it weakly: once the owner is gone they do nothing.
        std::shared_ptr<Detail::RagdollsState> m_state;
    };
} // namespace Fenrir
