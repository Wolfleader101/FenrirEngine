// Bodies in the entity hierarchy through the frame seam: compound bodies built from child colliders, and bodies under
// moved and rotated parents.

#include <array>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    /** A child of `parent` at local `position`, with a unit box collider. */
    Entity AddChildBox(EntityList& entities, Entity parent, const Math::Vec3& position)
    {
        Entity child = Place(entities, position);
        child.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
        parent.AddChild(child);
        return child;
    }
} // namespace

TEST_CASE("Colliders on two child entities form one compound body with their parent's RigidBody",
          "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity body = Place(test.Entities(), Math::Vec3(0.0f, 4.0f, 0.0f));
    body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Dynamic});
    Entity left = AddChildBox(test.Entities(), body, Math::Vec3(-1.0f, 0.0f, 0.0f));
    Entity right = AddChildBox(test.Entities(), body, Math::Vec3(1.0f, 0.0f, 0.0f));

    test.Run(3.0);

    INFO("the parent, which has no collider of its own, fell with its children's boxes and rests on them, half a unit "
         "above the ground; it is at "
         << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(0.5f).margin(restSlop));
    INFO("the children kept their places beside it: one body, not three");
    REQUIRE(PositionOf(left).x == Approx(-1.0f).margin(1e-3));
    REQUIRE(PositionOf(right).x == Approx(1.0f).margin(1e-3));
    REQUIRE(PositionOf(left).y == Approx(0.5f).margin(restSlop));
    REQUIRE(PositionOf(right).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("Adding, moving and removing a child collider rebuilds its compound body", "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity body = Place(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Dynamic});
    AddChildBox(test.Entities(), body, Math::Vec3(0.0f));
    test.Run(1.0);
    INFO("one unit box: the body rests half a unit up, at " << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(0.5f).margin(restSlop));

    // Lifted clear so the new box below it does not start inside the ground.
    body.GetComponent<Transform>().pos.y = 3.0f;
    Entity leg = AddChildBox(test.Entities(), body, Math::Vec3(0.0f, -1.0f, 0.0f));
    test.Run(2.0);
    INFO("a second box added a unit below: the body rests on it, 1.5 up, at " << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(1.5f).margin(restSlop));

    body.GetComponent<Transform>().pos.y = 4.0f;
    leg.GetComponent<Transform>().pos.y = -2.0f;
    test.Run(2.0);
    INFO("that box moved two units below: the body rests 2.5 up, at " << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(2.5f).margin(restSlop));

    leg.RemoveComponent<Collider>();
    test.Run(2.0);
    INFO("its collider removed: the body falls back onto its own box, 0.5 up, at " << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("A collider moved under another body's entity joins that body", "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity body = Place(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f));
    body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Dynamic});
    AddChildBox(test.Entities(), body, Math::Vec3(0.0f));
    Entity loose = Place(test.Entities(), Math::Vec3(5.0f, 3.0f, 0.0f));
    loose.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});

    test.Run(0.1);
    loose.GetComponent<Transform>().pos = Math::Vec3(0.0f, -1.0f, 0.0f);
    body.AddChild(loose);
    test.Run(2.5);

    INFO("the static collider, reparented a unit below the body, became part of it: the body rests on it, 1.5 up, at "
         << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(1.5f).margin(restSlop));
}

