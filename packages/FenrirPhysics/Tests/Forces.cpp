// Forces, impulses and gravity through the frame seam: game code pushes bodies by entity and sets each world's gravity,
// and sees the result in the bodies' components after the next tick.

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // A dynamic box of `mass` kilograms at `position`.
    Entity AddMassiveBody(EntityList& entities, const Math::Vec3& position, float mass)
    {
        Entity body = AddBody(entities, position);
        body.GetComponent<RigidBody>().mass = mass;
        return body;
    }
} // namespace

TEST_CASE("An impulse changes a body's velocity by impulse over mass", "[physics][forces]")
{
    PhysicsApp test;
    Entity box = AddMassiveBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f), 2.0f);
    test.Run(tick);
    PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(world != nullptr);

    world->AddImpulse(box, Math::Vec3(4.0f, 0.0f, 0.0f));
    test.Run(tick);

    const float speed = box.GetComponent<RigidBody>().linearVelocity.x;
    INFO("an impulse of 4 N s on 2 kg adds 2 m/s (less a tick of damping), it moves at " << speed);
    REQUIRE(speed == Approx(2.0f).margin(0.01));
}

TEST_CASE("A force added each tick accelerates a body by force over mass, and stops when no longer added",
          "[physics][forces]")
{
    PhysicsApp test;
    Entity box = AddMassiveBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f), 4.0f);
    test.Run(tick);

    // Game code pushes the box for half a second.
    int ticks = 0;
    test.app.AddSequentialSystems(
        SchedulePriority::Tick, {[&](App& app) {
            if (ticks++ < 30)
            {
                PhysicsWorld::Of(app.GetActiveScene())->AddForce(box, Math::Vec3(0.0f, 0.0f, 8.0f));
            }
        }});
    test.Run(0.5);

    const float pushed = box.GetComponent<RigidBody>().linearVelocity.z;
    INFO("8 N on 4 kg for half a second adds 1 m/s (less damping), it moves at " << pushed);
    REQUIRE(pushed == Approx(1.0f).margin(0.02));

    test.Run(0.5);
    const float coasting = box.GetComponent<RigidBody>().linearVelocity.z;
    INFO("a force lasts only the step after it is added: with no more pushes the box coasts at " << coasting);
    REQUIRE(coasting <= pushed);
    REQUIRE(coasting == Approx(pushed).margin(0.05));
}

