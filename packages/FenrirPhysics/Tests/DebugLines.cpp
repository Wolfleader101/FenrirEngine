// Debug lines through the frame seam: a world's lines are switched on, frames run, and the lines it made are checked
// against what its bodies are known to be.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Joints.hpp"
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

    Entity AddStatic(EntityList& entities, const Math::Vec3& position, CollisionShape shape = BoxShape{})
    {
        Entity entity = Place(entities, position);
        entity.AddComponent<Collider>(Collider{shape});
        return entity;
    }

    bool Inside(const Math::Vec3& point, const Math::AABB& box, float margin = 1.0e-3f)
    {
        return point.x >= box.min.x - margin && point.x <= box.max.x + margin && point.y >= box.min.y - margin &&
               point.y <= box.max.y + margin && point.z >= box.min.z - margin && point.z <= box.max.z + margin;
    }

    std::vector<float> SortedLengths(std::span<const DebugLine> lines)
    {
        std::vector<float> lengths;
        for (const DebugLine& line : lines)
        {
            lengths.push_back(glm::length(line.to - line.from));
        }
        std::sort(lengths.begin(), lengths.end());
        return lengths;
    }
} // namespace

TEST_CASE("A world draws no debug lines until they are switched on", "[physics][debug]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    test.Run(0.5);

    PhysicsWorld& world = WorldOf(test);
    INFO("debug drawing is off by default");
    REQUIRE(world.DebugDrawCategories() == DebugDraw::None);
    INFO("so a world with bodies touching and moving has no lines");
    REQUIRE(world.DebugLines().empty());
}

TEST_CASE("A box collider is drawn as its twelve edges, inside its bounds, in its motion type's colour",
          "[physics][debug]")
{
    PhysicsApp test;
    // Two by one by four, turned a quarter turn about y, so its pose shows in where the lines are.
    Entity box = AddStatic(test.Entities(), Math::Vec3(3.0f, 1.0f, -2.0f), BoxShape{Math::Vec3(1.0f, 0.5f, 2.0f)});
    box.GetComponent<Transform>().rot = Math::AngleAxis(0.5f * 3.14159265f, Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(DebugDraw::Colliders);
    INFO("lines are made by a step: none yet");
    REQUIRE(world.DebugLines().empty());

    test.Run(tick);
    const std::span<const DebugLine> lines = world.DebugLines();
    INFO("a box has twelve edges");
    REQUIRE(lines.size() == 12);

    const Math::AABB bounds = world.Bounds(box).value();
    INFO("its bounds, turned, run 4 along x and 2 along z");
    REQUIRE(bounds.max.x - bounds.min.x == Approx(4.0f).margin(1.0e-3f));
    REQUIRE(bounds.max.z - bounds.min.z == Approx(2.0f).margin(1.0e-3f));
    for (const DebugLine& line : lines)
    {
        INFO("every edge lies inside the box's bounds");
        REQUIRE(Inside(line.from, bounds));
        REQUIRE(Inside(line.to, bounds));
        INFO("and is drawn in the static colour");
        REQUIRE(line.colour == DebugColour::Static);
    }

    const std::vector<float> lengths = SortedLengths(lines);
    INFO("four edges each 1, 2 and 4 long");
    for (std::size_t i = 0; i < 12; ++i)
    {
        const float expected = i < 4 ? 1.0f : (i < 8 ? 2.0f : 4.0f);
        REQUIRE(lengths[i] == Approx(expected).margin(1.0e-4f));
    }
}

TEST_CASE("Each category of debug lines is drawn only when it is switched on", "[physics][debug]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // Resting on the ground: touching it.
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);

    const auto coloured = [&world](std::uint32_t colour) {
        return static_cast<std::size_t>(
            std::count_if(world.DebugLines().begin(), world.DebugLines().end(),
                          [colour](const DebugLine& line) { return line.colour == colour; }));
    };
    const auto draw = [&](DebugDraw categories) {
        world.SetDebugDraw(categories);
        test.Run(tick);
    };

    draw(DebugDraw::Contacts);
    INFO("contacts alone: a cross and a normal for each contact, nothing else");
    REQUIRE(!world.DebugLines().empty());
    REQUIRE(coloured(DebugColour::Contact) == world.DebugLines().size());
    REQUIRE(world.DebugLines().size() % 4 == 0);

    draw(DebugDraw::Bounds);
    INFO("bounds alone: twelve edges for each of the two bodies");
    REQUIRE(world.DebugLines().size() == 24);
    REQUIRE(coloured(DebugColour::Bounds) == 24);

    // Sent sliding, so it moves whether or not it has settled.
    box.GetComponent<RigidBody>().linearVelocity = Math::Vec3(3.0f, 0.0f, 0.0f);
    draw(DebugDraw::Velocities);
    INFO("velocities alone: one line for the moving body, none for the static ground");
    REQUIRE(world.DebugLines().size() == 1);
    REQUIRE(coloured(DebugColour::Velocity) == 1);
    const DebugLine& velocity = world.DebugLines()[0];
    INFO("it starts at the body and points along its velocity, as far as a tenth of a second takes it");
    REQUIRE(velocity.from.x == Approx(PositionOf(box).x).margin(1.0e-4f));
    REQUIRE(velocity.to.x - velocity.from.x == Approx(0.3f).margin(0.05f));

    draw(DebugDraw::Colliders);
    INFO("colliders alone: the two boxes' edges, the ground static and the body dynamic");
    REQUIRE(world.DebugLines().size() == 24);
    REQUIRE(coloured(DebugColour::Static) == 12);
    REQUIRE(coloured(DebugColour::Dynamic) == 12);

    draw(DebugDraw::Colliders | DebugDraw::Bounds);
    INFO("categories combine");
    REQUIRE(world.DebugLines().size() == 48);

    draw(DebugDraw::None);
    INFO("switched off, the next step clears them");
    REQUIRE(world.DebugLines().empty());
}

