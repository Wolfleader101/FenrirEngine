// Interpolation through the frame seam, with frames shorter than a tick: what game code sees in world transforms
// between ticks, and what Tick systems and physics's own diff see.

#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // A frame a little under a third of a tick: ticks land on different frames, with varied fractions left over.
    constexpr double shortFrame = tick * 0.3;

    // The fraction of a tick owed after the frame just run: how far between its last two step poses a body is shown.
    float FractionOf(const App& app)
    {
        return static_cast<float>(app.GetTime().accumulator / app.GetTime().tickRate);
    }

    Math::Vec3 Blend(const Math::Vec3& from, const Math::Vec3& to, float fraction)
    {
        return from + (to - from) * fraction;
    }

    using Poses = std::shared_ptr<std::vector<Transform>>;

    // Records an entity's world transform after each step: physics's PostTick system runs before systems added after
    // it, so these are the step poses.
    Poses RecordStepPoses(App& app, const Entity& entity)
    {
        auto poses = std::make_shared<std::vector<Transform>>();
        app.AddSequentialSystems(SchedulePriority::PostTick,
                                 {[poses, entity](App&) { poses->push_back(GetWorldTransform(entity)); }});
        return poses;
    }

    // Records the entity's world transform as Tick systems see it, each tick.
    Poses RecordTickPoses(App& app, const Entity& entity)
    {
        auto poses = std::make_shared<std::vector<Transform>>();
        app.AddSequentialSystems(SchedulePriority::Tick,
                                 {[poses, entity](App&) { poses->push_back(GetWorldTransform(entity)); }});
        return poses;
    }

    bool BitIdentical(const Transform& a, const Transform& b)
    {
        return std::memcmp(&a.pos, &b.pos, sizeof(Math::Vec3)) == 0 &&
               std::memcmp(&a.rot, &b.rot, sizeof(Math::Quat)) == 0;
    }

    // A box tumbling onto the ground, turning as it lands.
    Entity AddTumblingBox(EntityList& entities)
    {
        AddGround(entities);
        Entity box = AddBody(entities, Math::Vec3(0.1f, 2.0f, 0.0f));
        box.GetComponent<Transform>().rot = Math::AngleAxis(0.4f, Math::Normalized(Math::Vec3(1.0f, 1.0f, 0.0f)));
        return box;
    }
} // namespace

TEST_CASE("Between ticks a falling body is shown between its last two step poses", "[physics][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 20.0f, 0.0f));
    const Poses steps = RecordStepPoses(test.app, box);
    test.Run(0.25); // falling, and well past the first step

    int blended = 0;
    for (int frame = 0; frame < 40; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const float fraction = FractionOf(test.app);
        const Math::Vec3 from = (*steps)[steps->size() - 2].pos;
        const Math::Vec3 to = steps->back().pos;
        const Math::Vec3 expected = Blend(from, to, fraction);
        const Math::Vec3 shown = PositionOf(box);
        INFO("frame " << frame << ": " << fraction << " of the way from step pose y = " << from.y << " to y = " << to.y
                      << " is y = " << expected.y << ", shown at y = " << shown.y);
        REQUIRE(shown.y == Approx(expected.y).margin(1e-5));
        REQUIRE(shown.x == Approx(0.0f).margin(1e-5));
        if (fraction > 0.05f)
        {
            ++blended;
            REQUIRE(shown.y > to.y);
            REQUIRE(shown.y < from.y);
        }
    }
    INFO("most frames end part of the way through a tick");
    REQUIRE(blended > 20);
}