// A unit box of 1 kg has a moment of inertia of m (1² + 1²) / 12 = 1/6 kg m² about each axis.
TEST_CASE("An angular impulse spins a body by impulse over its moment of inertia", "[physics][forces]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f));
    test.Run(tick);

    PhysicsWorld::Of(test.app.GetActiveScene())->AddAngularImpulse(box, Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(tick);

    const Math::Vec3 spin = box.GetComponent<RigidBody>().angularVelocity;
    INFO("1 N m s on 1/6 kg m² spins it at 6 rad/s about y, it spins at " << spin.x << ", " << spin.y << ", "
                                                                          << spin.z);
    REQUIRE(spin.y == Approx(6.0f).margin(0.02));
    REQUIRE(spin.x == Approx(0.0f).margin(1e-4));
    REQUIRE(spin.z == Approx(0.0f).margin(1e-4));
}

TEST_CASE("A torque added each tick spins a body up", "[physics][forces]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f));
    test.Run(tick);

    int ticks = 0;
    test.app.AddSequentialSystems(
        SchedulePriority::Tick, {[&](App& app) {
            if (ticks++ < 30)
            {
                PhysicsWorld::Of(app.GetActiveScene())->AddTorque(box, Math::Vec3(1.0f, 0.0f, 0.0f));
            }
        }});
    test.Run(0.5);

    const float spin = box.GetComponent<RigidBody>().angularVelocity.x;
    INFO("1 N m on 1/6 kg m² for half a second spins it at 3 rad/s about x (less damping), it spins at " << spin);
    REQUIRE(spin == Approx(3.0f).margin(0.05));
}

TEST_CASE("Pushing an entity with no dynamic body does nothing", "[physics][forces]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity bodiless = Place(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f));
    Entity wall = Place(test.Entities(), Math::Vec3(3.0f, 5.0f, 0.0f));
    wall.AddComponent<Collider>();
    Entity gone = AddBody(test.Entities(), Math::Vec3(-3.0f, 5.0f, 0.0f));
    test.Run(tick);
    PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(world != nullptr);

    gone.RemoveComponent<Collider>(); // its body ends at the next tick, before the pushes apply
    for (const Entity& entity : {bodiless, wall, gone})
    {
        world->AddForce(entity, Math::Vec3(100.0f));
        world->AddImpulse(entity, Math::Vec3(100.0f));
        world->AddTorque(entity, Math::Vec3(100.0f));
        world->AddAngularImpulse(entity, Math::Vec3(100.0f));
    }
    test.Run(0.5);

    INFO("an entity without a body is not moved by physics");
    REQUIRE(PositionOf(bodiless).y == Approx(5.0f).margin(0.0));
    INFO("a static collider is not pushed");
    REQUIRE(PositionOf(wall).y == Approx(5.0f).margin(0.0));
    INFO("a body removed before the pushes apply is not pushed");
    REQUIRE(PositionOf(gone).x == Approx(-3.0f).margin(0.0));
}

TEST_CASE("A scene without physics has no world to push", "[physics][forces]")
{
    PhysicsApp test;
    Place(test.Entities(), Math::Vec3(0.0f));
    test.Run(tick);

    INFO("game code finds no world, so there is nothing to push and nothing breaks");
    REQUIRE(PhysicsWorld::Of(test.app.GetActiveScene()) == nullptr);
}

TEST_CASE("Each scene's world has its own gravity, and zero gravity leaves bodies floating", "[physics][gravity]")
{
    PhysicsApp test;
    const std::string first = test.app.GetActiveScene().GetName();
    Entity floating = AddBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f));
    test.Run(tick);
    PhysicsWorld* still = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(still != nullptr);
    INFO("a new world starts with the PhysicsConfig's gravity");
    REQUIRE(still->Gravity().y == Approx(-9.81f).margin(0.0));

    Scene& other = test.app.CreateScene("Other");
    test.app.ChangeActiveScene("Other");
    Entity falling = AddBody(other.GetEntityList(), Math::Vec3(0.0f, 50.0f, 0.0f));
    test.Run(tick);
    REQUIRE(PhysicsWorld::Of(other) != nullptr);

    still->SetGravity(Math::Vec3(0.0f));
    floating.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f); // undo its first tick of falling
    INFO("the world reports the gravity it was given");
    REQUIRE(still->Gravity().x == Approx(0.0f).margin(0.0));
    REQUIRE(still->Gravity().y == Approx(0.0f).margin(0.0));
    REQUIRE(still->Gravity().z == Approx(0.0f).margin(0.0));
    INFO("and the other scene's world keeps its own");
    REQUIRE(PhysicsWorld::Of(other)->Gravity().y == Approx(-9.81f).margin(0.0));

    test.Run(0.5);
    INFO("the other scene's body still falls under the default gravity");
    REQUIRE(falling.GetComponent<RigidBody>().linearVelocity.y < -4.0f);

    test.app.ChangeActiveScene(first);
    const float height = PositionOf(floating).y;
    test.Run(1.0);
    INFO("with zero gravity the body floats where it was");
    REQUIRE(PositionOf(floating).y == Approx(height).margin(1e-5));
    REQUIRE(floating.GetComponent<RigidBody>().linearVelocity.y == Approx(0.0f).margin(1e-5));
}

TEST_CASE("Gravity set after bodies have gone to sleep moves them", "[physics][gravity]")
{
    PhysicsApp test;
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f));
    test.Run(tick);
    PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    world->SetGravity(Math::Vec3(0.0f));
    box.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f);
    test.Run(3.0); // long enough to fall asleep at rest

    world->SetGravity(Math::Vec3(2.0f, 0.0f, 0.0f));
    test.Run(0.5);

    const float sideways = box.GetComponent<RigidBody>().linearVelocity.x;
    INFO("gravity of 2 sideways for half a second gives 1 m/s (less damping), it moves at " << sideways);
    REQUIRE(sideways == Approx(1.0f).margin(0.02));
}

