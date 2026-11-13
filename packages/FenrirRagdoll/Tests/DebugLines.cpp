// Ragdoll debug lines (ragdolls spec, "FenrirRagdoll: debug lines"; ADR-0025): with PhysicsDebugCategories::Ragdolls on
// in the scene's physics world, each resolved body of an active ragdoll draws its bone's frame and a line from the bone
// to the body in its effective mode's colour, and each powered joint its motor's target as a frame at the joint, in
// the Physics debug group; inactive ragdolls and skipped bodies draw nothing, and nothing is drawn with the category
// off.

#include <cmath>
#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/DebugLines.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/DebugLines.hpp"
#include "RagdollTest.hpp"

using Fenrir::Colour;
using Fenrir::Entity;
using Fenrir::PhysicsDebugCategories;
using Fenrir::RagdollMode;
using Fenrir::Math::Vec3;
using RagdollTest::BodyOf;
using RagdollTest::Character;
using RagdollTest::Near;
namespace RagdollColour = Fenrir::RagdollDebugColour;

namespace
{
    struct Line
    {
        Vec3 from;
        Vec3 to;
        Colour colour;
    };

    bool Same(const Colour& a, const Colour& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    // Every line the scene's DebugDraw shows now.
    std::vector<Line> Lines(Character& character)
    {
        const Fenrir::DebugDraw* draw = character.test.level.GetResource<Fenrir::DebugDraw>();
        REQUIRE(draw != nullptr);
        std::vector<Line> lines;
        for (const Fenrir::DebugLineBatch& batch : draw->Tessellate().lines)
        {
            for (std::size_t i = 0; i + 1 < batch.vertices.size(); i += 2)
            {
                lines.push_back({batch.vertices[i].position, batch.vertices[i + 1].position, batch.vertices[i].colour});
            }
        }
        return lines;
    }

    std::size_t Count(const std::vector<Line>& lines, const Colour& colour)
    {
        std::size_t found = 0;
        for (const Line& line : lines)
        {
            found += Same(line.colour, colour) ? 1 : 0;
        }
        return found;
    }

    // Runs a frame, so the scene has its physics world, and has that world draw `categories` from then on.
    void DrawCategories(Character& character, PhysicsDebugCategories categories = PhysicsDebugCategories::Ragdolls)
    {
        character.test.Run(1);
        Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(character.test.level);
        REQUIRE(world != nullptr);
        world->SetDebugDraw(categories);
    }

    constexpr std::size_t Bodies = 16;
    constexpr std::size_t Joints = 15;
    constexpr std::size_t LinesPerBody = 4; // the bone frame's three axes and the line to the body
} // namespace

TEST_CASE("Ragdolls is a physics debug category, in All", "[ragdoll][debug]")
{
    INFO("Ragdolls is the bit after Vehicles, and All includes it");
    STATIC_REQUIRE(Fenrir::Includes(PhysicsDebugCategories::All, PhysicsDebugCategories::Ragdolls));
    STATIC_REQUIRE(static_cast<std::uint32_t>(PhysicsDebugCategories::Ragdolls) == (1u << 7));
}

TEST_CASE("Each body draws its bone frame and a line to the body in its mode's colour", "[ragdoll][debug]")
{
    Character character;
    DrawCategories(character);

    SECTION("following: grey")
    {
        character.test.Run(2);
        const std::vector<Line> lines = Lines(character);
        INFO("four grey lines per body, and nothing else");
        REQUIRE(Count(lines, RagdollColour::Following) == Bodies * LinesPerBody);
        REQUIRE(lines.size() == Bodies * LinesPerBody);
    }
    SECTION("simulating: orange")
    {
        character.Ragdoll().mode = RagdollMode::Simulating;
        character.test.Run(2);
        const std::vector<Line> lines = Lines(character);
        INFO("four orange lines per body, and nothing else");
        REQUIRE(Count(lines, RagdollColour::Simulating) == Bodies * LinesPerBody);
        REQUIRE(lines.size() == Bodies * LinesPerBody);
    }
    SECTION("powered: green, with a target frame at every joint")
    {
        character.Ragdoll().mode = RagdollMode::Powered;
        character.test.Run(2);
        const std::vector<Line> lines = Lines(character);
        INFO("four green lines per body, and each joint's target frame: an X, Y and Z axis, red, green and blue");
        REQUIRE(Count(lines, RagdollColour::Powered) == Bodies * LinesPerBody);
        REQUIRE(Count(lines, Colour::Red) == Joints);
        REQUIRE(Count(lines, Colour::Green) == Joints);
        REQUIRE(Count(lines, Colour::Blue) == Joints);
        REQUIRE(lines.size() == Bodies * LinesPerBody + 3 * Joints);
    }
    SECTION("a partial ragdoll: the simulating arm orange, the rest grey, no target frame")
    {
        RagdollTest::SetModes(character.spawned, {"upperarm_l", "lowerarm_l", "hand_l"},
                              Fenrir::RagdollBodyMode::Simulating);
        character.test.Run(2);
        const std::vector<Line> lines = Lines(character);
        INFO("the arm's three bodies orange, the other thirteen grey, no joint driven");
        REQUIRE(Count(lines, RagdollColour::Simulating) == 3 * LinesPerBody);
        REQUIRE(Count(lines, RagdollColour::Following) == (Bodies - 3) * LinesPerBody);
        REQUIRE(Count(lines, Colour::Red) == 0);
    }
    SECTION("one powered forearm: a target frame at its elbow only")
    {
        BodyOf(character.spawned, "lowerarm_l").GetComponent<Fenrir::RagdollBody>().mode =
            Fenrir::RagdollBodyMode::Powered;
        character.test.Run(2);
        const std::vector<Line> lines = Lines(character);
        INFO("the forearm green, and one target frame: its elbow's");
        REQUIRE(Count(lines, RagdollColour::Powered) == LinesPerBody);
        REQUIRE(Count(lines, Colour::Red) == 1);
    }
}

TEST_CASE("A body's lines run from its bone's simulation pose to the body", "[ragdoll][debug]")
{
    Character character(RagdollTest::Weightless());
    DrawCategories(character);
    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(1);
    character.Throw(Vec3(0.0f), Vec3(0.0f, 0.0f, 2.0f));
    character.test.Run(20); // drifted a third of a metre: bodies away from their bones
    const Entity chest = BodyOf(character.spawned, "spine_03");
    const Fenrir::Math::Mat4 bone = *Fenrir::Skeletons::GetBoneWorldMatrix(
        character.test.level, character.skeleton, character.BoneOf(chest), Fenrir::PoseKind::Simulation);
    const Vec3 origin(bone[3]);
    const Vec3 body = Fenrir::GetWorldTransform(chest).pos;
    {
        INFO("the chest body has drifted off its bone");
        REQUIRE_FALSE(Near(origin, body, 0.05f));
    }

    const std::vector<Line> lines = Lines(character);
    std::size_t fromBone = 0;
    bool toBody = false;
    for (const Line& line : lines)
    {
        if (Near(line.from, origin, 1e-4f))
        {
            ++fromBone;
            toBody = toBody || Near(line.to, body, 1e-4f);
        }
    }
    INFO("four lines start at the chest bone: its three axes and the one ending on the body");
    REQUIRE(fromBone == LinesPerBody);
    REQUIRE(toBody);
    for (const Line& line : lines)
    {
        if (Near(line.from, origin, 1e-4f) && !Near(line.to, body, 1e-4f))
        {
            INFO("an axis is short, along one of the bone's own axes");
            const Vec3 axis = line.to - line.from;
            REQUIRE(Fenrir::Math::Magnitude(axis) > 0.0f);
            REQUIRE(Fenrir::Math::Magnitude(axis) <= 0.2f);
        }
    }
}

TEST_CASE("A powered joint's target frame is at the joint, where frameB is once the motor holds", "[ragdoll][debug]")
{
    // Weightless at the rest pose: every powered joint holds its target, so frameB on the child body is where the
    // target puts it, worked out here from the child body alone.
    Character character(RagdollTest::Weightless());
    DrawCategories(character);
    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(30);
    const Fenrir::Joint& wrist = *RagdollTest::JointTo(character.spawned, BodyOf(character.spawned, "hand_l"));
    const Fenrir::Transform child = Fenrir::GetWorldTransform(wrist.bodyB);
    const Vec3 anchor = child.pos + Fenrir::Math::Rotate(child.rot, wrist.frameB.position);
    const Vec3 x = Fenrir::Math::Rotate(child.rot * wrist.frameB.rotation, Vec3(1.0f, 0.0f, 0.0f));

    bool found = false;
    for (const Line& line : Lines(character))
    {
        if (Same(line.colour, Colour::Red) && Near(line.from, anchor, 1e-3f))
        {
            found = true;
            const Vec3 along = line.to - line.from;
            INFO("its X axis points along frameB's X on the hand");
            REQUIRE(Near(along / Fenrir::Math::Magnitude(along), x, 1e-3f));
        }
    }
    INFO("a red axis starts at the wrist joint, where frameB is on the hand");
    REQUIRE(found);
}

TEST_CASE("A game joint of another kind between two powered bodies draws no target frame", "[ragdoll][debug]")
{
    Character character(RagdollTest::Weightless());
    DrawCategories(character);
    Entity extra = character.test.Entities().CreateEntity();
    Fenrir::Joint fixed; // a fixed joint: no motor
    fixed.bodyA = BodyOf(character.spawned, "upperarm_l");
    fixed.bodyB = BodyOf(character.spawned, "hand_l");
    extra.AddComponent<Fenrir::Joint>(fixed);
    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(2);
    INFO("the fifteen ragdoll joints' target frames, and none for the fixed joint");
    REQUIRE(Count(Lines(character), Colour::Red) == Joints);
}

TEST_CASE("An inactive ragdoll and a skipped body draw nothing", "[ragdoll][debug]")
{
    Character character;
    DrawCategories(character);

    SECTION("a frame-posed skeleton: nothing at all")
    {
        character.skeleton.GetComponent<Fenrir::SkeletonPose>().timing = Fenrir::PoseTiming::Frame;
        character.test.Run(2);
        INFO("inactive: no line");
        REQUIRE(Lines(character).empty());
    }
    SECTION("a skipped hand: the other fifteen")
    {
        BodyOf(character.spawned, "hand_l").GetComponent<Fenrir::RagdollBody>().bone = RagdollTest::NameOf("nope");
        character.test.Run(2);
        INFO("the fifteen resolved bodies' lines only");
        REQUIRE(Lines(character).size() == (Bodies - 1) * LinesPerBody);
    }
    SECTION("a removed Ragdoll: nothing")
    {
        character.skeleton.RemoveComponent<Fenrir::Ragdoll>();
        character.test.Run(2);
        INFO("no Ragdoll: no line");
        REQUIRE(Lines(character).empty());
    }
}

TEST_CASE("Ragdoll lines are drawn only with the category on, in the Physics group", "[ragdoll][debug]")
{
    Character character;
    character.Ragdoll().mode = RagdollMode::Powered;

    SECTION("off: no ragdoll line")
    {
        DrawCategories(character, PhysicsDebugCategories::None);
        character.test.Run(2);
        INFO("no category: no line");
        REQUIRE(Lines(character).empty());
    }
    SECTION("other categories only: none in the ragdoll colours")
    {
        DrawCategories(character, PhysicsDebugCategories::Colliders | PhysicsDebugCategories::Joints);
        character.test.Run(2);
        const std::vector<Line> lines = Lines(character);
        INFO("physics draws its colliders and joints, and none is a ragdoll line or target frame");
        REQUIRE_FALSE(lines.empty());
        REQUIRE(Count(lines, RagdollColour::Following) == 0);
        REQUIRE(Count(lines, RagdollColour::Powered) == 0);
        REQUIRE(Count(lines, Colour::Red) == 0);
    }
    SECTION("All: the ragdoll's lines among the rest")
    {
        DrawCategories(character, PhysicsDebugCategories::All);
        character.test.Run(2);
        INFO("every body's four green lines");
        REQUIRE(Count(Lines(character), RagdollColour::Powered) == Bodies * LinesPerBody);
    }
    SECTION("the Physics group hidden: none shown")
    {
        DrawCategories(character);
        character.test.level.GetResource<Fenrir::DebugDraw>()->HideGroup(Fenrir::PhysicsDebugGroup);
        character.test.Run(2);
        INFO("drawn into the hidden Physics group: none shown");
        REQUIRE(Lines(character).empty());
    }
}
