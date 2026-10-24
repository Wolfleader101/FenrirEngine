// Character debug lines through the frame seam (character spec, "FenrirPhysics: debug lines"): with Characters on,
// each active character draws the shape it uses, its ground normal and its moved velocity, after each step.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "CharacterTest.hpp"
#include "FenrirPhysics/DebugLines.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // A capsule's wireframe: a ring at each end (32 segments each), a cross of half circles over each end (four of 16
    // segments) and four sides.
    constexpr std::size_t capsuleLines = 32 + 32 + 4 * 16 + 4;

    PhysicsWorld& WorldOf(PhysicsApp& test)
    {
        PhysicsWorld* world = PhysicsWorld::Of(test.app.GetActiveScene());
        REQUIRE(world != nullptr);
        return *world;
    }

    std::vector<DrawnLine> LinesIn(std::span<const DrawnLine> lines, const Colour& colour)
    {
        std::vector<DrawnLine> found;
        std::copy_if(lines.begin(), lines.end(), std::back_inserter(found),
                     [colour](const DrawnLine& line) { return SameColour(line.colour, colour); });
        return found;
    }

    // The lowest and highest y any of `lines` reaches.
    std::pair<float, float> HeightsOf(const std::vector<DrawnLine>& lines)
    {
        float low = lines.front().from.y;
        float high = low;
        for (const DrawnLine& line : lines)
        {
            low = std::min({low, line.from.y, line.to.y});
            high = std::max({high, line.from.y, line.to.y});
        }
        return {low, high};
    }
} // namespace

TEST_CASE("A character standing still is drawn as its shape and its ground normal, and nothing with Characters off",
          "[physics][debug][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(1.0f, 0.0f, 2.0f));
    Drive(test.app, character);
    test.Run(0.5);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnGround);

    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Characters);
    test.Run(tick);
    const std::vector<DrawnLine> lines = DrawnLines(test.app);

    INFO("the capsule in the Character colour, standing on the ground from y 0 to 1.6, around its centre");
    const std::vector<DrawnLine> shape = LinesIn(lines, PhysicsDebugColour::Character);
    REQUIRE(shape.size() == capsuleLines);
    const auto [low, high] = HeightsOf(shape);
    REQUIRE(low == Approx(0.0f).margin(0.03));
    REQUIRE(high == Approx(1.6f).margin(0.03));
    for (const DrawnLine& line : shape)
    {
        REQUIRE(std::abs(line.from.x - 1.0f) <= 0.3f + 1.0e-3f);
        REQUIRE(std::abs(line.from.z - 2.0f) <= 0.3f + 1.0e-3f);
    }

    INFO("one ground normal, in the walkable ground colour, from the ground point straight up");
    const std::vector<DrawnLine> normal = LinesIn(lines, PhysicsDebugColour::Ground);
    REQUIRE(normal.size() == 1);
    const CharacterState& state = character.GetComponent<CharacterBody>().state;
    REQUIRE(normal[0].from == state.groundPoint);
    REQUIRE(normal[0].to.y > normal[0].from.y);
    REQUIRE(normal[0].to.x == Approx(normal[0].from.x).margin(1.0e-4));
    REQUIRE(normal[0].to.z == Approx(normal[0].from.z).margin(1.0e-4));

    INFO("standing still it moved nothing, so no moved-velocity line: those are all its lines");
    REQUIRE(lines.size() == capsuleLines + 1);

    world.SetDebugDraw(PhysicsDebugCategories::None);
    test.Run(tick);
    INFO("with Characters off, nothing");
    REQUIRE(DrawnLines(test.app).empty());
}

TEST_CASE("A walking character's moved velocity is drawn as far as it travels in a tenth of a second",
          "[physics][debug][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    std::shared_ptr<Input> input = Drive(test.app, character);
    test.Run(0.5);
    input->walk = Math::Vec3(2.0f, 0.0f, 0.0f);
    test.Run(0.25);

    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Characters);
    test.Run(tick);
    const std::vector<DrawnLine> lines = DrawnLines(test.app);
    INFO("its shape, its ground normal and one moved-velocity line");
    REQUIRE(lines.size() == capsuleLines + 2);
    const std::vector<DrawnLine> moved = LinesIn(lines, PhysicsDebugColour::Velocity);
    REQUIRE(moved.size() == 1);
    INFO("from the character's centre, 2 m/s for a tenth of a second: 0.2 m along x");
    REQUIRE(moved[0].from == PositionOf(character));
    REQUIRE(moved[0].to.x - moved[0].from.x == Approx(0.2f).margin(1.0e-3));
    REQUIRE(moved[0].to.y - moved[0].from.y == Approx(0.0f).margin(1.0e-3));
    REQUIRE(moved[0].to.z - moved[0].from.z == Approx(0.0f).margin(1.0e-3));
}

