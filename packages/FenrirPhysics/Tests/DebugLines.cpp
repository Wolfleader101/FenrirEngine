// Debug lines through the frame seam: a world's lines are switched on, frames run, and the lines its scene's DebugDraw
// shows are checked against what its bodies are known to be.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
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

    std::size_t CountColoured(std::span<const DrawnLine> lines, const Colour& colour)
    {
        return static_cast<std::size_t>(std::count_if(
            lines.begin(), lines.end(), [colour](const DrawnLine& line) { return SameColour(line.colour, colour); }));
    }

    std::vector<float> SortedLengths(std::span<const DrawnLine> lines)
    {
        std::vector<float> lengths;
        for (const DrawnLine& line : lines)
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
    REQUIRE(world.DebugDrawCategories() == PhysicsDebugCategories::None);
    INFO("so a world with bodies touching and moving has no lines");
    REQUIRE(DrawnLines(test.app).empty());
}

TEST_CASE(
    "A world's debug lines are drawn into its scene's DebugDraw in the Physics group, and last until the next tick",
    "[physics][debug]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Colliders);
    test.Run(tick);
    INFO("the ground's twelve edges are drawn");
    REQUIRE(DrawnLines(test.app).size() == 12);

    const auto ticks = [&test] { return test.app.GetTime().tickCount; };
    const auto stepped = ticks();
    test.app.RunFrame(tick * 0.25);
    INFO("a frame with no tick, and so no step, still shows them");
    REQUIRE(ticks() == stepped);
    REQUIRE(DrawnLines(test.app).size() == 12);

    test.app.RunFrame(tick * 0.8);
    INFO("the next tick's step draws them again rather than adding to them");
    REQUIRE(ticks() == stepped + 1);
    REQUIRE(DrawnLines(test.app).size() == 12);

    DebugDraw& draw = *test.app.GetActiveScene().GetResource<DebugDraw>();
    draw.HideGroup(PhysicsDebugGroup);
    REQUIRE(PhysicsDebugGroup == "Physics");
    test.Run(tick);
    INFO("hiding the Physics group hides them");
    REQUIRE(DrawnLines(test.app).empty());

    draw.ShowGroup(PhysicsDebugGroup);
    INFO("shown again with no step between, the hidden step's lines were still recorded");
    REQUIRE(DrawnLines(test.app).size() == 12);

    world.SetDebugDraw(PhysicsDebugCategories::None);
    const auto last = ticks();
    test.app.RunFrame(tick * 0.25);
    INFO("switched off, the last step's lines stay until the next tick");
    REQUIRE(ticks() == last);
    REQUIRE(DrawnLines(test.app).size() == 12);
    test.app.RunFrame(tick * 0.8);
    INFO("and are gone after it");
    REQUIRE(ticks() == last + 1);
    REQUIRE(DrawnLines(test.app).empty());
}

