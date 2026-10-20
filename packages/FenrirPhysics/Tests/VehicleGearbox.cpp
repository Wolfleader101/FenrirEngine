// A vehicle's engine and gearbox (ADR-0015) through the frame seam: the gear ratios, Automatic's own shifting, Manual's
// gear and clutch from input, live edits to the engine and gearbox, and the warnings for out-of-range values.

#include <algorithm>
#include <cmath>
#include <functional>
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
    WheeledVehicle& WheeledOf(const Car& car)
    {
        return car.chassis.GetComponent<Vehicle>().kind.As<WheeledVehicle>();
    }

    VehicleInput& InputOf(const Car& car)
    {
        return car.chassis.GetComponent<Vehicle>().input;
    }

    /** The engine's RPM over its wheels' mean spin in rad/s: the gear's ratio times a constant. */
    float EngineToWheels(const Car& car)
    {
        float spin = 0.0f;
        for (const Entity& wheel : car.wheels)
        {
            spin += StateOf(wheel).angularVelocity;
        }
        return StateOf(car).engineRpm * static_cast<float>(car.wheels.size()) / spin;
    }

    /** Runs whole ticks until `done` holds, at most `seconds` of them; whether it held. */
    bool RunUntil(PhysicsApp& test, double seconds, const std::function<bool()>& done)
    {
        for (int frame = 0; frame < static_cast<int>(seconds / tick + 0.5); ++frame)
        {
            test.app.RunFrame(tick);
            if (done())
            {
                return true;
            }
        }
        return false;
    }

    /** A car on a floor, settled on its wheels. */
    struct Parked
    {
        Parked()
        {
            AddFloor(test.Entities());
            car = AddCar(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f));
            test.Run(1.0);
        }

        PhysicsApp test;
        Car car;
    };
} // namespace

TEST_CASE("Forward gears are spaced geometrically from first to top, as measured in Manual",
          "[physics][vehicles][vehicle-gearbox]")
{
    Parked parked;
    Car& car = parked.car;
    InputOf(car).forward = 0.5f;
    REQUIRE(RunUntil(parked.test, 20.0, [&car] { return StateOf(car).forwardSpeed > 20.0f; }));

    // Coasting in each gear from the top down, the clutch engaged: the engine turns with the wheels.
    WheeledOf(car).transmission.mode = TransmissionMode::Manual;
    InputOf(car).forward = 0.0f;
    std::vector<float> ratios(6);
    for (int gear = 5; gear >= 1; --gear)
    {
        InputOf(car).gear = gear;
        parked.test.Run(0.5);
        INFO("in gear " << gear << " at " << StateOf(car).forwardSpeed << " m/s the engine turns at "
                        << StateOf(car).engineRpm << " RPM, within its range");
        REQUIRE(StateOf(car).gear == gear);
        REQUIRE(StateOf(car).engineRpm > 1100.0f);
        REQUIRE(StateOf(car).engineRpm < 5900.0f);
        ratios[static_cast<std::size_t>(gear)] = EngineToWheels(car);
    }

    // (2.66 / 0.74)^(1/4): the default gears' common ratio.
    const float step = std::pow(2.66f / 0.74f, 0.25f);
    for (std::size_t gear = 1; gear < 5; ++gear)
    {
        const float measured = ratios[gear] / ratios[gear + 1];
        INFO("gear " << gear << " turns the engine " << measured << " times as fast as gear " << gear + 1
                     << " at the same wheel speed; the spacing is " << step);
        REQUIRE(measured == Approx(step).epsilon(0.015));
    }
}

TEST_CASE("In Manual the state echoes the gear and clutch input, clamped", "[physics][vehicles][vehicle-gearbox]")
{
    Parked parked;
    Car& car = parked.car;
    WheeledOf(car).transmission.mode = TransmissionMode::Manual;

    struct Case
    {
        int gear;
        float clutch;
        int engaged;
        float applied;
    };
    for (const Case& c : {Case{2, 0.4f, 2, 0.4f}, Case{-1, 0.0f, -1, 0.0f}, Case{0, 1.0f, 0, 1.0f},
                          Case{9, 2.0f, 5, 1.0f}, Case{-4, -1.0f, -1, 0.0f}})
    {
        InputOf(car).gear = c.gear;
        InputOf(car).clutch = c.clutch;
        parked.test.app.RunFrame(tick);
        INFO("gear " << c.gear << " and clutch " << c.clutch << " read back as gear " << StateOf(car).gear
                     << " and clutch " << StateOf(car).clutch << ", from the tick they are set");
        REQUIRE(StateOf(car).gear == c.engaged);
        REQUIRE(StateOf(car).clutch == Approx(c.applied).margin(1e-6));
        REQUIRE_FALSE(StateOf(car).shifting);
    }
}