TEST_CASE("All draws a character's lines, and Colliders draws none for it", "[physics][debug][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    Drive(test.app, character);
    test.Run(0.5);

    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Colliders);
    test.Run(tick);
    INFO("Colliders draws the ground's twelve edges, static grey, and nothing of the character");
    REQUIRE(DrawnLines(test.app).size() == 12);
    REQUIRE(LinesIn(DrawnLines(test.app), PhysicsDebugColour::Static).size() == 12);

    world.SetDebugDraw(PhysicsDebugCategories::All);
    test.Run(tick);
    INFO("All includes Characters: the capsule and the ground normal");
    REQUIRE(LinesIn(DrawnLines(test.app), PhysicsDebugColour::Character).size() == capsuleLines);
    REQUIRE(LinesIn(DrawnLines(test.app), PhysicsDebugColour::Ground).size() == 1);
}

TEST_CASE("A character's ground normal on ground too steep to walk is drawn in the steep ground colour",
          "[physics][debug][characters]")
{
    PhysicsApp test;
    // A ramp tilted 0.3 rad about z, under a character that calls anything over 0.1 rad steep.
    Entity ramp = AddStatic(test.Entities(), Math::Vec3(0.0f, -0.5f, 0.0f), Math::Vec3(10.0f, 0.5f, 10.0f));
    ramp.GetComponent<Transform>().rot = Math::AngleAxis(0.3f, Math::Vec3(0.0f, 0.0f, 1.0f));
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.1f, 0.0f));
    character.GetComponent<CharacterBody>().maxSlope = 0.1f;
    Drive(test.app, character);
    test.Run(0.25);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Characters);
    test.Run(tick);
    REQUIRE(character.GetComponent<CharacterBody>().state.ground == GroundState::OnSteepGround);

    INFO("one ground normal, steep coloured, leaning the ramp's way");
    const std::vector<DrawnLine> normal = LinesIn(DrawnLines(test.app), PhysicsDebugColour::SteepGround);
    REQUIRE(normal.size() == 1);
    REQUIRE(LinesIn(DrawnLines(test.app), PhysicsDebugColour::Ground).empty());
    const Math::Vec3 along = normal[0].to - normal[0].from;
    REQUIRE(glm::length(along) == Approx(0.5f).margin(1.0e-3));
    REQUIRE(along.x < -0.1f);
}

TEST_CASE("While a stand-up is blocked the crouched shape is drawn, and the standing one once it fits",
          "[physics][debug][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    // Crouched, 1.0 m tall, under a roof whose underside is 1.3 m up: too low to stand up to 1.6 m.
    Entity character = AddCharacter(test.Entities(), Math::Vec3(0.0f, 0.0f, 0.0f));
    character.GetComponent<Collider>().shape = CapsuleShape{.halfHeight = 0.2f, .radius = 0.3f};
    character.GetComponent<Transform>().pos.y = 0.5f;
    Drive(test.app, character);
    Entity roof = AddStatic(test.Entities(), Math::Vec3(0.0f, 1.8f, 0.0f), Math::Vec3(2.0f, 0.5f, 2.0f));
    test.Run(0.5);

    character.GetComponent<Collider>().shape = CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f};
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Characters);
    test.Run(tick);
    REQUIRE(character.GetComponent<CharacterBody>().state.shapeBlocked);
    INFO("the Collider asks for 1.6 m, but the shape drawn is the crouched one in use, from 0 to 1.0 m");
    {
        const std::vector<DrawnLine> shape = LinesIn(DrawnLines(test.app), PhysicsDebugColour::Character);
        REQUIRE(shape.size() == capsuleLines);
        const auto [low, high] = HeightsOf(shape);
        REQUIRE(low == Approx(0.0f).margin(0.03));
        REQUIRE(high == Approx(1.0f).margin(0.03));
    }

    test.Entities().DestroyEntity(roof);
    test.Run(tick);
    REQUIRE_FALSE(character.GetComponent<CharacterBody>().state.shapeBlocked);
    INFO("with the roof gone it stands, and the standing shape is drawn, from 0 to 1.6 m");
    const std::vector<DrawnLine> shape = LinesIn(DrawnLines(test.app), PhysicsDebugColour::Character);
    REQUIRE(shape.size() == capsuleLines);
    const auto [low, high] = HeightsOf(shape);
    REQUIRE(low == Approx(0.0f).margin(0.03));
    REQUIRE(high == Approx(1.6f).margin(0.03));
}

TEST_CASE("Disabled and inactive characters draw nothing", "[physics][debug][characters]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity disabled = AddCharacter(test.Entities(), Math::Vec3(-2.0f, 0.0f, 0.0f));
    Entity inactive = AddCharacter(test.Entities(), Math::Vec3(2.0f, 0.0f, 0.0f));
    Drive(test.app, disabled);
    Drive(test.app, inactive);
    test.Run(0.5);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Characters);
    test.Run(tick);
    INFO("both are drawn while active and enabled");
    REQUIRE(LinesIn(DrawnLines(test.app), PhysicsDebugColour::Character).size() == 2 * capsuleLines);

    disabled.GetComponent<CharacterBody>().enabled = false;
    // Inactive: a character with no Collider has no shape.
    inactive.RemoveComponent<Collider>();
    test.Run(tick);
    INFO("disabled, and with no Collider, neither draws a line");
    REQUIRE(DrawnLines(test.app).empty());
}
