// Every kind of collision shape, and the collision meshes hull and mesh shapes refer to, through the frame seam.

#include <array>
#include <cstddef>
#include <cstdint>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsProbe.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr float quarterTurn = 1.5707964f;
} // namespace

TEST_CASE("A dynamic capsule falls and comes to rest on its side", "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity capsule = AddBody(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f), CapsuleShape{1.0f, 0.25f});
    // Lying along x: a capsule standing on its end would topple anyway, so the test starts it lying down.
    capsule.GetComponent<Transform>().rot = Math::AngleAxis(quarterTurn, Math::Vec3(0.0f, 0.0f, 1.0f));

    test.Run(3.0);

    INFO("a capsule of radius 0.25 lying down rests with its centre 0.25 above the ground, it is at "
         << PositionOf(capsule).y);
    REQUIRE(PositionOf(capsule).y == Approx(0.25f).margin(restSlop));
}

namespace
{
    // A cube with its corners at +-half, wound outwards: 8 vertices and 12 triangles.
    struct CubeMesh
    {
        explicit CubeMesh(float half)
        {
            for (std::size_t i = 0; i < vertices.size(); ++i)
            {
                vertices[i] = Math::Vec3((i & 1u) ? half : -half, (i & 2u) ? half : -half, (i & 4u) ? half : -half);
            }
        }

        std::array<Math::Vec3, 8> vertices;
        std::array<std::uint32_t, 36> indices = {
            0, 2, 1, 1, 2, 3, // -z
            4, 5, 6, 5, 7, 6, // +z
            0, 1, 4, 1, 5, 4, // -y
            2, 6, 3, 3, 6, 7, // +y
            0, 4, 2, 2, 4, 6, // -x
            1, 3, 5, 3, 7, 5, // +x
        };
    };
} // namespace