TEST_CASE("A sleeping body's collider is drawn dimmed; kinematic bodies and triggers have their own colours",
          "[physics][debug]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 0.5f, 0.0f));
    AddBody(test.Entities(), Math::Vec3(5.0f, 2.0f, 0.0f), BoxShape{}, MotionType::Kinematic);
    Entity trigger = AddStatic(test.Entities(), Math::Vec3(-5.0f, 2.0f, 0.0f));
    trigger.AddComponent<Trigger>();
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(DebugDraw::Colliders);
    test.Run(3.0);
    REQUIRE(world.IsSleeping(box));

    const auto coloured = [&world](std::uint32_t colour) {
        return static_cast<std::size_t>(
            std::count_if(world.DebugLines().begin(), world.DebugLines().end(),
                          [colour](const DebugLine& line) { return line.colour == colour; }));
    };
    INFO("dimmed is half as bright, as opaque");
    REQUIRE(DebugColour::Dimmed(0xFF8040FFu) == 0x7F4020FFu);
    INFO("the box at rest is asleep: its edges are dimmed");
    REQUIRE(coloured(DebugColour::Dimmed(DebugColour::Dynamic)) == 12);
    REQUIRE(coloured(DebugColour::Dynamic) == 0);
    INFO("the ground is static, the paddle kinematic, and the trigger has the trigger colour");
    REQUIRE(coloured(DebugColour::Static) == 12);
    REQUIRE(coloured(DebugColour::Kinematic) + coloured(DebugColour::Dimmed(DebugColour::Kinematic)) == 12);
    REQUIRE(coloured(DebugColour::Trigger) == 12);

    box.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f, 2.0f, 0.0f);
    test.Run(tick);
    INFO("woken, its edges are bright again");
    REQUIRE(coloured(DebugColour::Dynamic) == 12);
}

