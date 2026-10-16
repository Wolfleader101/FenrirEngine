// Physics worlds through the frame seam: one per scene, made when the active scene first has physics, paused while
// the scene is inactive, ended with the scene or with the Physics owner.

#include <optional>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

TEST_CASE("A scene without physics components gets no physics world", "[physics][worlds]")
{
    PhysicsApp test;
    Place(test.Entities(), Math::Vec3(0.0f));

    test.Run(0.5);

    INFO("nothing in the scene uses physics, so it has no world");
    REQUIRE(PhysicsWorld::Of(test.app.GetActiveScene()) == nullptr);
}

TEST_CASE("A physics world is made when a physics component appears in the active scene", "[physics][worlds]")
{
    PhysicsApp test;
    Entity box = Place(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f));
    test.Run(0.5);
    REQUIRE(PhysicsWorld::Of(test.app.GetActiveScene()) == nullptr);

    box.AddComponent<RigidBody>();
    box.AddComponent<Collider>();
    test.Run(tick);

    INFO("the tick after the scene first has a collider, it has a physics world");
    REQUIRE(PhysicsWorld::Of(test.app.GetActiveScene()) != nullptr);
    INFO("and the collider that was already there is a body: it has started to fall");
    REQUIRE(PositionOf(box).y < 5.0f);
}

TEST_CASE("Bodies added after the world exists are simulated, and removed colliders stop colliding",
          "[physics][worlds]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    Entity floor = AddGround(test.Entities());
    floor.GetComponent<Transform>().pos.y = -5.5f; // a second floor further down, its top at y = -5
    test.Run(tick);
    REQUIRE(PhysicsWorld::Of(test.app.GetActiveScene()) != nullptr);

    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f));
    test.Run(3.0);
    INFO("a body added to a scene that already has a world falls and rests on the ground");
    REQUIRE(PositionOf(box).y == Approx(0.5f).margin(restSlop));

    ground.RemoveComponent<Collider>();
    test.Run(3.0);
    INFO("with the ground's collider removed, the box falls to the floor below");
    REQUIRE(PositionOf(box).y == Approx(-4.5f).margin(restSlop));
}

TEST_CASE("An inactive scene's physics world does not step, and resumes when the scene is active again",
          "[physics][worlds]")
{
    PhysicsApp test;
    const std::string first = test.app.GetActiveScene().GetName();
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f));
    test.Run(0.5);
    const float paused = PositionOf(box).y;
    const float speed = box.GetComponent<RigidBody>().linearVelocity.y;

    test.app.CreateScene("Other");
    test.app.ChangeActiveScene("Other");
    test.Run(1.0);
    INFO("while its scene is inactive, the body does not move");
    REQUIRE(PositionOf(box).y == Approx(paused).margin(0.0));
    INFO("the other scene has no physics, so no world");
    REQUIRE(PhysicsWorld::Of(test.app.GetScene("Other")) == nullptr);

    test.app.ChangeActiveScene(first);
    test.Run(tick);
    INFO("active again, the body carries on falling from where it stopped, at the speed it had");
    REQUIRE(PositionOf(box).y < paused);
    REQUIRE(PositionOf(box).y > paused + speed * static_cast<float>(tick) - 0.01f);
}

TEST_CASE("Destroying a scene ends its physics world", "[physics][worlds]")
{
    PhysicsApp test;
    const std::string first = test.app.GetActiveScene().GetName();
    Scene& other = test.app.CreateScene("Other");
    test.app.ChangeActiveScene("Other");
    AddBody(other.GetEntityList(), Math::Vec3(0.0f, 5.0f, 0.0f));
    test.Run(0.5);
    REQUIRE(PhysicsWorld::Of(other) != nullptr);

    test.app.ChangeActiveScene(first);
    test.app.DestroyScene("Other");
    AddBody(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f));
    test.Run(0.5);

    INFO("the remaining scene's physics carries on after the other scene and its world are gone");
    REQUIRE(PhysicsWorld::Of(test.app.GetActiveScene()) != nullptr);
}

TEST_CASE("Destroying the Physics owner ends every scene's world, and physics stops", "[physics][worlds]")
{
    App app(std::make_unique<SilentLogger>());
    std::optional<Physics> physics(Physics::Install(app));
    EntityList& entities = app.GetActiveScene().GetEntityList();
    Entity box = AddBody(entities, Math::Vec3(0.0f, 50.0f, 0.0f));
    app.RunFrame(tick);
    REQUIRE(PhysicsWorld::Of(app.GetActiveScene()) != nullptr);

    physics.reset();
    INFO("the owner's end removes the scene's world");
    REQUIRE(PhysicsWorld::Of(app.GetActiveScene()) == nullptr);

    const float stopped = PositionOf(box).y;
    app.RunFrame(tick);
    app.RunFrame(tick);
    INFO("frames after that run without physics: the body no longer moves");
    REQUIRE(PositionOf(box).y == Approx(stopped).margin(0.0));
}
