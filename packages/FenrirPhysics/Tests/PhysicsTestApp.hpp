#pragma once

// What the physics tests share: an App with no console output, and helpers that build bodies and run frames.

#include <cstddef>
#include <memory>
#include <string_view>
#include <thread>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Physics.hpp"

namespace PhysicsTest
{
    // A second ConsoleLogger in one process throws (its spdlog name is taken), so the tests log nowhere.
    class SilentLogger final : public Fenrir::ILogger
    {
      public:
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view) override
        {
        }
        void ErrorImpl(std::string_view) override
        {
        }
        void FatalImpl(std::string_view) override
        {
        }
    };

    inline constexpr double tick = 1.0 / 60.0;

    // Resting heights are within this of the exact contact height: the physics backend lets bodies sink a little into
    // what they stand on (about 0.02 for Jolt).
    inline constexpr float restSlop = 0.03f;

    inline const Fenrir::Math::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);

    /** An App with physics installed. The members end in reverse order: physics before the app. */
    struct PhysicsApp
    {
        PhysicsApp() : PhysicsApp(Fenrir::PhysicsConfig{})
        {
        }

        /** Physics installed with `config`, on an App with `workerThreads` worker threads. */
        explicit PhysicsApp(const Fenrir::PhysicsConfig& config,
                            std::size_t workerThreads = std::thread::hardware_concurrency())
            : app(std::make_unique<SilentLogger>(), workerThreads), physics(Fenrir::Physics::Install(app, config))
        {
        }

        Fenrir::App app;
        Fenrir::Physics physics;

        Fenrir::EntityList& Entities()
        {
            return app.GetActiveScene().GetEntityList();
        }

        /** Runs `seconds` of whole ticks, one tick per frame. */
        void Run(double seconds)
        {
            const int frames = static_cast<int>(seconds / tick + 0.5);
            for (int i = 0; i < frames; ++i)
            {
                app.RunFrame(tick);
            }
        }
    };

    inline Fenrir::Entity Place(Fenrir::EntityList& entities, const Fenrir::Math::Vec3& position,
                                const Fenrir::Math::Vec3& scale = Fenrir::Math::Vec3(1.0f))
    {
        Fenrir::Entity entity = entities.CreateEntity();
        entity.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, scale);
        return entity;
    }

    /** A static box whose top face is at y = 0, 20 wide and deep. */
    inline Fenrir::Entity AddGround(Fenrir::EntityList& entities)
    {
        Fenrir::Entity ground = Place(entities, Fenrir::Math::Vec3(0.0f, -0.5f, 0.0f));
        ground.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::BoxShape{Fenrir::Math::Vec3(10.0f, 0.5f, 10.0f)}});
        return ground;
    }

    /** A body with a collider of `shape` at `position`. */
    inline Fenrir::Entity AddBody(Fenrir::EntityList& entities, const Fenrir::Math::Vec3& position,
                                  Fenrir::CollisionShape shape = Fenrir::BoxShape{},
                                  Fenrir::MotionType motionType = Fenrir::MotionType::Dynamic)
    {
        Fenrir::Entity body = Place(entities, position);
        body.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.motionType = motionType});
        body.AddComponent<Fenrir::Collider>(Fenrir::Collider{shape});
        return body;
    }

    inline Fenrir::Math::Vec3 PositionOf(const Fenrir::Entity& entity)
    {
        return Fenrir::GetWorldTransform(entity).pos;
    }
} // namespace PhysicsTest