TEST_CASE("Convex hull colliders made from one collision mesh fall and rest on the ground", "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    const CubeMesh cube(0.5f);
    const CollisionMesh mesh = test.physics.CreateCollisionMesh(cube.vertices, cube.indices);
    Entity first = AddBody(test.Entities(), Math::Vec3(-2.0f, 3.0f, 0.0f), ConvexHullShape{mesh});
    Entity second = AddBody(test.Entities(), Math::Vec3(2.0f, 5.0f, 0.0f), ConvexHullShape{mesh});

    test.Run(3.0);

    INFO("the hull of a unit cube rests with its centre half a unit above the ground; the two bodies are at "
         << PositionOf(first).y << " and " << PositionOf(second).y);
    REQUIRE(PositionOf(first).y == Approx(0.5f).margin(restSlop));
    REQUIRE(PositionOf(second).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("A box falls onto a static triangle-mesh ground and rests on it", "[physics][shapes]")
{
    PhysicsApp test;
    // Two triangles making a 20 by 20 floor at y = 0, facing up.
    const std::array<Math::Vec3, 4> vertices = {Math::Vec3(-10.0f, 0.0f, -10.0f), Math::Vec3(10.0f, 0.0f, -10.0f),
                                                Math::Vec3(-10.0f, 0.0f, 10.0f), Math::Vec3(10.0f, 0.0f, 10.0f)};
    const std::array<std::uint32_t, 6> indices = {0, 2, 1, 1, 2, 3};
    const CollisionMesh floor = test.physics.CreateCollisionMesh(vertices, indices);
    Place(test.Entities(), Math::Vec3(0.0f)).AddComponent<Collider>(Collider{MeshShape{floor}});
    Entity box = AddBody(test.Entities(), Math::Vec3(1.0f, 3.0f, 0.0f));

    test.Run(3.0);

    INFO("a unit box rests with its centre half a unit above the mesh floor, it is at " << PositionOf(box).y);
    REQUIRE(PositionOf(box).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("A dynamic triangle-mesh body falls and rests on the ground", "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    const CubeMesh cube(0.5f);
    Entity body = AddBody(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f),
                          MeshShape{test.physics.CreateCollisionMesh(cube.vertices, cube.indices)});

    test.Run(3.0);

    INFO("a unit cube's triangles rest with their centre half a unit above the ground, it is at "
         << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("Releasing a collision mesh in use leaves the bodies using it inactive, with a warning", "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    const CubeMesh cube(0.5f);
    const CollisionMesh mesh = test.physics.CreateCollisionMesh(cube.vertices, cube.indices);
    Entity platform = Place(test.Entities(), Math::Vec3(0.0f, 2.5f, 0.0f));
    platform.AddComponent<Collider>(Collider{MeshShape{mesh}});
    Entity falling = AddBody(test.Entities(), Math::Vec3(4.0f, 20.0f, 0.0f), ConvexHullShape{mesh});
    Entity resting = AddBody(test.Entities(), Math::Vec3(0.0f, 4.0f, 0.0f));
    test.Run(1.0);
    INFO("before the release the box rests on the static mesh platform (top at y = 3), it is at "
         << PositionOf(resting).y);
    REQUIRE(PositionOf(resting).y == Approx(3.5f).margin(restSlop));
    const float fallenTo = PositionOf(falling).y;
    INFO("the hull body is still falling, at " << fallenTo);
    REQUIRE(fallenTo < 20.0f);
    REQUIRE(fallenTo > 10.0f);

    REQUIRE(test.physics.ReleaseCollisionMesh(mesh));
    test.warnings.clear();
    test.Run(1.0);

    INFO("the platform's body is gone, so the box it held falls to the ground; it is at " << PositionOf(resting).y);
    REQUIRE(PositionOf(resting).y == Approx(0.5f).margin(restSlop));
    INFO("the hull body stops where it was: inactive, it neither falls nor collides");
    REQUIRE(PositionOf(falling).y == Approx(fallenTo).margin(0.0));
    INFO("each collider left without a shape warns once, not every tick");
    REQUIRE(test.warnings.size() == 2);

    falling.GetComponent<Collider>().shape = SphereShape{0.5f};
    test.Run(2.0);
    INFO("given a new shape, the inactive body is simulated again and falls to the ground");
    REQUIRE(PositionOf(falling).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("A released collision mesh handle is never handed out again, and a collider given one is inactive",
          "[physics][shapes]")
{
    PhysicsApp test;
    const CubeMesh cube(0.5f);
    const CollisionMesh released = test.physics.CreateCollisionMesh(cube.vertices, cube.indices);
    REQUIRE(test.physics.ReleaseCollisionMesh(released));

    const CollisionMesh next = test.physics.CreateCollisionMesh(cube.vertices, cube.indices);
    INFO("the next mesh gets a different handle from the released one");
    REQUIRE_FALSE(next == released);
    INFO("releasing a released handle again warns and fails");
    REQUIRE_FALSE(test.physics.ReleaseCollisionMesh(released));

    Entity body = AddBody(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f), ConvexHullShape{released});
    test.warnings.clear();
    test.Run(1.0);
    INFO("a collider referring to a released mesh has no body: it does not fall");
    REQUIRE(PositionOf(body).y == 5.0f);
    REQUIRE(test.warnings.size() == 1);
}

TEST_CASE("A collision mesh is not made from invalid data", "[physics][shapes]")
{
    PhysicsApp test;
    const std::array<Math::Vec3, 3> triangle = {Math::Vec3(0.0f), Math::Vec3(1.0f, 0.0f, 0.0f),
                                                Math::Vec3(0.0f, 0.0f, 1.0f)};
    const std::array<std::uint32_t, 3> outOfRange = {0, 1, 3};
    const std::array<std::uint32_t, 2> notTriangles = {0, 1};

    INFO("no vertices, an index past the last vertex, or indices that are not whole triangles give no mesh");
    REQUIRE(test.physics.CreateCollisionMesh({}, {}) == CollisionMesh{});
    REQUIRE(test.physics.CreateCollisionMesh(triangle, outOfRange) == CollisionMesh{});
    REQUIRE(test.physics.CreateCollisionMesh(triangle, notTriangles) == CollisionMesh{});
}

TEST_CASE("Editing a capsule's dimensions takes effect on the next tick", "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity capsule = AddBody(test.Entities(), Math::Vec3(0.0f, 0.25f, 0.0f), CapsuleShape{1.0f, 0.25f});
    capsule.GetComponent<Transform>().rot = Math::AngleAxis(quarterTurn, Math::Vec3(0.0f, 0.0f, 1.0f));
    test.Run(0.5);

    capsule.GetComponent<Collider>().shape.As<CapsuleShape>().radius = 1.0f;
    test.Run(2.0);

    INFO("a lying capsule of radius 1 rests with its centre 1 above the ground, it is at " << PositionOf(capsule).y);
    REQUIRE(PositionOf(capsule).y == Approx(1.0f).margin(restSlop));
}

TEST_CASE("A capsule's radius follows the larger of the entity's x and z scales", "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity capsule = Place(test.Entities(), Math::Vec3(0.0f, 3.0f, 0.0f), Math::Vec3(1.0f, 1.0f, 2.0f));
    capsule.GetComponent<Transform>().rot = Math::AngleAxis(quarterTurn, Math::Vec3(0.0f, 0.0f, 1.0f));
    capsule.AddComponent<RigidBody>();
    capsule.AddComponent<Collider>(Collider{CapsuleShape{1.0f, 0.25f}});

    test.Run(3.0);

    INFO("a capsule can only be scaled uniformly about its axis, so a z scale of 2 doubles its radius to 0.5 in every "
         "direction; lying down it rests at "
         << PositionOf(capsule).y);
    REQUIRE(PositionOf(capsule).y == Approx(0.5f).margin(restSlop));
}

TEST_CASE("Hulls and meshes take a non-uniform scale, and a scale change takes effect on the next tick",
          "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    const CubeMesh cube(0.5f);
    const CollisionMesh mesh = test.physics.CreateCollisionMesh(cube.vertices, cube.indices);
    Entity hull = Place(test.Entities(), Math::Vec3(-2.0f, 3.0f, 0.0f), Math::Vec3(1.0f, 3.0f, 1.0f));
    hull.AddComponent<RigidBody>();
    hull.AddComponent<Collider>(Collider{ConvexHullShape{mesh}});
    Entity triangles = AddBody(test.Entities(), Math::Vec3(2.0f, 3.0f, 0.0f), MeshShape{mesh});
    test.Run(2.0);
    INFO("a unit cube hull 3 tall rests with its centre 1.5 above the ground, it is at " << PositionOf(hull).y);
    REQUIRE(PositionOf(hull).y == Approx(1.5f).margin(restSlop));

    triangles.GetComponent<Transform>().scale = Math::Vec3(2.0f, 0.5f, 2.0f);
    test.Run(2.0);
    INFO("the triangle mesh scaled to half height rests with its centre 0.25 above the ground, it is at "
         << PositionOf(triangles).y);
    REQUIRE(PositionOf(triangles).y == Approx(0.25f).margin(restSlop));
}

TEST_CASE("A very thin box, or one with no thickness at all, is simulated without the backend asserting",
          "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity sheet = AddBody(test.Entities(), Math::Vec3(-2.0f, 2.0f, 0.0f), BoxShape{Math::Vec3(1.0f, 0.002f, 1.0f)});
    Entity flat = AddBody(test.Entities(), Math::Vec3(2.0f, 2.0f, 0.0f), BoxShape{Math::Vec3(1.0f, 0.0f, 1.0f)});
    Entity squashed = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 3.0f));
    squashed.GetComponent<Transform>().scale = Math::Vec3(1.0f, 0.0f, 1.0f);

    test.Run(3.0);

    INFO("each rests on the ground, its centre about its (tiny) half height above it: "
         << PositionOf(sheet).y << ", " << PositionOf(flat).y << ", " << PositionOf(squashed).y);
    REQUIRE(PositionOf(sheet).y == Approx(0.002f).margin(restSlop));
    REQUIRE(PositionOf(flat).y == Approx(0.0f).margin(restSlop));
    REQUIRE(PositionOf(squashed).y == Approx(0.0f).margin(restSlop));
}

TEST_CASE("Colliders with identical primitive shapes share one backend shape", "[physics][shapes]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    for (int i = 0; i < 20; ++i)
    {
        AddBody(test.Entities(),
                Math::Vec3(static_cast<float>(i % 5) * 1.5f, 1.0f + static_cast<float>(i / 5) * 1.5f, 0.0f));
        AddBody(test.Entities(), Math::Vec3(static_cast<float>(i % 5) * 1.5f, 1.0f, 3.0f), SphereShape{0.5f});
    }
    // A box of half extents 1 and a unit box scaled by 2 are the same shape once scale is baked in.
    AddBody(test.Entities(), Math::Vec3(-4.0f, 1.0f, -4.0f), BoxShape{Math::Vec3(1.0f)});
    Entity scaled = Place(test.Entities(), Math::Vec3(4.0f, 1.0f, -4.0f), Math::Vec3(2.0f));
    scaled.AddComponent<Collider>();
    test.Run(tick);

    const PhysicsWorld& world = *PhysicsWorld::Of(test.app.GetActiveScene());
    INFO("42 colliders hold four distinct shapes after scale (the ground, the unit box, the sphere and the box of half "
         "extents 1), so the backend holds four shapes");
    REQUIRE(PhysicsProbe::DistinctShapeCount(world) == 4);

    scaled.GetComponent<Transform>().scale = Math::Vec3(3.0f);
    test.Run(tick);
    INFO("rescaling the last one gives it a shape of its own, and the box of half extents 1 is still used once");
    REQUIRE(PhysicsProbe::DistinctShapeCount(world) == 5);
}
