// Queries through the frame seam: bodies made by running frames, then questions asked of the scene's physics world.

#include <array>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    PhysicsWorld& WorldOf(PhysicsApp& test)
    {
        PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
        REQUIRE(world != nullptr);
        return *world;
    }

    // Static bodies stay where they are put, so hits on them have exact expected values.
    Entity AddStatic(EntityList& entities, const Math::Vec3& position, CollisionShape shape = BoxShape{})
    {
        Entity entity = Place(entities, position);
        entity.AddComponent<Collider>(Collider{shape});
        return entity;
    }

    const Math::Vec3 down(0.0f, -1.0f, 0.0f);
    const Math::Vec3 east(1.0f, 0.0f, 0.0f);
} // namespace

TEST_CASE("A ray hits the nearest body, naming it, with the point, normal and distance of the hit",
          "[physics][queries]")
{
    PhysicsApp test;
    Entity near = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    AddStatic(test.Entities(), Math::Vec3(6.0f, 0.0f, 0.0f));
    test.Run(tick);

    const std::optional<RaycastHit> hit = WorldOf(test).Raycast(Math::Vec3(0.0f), east * 2.0f, 100.0f);

    INFO("the ray along +x from the origin meets the box at x = 3 first");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == near);
    REQUIRE(hit->collider == near);
    INFO("on its near face, half a metre before its centre, 2.5 m along the ray (the direction's length does not "
         "matter); it is at "
         << hit->point.x << " with distance " << hit->distance);
    REQUIRE(hit->point.x == Approx(2.5f).margin(1e-4f));
    REQUIRE(hit->point.y == Approx(0.0f).margin(1e-4f));
    REQUIRE(hit->distance == Approx(2.5f).margin(1e-4f));
    INFO("the face's normal points back along the ray");
    REQUIRE(hit->normal.x == Approx(-1.0f).margin(1e-4f));
    REQUIRE(hit->normal.y == Approx(0.0f).margin(1e-4f));
}

TEST_CASE("RaycastAll lists every body the ray hits, nearest first", "[physics][queries]")
{
    PhysicsApp test;
    // Made out of order, so the list's order comes from distance.
    Entity far = AddStatic(test.Entities(), Math::Vec3(9.0f, 0.0f, 0.0f));
    Entity near = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    Entity middle = AddStatic(test.Entities(), Math::Vec3(6.0f, 0.0f, 0.0f), SphereShape{1.0f});
    test.Run(tick);

    std::array<RaycastHit, 8> hits;
    const std::size_t count = WorldOf(test).RaycastAll(Math::Vec3(0.0f), east, 100.0f, hits);

    INFO("the ray passes through three bodies; it listed " << count);
    REQUIRE(count == 3);
    INFO("the box at 3 first, met at 2.5");
    REQUIRE(hits[0].body == near);
    REQUIRE(hits[0].distance == Approx(2.5f).margin(1e-4f));
    INFO("the sphere of radius 1 at 6 second, met at 5");
    REQUIRE(hits[1].body == middle);
    REQUIRE(hits[1].distance == Approx(5.0f).margin(1e-4f));
    INFO("the box at 9 last, met at 8.5");
    REQUIRE(hits[2].body == far);
    REQUIRE(hits[2].distance == Approx(8.5f).margin(1e-4f));
}

TEST_CASE("RaycastAll stops at the nearest hits when the buffer is full", "[physics][queries]")
{
    PhysicsApp test;
    AddStatic(test.Entities(), Math::Vec3(9.0f, 0.0f, 0.0f));
    Entity second = AddStatic(test.Entities(), Math::Vec3(6.0f, 0.0f, 0.0f));
    AddStatic(test.Entities(), Math::Vec3(12.0f, 0.0f, 0.0f));
    Entity first = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    test.Run(tick);

    std::array<RaycastHit, 2> hits;
    const std::size_t count = WorldOf(test).RaycastAll(Math::Vec3(0.0f), east, 100.0f, hits);

    INFO("four bodies are hit but the buffer holds two; it wrote " << count);
    REQUIRE(count == 2);
    INFO("the two written are the two nearest, in order");
    REQUIRE(hits[0].body == first);
    REQUIRE(hits[1].body == second);
}

