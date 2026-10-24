// Vehicle debug lines through the frame seam (vehicles spec, "FenrirPhysics: debug lines"): with Vehicles on, each
// wheel of each active, enabled vehicle draws its suspension, its circle and, on ground, its contact coloured by grip.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/DebugLines.hpp"
#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;

namespace
{
    // Per wheel: the suspension and its two-line cross-tick; a 32-segment circle and its spoke; on ground, a
    // three-line cross and the normal.
    constexpr std::size_t suspensionLines = 3;
    constexpr std::size_t wheelLines = 32 + 1;
    constexpr std::size_t contactLines = 3 + 1;

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

    // The colour of the contact normal drawn from `wheel`'s contact point: the line that starts exactly there and
    // leaves it along the normal. Transparent black if there is none.
    Colour ContactColourOf(std::span<const DrawnLine> lines, const Entity& wheel)
    {
        const VehicleWheelState& state = StateOf(wheel);
        for (const DrawnLine& line : lines)
        {
            if (glm::length(line.from - state.contactPoint) < 1.0e-6f &&
                glm::dot(line.to - line.from, state.contactNormal) > 0.0f)
            {
                return line.colour;
            }
        }
        return Colour{0.0f, 0.0f, 0.0f, 0.0f};
    }

    // The colour a wheel's contact should have from its state's slips and its tyres.
    Colour ExpectedGrip(const Entity& wheel)
    {
        const VehicleWheel& settings = wheel.GetComponent<VehicleWheel>();
        const float along = std::abs(settings.state.longitudinalSlip);
        const float across = std::abs(settings.state.lateralSlip);
        if (along > settings.longitudinal.slideSlip || across > settings.lateral.slideSlip)
        {
            return PhysicsDebugColour::PastSlideSlip;
        }
        if (along > settings.longitudinal.peakSlip || across > settings.lateral.peakSlip)
        {
            return PhysicsDebugColour::PastPeakSlip;
        }
        return PhysicsDebugColour::Gripping;
    }
} // namespace

TEST_CASE("A car resting on its four wheels draws each wheel's suspension, circle and contact",
          "[physics][debug][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(2.0f, 1.0f, -3.0f));
    test.Run(1.0);
    REQUIRE(StateOf(car).wheelsOnGround == 4);

    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Vehicles);
    test.Run(tick);
    const std::vector<DrawnLine> lines = DrawnLines(test.app);

    INFO("four wheels' suspensions, circles and contacts, and nothing else");
    const std::vector<DrawnLine> suspension = LinesIn(lines, PhysicsDebugColour::Suspension);
    const std::vector<DrawnLine> wheels = LinesIn(lines, PhysicsDebugColour::Wheel);
    REQUIRE(suspension.size() == 4 * suspensionLines);
    REQUIRE(wheels.size() == 4 * wheelLines);
    const std::size_t contacts = LinesIn(lines, PhysicsDebugColour::Gripping).size() +
                                 LinesIn(lines, PhysicsDebugColour::PastPeakSlip).size() +
                                 LinesIn(lines, PhysicsDebugColour::PastSlideSlip).size();
    REQUIRE(contacts == 4 * contactLines);
    REQUIRE(lines.size() == 4 * (suspensionLines + wheelLines + contactLines));

    const Transform chassis = GetWorldTransform(car.chassis);
    for (const Entity& wheel : car.wheels)
    {
        const VehicleWheel& settings = wheel.GetComponent<VehicleWheel>();
        const Math::Vec3 centre = GetWorldTransform(wheel).pos;

        INFO("its suspension runs from the top of its travel down to its full-extension point");
        const Math::Vec3 bottom = chassis.pos + chassis.rot * settings.position;
        const Math::Vec3 top = bottom + chassis.rot * Math::Vec3(0.0f, settings.suspension.travel, 0.0f);
        const auto run = std::ranges::find_if(suspension, [&](const DrawnLine& line) {
            return glm::length(line.from - top) < 1.0e-4f && glm::length(line.to - bottom) < 1.0e-4f;
        });
        REQUIRE(run != suspension.end());
        INFO("with its two-line cross-tick centred on the wheel's centre");
        const auto ticks = std::ranges::count_if(suspension, [&](const DrawnLine& line) {
            return glm::length(0.5f * (line.from + line.to) - centre) < 1.0e-4f;
        });
        REQUIRE(ticks == 2);

        INFO("its circle and spoke lie on a disc of its radius about its centre, across its axle");
        const Math::Vec3 axle = GetWorldTransform(wheel).rot * Math::Vec3(1.0f, 0.0f, 0.0f);
        const auto onWheel = std::ranges::count_if(wheels, [&](const DrawnLine& line) {
            return glm::length(line.to - centre) <= settings.radius + 1.0e-4f &&
                   std::abs(glm::dot(line.from - centre, axle)) < 1.0e-4f &&
                   std::abs(glm::dot(line.to - centre, axle)) < 1.0e-4f;
        });
        REQUIRE(onWheel == static_cast<std::ptrdiff_t>(wheelLines));

        INFO("its contact normal, from the contact point, is coloured by its slips");
        REQUIRE(SameColour(ContactColourOf(lines, wheel), ExpectedGrip(wheel)));
    }

    world.SetDebugDraw(PhysicsDebugCategories::None);
    test.Run(tick);
    INFO("with Vehicles off, nothing");
    REQUIRE(DrawnLines(test.app).empty());
}