TEST_CASE("Tick systems and physics see the true pose, not the blended one", "[physics][interpolation]")
{
    // Interpolated, with frames shorter than a tick.
    PhysicsApp blended{PhysicsConfig{}};
    Entity box = AddTumblingBox(blended.Entities());
    const Poses steps = RecordStepPoses(blended.app, box);
    const Poses seenInTick = RecordTickPoses(blended.app, box);
    while (steps->size() < 120)
    {
        blended.app.RunFrame(shortFrame);
    }

    // The same scene with step poses only, one tick per frame.
    PhysicsApp stepped;
    Entity reference = AddTumblingBox(stepped.Entities());
    const Poses referenceSteps = RecordStepPoses(stepped.app, reference);
    stepped.Run(2.0);
    REQUIRE(referenceSteps->size() == 120);

    for (std::size_t i = 1; i < 120; ++i)
    {
        INFO("tick " << i << ": a Tick system sees the pose the step before left, not a blended one");
        REQUIRE(BitIdentical((*seenInTick)[i], (*steps)[i - 1]));
    }
    for (std::size_t i = 0; i < 120; ++i)
    {
        INFO("step " << i
                     << ": the simulation is bit for bit the one without interpolation (the diff never took a blended "
                        "pose for a teleport)");
        REQUIRE(BitIdentical((*steps)[i], (*referenceSteps)[i]));
    }
}

TEST_CASE("Writing a body's Transform in Update teleports it, with no blend across the jump",
          "[physics][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 20.0f, 0.0f));
    const Poses seenInTick = RecordTickPoses(test.app, box);
    bool teleport = false;
    test.app.AddSequentialSystems(SchedulePriority::Update, {[&](App&) {
                                      if (teleport)
                                      {
                                          box.GetComponent<Transform>().pos = Math::Vec3(5.0f, 30.0f, 0.0f);
                                          teleport = false;
                                      }
                                  }});
    test.Run(0.25);
    test.app.RunFrame(shortFrame); // part of the way through a tick, so the body is shown blended

    teleport = true;
    test.app.RunFrame(shortFrame);
    const std::size_t ticksBefore = seenInTick->size();

    for (int frame = 0; frame < 20; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const Math::Vec3 shown = PositionOf(box);
        INFO("frame " << frame << " after the write: shown at " << shown.x << ", " << shown.y
                      << "; it never blends from where it was (x = 0, y < 20) to where it was put (x = 5, y = 30)");
        REQUIRE(shown.x == 5.0f);
        REQUIRE(shown.y > 29.0f);
        REQUIRE(shown.y <= 30.0f);
    }
    REQUIRE(seenInTick->size() > ticksBefore);
    INFO("the first tick after the write sees the game's value, not the true pose from before it");
    REQUIRE(
        BitIdentical((*seenInTick)[ticksBefore], Transform(Math::Vec3(5.0f, 30.0f, 0.0f), identity, Math::Vec3(1.0f))));
}

TEST_CASE("Writing a body's Transform in Tick teleports it, with no blend across the jump", "[physics][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 20.0f, 0.0f));
    bool teleport = false;
    test.app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) {
                                      if (teleport)
                                      {
                                          box.GetComponent<Transform>().pos = Math::Vec3(5.0f, 30.0f, 0.0f);
                                          teleport = false;
                                      }
                                  }});
    test.Run(0.25);

    teleport = true;
    while (teleport)
    {
        test.app.RunFrame(shortFrame);
    }
    for (int frame = 0; frame < 20; ++frame)
    {
        const Math::Vec3 shown = PositionOf(box);
        INFO("frame " << frame << " after the write: shown at " << shown.x << ", " << shown.y);
        REQUIRE(shown.x == 5.0f);
        REQUIRE(shown.y > 29.0f);
        test.app.RunFrame(shortFrame);
    }
}

TEST_CASE("A new body is not blended from the origin", "[physics][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    AddBody(test.Entities(), Math::Vec3(3.0f, 0.0f, 0.0f)); // the world exists before the new body
    test.Run(0.25);

    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 10.0f, 0.0f));
    for (int frame = 0; frame < 20; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const Math::Vec3 shown = PositionOf(box);
        INFO("frame " << frame << " after it was made, shown at y = " << shown.y << ": falling from y = 10");
        REQUIRE(shown.y > 9.9f);
        REQUIRE(shown.y <= 10.0f);
    }
}