TEST_CASE("A ray misses a body beyond its maximum distance", "[physics][queries]")
{
    PhysicsApp test;
    AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    test.Run(tick);

    INFO("the box's near face is 2.5 away, beyond a ray 2 long");
    REQUIRE_FALSE(WorldOf(test).Raycast(Math::Vec3(0.0f), east, 2.0f).has_value());
    INFO("and within one 3 long");
    REQUIRE(WorldOf(test).Raycast(Math::Vec3(0.0f), east, 3.0f).has_value());
}

TEST_CASE("A ray starting inside a box or sphere hits it at distance 0", "[physics][queries]")
{
    PhysicsApp test;
    Entity box = AddStatic(test.Entities(), Math::Vec3(0.0f));
    Entity sphere = AddStatic(test.Entities(), Math::Vec3(10.0f, 0.0f, 0.0f), SphereShape{1.0f});
    test.Run(tick);

    const std::optional<RaycastHit> inBox = WorldOf(test).Raycast(Math::Vec3(0.1f, 0.0f, 0.0f), east, 5.0f);
    INFO("a ray from inside the box hits the box, not its far face from within");
    REQUIRE(inBox.has_value());
    REQUIRE(inBox->body == box);
    REQUIRE(inBox->distance == 0.0f);
    INFO("at its start, with the normal against the ray");
    REQUIRE(inBox->point.x == Approx(0.1f).margin(1e-4f));
    REQUIRE(inBox->normal.x == Approx(-1.0f).margin(1e-4f));

    const std::optional<RaycastHit> inSphere = WorldOf(test).Raycast(Math::Vec3(10.0f, 0.5f, 0.0f), down, 5.0f);
    INFO("a ray from inside the sphere hits the sphere at distance 0");
    REQUIRE(inSphere.has_value());
    REQUIRE(inSphere->body == sphere);
    REQUIRE(inSphere->distance == 0.0f);
}

TEST_CASE("A ray starting inside a capsule or convex hull hits it at distance 0", "[physics][queries]")
{
    PhysicsApp test;
    Entity capsule = AddStatic(test.Entities(), Math::Vec3(0.0f), CapsuleShape{1.0f, 0.5f});
    // The corners of a unit cube: a hull needs no triangles.
    std::array<Math::Vec3, 8> corners;
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        corners[i] = Math::Vec3((i & 1u) ? 0.5f : -0.5f, (i & 2u) ? 0.5f : -0.5f, (i & 4u) ? 0.5f : -0.5f);
    }
    const CollisionMesh cube = test.physics.CreateCollisionMesh(corners, {});
    Entity hull = AddStatic(test.Entities(), Math::Vec3(10.0f, 0.0f, 0.0f), ConvexHullShape{cube});
    test.Run(tick);

    const std::optional<RaycastHit> inCapsule = WorldOf(test).Raycast(Math::Vec3(0.0f, 1.2f, 0.0f), east, 5.0f);
    INFO("a ray from inside the capsule's upper cap does not hit the capsule at distance 0");
    REQUIRE(inCapsule.has_value());
    REQUIRE(inCapsule->body == capsule);
    REQUIRE(inCapsule->distance == 0.0f);

    const std::optional<RaycastHit> inHull = WorldOf(test).Raycast(Math::Vec3(10.1f, 0.0f, 0.0f), east, 5.0f);
    INFO("a ray from inside the hull does not hit the hull at distance 0");
    REQUIRE(inHull.has_value());
    REQUIRE(inHull->body == hull);
    REQUIRE(inHull->distance == 0.0f);
}

TEST_CASE("A ray hits a triangle mesh's front faces and passes through its back faces", "[physics][queries]")
{
    PhysicsApp test;
    // Two triangles making a 20 by 20 floor at y = 0, facing up.
    const std::array<Math::Vec3, 4> vertices = {Math::Vec3(-10.0f, 0.0f, -10.0f), Math::Vec3(10.0f, 0.0f, -10.0f),
                                                Math::Vec3(-10.0f, 0.0f, 10.0f), Math::Vec3(10.0f, 0.0f, 10.0f)};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 1, 2, 3};
    Entity floor =
        AddStatic(test.Entities(), Math::Vec3(0.0f), MeshShape{test.physics.CreateCollisionMesh(vertices, indices)});
    test.Run(tick);

    const std::optional<RaycastHit> fromAbove = WorldOf(test).Raycast(Math::Vec3(1.0f, 2.0f, 1.0f), down, 5.0f);
    INFO("a ray down onto the floor's front faces does not hit it 2 m along");
    REQUIRE(fromAbove.has_value());
    REQUIRE(fromAbove->body == floor);
    REQUIRE(fromAbove->distance == Approx(2.0f).margin(1e-4f));

    INFO("a ray up through the floor's back faces hits it");
    REQUIRE_FALSE(WorldOf(test).Raycast(Math::Vec3(1.0f, -2.0f, 1.0f), Math::Vec3(0.0f, 1.0f, 0.0f), 5.0f).has_value());
}