TEST_CASE("New worlds start with the gravity given to Install", "[physics][gravity]")
{
    App app(std::make_unique<SilentLogger>());
    PhysicsConfig config;
    config.gravity = Math::Vec3(0.0f, -1.62f, 0.0f);
    Physics physics = Physics::Install(app, config);
    Entity box = AddBody(app.GetActiveScene().GetEntityList(), Math::Vec3(0.0f, 50.0f, 0.0f));

    for (int i = 0; i < 30; ++i)
    {
        app.RunFrame(tick);
    }

    INFO("a world made under lunar gravity reports it");
    REQUIRE(PhysicsWorld::Of(app.GetActiveScene())->Gravity().y == Approx(-1.62f).margin(0.0));
    const float speed = box.GetComponent<RigidBody>().linearVelocity.y;
    INFO("and half a second of lunar gravity gives about -0.81 m/s, it falls at " << speed);
    REQUIRE(speed == Approx(-0.81f).margin(0.02));
}

namespace
{
    // Keeps the warnings and errors logged, to check a capacity overflow is reported.
    class RecordingLogger final : public ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<std::vector<std::string>> messages) : m_messages(std::move(messages))
        {
        }
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
        void WarnImpl(std::string_view message) override
        {
            m_messages->emplace_back(message);
        }
        void ErrorImpl(std::string_view message) override
        {
            m_messages->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_messages->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_messages;
    };

    bool AnyMentions(const std::vector<std::string>& messages, std::string_view text)
    {
        return std::any_of(messages.begin(), messages.end(),
                           [text](const std::string& message) { return message.find(text) != std::string::npos; });
    }
} // namespace

TEST_CASE("More bodies than the world's capacity are reported, and the bodies that fit are simulated",
          "[physics][capacities]")
{
    auto messages = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(messages));
    PhysicsConfig config;
    config.maxBodies = 2;
    Physics physics = Physics::Install(app, config);
    EntityList& entities = app.GetActiveScene().GetEntityList();
    Entity first = AddBody(entities, Math::Vec3(0.0f, 50.0f, 0.0f));
    Entity second = AddBody(entities, Math::Vec3(5.0f, 50.0f, 0.0f));
    Entity third = AddBody(entities, Math::Vec3(10.0f, 50.0f, 0.0f));

    for (int i = 0; i < 30; ++i)
    {
        app.RunFrame(tick);
    }

    INFO("the third body did not fit, and that was logged");
    REQUIRE(AnyMentions(*messages, "full"));
    INFO("the two that fit fall");
    REQUIRE(PositionOf(first).y < 49.0f);
    REQUIRE(PositionOf(second).y < 49.0f);
    INFO("the one that did not is left where it was");
    REQUIRE(PositionOf(third).y == Approx(50.0f).margin(0.0));
}

TEST_CASE("More contacts than the world's capacity are reported rather than crashing", "[physics][capacities]")
{
    auto messages = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(messages));
    PhysicsConfig config;
    config.maxContacts = 2;
    Physics physics = Physics::Install(app, config);
    EntityList& entities = app.GetActiveScene().GetEntityList();
    AddGround(entities);
    for (int i = 0; i < 8; ++i)
    {
        AddBody(entities, Math::Vec3(static_cast<float>(i) * 2.0f - 7.0f, 0.5f, 0.0f));
    }

    for (int i = 0; i < 30; ++i)
    {
        app.RunFrame(tick);
    }

    INFO("eight boxes on the ground need more than two contacts, and running out was logged");
    REQUIRE(AnyMentions(*messages, "ran out of room"));
}

TEST_CASE("A handle to a destroyed entity pushes nothing, even once a new body is made after it", "[physics][forces]")
{
    PhysicsApp test;
    Entity destroyed = AddMassiveBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f), 1.0f);
    test.Run(tick);
    PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(world != nullptr);
    const Entity stale = destroyed;
    test.Entities().DestroyEntity(destroyed);
    // Made straight after the destroy, so the ECS may give it the destroyed entity's id with a new version.
    Entity fresh = AddMassiveBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f), 1.0f);
    test.Run(tick);

    world->AddImpulse(stale, Math::Vec3(4.0f, 0.0f, 0.0f));
    world->AddForce(stale, Math::Vec3(400.0f, 0.0f, 0.0f));
    world->AddAngularImpulse(stale, Math::Vec3(4.0f, 0.0f, 0.0f));
    world->AddTorque(stale, Math::Vec3(400.0f, 0.0f, 0.0f));
    test.Run(tick);

    const RigidBody& moved = fresh.GetComponent<RigidBody>();
    INFO("pushes through the destroyed entity's handle moved the new body: it moves at "
         << moved.linearVelocity.x << " and spins at " << moved.angularVelocity.x);
    REQUIRE(moved.linearVelocity.x == 0.0f);
    REQUIRE(moved.angularVelocity.x == 0.0f);
}
