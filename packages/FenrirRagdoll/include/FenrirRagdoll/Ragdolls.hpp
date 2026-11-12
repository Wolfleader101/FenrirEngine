#pragma once

#include <memory>

#include "FenrirRagdoll/Builder.hpp"
#include "FenrirRagdoll/Components.hpp"
#include "FenrirRagdoll/Description.hpp"

namespace Fenrir
{
    class App;

    namespace Detail
    {
        struct RagdollsState;
    } // namespace Detail

    /**
     * @brief The owner of FenrirRagdoll's glue (ADR-0053, ADR-0054): the systems that join each ragdoll's bodies to
     * its skeleton, on the active scene only. Move-only. Destroy it before the App; the systems then do nothing.
     *
     * **Each tick**, in `Tick` (after FenrirSkeleton's tick pass in `PreTick`, before physics steps in `PostTick`: a
     * phase boundary, so install order does not matter), for every `Ragdoll` on a `Ready`, tick-posed skeleton, its
     * bodies in the order they were spawned:
     *
     * 1. **Resolve:** each `RagdollBody`'s bone, by engine name. A ragdoll on a frame-posed or not-ready skeleton is
     *    inactive (`state.inactive`), writes nothing and warns once each time it becomes inactive; a body whose name
     *    matches no bone, whose entity has no `RigidBody`, or whose `ragdoll` is not a live `Ragdoll` entity of the
     *    scene is skipped, warned about once, with `state.bone = NoBone`.
     * 2. **Modes:** each body's effective mode (`Inherit` takes the ragdoll's). A following body's
     *    `RigidBody::motionType` is made kinematic.
     * 3. **Follow:** each following body's `Transform` is written so its world transform is
     *    `GetBoneWorldMatrix(skeleton, bone, PoseKind::Simulation) × offset`, so physics moves it there over the step,
     *    with no lag at any number of ticks per frame. Its `RigidBody::linearVelocity` and `angularVelocity` are
     *    written from this and its previous following pose over `Time::tickRate` (0 on its first following tick), so
     *    a body that becomes dynamic keeps moving as it did.
     * 4. **State:** `Ragdoll::state` and each `RagdollBody::state`.
     *
     * Simulating and powered bodies are not driven yet: their effective mode is reported and nothing of theirs is
     * written.
     */
    class Ragdolls
    {
      public:
        /** @brief Adds the glue's systems to `app`. */
        [[nodiscard]] static Ragdolls Install(App& app);

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