TEST_CASE("Without interpolation a body is shown at its step poses only", "[physics][interpolation]")
{
    auto showsStepPoses = [](PhysicsApp& test, const Entity& box) {
        const Poses steps = RecordStepPoses(test.app, box);
        test.Run(0.25);
        for (int frame = 0; frame < 40; ++frame)
        {
            test.app.RunFrame(shortFrame);
            INFO("frame " << frame << ": shown at y = " << PositionOf(box).y
                          << ", the last step left it at y = " << steps->back().pos.y);
            REQUIRE(BitIdentical(GetWorldTransform(box), steps->back()));
        }
    };

    SECTION("a body with interpolate = false")
    {
        PhysicsApp test{PhysicsConfig{}};
        Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 20.0f, 0.0f));
        box.GetComponent<RigidBody>().interpolate = false;
        showsStepPoses(test, box);
    }

    SECTION("every body, with interpolation off in PhysicsConfig")
    {
        PhysicsConfig config;
        config.interpolate = false;
        PhysicsApp test{config};
        Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 20.0f, 0.0f));
        showsStepPoses(test, box);
    }
}

TEST_CASE("Children of an interpolated body end up at consistent world transforms", "[physics][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    // A dynamic body under a dynamic parent, which simulates on its own in world space, and a mesh that does not.
    Entity parent = AddBody(test.Entities(), Math::Vec3(0.0f, 20.0f, 0.0f));
    Entity childBody = AddBody(test.Entities(), Math::Vec3(2.0f, 0.0f, 0.0f));
    childBody.GetComponent<RigidBody>().linearVelocity = Math::Vec3(0.0f, 3.0f, 0.0f); // not falling with its parent
    Entity mesh = Place(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f)); // no physics: moves with its parent
    parent.AddChild(childBody);
    parent.AddChild(mesh);
    const Poses childSteps = RecordStepPoses(test.app, childBody);
    test.Run(0.25);

    for (int frame = 0; frame < 40; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const float fraction = FractionOf(test.app);
        const Math::Vec3 expected = Blend((*childSteps)[childSteps->size() - 2].pos, childSteps->back().pos, fraction);
        const Math::Vec3 child = PositionOf(childBody);
        INFO("frame " << frame << ": the child body is shown " << fraction << " of the way between its own step poses, "
                      << "y = " << expected.y << "; it is at " << child.x << ", " << child.y);
        REQUIRE(child.y == Approx(expected.y).margin(1e-4));
        REQUIRE(child.x == Approx(expected.x).margin(1e-4));
        const Math::Vec3 parentShown = PositionOf(parent);
        INFO("the mesh keeps its place one unit above its blended parent");
        REQUIRE(PositionOf(mesh).y == Approx(parentShown.y + 1.0f).margin(1e-4));
    }
}

TEST_CASE("An inactive collider is not blended back to where it was made", "[physics][interpolation]")
{
    PhysicsApp test{PhysicsConfig{}};
    const std::array<Math::Vec3, 4> corners = {Math::Vec3(0.0f), Math::Vec3(1.0f, 0.0f, 0.0f),
                                               Math::Vec3(0.0f, 1.0f, 0.0f), Math::Vec3(0.0f, 0.0f, 1.0f)};
    const std::array<std::uint32_t, 12> faces = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    const CollisionMesh released = test.physics.CreateCollisionMesh(corners, faces);
    REQUIRE(test.physics.ReleaseCollisionMesh(released));
    Entity body = AddBody(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f), ConvexHullShape{released});
    test.Run(0.25); // inactive: its shape refers to a released mesh

    body.GetComponent<Transform>().pos = Math::Vec3(1.0f, 2.0f, 3.0f);
    for (int frame = 0; frame < 20; ++frame)
    {
        test.app.RunFrame(shortFrame);
        const Math::Vec3 shown = PositionOf(body);
        INFO("frame " << frame << ": game code moved it to 1, 2, 3; it is at " << shown.x << ", " << shown.y << ", "
                      << shown.z);
        REQUIRE(shown == Math::Vec3(1.0f, 2.0f, 3.0f));
    }
}