TEST_CASE("A world steps with its lines switched on in an app with no debug drawing", "[physics][debug]")
{
    App app(std::make_unique<SilentLogger>(), 1);
    Physics physics = Physics::Install(app, StepPosesOnly());
    AddGround(app.GetActiveScene().GetEntityList());
    app.RunFrame(tick);
    PhysicsWorld* world = PhysicsWorld::Of(app.GetActiveScene());
    REQUIRE(world != nullptr);
    world->SetDebugDraw(PhysicsDebugCategories::All);
    app.RunFrame(tick);

    INFO("there is no DebugDraw to draw into, and nothing fails");
    REQUIRE(app.GetActiveScene().GetResource<DebugDraw>() == nullptr);
    REQUIRE(world->DebugDrawCategories() == PhysicsDebugCategories::All);
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
    world.SetDebugDraw(PhysicsDebugCategories::Colliders);
    INFO("lines are made by a step: none yet");
    REQUIRE(DrawnLines(test.app).empty());

    test.Run(tick);
    const std::vector<DrawnLine> lines = DrawnLines(test.app);
    INFO("a box has twelve edges");
    REQUIRE(lines.size() == 12);

    const Math::AABB bounds = world.Bounds(box).value();
    INFO("its bounds, turned, run 4 along x and 2 along z");
    REQUIRE(bounds.max.x - bounds.min.x == Approx(4.0f).margin(1.0e-3f));
    REQUIRE(bounds.max.z - bounds.min.z == Approx(2.0f).margin(1.0e-3f));
    for (const DrawnLine& line : lines)
    {
        INFO("every edge lies inside the box's bounds");
        REQUIRE(Inside(line.from, bounds));
        REQUIRE(Inside(line.to, bounds));
        INFO("and is drawn in the static colour");
        REQUIRE(SameColour(line.colour, PhysicsDebugColour::Static));
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

    const auto coloured = [&test](const Colour& colour) { return CountColoured(DrawnLines(test.app), colour); };
    const auto draw = [&](PhysicsDebugCategories categories) {
        world.SetDebugDraw(categories);
        test.Run(tick);
    };

    draw(PhysicsDebugCategories::Contacts);
    INFO("contacts alone: a cross and a normal for each contact, nothing else");
    REQUIRE(!DrawnLines(test.app).empty());
    REQUIRE(coloured(PhysicsDebugColour::Contact) == DrawnLines(test.app).size());
    REQUIRE(DrawnLines(test.app).size() % 4 == 0);

    draw(PhysicsDebugCategories::Bounds);
    INFO("bounds alone: twelve edges for each of the two bodies");
    REQUIRE(DrawnLines(test.app).size() == 24);
    REQUIRE(coloured(PhysicsDebugColour::Bounds) == 24);

    // Sent sliding, so it moves whether or not it has settled.
    box.GetComponent<RigidBody>().linearVelocity = Math::Vec3(3.0f, 0.0f, 0.0f);
    draw(PhysicsDebugCategories::Velocities);
    INFO("velocities alone: one line for the moving body, none for the static ground");
    REQUIRE(DrawnLines(test.app).size() == 1);
    REQUIRE(coloured(PhysicsDebugColour::Velocity) == 1);
    const DrawnLine velocity = DrawnLines(test.app)[0];
    INFO("it starts at the body and points along its velocity, as far as a tenth of a second takes it");
    REQUIRE(velocity.from.x == Approx(PositionOf(box).x).margin(1.0e-4f));
    REQUIRE(velocity.to.x - velocity.from.x == Approx(0.3f).margin(0.05f));

    draw(PhysicsDebugCategories::Colliders);
    INFO("colliders alone: the two boxes' edges, the ground static and the body dynamic");
    REQUIRE(DrawnLines(test.app).size() == 24);
    REQUIRE(coloured(PhysicsDebugColour::Static) == 12);
    REQUIRE(coloured(PhysicsDebugColour::Dynamic) == 12);

    draw(PhysicsDebugCategories::Colliders | PhysicsDebugCategories::Bounds);
    INFO("categories combine");
    REQUIRE(DrawnLines(test.app).size() == 48);

    draw(PhysicsDebugCategories::None);
    INFO("switched off, the next step clears them");
    REQUIRE(DrawnLines(test.app).empty());
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
    world.SetDebugDraw(PhysicsDebugCategories::Colliders);
    test.Run(3.0);
    REQUIRE(world.IsSleeping(box));

    const auto coloured = [&test](const Colour& colour) { return CountColoured(DrawnLines(test.app), colour); };
    INFO("dimmed is half as bright as sRGB counts it, mid grey's share of each channel, alpha unchanged");
    const Colour dimmed = PhysicsDebugColour::Dimmed(Colour{1.0f, 0.5f, 0.25f, 0.75f});
    REQUIRE(dimmed.r == Approx(Colour::Grey.r));
    REQUIRE(dimmed.g == Approx(0.5f * Colour::Grey.g));
    REQUIRE(dimmed.b == Approx(0.25f * Colour::Grey.b));
    REQUIRE(dimmed.a == 0.75f);
    INFO("the box at rest is asleep: its edges are dimmed");
    REQUIRE(coloured(PhysicsDebugColour::Dimmed(PhysicsDebugColour::Dynamic)) == 12);
    REQUIRE(coloured(PhysicsDebugColour::Dynamic) == 0);
    INFO("the ground is static, the paddle kinematic, and the trigger has the trigger colour");
    REQUIRE(coloured(PhysicsDebugColour::Static) == 12);
    REQUIRE(coloured(PhysicsDebugColour::Kinematic) +
                coloured(PhysicsDebugColour::Dimmed(PhysicsDebugColour::Kinematic)) ==
            12);
    REQUIRE(coloured(PhysicsDebugColour::Trigger) == 12);

    box.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f, 2.0f, 0.0f);
    test.Run(tick);
    INFO("woken, its edges are bright again");
    REQUIRE(coloured(PhysicsDebugColour::Dynamic) == 12);
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
    world.SetDebugDraw(PhysicsDebugCategories::Colliders);
    test.Run(tick);

    const std::array<Entity, 4> bodies = {sphere, capsule, hull, mesh};
    std::array<std::size_t, 4> counts{};
    for (const DrawnLine& line : DrawnLines(test.app))
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
    const auto extent = [&world, &test](const Entity& entity, int axis) {
        const Math::AABB bounds = world.Bounds(entity).value();
        float low = 1e9f;
        float high = -1e9f;
        for (const DrawnLine& line : DrawnLines(test.app))
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
    world.SetDebugDraw(PhysicsDebugCategories::Colliders);
    test.Run(tick);

    INFO("two unit boxes: twelve edges each");
    REQUIRE(DrawnLines(test.app).size() == 24);
    const Math::AABB above(Math::Vec3(-0.5f, 3.5f, -0.5f), Math::Vec3(0.5f, 4.5f, 0.5f));
    const Math::AABB below(Math::Vec3(-0.5f, -0.5f, -0.5f), Math::Vec3(0.5f, 0.5f, 0.5f));
    std::size_t inAbove = 0;
    std::size_t inBelow = 0;
    for (const DrawnLine& line : DrawnLines(test.app))
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
    const std::array<Colour, 6> jointColours = {
        PhysicsDebugColour::JointAX, PhysicsDebugColour::JointAY, PhysicsDebugColour::JointAZ,
        PhysicsDebugColour::JointBX, PhysicsDebugColour::JointBY, PhysicsDebugColour::JointBZ,
    };

    std::size_t JointLines(std::span<const DrawnLine> lines)
    {
        std::size_t count = 0;
        for (const Colour& colour : jointColours)
        {
            count += CountColoured(lines, colour);
        }
        return count;
    }

    // The line of `colour` that starts at `from`, if any.
    const DrawnLine* LineFrom(std::span<const DrawnLine> lines, const Colour& colour, const Math::Vec3& from)
    {
        for (const DrawnLine& line : lines)
        {
            if (SameColour(line.colour, colour) && glm::length(line.from - from) < 1.0e-3f)
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
    world.SetDebugDraw(PhysicsDebugCategories::Joints);
    test.Run(0.5);

    const std::vector<DrawnLine> lines = DrawnLines(test.app);
    INFO("two joints, two frames each, three axes a frame: twelve lines and nothing else");
    REQUIRE(lines.size() == 12);
    for (std::size_t axis = 0; axis < jointColours.size(); ++axis)
    {
        INFO("one line of each axis colour for each joint: axis colour " << axis);
        REQUIRE(CountColoured(lines, jointColours[axis]) == 2);
    }

    // The hinge is to the world, so frameB is in world space; the fixed joint's frameA is on a stretched post.
    for (const Math::Vec3& anchor : {joints.hingeAnchor, joints.fixedAnchor})
    {
        INFO("at the joint at " << anchor.x << ", " << anchor.y << ", " << anchor.z);
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const DrawnLine* a = LineFrom(lines, jointColours[axis], anchor);
            const DrawnLine* b = LineFrom(lines, jointColours[axis + 3], anchor);
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

    const DrawnLine* hingeAxis = LineFrom(lines, PhysicsDebugColour::JointAX, joints.hingeAnchor);
    REQUIRE(hingeAxis != nullptr);
    const Math::Vec3 along = glm::normalize(hingeAxis->to - hingeAxis->from);
    INFO("a frame's X axis is drawn along its world X: the hinge's, turned to point up");
    REQUIRE(along.y == Approx(1.0f).margin(1.0e-3f));
}

TEST_CASE("Joints are drawn only when their category is on, and All includes it", "[physics][debug][joints]")
{
    STATIC_REQUIRE(Includes(PhysicsDebugCategories::All, PhysicsDebugCategories::Joints));

    PhysicsApp test;
    const TwoJoints joints(test.Entities());
    test.Run(tick);
    PhysicsWorld& world = WorldOf(test);

    test.Run(tick);
    INFO("off by default: no lines");
    REQUIRE(DrawnLines(test.app).empty());

    world.SetDebugDraw(PhysicsDebugCategories::Colliders | PhysicsDebugCategories::Contacts |
                       PhysicsDebugCategories::Velocities | PhysicsDebugCategories::Bounds);
    test.Run(tick);
    INFO("every other category: lines, but none of a joint's");
    REQUIRE(!DrawnLines(test.app).empty());
    REQUIRE(JointLines(DrawnLines(test.app)) == 0);

    world.SetDebugDraw(PhysicsDebugCategories::All);
    test.Run(tick);
    INFO("All: the two joints' twelve axes among the rest");
    REQUIRE(JointLines(DrawnLines(test.app)) == 12);
}
