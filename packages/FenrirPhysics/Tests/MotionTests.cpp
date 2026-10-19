// Motion tests through the frame seam: bodies made by running frames, then a shape's moves and overlaps asked of the
// scene's physics world.

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

    // Static bodies stay where they are put, so motion tests against them have exact expected values.
    Entity AddStatic(EntityList& entities, const Math::Vec3& position, CollisionShape shape = BoxShape{})
    {
        Entity entity = Place(entities, position);
        entity.AddComponent<Collider>(Collider{shape});
        return entity;
    }

    Transform At(const Math::Vec3& position)
    {
        return Transform(position, identity, Math::Vec3(1.0f));
    }

    // The gap MoveCast leaves between a moved shape and what blocked it, measured along the blocking surface's normal.
    constexpr float skin = 0.02f;
} // namespace

TEST_CASE("MoveCast stops a shape the skin's gap short of a wall", "[physics][motion]")
{
    PhysicsApp test;
    Entity wall = AddStatic(test.Entities(), Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(tick);

    const std::optional<MoveCastHit> hit =
        WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(0.0f)), Math::Vec3(10.0f, 0.0f, 0.0f));

    INFO("a unit box moved 10 m along +x from the origin was not blocked by the wall at x = 5");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == wall);
    REQUIRE(hit->collider == wall);
    INFO("it touches the wall's face after 4 m, so it can move 4 m less the skin; it can move " << hit->distance);
    REQUIRE(hit->distance == Approx(4.0f - skin).margin(1e-3f));
    INFO("the point is not where the box's face meets the wall's, x = 4.5; it is at " << hit->point.x);
    REQUIRE(hit->point.x == Approx(4.5f).margin(1e-3f));
    INFO("the wall's normal does not point back at the box");
    REQUIRE(hit->normal.x == Approx(-1.0f).margin(1e-3f));
    REQUIRE(hit->normal.y == Approx(0.0f).margin(1e-3f));
}

TEST_CASE("MoveCast finds nothing when the whole motion is clear", "[physics][motion]")
{
    PhysicsApp test;
    AddStatic(test.Entities(), Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(tick);

    INFO("a unit box moved 3 m towards a wall it would touch after 4 m was blocked");
    REQUIRE_FALSE(WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(0.0f)), Math::Vec3(3.0f, 0.0f, 0.0f)).has_value());
    INFO("a box moved 10 m away from the wall was blocked");
    REQUIRE_FALSE(WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(0.0f)), Math::Vec3(-10.0f, 0.0f, 0.0f)).has_value());
}

TEST_CASE("MoveCast keeps the skin's gap from a body just past the end of the motion", "[physics][motion]")
{
    PhysicsApp test;
    Entity wall = AddStatic(test.Entities(), Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(tick);

    const std::optional<MoveCastHit> hit =
        WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(0.0f)), Math::Vec3(3.99f, 0.0f, 0.0f));
    INFO("a box moved 3.99 m, ending 0.01 m from the wall, was not blocked short of the skin's gap");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == wall);
    REQUIRE(hit->distance == Approx(4.0f - skin).margin(1e-3f));
}

TEST_CASE("MoveCast lets a shape resting on the ground slide along it", "[physics][motion]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    test.Run(tick);

    INFO("a unit box sitting on the ground, touching it, was stuck when moved along it");
    REQUIRE_FALSE(
        WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(0.0f, 0.5f, 0.0f)), Math::Vec3(5.0f, 0.0f, 0.0f)).has_value());
    INFO("one sunk 0.05 m into it was stuck");
    REQUIRE_FALSE(WorldOf(test)
                      .MoveCast(BoxShape{}, At(Math::Vec3(0.0f, 0.45f, 0.0f)), Math::Vec3(5.0f, 0.0f, 0.0f))
                      .has_value());
    INFO("one resting the skin's gap above it was stuck");
    REQUIRE_FALSE(WorldOf(test)
                      .MoveCast(BoxShape{}, At(Math::Vec3(0.0f, 0.5f + skin, 0.0f)), Math::Vec3(5.0f, 0.0f, 0.0f))
                      .has_value());
}

TEST_CASE("MoveCast ignores a body the shape starts in or touching, and blocks one it was moved against",
          "[physics][motion]")
{
    PhysicsApp test;
    AddStatic(test.Entities(), Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(tick);
    const Math::Vec3 motion(2.0f, 0.0f, 0.0f);

    INFO("a box touching the wall's face was blocked moving into it");
    REQUIRE_FALSE(WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(4.0f, 0.0f, 0.0f)), motion).has_value());
    INFO("a box halfway into the wall was blocked moving on into it");
    REQUIRE_FALSE(WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(4.5f, 0.0f, 0.0f)), motion).has_value());

    const std::optional<MoveCastHit> first =
        WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(0.0f)), Math::Vec3(10.0f, 0.0f, 0.0f));
    REQUIRE(first.has_value());
    const std::optional<MoveCastHit> again =
        WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(first->distance, 0.0f, 0.0f)), motion);
    INFO("a box moved as far as a MoveCast allowed was not blocked moving on into the wall");
    REQUIRE(again.has_value());
    INFO("it could move on " << again->distance << " m instead of none");
    REQUIRE(again->distance == Approx(0.0f).margin(1e-3f));
}