TEST_CASE("A sphere cast down hits the ground where the sphere first touches it", "[physics][queries]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    test.Run(tick);

    const Transform pose(Math::Vec3(1.0f, 5.0f, 0.0f), identity, Math::Vec3(1.0f));
    const std::optional<ShapeCastHit> hit = WorldOf(test).ShapeCast(SphereShape{0.5f}, pose, down, 10.0f);

    INFO("a sphere of radius 0.5 dropped from y = 5 hits the ground's top at y = 0");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == ground);
    INFO("after travelling 4.5; it travelled " << hit->distance);
    REQUIRE(hit->distance == Approx(4.5f).margin(1e-3f));
    INFO("touching at the bottom of the sphere, (1, 0, 0); the point is at y = " << hit->point.y);
    REQUIRE(hit->point.x == Approx(1.0f).margin(1e-3f));
    REQUIRE(hit->point.y == Approx(0.0f).margin(1e-3f));
    INFO("the ground's normal there points up, towards the sphere");
    REQUIRE(hit->normal.y == Approx(1.0f).margin(1e-3f));
}

TEST_CASE("ShapeCastAll lists the bodies a box sweeps through, nearest first", "[physics][queries]")
{
    PhysicsApp test;
    Entity far = AddStatic(test.Entities(), Math::Vec3(8.0f, 0.0f, 0.0f), SphereShape{0.5f});
    Entity near = AddStatic(test.Entities(), Math::Vec3(4.0f, 0.0f, 0.0f));
    AddStatic(test.Entities(), Math::Vec3(4.0f, 5.0f, 0.0f)); // out of the box's path
    test.Run(tick);

    std::array<ShapeCastHit, 4> hits;
    const Transform pose(Math::Vec3(0.0f), identity, Math::Vec3(1.0f));
    const std::size_t count = WorldOf(test).ShapeCastAll(BoxShape{}, pose, east, 20.0f, hits);

    INFO("a unit box swept along +x passes through the box at 4 and the sphere at 8; it listed " << count);
    REQUIRE(count == 2);
    INFO("the box first, touched after 3; it was at " << hits[0].distance);
    REQUIRE(hits[0].body == near);
    REQUIRE(hits[0].distance == Approx(3.0f).margin(1e-3f));
    INFO("the sphere second, touched after 7; it was at " << hits[1].distance);
    REQUIRE(hits[1].body == far);
    REQUIRE(hits[1].distance == Approx(7.0f).margin(1e-3f));
}

TEST_CASE("A shape cast scales its shape by the pose's scale", "[physics][queries]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    test.Run(tick);

    const Transform pose(Math::Vec3(0.0f, 5.0f, 0.0f), identity, Math::Vec3(2.0f));
    const std::optional<ShapeCastHit> hit = WorldOf(test).ShapeCast(BoxShape{}, pose, down, 10.0f);

    INFO("a unit box scaled by 2 reaches 1 below its centre, so from y = 5 it travels 4; it travelled "
         << (hit ? hit->distance : -1.0f));
    REQUIRE(hit.has_value());
    REQUIRE(hit->distance == Approx(4.0f).margin(1e-3f));
}

