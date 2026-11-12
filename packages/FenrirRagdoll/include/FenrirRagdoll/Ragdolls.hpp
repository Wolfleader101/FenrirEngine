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
     *    scene is skipped, warned about once, with `state.bone = NoBone`. Removing `Ragdoll` (or destroying the
     *    skeleton entity) leaves its bodies so: skipped, each warned about once, where they are.
     * 2. **Modes:** each body's effective mode (`Inherit` takes the ragdoll's), turned into its
     *    `RigidBody::motionType`: kinematic while following, dynamic otherwise. Switching moves no body: one made
     *    dynamic keeps the velocity its `RigidBody` holds, the last following move's, so a running character tumbles
     *    forward; one made kinematic is taken back onto its bone by the follow below over the next step.
     * 3. **Follow:** each following body's `Transform` is written so its world transform is
     *    `GetBoneWorldMatrix(skeleton, bone, PoseKind::Simulation) × offset`, so physics moves it there over the step,
     *    with no lag at any number of ticks per frame. Its `RigidBody::linearVelocity` and `angularVelocity` are
     *    written from this and its previous following pose over `Time::tickRate` (0 on its first following tick), so
     *    a body that becomes dynamic keeps moving as it did.
     * 4. **State:** `Ragdoll::state` and each `RagdollBody::state`; `state.anyAwake` is false once every body sleeps.
     *
     * **Each frame**, in `Update` (after physics interpolates the bodies in `AfterTicks`, before the pose pipeline in
     * `PostUpdate`), for every active ragdoll, from the modes the last tick applied:
     *
     * 1. **Follow root:** while the root body is not following and `followRoot` is true, the skeleton entity is moved
     *    so the hips bone's base pose (`GetBaseModel`, `PoseKind::Simulation`) under it lands on the root body's bone
     *    horizontally and in yaw; it keeps its height and scale and stays upright.
     * 2. **Override targets:** each simulating or powered body's bone is set where the body (interpolated) puts it,
     *    body × `offset`⁻¹, as `SetOverride(skeleton, bone, Target { World, trs, Translation | Rotation, weight 1 })`,
     *    each frame, asleep or awake. The glue never writes an override weight: gameplay sets them to 1 when it drops
     *    the ragdoll (on the subtree, for a partial ragdoll) and ramps them to 0 as it gets up (ADR-0038). A body
     *    destroyed while simulating stops being written, and its bone keeps its last entry.
     * 3. **Clearing:** while every body follows, the entries hold the last simulated pose, until every override
     *    weight of the bones written reads 0; then the glue clears them. `ClearOverrides` clears them at once.
     */
    class Ragdolls
    {
      public:
        /** @brief Adds the glue's systems to `app`. */
        [[nodiscard]] static Ragdolls Install(App& app);

        /**
         * @brief Clears the pose override entries of `skeleton`'s ragdoll at once: those on its bodies' bones and on
         * the bones of bodies destroyed since the glue wrote them. Game code's entries on other bones, and every
         * override weight, stay. False, clearing nothing, when `skeleton` has no `Ragdoll`. While a body simulates the
         * glue writes its entry again next frame.
         */
        static bool ClearOverrides(Scene& scene, Entity skeleton);

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