TEST_CASE("MoveCast keeps the skin's gap along the body's normal, so a glancing hit stops further back",
          "[physics][motion]")
{
    PhysicsApp test;
    Entity ball = AddStatic(test.Entities(), Math::Vec3(5.0f, 0.8f, 0.0f), SphereShape{0.5f});
    test.Run(tick);

    const std::optional<MoveCastHit> hit =
        WorldOf(test).MoveCast(SphereShape{0.5f}, At(Math::Vec3(0.0f)), Math::Vec3(10.0f, 0.0f, 0.0f));

    INFO("a ball moved along +x was not blocked by a ball 0.8 m above its path");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == ball);
    // The centres are a radius sum (1 m) apart at x = 5 - 0.6, where the normal (-0.6, -0.8) is at 0.6 to the
    // motion: backing off the skin along the normal takes 0.02 / 0.6 along the motion.
    INFO("the normal is not from the blocking ball's centre to the moved one's; it is (" << hit->normal.x << ", "
                                                                                         << hit->normal.y << ")");
    REQUIRE(hit->normal.x == Approx(-0.6f).margin(1e-3f));
    REQUIRE(hit->normal.y == Approx(-0.8f).margin(1e-3f));
    INFO("it can move " << hit->distance << " m, not 4.4 m less 0.02 / 0.6");
    REQUIRE(hit->distance == Approx(4.4f - skin / 0.6f).margin(1e-3f));

    INFO("a ball passing 1.01 m from the other's centre, clear of it, was blocked");
    REQUIRE_FALSE(WorldOf(test)
                      .MoveCast(SphereShape{0.5f}, At(Math::Vec3(0.0f, -0.21f, 0.0f)), Math::Vec3(10.0f, 0.0f, 0.0f))
                      .has_value());
}

TEST_CASE("Penetrations lists the bodies a shape overlaps, deepest first, with the way out and how far",
          "[physics][motion]")
{
    PhysicsApp test;
    Entity ground = AddGround(test.Entities());
    // Its face at x = 0.3.
    Entity wall = AddStatic(test.Entities(), Math::Vec3(0.8f, 0.0f, 0.0f));
    test.Run(tick);

    std::array<PenetrationHit, 4> hits;
    // A unit box 0.1 m down into the ground and 0.2 m into the wall.
    const std::size_t count = WorldOf(test).Penetrations(BoxShape{}, At(Math::Vec3(0.0f, 0.4f, 0.0f)), hits);

    INFO("the box overlaps the ground and the wall; it listed " << count);
    REQUIRE(count == 2);
    INFO("the wall, 0.2 m deep, is not first");
    REQUIRE(hits[0].body == wall);
    REQUIRE(hits[0].collider == wall);
    INFO("its depth is " << hits[0].depth << " and its way out (" << hits[0].normal.x << ", " << hits[0].normal.y
                         << "), not 0.2 along -x");
    REQUIRE(hits[0].depth == Approx(0.2f).margin(1e-3f));
    REQUIRE(hits[0].normal.x == Approx(-1.0f).margin(1e-3f));
    REQUIRE(hits[0].normal.y == Approx(0.0f).margin(1e-3f));
    INFO("its deepest point is not on the box's face in the wall, x = 0.5");
    REQUIRE(hits[0].point.x == Approx(0.5f).margin(1e-3f));

    INFO("the ground, 0.1 m deep, is not second");
    REQUIRE(hits[1].body == ground);
    INFO("its depth is " << hits[1].depth << ", not 0.1 straight up");
    REQUIRE(hits[1].depth == Approx(0.1f).margin(1e-3f));
    REQUIRE(hits[1].normal.y == Approx(1.0f).margin(1e-3f));
    INFO("its deepest point is not on the box's bottom face, y = -0.1");
    REQUIRE(hits[1].point.y == Approx(-0.1f).margin(1e-3f));
}