TEST_CASE("A dynamic body under a moved, rotated and scaled parent falls and rests in world space",
          "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity parent = Place(test.Entities(), Math::Vec3(3.0f, 2.0f, -1.0f), Math::Vec3(2.0f, 0.5f, 1.0f));
    parent.GetComponent<Transform>().rot = Math::AngleAxis(0.7f, Math::Vec3(0.0f, 1.0f, 0.0f));
    Entity box = AddBody(test.Entities(), Math::Vec3(0.5f, 6.0f, 1.0f));
    parent.AddChild(box);
    const Math::Vec3 start = PositionOf(box);

    test.Run(4.0);

    const Math::Vec3 rest = PositionOf(box);
    INFO("the box, scaled by its parent to half a unit tall, fell straight down in world space from "
         << start.x << ", " << start.y << ", " << start.z << " and rests a quarter unit up, at " << rest.x << ", "
         << rest.y << ", " << rest.z);
    REQUIRE(rest.y == Approx(0.25f).margin(restSlop));
    REQUIRE(rest.x == Approx(start.x).margin(1e-3));
    REQUIRE(rest.z == Approx(start.z).margin(1e-3));

    const Transform& local = box.GetComponent<Transform>();
    INFO("its local Transform is written against the parent: turning about y and scaling y by 0.5 leaves x and z "
         "local, and y = (0.25 - 2) / 0.5 = -3.5; it is "
         << local.pos.x << ", " << local.pos.y << ", " << local.pos.z);
    REQUIRE(local.pos.x == Approx(0.5f).margin(1e-3));
    REQUIRE(local.pos.y == Approx(-3.5f).margin(2.0f * restSlop));
    REQUIRE(local.pos.z == Approx(1.0f).margin(1e-3));

    INFO("at rest it falls asleep");
    REQUIRE(box.GetComponent<RigidBody>().linearVelocity == Math::Vec3(0.0f));
    REQUIRE(box.GetComponent<RigidBody>().angularVelocity == Math::Vec3(0.0f));
}

TEST_CASE("Moving the parent of a kinematic body moves the body", "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    Entity parent = Place(test.Entities(), Math::Vec3(-2.0f, 0.0f, 0.0f));
    Entity paddle = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f), BoxShape{Math::Vec3(0.25f, 0.5f, 1.0f)},
                            MotionType::Kinematic);
    parent.AddChild(paddle);

    // Game code sweeps the parent, not the paddle, 2 m to the right over one second.
    int ticks = 0;
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) {
                                      if (ticks++ < 60)
                                      {
                                          parent.GetComponent<Transform>().pos.x += 2.0f / 60.0f;
                                      }
                                  }});
    test.Run(1.5);

    INFO("the paddle followed its parent to x = 0, it is at " << PositionOf(paddle).x);
    REQUIRE(PositionOf(paddle).x == Approx(0.0f).margin(1e-4));
    INFO("and on the way it pushed the box to at least x = 0.75 less slop; the box is at " << PositionOf(box).x);
    REQUIRE(PositionOf(box).x > 0.7f);
}

TEST_CASE("Moving the parent of a static body moves the body", "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity parent = Place(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    Entity shelf =
        AddBody(test.Entities(), Math::Vec3(0.0f), BoxShape{Math::Vec3(1.0f, 0.1f, 1.0f)}, MotionType::Static);
    parent.AddChild(shelf);
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 2.6f, 0.0f));
    test.Run(1.0);
    INFO("the box rests on the shelf, 2.6 up, at " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(2.6f).margin(restSlop));

    parent.GetComponent<Transform>().pos.x = 5.0f;
    test.Run(2.0);

    INFO("the shelf moved away with its parent, so the box fell to the ground, 0.5 up, at " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("A dynamic body made before its dynamic parent body is written back against the parent's new pose",
          "[physics][hierarchy]")
{
    PhysicsApp test;
    // The child is made first, so it comes first in entity order.
    Entity child = Place(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    child.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Dynamic});
    child.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
    Entity parent = AddBody(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f));
    parent.AddChild(child);

    test.Run(0.5);

    INFO("both fell freely from y = 50 for the same time, so they are at the same height: the parent at "
         << PositionOf(parent).y << ", the child at " << PositionOf(child).y);
    REQUIRE(PositionOf(parent).y < 49.0f);
    REQUIRE(PositionOf(child).y == Approx(PositionOf(parent).y).margin(1e-4));
    INFO("and the child kept its offset in x");
    REQUIRE(PositionOf(child).x == Approx(3.0f).margin(1e-4));
}

TEST_CASE("A dynamic body placed upright under a tilted parent rests upright below where it was placed, and sleeps",
          "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    const Math::Quat tilt = Math::AngleAxis(0.9f, Math::Normalized(Math::Vec3(1.0f, 2.0f, 3.0f)));
    Entity parent = Place(test.Entities(), Math::Vec3(1.3f, 2.1f, -0.7f), Math::Vec3(0.7f));
    parent.GetComponent<Transform>().rot = tilt;
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f));
    parent.AddChild(box);
    // Upright in the world, a little above the ground, whatever the parent's tilt.
    SetWorldTransform(box, Transform(Math::Vec3(0.2f, 1.0f, 0.4f), identity, Math::Vec3(0.7f)));

    test.Run(4.0);

    INFO("it rests upright, 0.35 up, at " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(0.35f).margin(restSlop));
    REQUIRE(PositionOf(box).x == Approx(0.2f).margin(1e-3));
    REQUIRE(PositionOf(box).z == Approx(0.4f).margin(1e-3));
    INFO("and asleep: writing its pose back through the tilted parent does not keep waking it");
    REQUIRE(box.GetComponent<RigidBody>().linearVelocity == Math::Vec3(0.0f));
    REQUIRE(box.GetComponent<RigidBody>().angularVelocity == Math::Vec3(0.0f));
}