TEST_CASE("Spheres, capsules, hulls and meshes are drawn inside their bounds", "[physics][debug]")
{
    PhysicsApp test;
    // A unit cube about the origin, as a hull and as a mesh of its twelve triangles.
    const std::vector<Math::Vec3> corners = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
                                             {0.5f, 0.5f, -0.5f},   {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
                                             {-0.5f, 0.5f, 0.5f},   {0.5f, 0.5f, 0.5f}};
    const std::vector<std::uint32_t> indices = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                                2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
    const CollisionMesh cube = test.physics.CreateCollisionMesh(corners, indices);

    Entity sphere = AddStatic(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f), SphereShape{0.5f});
    Entity capsule = AddStatic(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f), CapsuleShape{1.0f, 0.5f});
    // Stretched, so the hull's and mesh's scale shows in their lines.
    Entity hull = Place(test.Entities(), Math::Vec3(6.0f, 0.0f, 0.0f), Math::Vec3(2.0f, 1.0f, 1.0f));
    hull.AddComponent<Collider>(Collider{ConvexHullShape{cube}});
    Entity mesh = Place(test.Entities(), Math::Vec3(9.0f, 0.0f, 0.0f), Math::Vec3(1.0f, 3.0f, 1.0f));
    mesh.AddComponent<Collider>(Collider{MeshShape{cube}});
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(DebugDraw::Colliders);
    test.Run(tick);

    const std::array<Entity, 4> bodies = {sphere, capsule, hull, mesh};
    std::array<std::size_t, 4> counts{};
    for (const DebugLine& line : world.DebugLines())
    {
        // Each line belongs to the body whose bounds hold it: the bodies are well apart.
        bool placed = false;
        for (std::size_t i = 0; i < bodies.size() && !placed; ++i)
        {
            const Math::AABB bounds = world.Bounds(bodies[i]).value();
            if (Inside(line.from, bounds, 0.01f) && Inside(line.to, bounds, 0.01f))
            {
                ++counts[i];
                placed = true;
            }
        }
        INFO("every line lies inside one body's bounds; this one runs from x = " << line.from.x << " to " << line.to.x);
        REQUIRE(placed);
    }
    INFO("the sphere and capsule are drawn with many segments");
    REQUIRE(counts[0] > 12);
    REQUIRE(counts[1] > 12);
    INFO("the hull of a cube has twelve edges; the mesh three for each of its twelve triangles");
    REQUIRE(counts[2] == 12);
    REQUIRE(counts[3] == 36);

    INFO("the hull is drawn stretched, two units along x, and the mesh three units along y");
    const auto extent = [&world](const Entity& entity, int axis) {
        const Math::AABB bounds = world.Bounds(entity).value();
        float low = 1e9f;
        float high = -1e9f;
        for (const DebugLine& line : world.DebugLines())
        {
            if (Inside(line.from, bounds, 0.01f) && Inside(line.to, bounds, 0.01f))
            {
                low = std::min({low, line.from[axis], line.to[axis]});
                high = std::max({high, line.from[axis], line.to[axis]});
            }
        }
        return high - low;
    };
    REQUIRE(extent(hull, 0) == Approx(2.0f).margin(1.0e-3f));
    REQUIRE(extent(mesh, 1) == Approx(3.0f).margin(1.0e-3f));
}

TEST_CASE("Each collider of a compound body is drawn at its own world pose", "[physics][debug]")
{
    PhysicsApp test;
    Entity body = Place(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Kinematic});
    for (const float x : {-2.0f, 2.0f})
    {
        Entity child = Place(test.Entities(), Math::Vec3(x, 0.0f, 0.0f));
        child.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
        body.AddChild(child);
    }
    // A quarter turn about z: the children, two units either side along x, are drawn above and below the body.
    body.GetComponent<Transform>().rot = Math::AngleAxis(0.5f * 3.14159265f, Math::Vec3(0.0f, 0.0f, 1.0f));
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(DebugDraw::Colliders);
    test.Run(tick);

    INFO("two unit boxes: twelve edges each");
    REQUIRE(world.DebugLines().size() == 24);
    const Math::AABB above(Math::Vec3(-0.5f, 3.5f, -0.5f), Math::Vec3(0.5f, 4.5f, 0.5f));
    const Math::AABB below(Math::Vec3(-0.5f, -0.5f, -0.5f), Math::Vec3(0.5f, 0.5f, 0.5f));
    std::size_t inAbove = 0;
    std::size_t inBelow = 0;
    for (const DebugLine& line : world.DebugLines())
    {
        inAbove += Inside(line.from, above) && Inside(line.to, above) ? 1 : 0;
        inBelow += Inside(line.from, below) && Inside(line.to, below) ? 1 : 0;
    }
    INFO("one box is drawn centred two units above the body, the other two below");
    REQUIRE(inAbove == 12);
    REQUIRE(inBelow == 12);
}

namespace
{
    // The colours of a joint's axes: frameA's X, Y, Z, then frameB's.
    const std::array<std::uint32_t, 6> jointColours = {
        DebugColour::JointAX, DebugColour::JointAY, DebugColour::JointAZ,
        DebugColour::JointBX, DebugColour::JointBY, DebugColour::JointBZ,
    };

    std::size_t CountColoured(std::span<const DebugLine> lines, std::uint32_t colour)
    {
        return static_cast<std::size_t>(std::count_if(
            lines.begin(), lines.end(), [colour](const DebugLine& line) { return line.colour == colour; }));
    }

    std::size_t JointLines(std::span<const DebugLine> lines)
    {
        std::size_t count = 0;
        for (const std::uint32_t colour : jointColours)
        {
            count += CountColoured(lines, colour);
        }
        return count;
    }

    // The line of `colour` that starts at `from`, if any.
    const DebugLine* LineFrom(std::span<const DebugLine> lines, std::uint32_t colour, const Math::Vec3& from)
    {
        for (const DebugLine& line : lines)
        {
            if (line.colour == colour && glm::length(line.from - from) < 1.0e-3f)
            {
                return &line;
            }
        }
        return nullptr;
    }