TEST_CASE("Overlap finds the bodies a box or sphere touches", "[physics][queries]")
{
    PhysicsApp test;
    Entity left = AddStatic(test.Entities(), Math::Vec3(-1.0f, 0.0f, 0.0f));
    Entity right = AddStatic(test.Entities(), Math::Vec3(1.0f, 0.0f, 0.0f), SphereShape{0.5f});
    AddStatic(test.Entities(), Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(tick);

    std::array<OverlapHit, 4> hits;
    const Transform pose(Math::Vec3(0.0f), identity, Math::Vec3(1.0f));
    const std::size_t inBox = WorldOf(test).Overlap(BoxShape{Math::Vec3(0.75f)}, pose, hits);
    INFO("a box reaching 0.75 each way overlaps the box at -1 and the sphere at 1, not the box at 5; it found "
         << inBox);
    REQUIRE(inBox == 2);
    const bool found =
        (hits[0].body == left && hits[1].body == right) || (hits[0].body == right && hits[1].body == left);
    REQUIRE(found);
    REQUIRE(hits[0].collider == hits[0].body);

    const std::size_t inSphere = WorldOf(test).Overlap(SphereShape{0.25f}, pose, hits);
    INFO("a sphere of radius 0.25 at the origin, in the gap between them, overlaps neither; it found " << inSphere);
    REQUIRE(inSphere == 0);
}

TEST_CASE("Overlap stops when the buffer is full", "[physics][queries]")
{
    PhysicsApp test;
    for (int i = 0; i < 4; ++i)
    {
        AddStatic(test.Entities(), Math::Vec3(static_cast<float>(i) * 1.5f, 0.0f, 0.0f));
    }
    test.Run(tick);

    std::array<OverlapHit, 2> hits;
    const Transform pose(Math::Vec3(2.25f, 0.0f, 0.0f), identity, Math::Vec3(1.0f));
    const std::size_t count = WorldOf(test).Overlap(BoxShape{Math::Vec3(5.0f)}, pose, hits);
    INFO("four bodies are inside the box but the buffer holds two; it wrote " << count);
    REQUIRE(count == 2);
}

TEST_CASE("OverlapPoint finds the bodies containing a point", "[physics][queries]")
{
    PhysicsApp test;
    Entity box = AddStatic(test.Entities(), Math::Vec3(0.0f));
    Entity sphere = AddStatic(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.8f), SphereShape{0.5f});
    test.Run(tick);

    std::array<OverlapHit, 4> hits;
    INFO("the box's centre is inside the box only");
    REQUIRE(WorldOf(test).OverlapPoint(Math::Vec3(0.0f), hits) == 1);
    REQUIRE(hits[0].body == box);
    INFO("a point in both is in both");
    REQUIRE(WorldOf(test).OverlapPoint(Math::Vec3(0.0f, 0.0f, 0.45f), hits) == 2);
    INFO("the sphere's far side is in the sphere only");
    REQUIRE(WorldOf(test).OverlapPoint(Math::Vec3(0.0f, 0.0f, 1.2f), hits) == 1);
    REQUIRE(hits[0].body == sphere);
    INFO("a point outside both is in neither");
    REQUIRE(WorldOf(test).OverlapPoint(Math::Vec3(3.0f, 0.0f, 0.0f), hits) == 0);
}

TEST_CASE("A query's mask leaves out bodies in other layers", "[physics][queries][layers]")
{
    PhysicsApp test;
    Entity near = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    near.AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u << 1, .mask = ~0u});
    Entity far = AddStatic(test.Entities(), Math::Vec3(6.0f, 0.0f, 0.0f)); // layer 0, the default
    test.Run(tick);

    const std::optional<RaycastHit> layerZero =
        WorldOf(test).Raycast(Math::Vec3(0.0f), east, 100.0f, QueryFilter{.mask = 1u << 0});
    INFO("a ray for layer 0 passes the nearer body, in layer 1, and hits the one in layer 0");
    REQUIRE(layerZero.has_value());
    REQUIRE(layerZero->body == far);

    std::array<OverlapHit, 4> hits;
    const Transform pose(Math::Vec3(4.5f, 0.0f, 0.0f), identity, Math::Vec3(1.0f));
    const std::size_t count =
        WorldOf(test).Overlap(BoxShape{Math::Vec3(3.0f)}, pose, hits, QueryFilter{.mask = 1u << 1});
    INFO("an overlap for layer 1 finds only the body in layer 1, though it covers both; it found " << count);
    REQUIRE(count == 1);
    REQUIRE(hits[0].body == near);

    INFO("a mask of no layers finds nothing");
    REQUIRE_FALSE(WorldOf(test).Raycast(Math::Vec3(0.0f), east, 100.0f, QueryFilter{.mask = 0u}).has_value());
}