TEST_CASE("Penetrations writes the deepest overlaps when the buffer is too short", "[physics][motion]")
{
    PhysicsApp test;
    // Made shallowest first, so the order written comes from depth: 0.1, 0.3 and 0.2 m into a box at the origin.
    AddStatic(test.Entities(), Math::Vec3(-0.9f, 0.0f, 0.0f));
    Entity deepest = AddStatic(test.Entities(), Math::Vec3(0.7f, 0.0f, 0.0f));
    Entity second = AddStatic(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.8f));
    test.Run(tick);

    std::array<PenetrationHit, 2> hits;
    const std::size_t count = WorldOf(test).Penetrations(BoxShape{}, At(Math::Vec3(0.0f)), hits);

    INFO("three bodies overlap the box but the buffer holds two; it wrote " << count);
    REQUIRE(count == 2);
    INFO("the two written are not the two deepest, in order");
    REQUIRE(hits[0].body == deepest);
    REQUIRE(hits[1].body == second);
    REQUIRE(hits[1].depth == Approx(0.2f).margin(1e-3f));
}

TEST_CASE("Penetrations lists a triangle mesh once, at its deepest", "[physics][motion]")
{
    PhysicsApp test;
    // Two triangles making a 20 by 20 floor at y = 0, facing up; a box over their shared diagonal overlaps both.
    const std::array<Math::Vec3, 4> vertices = {Math::Vec3(-10.0f, 0.0f, -10.0f), Math::Vec3(10.0f, 0.0f, -10.0f),
                                                Math::Vec3(-10.0f, 0.0f, 10.0f), Math::Vec3(10.0f, 0.0f, 10.0f)};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 1, 2, 3};
    Entity floor =
        AddStatic(test.Entities(), Math::Vec3(0.0f), MeshShape{test.physics.CreateCollisionMesh(vertices, indices)});
    test.Run(tick);

    std::array<PenetrationHit, 4> hits;
    const std::size_t count = WorldOf(test).Penetrations(BoxShape{}, At(Math::Vec3(0.0f, 0.4f, 0.0f)), hits);

    INFO("the floor was not listed once; it was listed " << count << " times");
    REQUIRE(count == 1);
    REQUIRE(hits[0].body == floor);
    INFO("the box is 0.1 m into the floor; the depth is " << hits[0].depth);
    REQUIRE(hits[0].depth == Approx(0.1f).margin(1e-3f));
    REQUIRE(hits[0].normal.y == Approx(1.0f).margin(1e-3f));
}

TEST_CASE("Penetrations finds nothing for a shape that is clear", "[physics][motion]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    test.Run(tick);

    std::array<PenetrationHit, 4> hits;
    INFO("a box resting on the ground, overlapping nothing, has a penetration");
    REQUIRE(WorldOf(test).Penetrations(BoxShape{}, At(Math::Vec3(0.0f, 0.5f + skin, 0.0f)), hits) == 0);
}

TEST_CASE("Depenetrate moves a shape out of one overlap, leaving the skin's gap", "[physics][motion]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    test.Run(tick);

    const Transform sunk = At(Math::Vec3(0.0f, 0.4f, 0.0f));
    const Math::Vec3 offset = WorldOf(test).Depenetrate(BoxShape{}, sunk);

    INFO("a unit box 0.1 m into the ground was not moved up 0.1 m and the skin; it was moved ("
         << offset.x << ", " << offset.y << ", " << offset.z << ")");
    REQUIRE(offset.x == Approx(0.0f).margin(1e-3f));
    REQUIRE(offset.y == Approx(0.1f + skin).margin(1e-3f));
    REQUIRE(offset.z == Approx(0.0f).margin(1e-3f));
    std::array<PenetrationHit, 4> hits;
    INFO("moved by the offset, it still overlaps the ground");
    REQUIRE(WorldOf(test).Penetrations(BoxShape{}, At(sunk.pos + offset), hits) == 0);
}

TEST_CASE("Depenetrate moves a shape out of two overlaps at once", "[physics][motion]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    AddStatic(test.Entities(), Math::Vec3(0.8f, 0.0f, 0.0f)); // its face at x = 0.3
    test.Run(tick);

    const Transform cornered = At(Math::Vec3(0.0f, 0.4f, 0.0f));
    const Math::Vec3 offset = WorldOf(test).Depenetrate(BoxShape{}, cornered);

    INFO("a unit box 0.1 m into the ground and 0.2 m into the wall was not moved out of both, the skin clear of "
         "each; it was moved ("
         << offset.x << ", " << offset.y << ", " << offset.z << ")");
    REQUIRE(offset.x == Approx(-0.2f - skin).margin(1e-3f));
    REQUIRE(offset.y == Approx(0.1f + skin).margin(1e-3f));
    REQUIRE(offset.z == Approx(0.0f).margin(1e-3f));
    std::array<PenetrationHit, 4> hits;
    INFO("moved by the offset, it still overlaps something");
    REQUIRE(WorldOf(test).Penetrations(BoxShape{}, At(cornered.pos + offset), hits) == 0);
}

