// Tracked vehicles through the frame seam (vehicles spec, kinds: TrackedVehicle and VehicleTrack): the spike's tank
// driving forward, pivoting on opposite track ratios with its track speeds read back, held still on one side by a 0
// ratio, ignoring steer, handbrake, the tyre fields and differentials, taking VehicleTrack edits live, and rebuilt by
// a kind change to or from tracked carrying over its engine speed, gear and wheel spin.

#include <cmath>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "VehicleTest.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // How far one tick may move a carried engine speed or wheel spin: a fresh one would be off by far more.
    constexpr float carried = 0.03f;

    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    Vehicle& VehicleOf(const Car& vehicle)
    {
        return vehicle.chassis.GetComponent<Vehicle>();
    }

    TrackedVehicle& TracksOf(const Car& tank)
    {
        return VehicleOf(tank).kind.As<TrackedVehicle>();
    }

    float HorizontalDistance(const Math::Vec3& a, const Math::Vec3& b)
    {
        return std::hypot(a.x - b.x, a.z - b.z);
    }

    // Runs `seconds` of ticks; the radians the chassis turns left meanwhile, unwrapped.
    float TurnOver(PhysicsApp& test, const Entity& chassis, double seconds)
    {
        constexpr float turn = 2.0f * std::numbers::pi_v<float>;
        float turned = 0.0f;
        float yaw = YawOf(chassis);
        for (int i = 0; i < static_cast<int>(seconds * 60.0); ++i)
        {
            test.Run(tick);
            float step = YawOf(chassis) - yaw;
            step -= turn * std::round(step / turn);
            turned += step;
            yaw = YawOf(chassis);
        }
        return turned;
    }

    // The tank on the floor, settled for a second.
    Car SettledTank(PhysicsApp& test)
    {
        AddFloor(test.Entities());
        Car tank = AddTank(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
        test.Run(1.0);
        // The end wheels sit higher, clear of flat ground.
        REQUIRE(StateOf(tank).wheelsOnGround == 2 * (tankWheelsASide - 2));
        return tank;
    }

    // What a rebuild should carry over, read after the tick before it.
    struct Before
    {
        float engineRpm;
        int gear;
        std::vector<float> spins;
    };

    Before Read(const Car& vehicle)
    {
        Before before{.engineRpm = StateOf(vehicle).engineRpm, .gear = StateOf(vehicle).gear, .spins = {}};
        for (const Entity& wheel : vehicle.wheels)
        {
            before.spins.push_back(StateOf(wheel).angularVelocity);
        }
        return before;
    }

    // `rpmMargin`: how far the first tick may move the engine speed.
    void RequireCarried(const Car& vehicle, const Before& before, float rpmMargin = carried)
    {
        const VehicleState& state = StateOf(vehicle);
        INFO("the engine speed carries over (" << before.engineRpm << " to " << state.engineRpm << " RPM)");
        REQUIRE(state.engineRpm == Approx(before.engineRpm).epsilon(rpmMargin));
        INFO("so does the gear");
        REQUIRE(state.gear == before.gear);
        for (std::size_t i = 0; i < vehicle.wheels.size(); ++i)
        {
            const float spin = StateOf(vehicle.wheels[i]).angularVelocity;
            INFO("wheel " << i << "'s spin carries over (" << before.spins[i] << " to " << spin << " rad/s)");
            REQUIRE(spin == Approx(before.spins[i]).epsilon(carried));
        }
    }

    // Two seconds of the tank driving gently forward, steering into its left track, then its chassis's pose.
    Transform DriveTwoSeconds(void (*edit)(EntityList&, const Car&))
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        const Car tank = AddTank(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
        edit(test.Entities(), tank);
        VehicleOf(tank).input.forward = 0.5f;
        VehicleOf(tank).input.leftTrack = 0.6f;
        test.Run(2.0);
        REQUIRE(test.warnings.empty());
        return GetWorldTransform(tank.chassis);
    }
} // namespace

