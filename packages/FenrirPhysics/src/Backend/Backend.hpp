#pragma once

// PROTOTYPE (physics spike): the private backend seam (ADR-0010). A concrete class, no virtual calls, no backend type.
// Its only implementation is src/Backend/Jolt/, the only code that includes Jolt.

#include <cstdint>
#include <memory>

#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Components.hpp"

namespace Fenrir::Backend
{
    struct BodyId
    {
        std::uint32_t value = ~0u;
    };

    struct BodyDesc
    {
        MotionType motionType = MotionType::Static;
        CollisionShape shape;
        Math::Vec3 scale = Math::Vec3(1.0f);
        Math::Vec3 position = Math::Vec3(0.0f);
        Math::Quat rotation = Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
        Math::Vec3 linearVelocity = Math::Vec3(0.0f);
        Math::Vec3 angularVelocity = Math::Vec3(0.0f);
        float mass = 1.0f;
        std::uint64_t userData = 0;
    };

    struct BodyState
    {
        Math::Vec3 position;
        Math::Quat rotation;
        Math::Vec3 linearVelocity;
        Math::Vec3 angularVelocity;
    };

    struct WorldSettings
    {
        Math::Vec3 gravity;
        std::uint32_t maxBodies;
        std::uint32_t maxBodyPairs;
        std::uint32_t maxContacts;
    };

    // Process-wide setup, reference counted.
    void Acquire();
    void Release();

    class World
    {
      public:
        explicit World(const WorldSettings& settings);
        ~World();
        World(const World&) = delete;
        World& operator=(const World&) = delete;

        BodyId CreateBody(const BodyDesc& desc);
        void DestroyBody(BodyId id);

        void Teleport(BodyId id, const Math::Vec3& position, const Math::Quat& rotation);
        void MoveKinematic(BodyId id, const Math::Vec3& position, const Math::Quat& rotation, float dt);
        void SetVelocity(BodyId id, const Math::Vec3& linear, const Math::Vec3& angular);
        void SetShape(BodyId id, const CollisionShape& shape, const Math::Vec3& scale);
        [[nodiscard]] BodyState GetState(BodyId id) const;

        void SetGravity(const Math::Vec3& gravity);
        void Step(float dt, int collisionSteps);

      private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace Fenrir::Backend