    // Two joints whose rest poses are known: a box hung from the world by a hinge whose axis is world y, and a box held
    // by a fixed joint to the corner of a static post stretched to twice its width.
    struct TwoJoints
    {
        Math::Vec3 hingeAnchor = Math::Vec3(0.0f, 10.0f, 0.0f);
        Math::Quat hingeRotation = Math::AngleAxis(0.5f * 3.14159265f, Math::Vec3(0.0f, 0.0f, 1.0f));
        Math::Vec3 fixedAnchor = Math::Vec3(6.0f, 0.5f, 0.5f);

        explicit TwoJoints(EntityList& entities)
        {
            Entity hung = AddBody(entities, hingeAnchor - Math::Vec3(0.0f, 0.5f, 0.0f));
            const JointFrames hinge = FrameFromWorld(hung, Entity(), JointFrame{hingeAnchor, hingeRotation});
            entities.CreateEntity().AddComponent<Joint>(Joint{.bodyA = hung,
                                                              .bodyB = Entity(),
                                                              .frameA = hinge.frameA,
                                                              .frameB = hinge.frameB,
                                                              .kind = HingeJoint{}});

            Entity post = Place(entities, Math::Vec3(5.0f, 0.0f, 0.0f), Math::Vec3(2.0f, 1.0f, 1.0f));
            post.AddComponent<Collider>(Collider{BoxShape{}});
            Entity held = AddBody(entities, fixedAnchor + Math::Vec3(1.0f, 0.5f, 0.0f));
            const JointFrames fixed = FrameFromWorld(post, held, JointFrame{fixedAnchor});
            entities.CreateEntity().AddComponent<Joint>(Joint{
                .bodyA = post, .bodyB = held, .frameA = fixed.frameA, .frameB = fixed.frameB, .kind = FixedJoint{}});
        }
    };
} // namespace

TEST_CASE("Each joint is drawn as its two frames' axes, coinciding at its rest pose", "[physics][debug][joints]")
{
    PhysicsApp test;
    const TwoJoints joints(test.Entities());
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(DebugDraw::Joints);
    test.Run(0.5);

    const std::span<const DebugLine> lines = world.DebugLines();
    INFO("two joints, two frames each, three axes a frame: twelve lines and nothing else");
    REQUIRE(lines.size() == 12);
    for (const std::uint32_t colour : jointColours)
    {
        INFO("one line of each axis colour for each joint: colour " << std::hex << colour);
        REQUIRE(CountColoured(lines, colour) == 2);
    }

    // The hinge is to the world, so frameB is in world space; the fixed joint's frameA is on a stretched post.
    for (const Math::Vec3& anchor : {joints.hingeAnchor, joints.fixedAnchor})
    {
        INFO("at the joint at " << anchor.x << ", " << anchor.y << ", " << anchor.z);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const DebugLine* a = LineFrom(lines, jointColours[axis], anchor);
            const DebugLine* b = LineFrom(lines, jointColours[axis + 3], anchor);
            INFO("both frames' axis " << axis << " start at the anchor");
            REQUIRE(a != nullptr);
            REQUIRE(b != nullptr);
            const Math::Vec3 alongA = a->to - a->from;
            const Math::Vec3 alongB = b->to - b->from;
            INFO("and point the same way, frameA's longer than frameB's");
            REQUIRE(glm::length(glm::normalize(alongA) - glm::normalize(alongB)) < 1.0e-3f);
            REQUIRE(glm::length(alongA) > glm::length(alongB));
        }
    }

    const DebugLine* hingeAxis = LineFrom(lines, DebugColour::JointAX, joints.hingeAnchor);
    REQUIRE(hingeAxis != nullptr);
    const Math::Vec3 along = glm::normalize(hingeAxis->to - hingeAxis->from);
    INFO("a frame's X axis is drawn along its world X: the hinge's, turned to point up");
    REQUIRE(along.y == Approx(1.0f).margin(1.0e-3f));
}

TEST_CASE("Joints are drawn only when their category is on, and All includes it", "[physics][debug][joints]")
{
    STATIC_REQUIRE(Includes(DebugDraw::All, DebugDraw::Joints));

    PhysicsApp test;
    const TwoJoints joints(test.Entities());
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);

    test.Run(tick);
    INFO("off by default: no lines");
    REQUIRE(world.DebugLines().empty());

    world.SetDebugDraw(DebugDraw::Colliders | DebugDraw::Contacts | DebugDraw::Velocities | DebugDraw::Bounds);
    test.Run(tick);
    INFO("every other category: lines, but none of a joint's");
    REQUIRE(!world.DebugLines().empty());
    REQUIRE(JointLines(world.DebugLines()) == 0);

    world.SetDebugDraw(DebugDraw::All);
    test.Run(tick);
    INFO("All: the two joints' twelve axes among the rest");
    REQUIRE(JointLines(world.DebugLines()) == 12);
}