TEST_CASE("A TrackedVehicle made with no arguments has the spec's defaults", "[physics][vehicles][tracked]")
{
    const VehicleKind kind = TrackedVehicle{};
    REQUIRE(kind.Type() == VehicleType::Tracked);
    const TrackedVehicle& tracked = kind.As<TrackedVehicle>();
    INFO("an engine of 500 to 4000 RPM peaking at 3000, and four gears from 4.0 to 1.0");
    REQUIRE(Exactly(tracked.engine.minRpm, 500.0f));
    REQUIRE(Exactly(tracked.engine.maxRpm, 4000.0f));
    REQUIRE(Exactly(tracked.engine.peakTorqueRpm, 3000.0f));
    REQUIRE(tracked.transmission.gearCount == 4);
    REQUIRE(Exactly(tracked.transmission.firstGear, 4.0f));
    REQUIRE(Exactly(tracked.transmission.topGear, 1.0f));
    REQUIRE(Exactly(tracked.transmission.reverseGear, 4.0f));
    REQUIRE(Exactly(tracked.transmission.shiftUpRpm, 3500.0f));
    REQUIRE(Exactly(tracked.transmission.shiftDownRpm, 1000.0f));
    for (const VehicleTrack& track : {tracked.left, tracked.right})
    {
        REQUIRE(Exactly(track.inertia, 10.0f));
        REQUIRE(Exactly(track.angularDamping, 0.5f));
        REQUIRE(Exactly(track.maxBrakeTorque, 15000.0f));
        REQUIRE(Exactly(track.differentialRatio, 6.0f));
        REQUIRE(Exactly(track.longitudinalFriction, 4.0f));
        REQUIRE(Exactly(track.lateralFriction, 2.0f));
    }
}

TEST_CASE("The default tank drives forward on both tracks, its tracked wheels reading no slip",
          "[physics][vehicles][tracked]")
{
    PhysicsApp test;
    const Car tank = SettledTank(test);
    const Math::Vec3 start = PositionOf(tank.chassis);
    VehicleOf(tank).input.forward = 1.0f;
    test.Run(3.0);

    const VehicleState& state = StateOf(tank);
    INFO("it has driven forward (-Z) " << start.z - PositionOf(tank.chassis).z << " m, at " << state.forwardSpeed
                                       << " m/s in gear " << state.gear);
    REQUIRE(PositionOf(tank.chassis).z < start.z - 5.0f);
    REQUIRE(std::abs(PositionOf(tank.chassis).x - start.x) < 0.5f);
    REQUIRE(state.forwardSpeed > 2.0f);
    REQUIRE(state.gear >= 1);
    INFO("both tracks run at its speed (" << state.leftTrackSpeed << " and " << state.rightTrackSpeed << " m/s)");
    REQUIRE(state.leftTrackSpeed == Approx(state.forwardSpeed).epsilon(0.1f));
    REQUIRE(state.rightTrackSpeed == Approx(state.leftTrackSpeed).epsilon(0.01f));
    for (const Entity& wheel : tank.wheels)
    {
        INFO("a tracked wheel has no slip");
        REQUIRE(Exactly(StateOf(wheel).longitudinalSlip, 0.0f));
        REQUIRE(Exactly(StateOf(wheel).lateralSlip, 0.0f));
        REQUIRE(StateOf(wheel).angularVelocity > 0.0f);
    }
    REQUIRE(test.warnings.empty());
}