TEST_CASE("Removing a RigidBody makes its colliders static where they are", "[physics][hierarchy]")
{
    PhysicsApp test;
    Entity body = Place(test.Entities(), Math::Vec3(0.0f, 10.0f, 0.0f));
    body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Dynamic});
    body.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
    Entity child = AddChildBox(test.Entities(), body, Math::Vec3(2.0f, 0.0f, 0.0f));
    test.Run(0.5);
    const float stoppedAt = PositionOf(body).y;
    REQUIRE(stoppedAt < 9.0f);

    body.RemoveComponent<RigidBody>();
    test.Run(1.0);
    INFO("without its RigidBody the body stopped falling at " << stoppedAt << "; it is at " << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == stoppedAt);

    // A box dropped onto each of the two colliders, now static, rests on it.
    Entity onBody = AddBody(test.Entities(), Math::Vec3(0.0f, stoppedAt + 2.0f, 0.0f));
    Entity onChild = AddBody(test.Entities(), PositionOf(child) + Math::Vec3(0.0f, 2.0f, 0.0f));
    test.Run(2.0);
    INFO("boxes dropped on them rest a unit above each: " << PositionOf(onBody).y << " and " << PositionOf(onChild).y
                                                          << " over " << stoppedAt);
    REQUIRE(PositionOf(onBody).y == Approx(stoppedAt + 1.0f).margin(restSlop));
    REQUIRE(PositionOf(onChild).y == Approx(stoppedAt + 1.0f).margin(restSlop));
}

TEST_CASE("A RigidBody left with no collider stays, inactive in physics, with a warning", "[physics][hierarchy]")
{
    PhysicsApp test;
    Entity body = AddBody(test.Entities(), Math::Vec3(0.0f, 10.0f, 0.0f));
    test.Run(0.5);
    const float stoppedAt = PositionOf(body).y;
    test.warnings.clear();

    body.RemoveComponent<Collider>();
    test.Run(1.0);
    INFO("the entity keeps its RigidBody, and no longer moves");
    REQUIRE(test.Entities().HasEntity(body.GetId()));
    REQUIRE(body.HasComponent<RigidBody>());
    REQUIRE(PositionOf(body).y == stoppedAt);
    INFO("one warning says it is inactive");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(test.warnings[0].find("no collider") != std::string::npos);

    body.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
    test.Run(0.5);
    INFO("given a collider again, it falls again, from " << stoppedAt << " to " << PositionOf(body).y);
    REQUIRE(PositionOf(body).y < stoppedAt - 0.5f);
    REQUIRE(test.warnings.size() == 1);
}

TEST_CASE("A CollisionFilter on a child collider is ignored, with a warning; the body's filter applies",
          "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity body = Place(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Dynamic});
    Entity child = AddChildBox(test.Entities(), body, Math::Vec3(0.0f));
    child.AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u << 3, .mask = 0u});

    test.Run(2.0);

    INFO("the child's filter, which collides with nothing, is ignored: the body rests on the ground, at "
         << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(0.5f).margin(restSlop));
    INFO("and a warning names the ignored filter");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(test.warnings[0].find("CollisionFilter") != std::string::npos);
}

TEST_CASE("A compound body's mass is spread over all its colliders", "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // A narrow pillar, its top at y = 2, under the body's own box only.
    Entity pillar = Place(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    pillar.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.25f, 1.0f, 0.25f)}});
    Entity body = AddBody(test.Entities(), Math::Vec3(0.0f, 2.5f, 0.0f));
    AddChildBox(test.Entities(), body, Math::Vec3(1.0f, 0.0f, 0.0f));

    test.Run(3.0);

    INFO("its centre of mass is between its two boxes, half a unit out past the pillar's edge, so it tipped off the "
         "pillar (resting on it, the body would be at y = 2.5); it is at y = "
         << PositionOf(body).y);
    REQUIRE(PositionOf(body).y < 2.0f);
}

