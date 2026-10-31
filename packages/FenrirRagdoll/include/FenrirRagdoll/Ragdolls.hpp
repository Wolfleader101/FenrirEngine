#pragma once

#include <memory>

#include "FenrirECS/Entity.hpp"
#include "FenrirRagdoll/Ragdoll.hpp"
#include "FenrirRagdoll/RagdollDescription.hpp"

namespace Fenrir
{
    class App;
    class Scene;

    namespace Detail
    {
        struct RagdollsState;
    } // namespace Detail

    /**
     * @brief The owner of the ragdoll glue (ADR-0053, ADR-0054): the systems that apply each ragdoll's mode. Move-only.
     * Install after FenrirSkeleton and FenrirPhysics; destroy it before them.
     *
     * In `Tick` (after FenrirSkeleton's PreTick pass, before physics's PostTick), for every Ragdoll on a ready
     * tick-posed skeleton: each body's effective mode becomes its RigidBody's motion type; following bodies get their
     * Transform from `GetBoneWorldMatrix(bone, Simulation) × offset`, and their RigidBody velocities from the move
     * (so the switch to dynamic keeps it); each joint's motor is Position while its child body is powered, with the
     * child bone's base-pose rotation relative to the parent as its target and torque and springs from `strength`,
     * else Off. In `Update` (after physics's AfterTicks), while any body simulates: the skeleton entity follows the
     * root body in position and yaw (`followRoot`), and each simulating body's bone gets a world override target
     * `body × offset⁻¹`; a following body's entry is cleared once its weight is 0.
     */
    class Ragdolls
    {
      public:
        [[nodiscard]] static Ragdolls Install(App& app);

        Ragdolls(Ragdolls&& other) noexcept;
        Ragdolls& operator=(Ragdolls&& other) noexcept;
        Ragdolls(const Ragdolls&) = delete;
        Ragdolls& operator=(const Ragdolls&) = delete;
        ~Ragdolls();

      private:
        explicit Ragdolls(std::shared_ptr<Detail::RagdollsState> state);

        std::shared_ptr<Detail::RagdollsState> m_state;
    };
} // namespace Fenrir