TEST_CASE("In Manual a negative forward is a released pedal, and the gear sets the direction",
          "[physics][vehicles][vehicle-gearbox]")
{
    // In first gear, one car with forward -1 and one with 0: both creep at the idling engine's pace, as a car does
    // with its clutch engaged and no throttle.
    Parked negative;
    Parked released;
    for (Parked* parked : {&negative, &released})
    {
        WheeledOf(parked->car).transmission.mode = TransmissionMode::Manual;
        InputOf(parked->car).gear = 1;
        InputOf(parked->car).forward = parked == &negative ? -1.0f : 0.0f;
        parked->test.Run(3.0);
    }
    const VehicleState& state = StateOf(negative.car);
    INFO("forward -1 drives as no throttle does: " << state.forwardSpeed << " m/s against "
                                                   << StateOf(released.car).forwardSpeed << ", the engine idling at "
                                                   << state.engineRpm << " RPM");
    REQUIRE(state.forwardSpeed == Approx(StateOf(released.car).forwardSpeed).margin(1e-3));
    REQUIRE(state.forwardSpeed > 0.0f);
    REQUIRE(state.engineRpm < 1100.0f);

    InputOf(negative.car).gear = -1;
    InputOf(negative.car).forward = 1.0f;
    negative.test.Run(4.0);
    INFO("in reverse with forward 1 it backs away (" << state.forwardSpeed << " m/s)");
    REQUIRE(state.forwardSpeed < -1.0f);
}

TEST_CASE("Automatic shifts up under throttle and down braking, shifting as it does, and rests in neutral",
          "[physics][vehicles][vehicle-gearbox]")
{
    Parked parked;
    Car& car = parked.car;
    INFO("at rest it is in neutral");
    REQUIRE(StateOf(car).gear == 0);

    InputOf(car).forward = 1.0f;
    int top = 0;
    bool shifting = false;
    RunUntil(parked.test, 8.0, [&] {
        top = std::max(top, StateOf(car).gear);
        shifting = shifting || StateOf(car).shifting;
        return false;
    });
    INFO("under full throttle it shifted up to gear " << top << ", and was seen shifting");
    REQUIRE(top >= 3);
    REQUIRE(shifting);

    InputOf(car).forward = 0.0f;
    InputOf(car).brake = 1.0f;
    std::vector<int> gears = {StateOf(car).gear};
    RunUntil(parked.test, 8.0, [&] {
        if (StateOf(car).gear != gears.back())
        {
            gears.push_back(StateOf(car).gear);
        }
        return false;
    });
    INFO("braking it shifted down, never up, through " << gears.size() << " gears, to neutral");
    REQUIRE(gears.size() >= 2);
    for (std::size_t i = 1; i < gears.size(); ++i)
    {
        REQUIRE(gears[i] < gears[i - 1]);
    }
    REQUIRE(gears.back() == 0);
    INFO("and it stopped (" << StateOf(car).forwardSpeed << " m/s)");
    REQUIRE(std::abs(StateOf(car).forwardSpeed) < 0.05f);
}

TEST_CASE("A higher maxTorque accelerates the car faster from the tick it is set",
          "[physics][vehicles][vehicle-gearbox]")
{
    // Two cars in two worlds in first gear in Manual, so neither shifts; one's torque is doubled after a second.
    Parked plain;
    Parked tuned;
    for (Parked* parked : {&plain, &tuned})
    {
        WheeledOf(parked->car).transmission.mode = TransmissionMode::Manual;
        InputOf(parked->car).gear = 1;
        InputOf(parked->car).forward = 0.2f;
        // Past the speed at which first gear holds the engine at idle, where the clutch, not the torque, drives it.
        parked->test.Run(3.0);
    }
    const float before = StateOf(tuned.car).forwardSpeed;
    INFO("alike so far: " << StateOf(plain.car).forwardSpeed << " and " << before << " m/s");
    REQUIRE(StateOf(plain.car).forwardSpeed == Approx(before).margin(1e-3));

    WheeledOf(tuned.car).engine.maxTorque = 1000.0f;
    plain.test.Run(0.1);
    tuned.test.Run(0.1);
    const float plainGain = StateOf(plain.car).forwardSpeed - before;
    const float tunedGain = StateOf(tuned.car).forwardSpeed - before;
    INFO("over the next 0.1 s the tuned car gained " << tunedGain << " m/s, the plain one " << plainGain);
    REQUIRE(tunedGain > 1.3f * plainGain);
}