TEST_CASE("A wheel's contact is drawn gripping, past peakSlip and past slideSlip as its slip grows",
          "[physics][debug][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    // Two seconds' cruise to 4 m/s, then a second steering in to full lock, which slips the front tyres across past
    // their peakSlip, then the brakes, which lock the wheels: a slip of 1 along them, past their slideSlip.
    constexpr int steerFrom = 120;
    constexpr int brakeFrom = 240;
    Drive(test.app, car.chassis, [](int i, Vehicle& vehicle) {
        vehicle.input.forward = i < brakeFrom ? 0.2f : 0.0f;
        vehicle.input.steer = std::clamp(static_cast<float>(i - steerFrom) / 60.0f, 0.0f, 1.0f);
        vehicle.input.brake = i < brakeFrom ? 0.0f : 1.0f;
    });
    // Pulling away from rest, its slip along the wheels swings on alternate ticks; by 1.5 s it has settled.
    constexpr int settled = 90;
    test.Run(settled * tick);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Vehicles);

    const Entity& wheel = car.wheels[0];
    // The grips seen, in order: 0 gripping, 1 past peakSlip, 2 past slideSlip.
    const std::array<Colour, 3> grips = {PhysicsDebugColour::Gripping, PhysicsDebugColour::PastPeakSlip,
                                         PhysicsDebugColour::PastSlideSlip};
    std::vector<std::size_t> seen;
    for (int i = settled; i < brakeFrom + 30; ++i)
    {
        test.Run(tick);
        REQUIRE(StateOf(wheel).onGround);
        const Colour colour = ContactColourOf(DrawnLines(test.app), wheel);
        INFO("tick " << i << ": the contact's colour is its slips' against its tyres'");
        REQUIRE(SameColour(colour, ExpectedGrip(wheel)));
        const auto grip = static_cast<std::size_t>(
            std::ranges::find_if(grips, [&colour](const Colour& each) { return SameColour(each, colour); }) -
            grips.begin());
        REQUIRE(grip < grips.size());
        if (seen.empty() || seen.back() != grip)
        {
            seen.push_back(grip);
        }
    }
    INFO("from gripping, past peakSlip, to past slideSlip, once each");
    REQUIRE(seen == std::vector<std::size_t>{0, 1, 2});
}

TEST_CASE("Disabled and inactive vehicles draw nothing, and Colliders still draws their chassis",
          "[physics][debug][vehicles]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    Car disabled = AddCar(test.Entities(), Math::Vec3(-5.0f, 1.0f, 0.0f));
    Car inactive = AddCar(test.Entities(), Math::Vec3(5.0f, 1.0f, 0.0f));
    test.Run(1.0);
    PhysicsWorld& world = WorldOf(test);
    world.SetDebugDraw(PhysicsDebugCategories::Vehicles);
    test.Run(tick);
    INFO("both are drawn while active and enabled");
    REQUIRE(LinesIn(DrawnLines(test.app), PhysicsDebugColour::Wheel).size() == 2 * 4 * wheelLines);

    disabled.chassis.GetComponent<Vehicle>().enabled = false;
    // Inactive: a kinematic chassis cannot carry a vehicle.
    inactive.chassis.GetComponent<RigidBody>().motionType = MotionType::Kinematic;
    test.Run(tick);
    INFO("disabled, and on a kinematic chassis, neither draws a line");
    REQUIRE(DrawnLines(test.app).empty());

    world.SetDebugDraw(PhysicsDebugCategories::Colliders);
    test.Run(tick);
    INFO("Colliders draws the floor and both chassis boxes, and no wheel");
    REQUIRE(DrawnLines(test.app).size() == 3 * 12);
}

TEST_CASE("All includes Vehicles, and Colliders draws none of a vehicle's wheel lines", "[physics][debug][vehicles]")
{
    STATIC_REQUIRE(Includes(PhysicsDebugCategories::All, PhysicsDebugCategories::Vehicles));
    PhysicsApp test;
    AddFloor(test.Entities());
    Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
    test.Run(1.0);
    PhysicsWorld& world = WorldOf(test);

    world.SetDebugDraw(PhysicsDebugCategories::Colliders);
    test.Run(tick);
    INFO("Colliders: the floor's and the chassis's twelve edges each");
    REQUIRE(DrawnLines(test.app).size() == 2 * 12);

    world.SetDebugDraw(PhysicsDebugCategories::All);
    test.Run(tick);
    INFO("All: the four wheels among the rest");
    REQUIRE(LinesIn(DrawnLines(test.app), PhysicsDebugColour::Suspension).size() == 4 * suspensionLines);
    REQUIRE(LinesIn(DrawnLines(test.app), PhysicsDebugColour::Wheel).size() == 4 * wheelLines);
}
