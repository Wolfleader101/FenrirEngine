// Collision groups through the frame seam (the physics map's ticket 51): bodies whose CollisionGroup is the same
// non-zero group never touch, observed in their contacts as physics steps. SPIKE (ticket 55).

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Characters.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;

namespace
{
    /** Whether `a`'s body touches `b`'s now, as of the last step. */
    bool Touching(PhysicsApp& test, const Entity& a, const Entity& b)
    {
        bool touching = false;
        PhysicsWorld::Of(test.app.GetActiveScene())->ForEachContact(a, [&](const Contact& contact) {
            touching = touching || contact.otherBody == b;
        });
        return touching;
    }
} // namespace

TEST_CASE("Bodies in the same collision group pass through each other, and in different groups collide",
          "[physics][groups][ragdoll-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // A box resting on the ground, and one dropped from above it, both in group 7.
    Entity lower = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    Entity upper = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    lower.AddComponent<CollisionGroup>(CollisionGroup{7});
    upper.AddComponent<CollisionGroup>(CollisionGroup{7});
    // A third box in group 8 dropped beside them onto a fourth in group 7.
    Entity other = AddBody(test.Entities(), Math::Vec3(3.0f, 2.0f, 0.0f));
    Entity base = AddBody(test.Entities(), Math::Vec3(3.0f, 0.5f, 0.0f));
    other.AddComponent<CollisionGroup>(CollisionGroup{8});
    base.AddComponent<CollisionGroup>(CollisionGroup{7});

    test.Run(2.0);
    INFO("the upper box fell through the lower one onto the ground: both rest at the same height");
    REQUIRE(std::abs(PositionOf(upper).y - 0.5f) < restSlop);
    REQUIRE(std::abs(PositionOf(lower).y - 0.5f) < restSlop);
    REQUIRE_FALSE(Touching(test, upper, lower));
    INFO("the box in another group landed on the one below it");
    REQUIRE(std::abs(PositionOf(other).y - 1.5f) < restSlop);
    REQUIRE(Touching(test, other, base));
}

TEST_CASE("Removing a body's collision group makes it collide again, as a field edit",
          "[physics][groups][ragdoll-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity lower = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f), BoxShape{}, MotionType::Static);
    Entity upper = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    lower.AddComponent<CollisionGroup>(CollisionGroup{3});
    upper.AddComponent<CollisionGroup>(CollisionGroup{3});
    test.Run(1.5);
    INFO("grouped, the dynamic box fell through the static one");
    REQUIRE(PositionOf(upper).y < 0.9f);

    upper.GetComponent<CollisionGroup>().group = 0;
    upper.GetComponent<Transform>().pos = Math::Vec3(0.0f, 2.0f, 0.0f);
    upper.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f);
    test.Run(1.5);
    INFO("with no group, dropped again, it rests on the static box");
    REQUIRE(std::abs(PositionOf(upper).y - 1.5f) < restSlop);
    REQUIRE(Touching(test, upper, lower));
}

TEST_CASE("A character body in a group walks through that group's bodies, and they ignore it",
          "[physics][groups][characters][ragdoll-spike]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // A character standing on the ground, in group 5, with a dynamic box of the same group dropped onto its head.
    Entity character = Place(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    character.AddComponent<Collider>(Collider{CapsuleShape{.halfHeight = 0.5f, .radius = 0.5f}});
    character.AddComponent<CharacterBody>();
    character.AddComponent<CollisionGroup>(CollisionGroup{5});
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    box.AddComponent<CollisionGroup>(CollisionGroup{5});
    // Gravity is game code's.
    test.app.AddSequentialSystem(SchedulePriority::Tick, [character](App& running) {
        const PhysicsWorld* world = PhysicsWorld::Of(running.GetActiveScene());
        if (world != nullptr)
        {
            character.GetComponent<CharacterBody>().velocity.y += world->Gravity().y * static_cast<float>(tick);
        }
    });

    test.Run(2.0);
    INFO("the box fell through the character onto the ground");
    REQUIRE(std::abs(PositionOf(box).y - 0.5f) < restSlop);
    INFO("the character stands on the ground, not on the box");
    REQUIRE(character.GetComponent<CharacterBody>().state.groundBody != box);
}