TEST_CASE("On leftTrack -1 and rightTrack 1 the tank pivots on the spot, its track speeds opposite",
          "[physics][vehicles][tracked]")
{
    PhysicsApp test;
    const Car tank = SettledTank(test);
    const Math::Vec3 start = PositionOf(tank.chassis);
    Vehicle& vehicle = VehicleOf(tank);
    vehicle.input.forward = 1.0f;
    vehicle.input.leftTrack = -1.0f;
    vehicle.input.rightTrack = 1.0f;
    const float turned = TurnOver(test, tank.chassis, 3.0);

    const VehicleState& state = StateOf(tank);
    INFO("the tracks run opposite ways (" << state.leftTrackSpeed << " and " << state.rightTrackSpeed << " m/s)");
    REQUIRE(state.leftTrackSpeed < -0.5f);
    REQUIRE(state.rightTrackSpeed == Approx(-state.leftTrackSpeed).epsilon(0.01f));
    INFO("it has turned left " << turned << " rad, moving " << HorizontalDistance(PositionOf(tank.chassis), start)
                               << " m");
    REQUIRE(turned > 1.0f);
    REQUIRE(HorizontalDistance(PositionOf(tank.chassis), start) < 0.5f);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A track ratio of 0 holds that track still while the other drives", "[physics][vehicles][tracked]")
{
    PhysicsApp test;
    const Car tank = SettledTank(test);
    Vehicle& vehicle = VehicleOf(tank);
    vehicle.input.forward = 1.0f;
    vehicle.input.leftTrack = 0.0f;
    const float turned = TurnOver(test, tank.chassis, 3.0);

    const VehicleState& state = StateOf(tank);
    INFO("the left track holds (" << state.leftTrackSpeed << " m/s), the right drives (" << state.rightTrackSpeed
                                  << " m/s)");
    REQUIRE(std::abs(state.leftTrackSpeed) < 0.01f);
    REQUIRE(state.rightTrackSpeed > 0.5f);
    for (int i = 0; i < tankWheelsASide; ++i)
    {
        REQUIRE(std::abs(StateOf(tank.wheels[static_cast<std::size_t>(i)]).angularVelocity) < 0.05f);
    }
    INFO("so it turns left about its left track (" << turned << " rad)");
    REQUIRE(turned > 1.0f);
    REQUIRE(test.warnings.empty());
}

TEST_CASE("A tank ignores steer, handbrake, its wheels' tyre, brake and steer fields, and differentials",
          "[physics][vehicles][tracked]")
{
    const Transform plain = DriveTwoSeconds([](EntityList&, const Car&) {});
    const Transform ignored = DriveTwoSeconds([](EntityList& entities, const Car& tank) {
        VehicleOf(tank).input.steer = 1.0f;
        VehicleOf(tank).input.handbrake = 1.0f;
        for (const Entity& wheel : tank.wheels)
        {
            VehicleWheel& settings = wheel.GetComponent<VehicleWheel>();
            settings.maxSteerAngle = 0.5f;
            settings.maxBrakeTorque = 0.0f;
            settings.maxHandbrakeTorque = 100000.0f;
            settings.longitudinal.peakFriction = 0.1f;
            settings.lateral.peakFriction = 0.1f;
        }
        Entity differential = entities.CreateEntity();
        differential.AddComponent<VehicleDifferential>(
            VehicleDifferential{.leftWheel = tank.wheels[0], .rightWheel = tank.wheels[tankWheelsASide]});
    });
    INFO("both drives end where the plain one does, bit for bit (z " << plain.pos.z << " and " << ignored.pos.z << ")");
    REQUIRE(plain.pos.z < -1.0f);
    REQUIRE(std::memcmp(&plain.pos, &ignored.pos, sizeof(plain.pos)) == 0);
    REQUIRE(std::memcmp(&plain.rot, &ignored.rot, sizeof(plain.rot)) == 0);
}

TEST_CASE("A VehicleTrack edit is live and wakes the chassis", "[physics][vehicles][tracked]")
{
    PhysicsApp test;
    const Car tank = SettledTank(test);
    SECTION("no grip on either track: the tracks spin and the tank stays put")
    {
        const Math::Vec3 start = PositionOf(tank.chassis);
        TracksOf(tank).left.longitudinalFriction = 0.0f;
        TracksOf(tank).right.longitudinalFriction = 0.0f;
        VehicleOf(tank).input.forward = 1.0f;
        test.Run(2.0);
        INFO("the tracks run at " << StateOf(tank).leftTrackSpeed << " m/s, the tank moving "
                                  << HorizontalDistance(PositionOf(tank.chassis), start) << " m");
        REQUIRE(StateOf(tank).leftTrackSpeed > 2.0f);
        REQUIRE(HorizontalDistance(PositionOf(tank.chassis), start) < 0.1f);
    }
    SECTION("asleep, an edit wakes it")
    {
        for (int frame = 0; frame < 20 * 60 && !World(test).IsSleeping(tank.chassis); ++frame)
        {
            test.Run(tick);
        }
        REQUIRE(World(test).IsSleeping(tank.chassis));
        TracksOf(tank).right.maxBrakeTorque = 5000.0f;
        test.Run(tick);
        INFO("the tick after, the chassis is awake");
        REQUIRE_FALSE(World(test).IsSleeping(tank.chassis));
    }
    SECTION("a wheel moved to the other track: every wheel on the left, the right track has none")
    {
        for (const Entity& wheel : tank.wheels)
        {
            wheel.GetComponent<VehicleWheel>().track = VehicleTrackSide::Left;
        }
        VehicleOf(tank).input.forward = 1.0f;
        test.Run(2.0);
        INFO("the left track drives every wheel (" << StateOf(tank).leftTrackSpeed << " m/s)");
        REQUIRE(StateOf(tank).leftTrackSpeed > 1.0f);
        REQUIRE(Exactly(StateOf(tank).rightTrackSpeed, 0.0f));
        REQUIRE(StateOf(tank.wheels.back()).angularVelocity > 1.0f);
    }
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Changing kind to or from tracked rebuilds the vehicle keeping the engine speed, gear and spin",
          "[physics][vehicles][tracked][rebuilds]")
{
    PhysicsApp test;
    AddFloor(test.Entities());
    SECTION("tracked to wheeled, its drivetrain kept")
    {
        const Car tank = AddTank(test.Entities(), Math::Vec3(0.0f, 1.5f, 0.0f));
        // Geared as a wheeled vehicle's default differential is, so the engine turns as fast after the change.
        TracksOf(tank).left.differentialRatio = VehicleDifferential{}.ratio;
        TracksOf(tank).right.differentialRatio = VehicleDifferential{}.ratio;
        VehicleOf(tank).input.forward = 0.3f;
        test.Run(2.5);
        REQUIRE(StateOf(tank).gear >= 1);
        REQUIRE(StateOf(tank.wheels[0]).angularVelocity > 1.0f);
        const Before before = Read(tank);
        const TrackedVehicle tracked = TracksOf(tank);
        VehicleOf(tank).kind = WheeledVehicle{.engine = tracked.engine, .transmission = tracked.transmission};
        test.Run(tick);
        RequireCarried(tank, before);
        INFO("a wheeled vehicle reads no track speeds");
        REQUIRE(Exactly(StateOf(tank).leftTrackSpeed, 0.0f));
        REQUIRE(Exactly(StateOf(tank).rightTrackSpeed, 0.0f));
    }
    SECTION("wheeled to tracked")
    {
        const Car car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
        for (const std::size_t right : {std::size_t{1}, std::size_t{3}})
        {
            car.wheels[right].GetComponent<VehicleWheel>().track = VehicleTrackSide::Right;
        }
        VehicleOf(car).input.forward = 0.2f;
        test.Run(2.5);
        REQUIRE(StateOf(car).gear >= 1);
        const Before before = Read(car);
        const WheeledVehicle wheeled = VehicleOf(car).kind.As<WheeledVehicle>();
        const VehicleTrack geared{.differentialRatio = VehicleDifferential{}.ratio};
        VehicleOf(car).kind = TrackedVehicle{
            .engine = wheeled.engine, .transmission = wheeled.transmission, .left = geared, .right = geared};
        test.Run(tick);
        // A tracked engine is locked to its tracks from its first step, so the car's clutch slip goes: a fresh engine
        // would read its 1000 RPM idle.
        RequireCarried(car, before, 0.1f);
        REQUIRE(VehicleOf(car).kind.Is<TrackedVehicle>());
        INFO("it reads its tracks' speeds");
        REQUIRE(StateOf(car).leftTrackSpeed > 0.5f);
    }
    REQUIRE(test.warnings.empty());
}

TEST_CASE("Out-of-range track settings are corrected with one warning", "[physics][vehicles][tracked]")
{
    PhysicsApp test;
    const Car tank = SettledTank(test);
    TracksOf(tank).left.inertia = -1.0f;
    TracksOf(tank).right.lateralFriction = -2.0f;
    TracksOf(tank).left.differentialRatio = 0.0f;
    VehicleOf(tank).input.forward = 1.0f;
    test.Run(1.0);
    INFO("one warning, and the tank still drives");
    REQUIRE(test.warnings.size() == 1);
    REQUIRE(test.warnings[0].find("a negative track inertia, brake torque or friction is treated as 0") !=
            std::string::npos);
    REQUIRE(std::isfinite(PositionOf(tank.chassis).z));
}