namespace
{
    /** A static compound body: a parent with a static RigidBody and unit boxes on children at x = 3 and x = 6. */
    struct StaticCompound
    {
        Entity body;
        Entity near;
        Entity far;
    };

    StaticCompound AddStaticCompound(EntityList& entities)
    {
        Entity body = Place(entities, Math::Vec3(0.0f));
        body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Static});
        Entity near = AddChildBox(entities, body, Math::Vec3(3.0f, 0.0f, 0.0f));
        Entity far = AddChildBox(entities, body, Math::Vec3(6.0f, 0.0f, 0.0f));
        return StaticCompound{body, near, far};
    }
} // namespace

TEST_CASE("A ray hitting a compound body names the body and the collider it hit", "[physics][hierarchy]")
{
    PhysicsApp test;
    const StaticCompound compound = AddStaticCompound(test.Entities());
    test.Run(tick);
    PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(world != nullptr);

    const std::optional<RaycastHit> along = world->Raycast(Math::Vec3(0.0f), Math::Vec3(1.0f, 0.0f, 0.0f), 100.0f);
    INFO("a ray along +x meets the box at x = 3 first, on its near face at x = 2.5");
    REQUIRE(along.has_value());
    REQUIRE(along->body == compound.body);
    REQUIRE(along->collider == compound.near);
    REQUIRE(along->point.x == Approx(2.5f).margin(1e-4f));

    const std::optional<RaycastHit> onto =
        world->Raycast(Math::Vec3(6.0f, 5.0f, 0.0f), Math::Vec3(0.0f, -1.0f, 0.0f), 100.0f);
    INFO("a ray down onto x = 6 meets the far box");
    REQUIRE(onto.has_value());
    REQUIRE(onto->body == compound.body);
    REQUIRE(onto->collider == compound.far);
}

TEST_CASE("Overlap queries report each collider of a compound body they touch, with the body", "[physics][hierarchy]")
{
    PhysicsApp test;
    const StaticCompound compound = AddStaticCompound(test.Entities());
    test.Run(tick);
    PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(world != nullptr);

    std::array<OverlapHit, 4> hits;
    INFO("a point inside the far box overlaps that box alone");
    REQUIRE(world->OverlapPoint(Math::Vec3(6.2f, 0.1f, 0.0f), hits) == 1);
    REQUIRE(hits[0].body == compound.body);
    REQUIRE(hits[0].collider == compound.far);

    const std::size_t count = world->Overlap(BoxShape{Math::Vec3(2.0f, 0.5f, 0.5f)},
                                             Transform(Math::Vec3(4.5f, 0.0f, 0.0f), identity, Math::Vec3(1.0f)), hits);
    INFO("a box spanning both overlaps both, each named with the one body");
    REQUIRE(count == 2);
    REQUIRE(hits[0].body == compound.body);
    REQUIRE(hits[1].body == compound.body);
    REQUIRE(hits[0].collider != hits[1].collider);
    REQUIRE((hits[0].collider == compound.near || hits[0].collider == compound.far));
    REQUIRE((hits[1].collider == compound.near || hits[1].collider == compound.far));
}

TEST_CASE("Pushing a child collider's entity pushes the compound body it belongs to", "[physics][hierarchy]")
{
    PhysicsApp test;
    Entity body = Place(test.Entities(), Math::Vec3(0.0f, 50.0f, 0.0f));
    body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Dynamic, .mass = 2.0f});
    Entity child = AddChildBox(test.Entities(), body, Math::Vec3(0.0f));
    test.Run(tick);
    PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
    REQUIRE(world != nullptr);

    world->AddImpulse(child, Math::Vec3(4.0f, 0.0f, 0.0f));
    test.Run(tick);

    const float speed = body.GetComponent<RigidBody>().linearVelocity.x;
    INFO("an impulse of 4 N s on the child's 2 kg body adds 2 m/s (less a tick of damping), it moves at " << speed);
    REQUIRE(speed == Approx(2.0f).margin(0.01));
}