TEST_CASE("A query's ignored body is not found, so a body can cast from inside itself", "[physics][queries]")
{
    PhysicsApp test;
    Entity shooter = AddStatic(test.Entities(), Math::Vec3(0.0f));
    Entity target = AddStatic(test.Entities(), Math::Vec3(4.0f, 0.0f, 0.0f));
    test.Run(tick);

    INFO("without the filter, a ray from the shooter's centre hits the shooter");
    REQUIRE(WorldOf(test).Raycast(Math::Vec3(0.0f), east, 100.0f)->body == shooter);

    const std::optional<RaycastHit> hit =
        WorldOf(test).Raycast(Math::Vec3(0.0f), east, 100.0f, QueryFilter{.ignoreBody = shooter});
    INFO("ignoring the shooter, it hits the target");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == target);

    std::array<OverlapHit, 4> hits;
    INFO("a point query ignoring the shooter does not find it");
    REQUIRE(WorldOf(test).OverlapPoint(Math::Vec3(0.0f), hits, QueryFilter{.ignoreBody = shooter}) == 0);
}

TEST_CASE("Queries see the world as the last step left it until the next tick", "[physics][queries]")
{
    PhysicsApp test;
    Entity box = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    test.Run(tick);

    box.GetComponent<Transform>().pos = Math::Vec3(3.0f, 10.0f, 0.0f);
    Entity added = AddStatic(test.Entities(), Math::Vec3(0.0f, 10.0f, 3.0f));

    const std::optional<RaycastHit> before = WorldOf(test).Raycast(Math::Vec3(0.0f), east, 100.0f);
    INFO("moved in the ECS but not yet stepped, the box is found where it was");
    REQUIRE(before.has_value());
    REQUIRE(before->body == box);
    INFO("and a collider added since is not found yet");
    REQUIRE_FALSE(
        WorldOf(test).Raycast(Math::Vec3(0.0f, 10.0f, 0.0f), Math::Vec3(0.0f, 0.0f, 1.0f), 100.0f).has_value());

    test.Run(tick);
    INFO("after the next tick, the box is gone from its old place");
    REQUIRE_FALSE(WorldOf(test).Raycast(Math::Vec3(0.0f), east, 100.0f).has_value());
    INFO("found at its new one");
    REQUIRE(WorldOf(test).Raycast(Math::Vec3(0.0f, 10.0f, 0.0f), east, 100.0f)->body == box);
    INFO("and the added collider is found");
    REQUIRE(WorldOf(test).Raycast(Math::Vec3(0.0f, 10.0f, 0.0f), Math::Vec3(0.0f, 0.0f, 1.0f), 100.0f)->body == added);
}

TEST_CASE("A body resting on the ground is asleep, inside bounds around its shape", "[physics][queries]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);
    INFO("a falling box is awake");
    REQUIRE_FALSE(world.IsSleeping(box));

    test.Run(3.0);
    INFO("after resting for a while it is asleep");
    REQUIRE(world.IsSleeping(box));
    INFO("a static body never sleeps");
    REQUIRE_FALSE(world.IsSleeping(ground));

    const std::optional<Math::AABB> bounds = world.Bounds(box);
    REQUIRE(bounds.has_value());
    INFO("its bounds are the unit box resting on y = 0: x from -0.5 to 0.5, y from 0 to 1 (within the rest slop); "
         "they run from y = "
         << bounds->min.y << " to " << bounds->max.y);
    REQUIRE(bounds->min.x == Approx(-0.5f).margin(restSlop));
    REQUIRE(bounds->max.x == Approx(0.5f).margin(restSlop));
    REQUIRE(bounds->min.y == Approx(0.0f).margin(restSlop));
    REQUIRE(bounds->max.y == Approx(1.0f).margin(restSlop));

    Entity bare = Place(test.Entities(), Math::Vec3(0.0f));
    INFO("an entity with no body is not asleep and has no bounds");
    REQUIRE_FALSE(world.IsSleeping(bare));
    REQUIRE_FALSE(world.Bounds(bare).has_value());
}