TEST_CASE("Fewer gears or a lower top gear, set mid-drive, lower the car's top speed",
          "[physics][vehicles][vehicle-gearbox]")
{
    struct Case
    {
        const char* what;
        void (*edit)(WheeledVehicle&);
        // Whether the edit holds the car to first gear's top speed.
        bool limited;
    };
    // First gear's top speed: 6000 RPM through 2.66 and the differential's 3.42, on 0.3 m wheels.
    const float firstGearTop = 6000.0f * 2.0f * std::numbers::pi_v<float> / 60.0f / (2.66f * 3.42f) * 0.3f;
    for (const Case& c :
         {Case{"unchanged", [](WheeledVehicle&) {}, false},
          Case{"one gear", [](WheeledVehicle& wheeled) { wheeled.transmission.gearCount = 1; }, true},
          Case{"top gear as first", [](WheeledVehicle& wheeled) { wheeled.transmission.topGear = 2.66f; }, true}})
    {
        Parked parked;
        Car& car = parked.car;
        InputOf(car).forward = 1.0f;
        // Into second gear first, so one gear takes it out of a gear it no longer has.
        REQUIRE(RunUntil(parked.test, 10.0, [&car] { return StateOf(car).gear == 2; }));
        c.edit(WheeledOf(car));
        int top = 0;
        RunUntil(parked.test, 12.0, [&] {
            top = std::max(top, StateOf(car).gear);
            return false;
        });
        const float speed = StateOf(car).forwardSpeed;
        INFO(c.what << ": after 12 s at full throttle the car does " << speed << " m/s in gear " << StateOf(car).gear
                    << ", first gear's top speed being " << firstGearTop);
        if (c.limited)
        {
            REQUIRE(speed < firstGearTop + 1.0f);
        }
        else
        {
            REQUIRE(speed > firstGearTop + 5.0f);
        }
        if (std::string(c.what) == "one gear")
        {
            INFO("with one gear it left second gear at once and never went above first");
            REQUIRE(top == 1);
        }
    }
}

TEST_CASE("Switching Automatic and Manual mid-drive, even mid-shift, takes over at once and drives on",
          "[physics][vehicles][vehicle-gearbox]")
{
    Parked parked;
    Car& car = parked.car;
    const PhysicsWorld* world = PhysicsWorld::Of(parked.test.app.GetActiveScene());
    InputOf(car).forward = 1.0f;
    REQUIRE(RunUntil(parked.test, 10.0, [&car] { return StateOf(car).shifting; }));

    WheeledOf(car).transmission.mode = TransmissionMode::Manual;
    InputOf(car).gear = 3;
    parked.test.app.RunFrame(tick);
    INFO("switched to Manual mid-shift, the car is in the gear asked for at once, its clutch engaged, not shifting");
    REQUIRE(StateOf(car).gear == 3);
    REQUIRE(StateOf(car).clutch == Approx(0.0f).margin(1e-6));
    REQUIRE_FALSE(StateOf(car).shifting);
    const float manual = StateOf(car).forwardSpeed;
    parked.test.Run(1.0);
    INFO("and it drives on in third, from " << manual << " to " << StateOf(car).forwardSpeed << " m/s");
    REQUIRE(StateOf(car).gear == 3);
    REQUIRE(StateOf(car).forwardSpeed > manual + 1.0f);

    WheeledOf(car).transmission.mode = TransmissionMode::Automatic;
    const float automatic = StateOf(car).forwardSpeed;
    int top = 0;
    RunUntil(parked.test, 4.0, [&] {
        top = std::max(top, StateOf(car).gear);
        return false;
    });
    INFO("back in Automatic it shifts by itself, up to gear " << top << ", and drives on, from " << automatic << " to "
                                                              << StateOf(car).forwardSpeed << " m/s");
    REQUIRE(top >= 4);
    REQUIRE(StateOf(car).forwardSpeed > automatic + 3.0f);

    // Mid-shift into Manual's neutral, braking: an automatic shift left unfinished would keep the chassis awake.
    REQUIRE(RunUntil(parked.test, 10.0, [&car] { return StateOf(car).shifting; }));
    WheeledOf(car).transmission.mode = TransmissionMode::Manual;
    InputOf(car) = VehicleInput{.brake = 1.0f, .gear = 0};
    INFO("stopped in neutral, the car falls asleep");
    REQUIRE(RunUntil(parked.test, 20.0, [&] { return world->IsSleeping(car.chassis); }));
}