TEST_CASE("Depenetrate leaves a shape that overlaps nothing where it is", "[physics][motion]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    test.Run(tick);

    const Math::Vec3 offset = WorldOf(test).Depenetrate(BoxShape{}, At(Math::Vec3(0.0f, 0.51f, 0.0f)));
    INFO("a box just clear of the ground was moved");
    REQUIRE(offset.x == 0.0f);
    REQUIRE(offset.y == 0.0f);
    REQUIRE(offset.z == 0.0f);
}

TEST_CASE("Motion tests scale the shape by the pose's scale, as a collider is", "[physics][motion]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    AddStatic(test.Entities(), Math::Vec3(5.0f, 3.0f, 0.0f)); // its face at x = 4.5
    test.Run(tick);
    const Math::Vec3 doubled(2.0f);

    const std::optional<MoveCastHit> hit = WorldOf(test).MoveCast(
        BoxShape{}, Transform(Math::Vec3(0.0f, 3.0f, 0.0f), identity, doubled), Math::Vec3(10.0f, 0.0f, 0.0f));
    INFO("a unit box scaled by 2 reaches 1 m from its centre, so it touches the wall after 3.5 m; it can move "
         << (hit ? hit->distance : -1.0f) << " m");
    REQUIRE(hit.has_value());
    REQUIRE(hit->distance == Approx(3.5f - skin).margin(1e-3f));

    // Its bottom 1 m below its centre: 0.1 m into the ground.
    const Transform sunk(Math::Vec3(0.0f, 0.9f, 0.0f), identity, doubled);
    std::array<PenetrationHit, 4> hits;
    REQUIRE(WorldOf(test).Penetrations(BoxShape{}, sunk, hits) == 1);
    INFO("the box scaled by 2 is 0.1 m into the ground; the depth is " << hits[0].depth);
    REQUIRE(hits[0].depth == Approx(0.1f).margin(1e-3f));
    INFO("Depenetrate did not move it up by the same 0.1 m and the skin");
    REQUIRE(WorldOf(test).Depenetrate(BoxShape{}, sunk).y == Approx(0.1f + skin).margin(1e-3f));
}

TEST_CASE("Motion tests find nothing for a triangle-mesh shape", "[physics][motion]")
{
    PhysicsApp test;
    AddStatic(test.Entities(), Math::Vec3(5.0f, 0.0f, 0.0f));
    // The corners of a unit cube, wound outwards as twelve triangles.
    std::array<Math::Vec3, 8> corners;
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        corners[i] = Math::Vec3((i & 1u) ? 0.5f : -0.5f, (i & 2u) ? 0.5f : -0.5f, (i & 4u) ? 0.5f : -0.5f);
    }
    const std::array<std::uint32_t, 36> triangles = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                                     2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
    const MeshShape cube{test.physics.CreateCollisionMesh(corners, triangles)};
    test.Run(tick);

    INFO("a triangle-mesh cube moved into the box was blocked");
    REQUIRE_FALSE(WorldOf(test).MoveCast(cube, At(Math::Vec3(0.0f)), Math::Vec3(10.0f, 0.0f, 0.0f)).has_value());
    std::array<PenetrationHit, 4> hits;
    const Transform overBox = At(Math::Vec3(4.8f, 0.0f, 0.0f));
    INFO("one placed half over the box found a penetration");
    REQUIRE(WorldOf(test).Penetrations(cube, overBox, hits) == 0);
    const Math::Vec3 offset = WorldOf(test).Depenetrate(cube, overBox);
    INFO("Depenetrate moved it");
    REQUIRE(offset.x == 0.0f);
    REQUIRE(offset.y == 0.0f);
    REQUIRE(offset.z == 0.0f);
}

TEST_CASE("A motion test filtered like a body passes what that body would pass", "[physics][motion][layers]")
{
    PhysicsApp test;
    // Debris in layer 1 that collides only with layer 1, then a wall in layer 0.
    Entity debris = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f));
    debris.AddComponent<CollisionFilter>(CollisionFilter{.layers = 1u << 1, .mask = 1u << 1});
    Entity wall = AddStatic(test.Entities(), Math::Vec3(6.0f, 0.0f, 0.0f));
    Entity mover = AddStatic(test.Entities(), Math::Vec3(0.0f));
    test.Run(tick);

    const CollisionFilter cf; // the mover's: layer 0, colliding with everything
    const QueryFilter likeMover{.mask = cf.mask, .layers = cf.layers, .ignoreBody = mover};
    const std::optional<MoveCastHit> hit =
        WorldOf(test).MoveCast(BoxShape{}, At(Math::Vec3(0.0f)), Math::Vec3(10.0f, 0.0f, 0.0f), likeMover);

    INFO("a box filtered like a body in layer 0 was blocked by itself or by debris that does not collide with layer 0");
    REQUIRE(hit.has_value());
    REQUIRE(hit->body == wall);
}