TEST_CASE("A scene without physics has no world to query, and an emptied world finds nothing", "[physics][queries]")
{
    PhysicsApp test;
    test.Run(tick);
    INFO("a scene that has never had a collider has no physics world");
    REQUIRE(PhysicsWorld::Of(test.app.GetActiveScene()) == nullptr);

    Entity box = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    test.Run(tick);
    box.RemoveComponent<Collider>();
    test.Run(tick);

    std::array<OverlapHit, 4> hits;
    INFO("once its only collider is removed, the world finds nothing");
    REQUIRE_FALSE(WorldOf(test).Raycast(Math::Vec3(0.0f), east, 100.0f).has_value());
    REQUIRE(WorldOf(test).OverlapPoint(Math::Vec3(3.0f, 0.0f, 0.0f), hits) == 0);
}

TEST_CASE("A body whose entity was destroyed since the last step is not found", "[physics][queries]")
{
    PhysicsApp test;
    Entity box = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    Entity behind = AddStatic(test.Entities(), Math::Vec3(6.0f, 0.0f, 0.0f));
    test.Run(tick);

    test.Entities().DestroyEntity(box);

    const std::optional<RaycastHit> hit = WorldOf(test).Raycast(Math::Vec3(0.0f), east, 100.0f);
    INFO("a hit names a live entity, so the ray passes the destroyed one's body and hits the one behind");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == behind);
}

TEST_CASE("A destroyed entity has no body to ask about, even before the next step", "[physics][queries]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    test.Run(3.0);
    PhysicsWorld& world = WorldOf(test);
    INFO("before it is destroyed, the box rests asleep on the ground, so there is something to find");
    REQUIRE(world.IsSleeping(box));

    test.Entities().DestroyEntity(box);

    INFO("a destroyed entity's body is reported asleep");
    REQUIRE_FALSE(world.IsSleeping(box));
    INFO("a destroyed entity's body has bounds");
    REQUIRE_FALSE(world.Bounds(box).has_value());
    int contacts = 0;
    world.ForEachContact(box, [&](const Contact&) { ++contacts; });
    INFO("a destroyed entity's body is reported touching the ground");
    REQUIRE(contacts == 0);
}

TEST_CASE("Queries take convex hull and triangle-mesh shapes, scaled by the pose", "[physics][queries]")
{
    PhysicsApp test;
    Entity box = AddStatic(test.Entities(), Math::Vec3(5.0f, 0.0f, 0.0f));
    // The corners of a unit cube, wound outwards as twelve triangles.
    std::array<Math::Vec3, 8> corners;
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        corners[i] = Math::Vec3((i & 1u) ? 0.5f : -0.5f, (i & 2u) ? 0.5f : -0.5f, (i & 4u) ? 0.5f : -0.5f);
    }
    const std::array<std::uint32_t, 36> triangles = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                                     2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
    const CollisionMesh cube = test.physics.CreateCollisionMesh(corners, triangles);
    test.Run(tick);

    const Transform atOrigin(Math::Vec3(0.0f), identity, Math::Vec3(1.0f));
    const std::optional<ShapeCastHit> hullHit = WorldOf(test).ShapeCast(ConvexHullShape{cube}, atOrigin, east, 10.0f);
    INFO("a unit hull swept along +x from the origin does not touch the box (its face at x = 4.5) after 4 m");
    REQUIRE(hullHit.has_value());
    REQUIRE(hullHit->body == box);
    REQUIRE(hullHit->distance == Approx(4.0f).margin(0.01f));

    const Transform doubled(Math::Vec3(0.0f), identity, Math::Vec3(2.0f));
    const std::optional<ShapeCastHit> scaledHit = WorldOf(test).ShapeCast(ConvexHullShape{cube}, doubled, east, 10.0f);
    INFO("the hull scaled by 2 does not touch the box after 3.5 m");
    REQUIRE(scaledHit.has_value());
    REQUIRE(scaledHit->distance == Approx(3.5f).margin(0.01f));

    std::array<OverlapHit, 4> hits;
    const Transform overBox(Math::Vec3(5.5f, 0.0f, 0.0f), identity, Math::Vec3(1.0f));
    INFO("a triangle-mesh cube placed half over the box does not overlap it");
    REQUIRE(WorldOf(test).Overlap(MeshShape{cube}, overBox, hits) == 1);
    REQUIRE(hits[0].body == box);
}