namespace
{
    /** An out-of-range engine or gearbox value, another of the same kind, and how many warnings it gives. */
    struct OutOfRange
    {
        const char* what;
        void (*made)(WheeledVehicle&);
        void (*editedAgain)(WheeledVehicle&);
        // A range made empty also moves the RPMs that lay in it.
        std::size_t warnings = 1;
    };

    const OutOfRange outOfRange[] = {
        {"a negative maximum torque", [](WheeledVehicle& w) { w.engine.maxTorque = -100.0f; },
         [](WheeledVehicle& w) { w.engine.torqueAtMinRpm = -0.5f; }},
        {"a negative torque fraction", [](WheeledVehicle& w) { w.engine.torqueAtMaxRpm = -0.1f; },
         [](WheeledVehicle& w) { w.engine.maxTorque = -1.0f; }},
        {"a negative engine inertia", [](WheeledVehicle& w) { w.engine.inertia = -1.0f; },
         [](WheeledVehicle& w) { w.engine.inertia = -2.0f; }},
        {"a maximum RPM below the minimum",
         [](WheeledVehicle& w) {
             w.engine.minRpm = 1000.0f;
             w.engine.maxRpm = 900.0f;
         },
         [](WheeledVehicle& w) { w.engine.maxRpm = 1000.0f; }, 4},
        {"a peak torque RPM outside the range", [](WheeledVehicle& w) { w.engine.peakTorqueRpm = 7000.0f; },
         [](WheeledVehicle& w) { w.engine.peakTorqueRpm = 500.0f; }},
        {"a shift point outside the range", [](WheeledVehicle& w) { w.transmission.shiftUpRpm = 7000.0f; },
         [](WheeledVehicle& w) { w.transmission.shiftDownRpm = 500.0f; }},
        {"a shift-down RPM above the shift-up RPM", [](WheeledVehicle& w) { w.transmission.shiftDownRpm = 4500.0f; },
         [](WheeledVehicle& w) { w.transmission.shiftDownRpm = 5000.0f; }},
        {"no gears", [](WheeledVehicle& w) { w.transmission.gearCount = 0; },
         [](WheeledVehicle& w) { w.transmission.gearCount = -2; }},
        {"a limited-slip ratio of 1", [](WheeledVehicle& w) { w.limitedSlipRatio = 1.0f; },
         [](WheeledVehicle& w) { w.limitedSlipRatio = 0.5f; }},
    };
} // namespace

TEST_CASE("Each out-of-range engine and gearbox value is corrected with one warning per vehicle, until a valid value "
          "clears it",
          "[physics][vehicles][vehicle-gearbox]")
{
    for (const OutOfRange& value : outOfRange)
    {
        PhysicsApp test;
        AddFloor(test.Entities());
        // Two cars holding it, far apart, driving.
        std::vector<Car> cars;
        for (const float x : {0.0f, 10.0f})
        {
            Car car = AddCar(test.Entities(), Math::Vec3(x, 1.0f, 0.0f));
            value.made(WheeledOf(car));
            InputOf(car).forward = 0.5f;
            cars.push_back(car);
        }

        test.Run(1.0);
        INFO(value.what << " gives " << value.warnings << " warnings for each of the two cars holding it, however many "
                        << "ticks it is applied; the warnings were:");
        for (const std::string& warning : test.warnings)
        {
            INFO("  " << warning);
        }
        REQUIRE(test.warnings.size() == 2 * value.warnings);
        INFO("the first names the first car's chassis");
        REQUIRE(test.warnings.front().find("entity " + std::to_string(cars[0].chassis.GetId())) != std::string::npos);
        INFO("and the corrected car is sound (" << StateOf(cars[0]).forwardSpeed << " m/s, "
                                                << StateOf(cars[0]).engineRpm << " RPM)");
        REQUIRE(std::isfinite(StateOf(cars[0]).engineRpm));
        REQUIRE(std::isfinite(StateOf(cars[0]).forwardSpeed));

        value.editedAgain(WheeledOf(cars[0]));
        test.Run(0.5);
        INFO("edited to another value of the same kind, the car is not warned about again");
        REQUIRE(test.warnings.size() == 2 * value.warnings);

        WheeledOf(cars[0]) = WheeledVehicle{};
        test.Run(0.5);
        value.made(WheeledOf(cars[0]));
        test.Run(0.5);
        INFO("given a valid value and then the out-of-range one again, it is warned about again");
        REQUIRE(test.warnings.size() == 3 * value.warnings);
        INFO("and nothing failed in the backend");
        REQUIRE(test.errors.empty());
    }
}
