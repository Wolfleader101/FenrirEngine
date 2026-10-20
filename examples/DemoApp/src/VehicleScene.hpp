#pragma once

// The Vehicles (C++) scene: a car (ADR-0015) driven from the keyboard round a small track, built in C++. F6 steps to
// it from Default and back.
//
// The car is a vehicle on its chassis: a dynamic RigidBody with an unscaled box Collider and a Vehicle, its look a
// scaled child with no collider. Each wheel is a VehicleWheel on its own unscaled wheel entity, not a child of the
// chassis, naming the chassis; physics poses the wheel entity, and its look is a child: a sphere scaled to the tyre,
// with a stripe across it to show its spin. Every setting is the default but the front wheels' steering; a
// VehicleDifferential on an entity of its own drives the rear wheels through an automatic gearbox, and two
// VehicleAntiRollBars, front and rear, keep it level in corners. Game code in Tick turns the keys into driver input;
// physics writes the car's state and each wheel's back after every step, and their names show it every frame. Settings
// are live edits: 1 softens the springs and 2 fits bigger wheels (their looks rescaled), each changing the ride height
// as the car drives, and 7 limits the chassis's tilt to 60 degrees; each key again undoes it.
//
// M switches the car's gearbox between Automatic and Manual. In Manual, game code changes gear: E and Q shift up and
// down (down past neutral is reverse), W is the pedal in the gear's direction, S brakes, and Shift holds the clutch.
//
// The track is built one function per piece, each with its own leg of the autopilot route, so later pieces add a
// function, a leg and their keys without touching the others': tarmac (friction 0.5) with a white start pad at the
// origin, facing north (-Z); ten bumps along the start straight from z = -25 to -43; and a ramp at z = -70 rising to
// 2 m, then a drop.
//
// The camera is a VehicleCamera: C++ game code that chases the car and writes the scene's DemoCamera, which
// DemoCameras copies into the app's camera. C frees the camera (WASD flies it, the arrows still drive) and snaps it
// back.

#ifdef FENRIR_PHYSICS

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "DemoCamera.hpp"
#include "DemoScenes.hpp"
#include "Events.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Vehicles.hpp"
#include "Material.hpp"
#include "ModelLibrary.hpp"

/**
 * @brief A chase camera, as game code writes one: behind and above what it follows, turning with it a little late,
 * and writing where it is into a DemoCamera. Switched off, it leaves the DemoCamera inactive.
 */
class VehicleCamera
{
  public:
    [[nodiscard]] bool Following() const
    {
        return m_following;
    }

    void Toggle()
    {
        m_following = !m_following;
    }

    /** @brief Each frame: swings round behind `target`'s forward (-Z) and writes the camera into `camera`. */
    void Update(DemoCamera& camera, const Fenrir::Transform& target, float dt)
    {
        camera.Active = m_following ? 1 : 0;
        if (!m_following)
        {
            return;
        }
        Fenrir::Math::Vec3 ahead = target.rot * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
        ahead.y = 0.0f;
        // Facing straight up or down, it keeps the way it looked.
        if (Fenrir::Math::Magnitude(ahead) > 0.1f)
        {
            float turn = std::atan2(ahead.z, ahead.x) / degrees - m_yaw;
            turn -= 360.0f * std::round(turn / 360.0f);
            m_yaw += turn * std::min(1.0f, catchUp * dt);
        }
        const float yaw = m_yaw * degrees;
        const float pitch = pitchDegrees * degrees;
        const Fenrir::Math::Vec3 front(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                                       std::cos(pitch) * std::sin(yaw));
        camera.Position = target.pos + Fenrir::Math::Vec3(0.0f, height, 0.0f) - front * distance;
        camera.Yaw = m_yaw;
        camera.Pitch = pitchDegrees;
    }

  private:
    static constexpr float degrees = std::numbers::pi_v<float> / 180.0f;
    static constexpr float distance = 9.0f;
    static constexpr float height = 1.5f;
    static constexpr float pitchDegrees = -15.0f;
    static constexpr float catchUp = 3.0f; // How much of its turn it makes up per second.

    float m_yaw = -90.0f;
    bool m_following = true;
};

class VehicleScene
{
  public:
    static constexpr const char* name = "Vehicles (C++)";

    /** @brief The scene as the switcher steps to it: F6 from Default and back. */
    static DemoScene Scene()
    {
        DemoScene scene{.name = name,
                        .familyKey = DemoFamilyKey::vehicles,
                        .keyHelp = "W or Up drives, S or Down brakes and then reverses once stopped, A/D or Left/Right "
                                   "steer, Space pulls the handbrake, C frees the camera (WASD flies it, the arrows "
                                   "still drive) and snaps it back. North of the start: ten bumps, then a ramp. L logs "
                                   "the car's state, P drives the track by itself. M switches the gearbox between "
                                   "Automatic and Manual; in Manual E/Q shift up and down (down past neutral is "
                                   "reverse), W drives in that gear, S brakes, and Shift holds the clutch. 1 softens "
                                   "the springs, 2 fits big wheels, 7 limits the tilt to 60 degrees (each again to "
                                   "undo). F6 goes on.",
                        .smokeKeys = {GLFW_KEY_P}};
        // The autopilot drives every piece while L logs the state, then stops, and each key is tapped twice.
        scene.smokeKeys.insert(scene.smokeKeys.end(), 70, GLFW_KEY_L);
        scene.smokeKeys.push_back(GLFW_KEY_P);
        for (const int key : {GLFW_KEY_C, GLFW_KEY_SPACE, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_7})
        {
            scene.smokeKeys.insert(scene.smokeKeys.end(), {key, key});
        }
        // Manual, up two gears, down one, the clutch, and back to Automatic.
        scene.smokeKeys.insert(scene.smokeKeys.end(), {GLFW_KEY_M, GLFW_KEY_E, GLFW_KEY_E, GLFW_KEY_Q,
                                                       GLFW_KEY_LEFT_SHIFT, GLFW_KEY_L, GLFW_KEY_M});
        scene.smokeKeys.push_back(GLFW_KEY_L);
        return scene;
    }

    VehicleScene(const Model& cube, const Model& sphere, const Shader& flatShader)
        : m_cube(&cube), m_sphere(&sphere), m_flatShader(&flatShader)
    {
    }

    // PostInit: the models and the flat shader are loaded in Init.
    void Init(Fenrir::App& app)
    {
        m_entities = &app.CreateScene(name).GetEntityList();
        BuildTarmac();
        BuildBumps();
        BuildRamp();
        MakeCar();
        GiveCarDrivetrain();

        m_camera = m_entities->CreateEntity();
        m_camera.GetComponent<Fenrir::Name>().Set("Vehicle camera");
        m_camera.AddComponent<DemoCamera>();
    }

    // Update, while the scene is active and before DemoCameras: keys, the camera, and the car's readings.
    void Update(Fenrir::App& app)
    {
        const bool active = app.GetActiveScene().GetName() == name;
        for (const KeyboardKeyEvent& event : m_keys.Read(app))
        {
            if (event.state == InputState::Released)
            {
                m_held.erase(event.key);
                continue;
            }
            if (!active || event.state != InputState::Pressed)
            {
                continue;
            }
            m_held.insert(event.key);
            OnKey(app, event.key);
        }
        if (!active)
        {
            return;
        }
        m_followCamera.Update(m_camera.GetComponent<DemoCamera>(), Fenrir::GetWorldTransform(m_car.chassis),
                              static_cast<float>(app.GetTime().deltaTime));
        ShowReadings();
    }

    // Tick, while the scene is active: game code reads the car's state and writes its driver input.
    void Tick(Fenrir::App& app)
    {
        if (app.GetActiveScene().GetName() != name)
        {
            return;
        }
        ++m_tick;
        Drive(app);
    }

  private:
    struct Driven
    {
        Fenrir::Entity chassis;
        // In wheel order: front left, front right, rear left, rear right.
        std::vector<Fenrir::Entity> wheels;
        // The way the keys last drove it: S brakes while it goes forward, then reverses.
        float direction = 1.0f;
    };

    // A point the autopilot drives through, and how fast it takes the way there.
    struct Waypoint
    {
        Fenrir::Math::Vec3 at;
        float speed = 12.0f; // m/s
        bool manual = false; // Whether the way there is driven in Manual, changing gear by hand.
    };

    void OnKey(Fenrir::App& app, int key)
    {
        if (key == GLFW_KEY_C)
        {
            m_followCamera.Toggle();
            app.Logger()->Info(m_followCamera.Following() ? "C: the camera chases the car"
                                                          : "C: camera free: WASD flies it, the arrows drive");
        }
        else if (key == GLFW_KEY_L)
        {
            LogState(app);
        }
        else if (key == GLFW_KEY_P)
        {
            m_autopilot = !m_autopilot;
            m_waypoint = 0;
            app.Logger()->Info(m_autopilot ? "P: autopilot: driving the track" : "P: autopilot off");
        }
        else if (key == GLFW_KEY_M)
        {
            m_manual = !m_manual;
            Fenrir::Vehicle& vehicle = m_car.chassis.GetComponent<Fenrir::Vehicle>();
            // On in the gear it is in, or first from neutral.
            vehicle.input.gear = vehicle.state.gear == 0 ? 1 : vehicle.state.gear;
            app.Logger()->Info(m_manual ? "M: Manual gearbox: E/Q shift, Shift holds the clutch"
                                        : "M: Automatic gearbox");
        }
        else if ((key == GLFW_KEY_E || key == GLFW_KEY_Q) && m_manual)
        {
            Shift(key == GLFW_KEY_E ? 1 : -1);
            app.Logger()->Info("{}: gear {}", key == GLFW_KEY_E ? "E" : "Q",
                               m_car.chassis.GetComponent<Fenrir::Vehicle>().input.gear);
        }
        else if (key == GLFW_KEY_1 || key == GLFW_KEY_2 || key == GLFW_KEY_7)
        {
            OnSettingsKey(app, key);
        }
    }

    // 1, 2 and 7: live edits to the car's settings, each undone by pressing it again.
    void OnSettingsKey(Fenrir::App& app, int key)
    {
        if (key == GLFW_KEY_1)
        {
            m_softSprings = !m_softSprings;
            for (Fenrir::Entity& wheel : m_car.wheels)
            {
                wheel.GetComponent<Fenrir::VehicleWheel>().suspension.spring =
                    m_softSprings ? Fenrir::JointSpring{.frequency = 1.15f, .damping = 0.4f}
                                  : Fenrir::JointSpring{.frequency = 1.5f, .damping = 0.5f};
            }
            app.Logger()->Info(m_softSprings ? "1: soft springs, 1.15 Hz: the car sits lower and rolls more"
                                             : "1: stock springs, 1.5 Hz");
        }
        else if (key == GLFW_KEY_2)
        {
            m_bigWheels = !m_bigWheels;
            const float radius = m_bigWheels ? 0.45f : 0.3f;
            for (Fenrir::Entity& entity : m_car.wheels)
            {
                Fenrir::VehicleWheel& wheel = entity.GetComponent<Fenrir::VehicleWheel>();
                wheel.radius = radius;
                // The look follows: the tyre, the wheel entity's only child, is scaled to the new size.
                entity.GetComponent<Fenrir::Relationship>().firstChild.GetComponent<Fenrir::Transform>().scale =
                    Fenrir::Math::Vec3(wheel.width, 2.0f * radius, 2.0f * radius);
            }
            app.Logger()->Info(m_bigWheels ? "2: big wheels, 0.45 m: the car sits higher" : "2: stock wheels, 0.3 m");
        }
        else
        {
            Fenrir::Vehicle& vehicle = m_car.chassis.GetComponent<Fenrir::Vehicle>();
            const bool limited = vehicle.maxTilt < std::numbers::pi_v<float>;
            vehicle.maxTilt = limited ? std::numbers::pi_v<float> : 60.0f * degrees;
            app.Logger()->Info(limited ? "7: tilt limit off"
                                       : "7: tilt limited to 60 degrees: the car cannot roll over");
        }
    }

    // Manual: one gear up or down, from reverse (-1) through neutral to the top gear.
    void Shift(int by)
    {
        Fenrir::Vehicle& vehicle = m_car.chassis.GetComponent<Fenrir::Vehicle>();
        const int top = vehicle.kind.As<Fenrir::WheeledVehicle>().transmission.gearCount;
        vehicle.input.gear = std::clamp(vehicle.input.gear + by, -1, top);
    }

    // The keys as normalised input, or the autopilot's while it drives. While the camera is free, WASD is the
    // camera's, so only the arrows drive.
    void Drive(Fenrir::App& app)
    {
        const bool wasd = m_followCamera.Following();
        const auto held = [this](int key) { return m_held.contains(key); };
        float throttle = held(GLFW_KEY_UP) || (wasd && held(GLFW_KEY_W)) ? 1.0f : 0.0f;
        float back = held(GLFW_KEY_DOWN) || (wasd && held(GLFW_KEY_S)) ? 1.0f : 0.0f;
        float steer = 0.0f;
        steer += held(GLFW_KEY_RIGHT) || (wasd && held(GLFW_KEY_D)) ? 1.0f : 0.0f;
        steer -= held(GLFW_KEY_LEFT) || (wasd && held(GLFW_KEY_A)) ? 1.0f : 0.0f;
        float clutch = held(GLFW_KEY_LEFT_SHIFT) || held(GLFW_KEY_RIGHT_SHIFT) ? 1.0f : 0.0f;
        bool manual = m_manual;
        if (m_autopilot)
        {
            Autopilot(app, throttle, back, steer, manual, clutch);
        }

        Fenrir::Vehicle& vehicle = m_car.chassis.GetComponent<Fenrir::Vehicle>();
        vehicle.kind.As<Fenrir::WheeledVehicle>().transmission.mode =
            manual ? Fenrir::TransmissionMode::Manual : Fenrir::TransmissionMode::Automatic;
        vehicle.input.steer = steer;
        vehicle.input.handbrake = held(GLFW_KEY_SPACE) ? 1.0f : 0.0f;
        vehicle.input.clutch = clutch;
        if (manual)
        {
            // The gear sets the direction: forward is the pedal, and back brakes.
            vehicle.input.forward = throttle;
            vehicle.input.brake = back;
            return;
        }
        // Asked to go the other way, it brakes until it has all but stopped, then drives that way.
        const float speed = vehicle.state.forwardSpeed;
        float forward = throttle - back;
        float brake = 0.0f;
        if (forward * m_car.direction < 0.0f)
        {
            if (std::abs(speed) > stoppedSpeed && speed * forward < 0.0f)
            {
                brake = std::abs(forward);
                forward = 0.0f;
            }
            else
            {
                m_car.direction = forward > 0.0f ? 1.0f : -1.0f;
            }
        }
        vehicle.input.forward = forward;
        vehicle.input.brake = brake;
    }

    // Steers for the next waypoint of the route, and holds its speed, in the waypoint's gearbox mode.
    void Autopilot(Fenrir::App& app, float& throttle, float& back, float& steer, bool& manual, float& clutch)
    {
        const std::vector<Waypoint>& route = Route();
        const Waypoint& next = route[m_waypoint % route.size()];
        const Fenrir::Transform at = Fenrir::GetWorldTransform(m_car.chassis);
        Fenrir::Math::Vec3 to = next.at - at.pos;
        to.y = 0.0f;
        if (Fenrir::Math::Magnitude(to) < reached)
        {
            ++m_waypoint;
            app.Logger()->Info("Autopilot: waypoint {} reached", m_waypoint % route.size());
            return;
        }
        const Fenrir::Math::Vec3 ahead = at.rot * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
        const Fenrir::Math::Vec3 right = at.rot * Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f);
        const float angle = std::atan2(Fenrir::Math::Dot(to, right), Fenrir::Math::Dot(to, ahead));
        steer = std::clamp(2.0f * angle, -1.0f, 1.0f);
        // Slower through a sharp turn.
        const float wanted = std::abs(angle) > 0.5f ? 0.5f * next.speed : next.speed;
        const float speed = m_car.chassis.GetComponent<Fenrir::Vehicle>().state.forwardSpeed;
        throttle = speed < wanted ? 0.7f : 0.0f;
        back = 0.0f;
        manual = next.manual;
        if (manual)
        {
            clutch = ShiftByHand();
        }
    }

    // The autopilot in Manual, as a driver would: up a gear past 4000 RPM, down below 2000, never more often than
    // twice a second, the clutch held for a moment each time. Returns the clutch.
    float ShiftByHand()
    {
        const Fenrir::Vehicle& vehicle = m_car.chassis.GetComponent<Fenrir::Vehicle>();
        const int top = vehicle.kind.As<Fenrir::WheeledVehicle>().transmission.gearCount;
        const int gear = vehicle.input.gear;
        if (m_tick - m_shifted >= shiftTicks)
        {
            const float rpm = vehicle.state.engineRpm;
            int wanted = gear;
            if (gear < 1)
            {
                wanted = 1;
            }
            else if (rpm > 4000.0f && gear < top)
            {
                wanted = gear + 1;
            }
            else if (rpm < 2000.0f && gear > 1)
            {
                wanted = gear - 1;
            }
            if (wanted != gear)
            {
                Shift(wanted - gear);
                m_shifted = m_tick;
            }
        }
        return m_tick - m_shifted < clutchTicks ? 1.0f : 0.0f;
    }

    // Track pieces, each with its leg of the autopilot route.

    // The ground: tarmac all round, and the start pad, a look only.
    void BuildTarmac()
    {
        Static("Tarmac", Fenrir::Math::Vec3(0.0f, -0.5f, -30.0f), Fenrir::Math::Vec3(200.0f, 1.0f, 260.0f),
               Fenrir::Math::Vec3(0.35f))
            .AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.friction = 0.5f});
        Look("Start", Fenrir::Math::Vec3(0.0f, 0.005f, 0.0f), Fenrir::Math::Vec3(4.0f, 0.01f, 6.0f),
             Fenrir::Math::Vec3(0.95f));
    }

    // Square bars turned 45° about their length, 0.14 m high, every 2 m along the start straight.
    void BuildBumps()
    {
        for (int i = 0; i < 10; ++i)
        {
            Fenrir::Entity bump = Static("Bump", Fenrir::Math::Vec3(0.0f, 0.0f, -25.0f - 2.0f * static_cast<float>(i)),
                                         Fenrir::Math::Vec3(8.0f, 0.2f, 0.2f), Fenrir::Math::Vec3(0.8f, 0.7f, 0.2f));
            bump.GetComponent<Fenrir::Transform>().rot =
                Fenrir::Math::AngleAxis(0.25f * std::numbers::pi_v<float>, Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));
        }
    }

    // Up the straight over the bumps, slowly.
    static std::vector<Waypoint> BumpsLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, -20.0f), .speed = 6.0f},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, -48.0f), .speed = 6.0f}};
    }

    // 10 m long and 6 m wide, rising north to 2 m, then a drop.
    void BuildRamp()
    {
        const float rise = std::asin(2.0f / 10.0f);
        Fenrir::Entity ramp = Static("Ramp", Fenrir::Math::Vec3(0.0f, 1.0f - 0.25f, -70.0f),
                                     Fenrir::Math::Vec3(6.0f, 0.5f, 10.0f), Fenrir::Math::Vec3(0.6f, 0.45f, 0.3f));
        ramp.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(rise, Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));
    }

    // Over the ramp and off its end, round to the east and back to the start.
    static std::vector<Waypoint> RampLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, -60.0f), .speed = 14.0f},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, -100.0f), .speed = 14.0f},
                Waypoint{.at = Fenrir::Math::Vec3(25.0f, 0.0f, -110.0f)},
                Waypoint{.at = Fenrir::Math::Vec3(25.0f, 0.0f, 10.0f)},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, 15.0f)},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f), .speed = 6.0f}};
    }

    // The gearbox: a loop to the west in Manual, the autopilot changing gear by hand, then back to the start.
    static std::vector<Waypoint> GearboxLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(-25.0f, 0.0f, -10.0f), .manual = true},
                Waypoint{.at = Fenrir::Math::Vec3(-25.0f, 0.0f, -110.0f), .speed = 20.0f, .manual = true},
                Waypoint{.at = Fenrir::Math::Vec3(-50.0f, 0.0f, -110.0f), .manual = true},
                Waypoint{.at = Fenrir::Math::Vec3(-50.0f, 0.0f, 10.0f), .speed = 20.0f, .manual = true},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, 15.0f)},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f), .speed = 6.0f}};
    }

    // The route: each piece's leg in turn, over and over.
    static const std::vector<Waypoint>& Route()
    {
        static const std::vector<Waypoint> route = [] {
            std::vector<Waypoint> legs;
            for (const std::vector<Waypoint>& leg : {BumpsLeg(), RampLeg(), GearboxLeg()})
            {
                legs.insert(legs.end(), leg.begin(), leg.end());
            }
            return legs;
        }();
        return route;
    }

    // Vehicles.

    // A 1500 kg car 4 m long on the start pad, its front wheels steering, every other setting the default.
    void MakeCar()
    {
        const Fenrir::Math::Vec3 at(0.0f, 1.0f, 0.0f);
        m_car.chassis =
            Chassis("Car", at, Fenrir::Math::Vec3(0.9f, 0.25f, 2.0f), 1500.0f, Fenrir::Math::Vec3(0.85f, 0.15f, 0.1f));
        m_car.chassis.AddComponent<Fenrir::Vehicle>();
        for (const float z : {-1.4f, 1.4f})
        {
            for (const float x : {-0.9f, 0.9f})
            {
                m_car.wheels.push_back(Wheel(Fenrir::VehicleWheel{.vehicle = m_car.chassis,
                                                                  .position = Fenrir::Math::Vec3(x, -0.55f, z),
                                                                  .maxSteerAngle = z < 0.0f ? 0.5f : 0.0f},
                                             at));
            }
        }
    }

    // The car's rear-wheel drive and anti-roll bars: a VehicleDifferential naming its rear wheels replaces the
    // all-wheel-drive default, and a VehicleAntiRollBar across each pair of wheels keeps it level in corners. Each is
    // an entity of its own, naming two wheel entities.
    void GiveCarDrivetrain()
    {
        Fenrir::Entity differential = m_entities->CreateEntity();
        differential.GetComponent<Fenrir::Name>().Set("Car rear differential");
        differential.AddComponent<Fenrir::VehicleDifferential>(
            Fenrir::VehicleDifferential{.leftWheel = m_car.wheels[2], .rightWheel = m_car.wheels[3]});
        for (const std::size_t left : {0u, 2u})
        {
            Fenrir::Entity bar = m_entities->CreateEntity();
            bar.GetComponent<Fenrir::Name>().Set(left == 0 ? "Car front anti-roll bar" : "Car rear anti-roll bar");
            bar.AddComponent<Fenrir::VehicleAntiRollBar>(
                Fenrir::VehicleAntiRollBar{.leftWheel = m_car.wheels[left], .rightWheel = m_car.wheels[left + 1]});
        }
    }

    // A chassis: unscaled, with a box collider of `half` extents, its look a scaled child with no collider.
    Fenrir::Entity Chassis(const char* label, const Fenrir::Math::Vec3& at, const Fenrir::Math::Vec3& half, float mass,
                           const Fenrir::Math::Vec3& colour)
    {
        Fenrir::Entity chassis = m_entities->CreateEntity();
        chassis.GetComponent<Fenrir::Name>().Set(label);
        chassis.GetComponent<Fenrir::Transform>().pos = at;
        chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = mass});
        chassis.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{half}});
        Fenrir::Entity look = Look("Chassis look", Fenrir::Math::Vec3(0.0f), 2.0f * half, colour);
        chassis.AddChild(look);
        return chassis;
    }

    // A wheel entity at its place on the chassis made at `at`: unscaled, posed by physics, not the chassis's child. Its
    // look is a child sphere scaled to the tyre, with a white stripe across it to show its spin.
    Fenrir::Entity Wheel(const Fenrir::VehicleWheel& settings, const Fenrir::Math::Vec3& at)
    {
        Fenrir::Entity wheel = m_entities->CreateEntity();
        wheel.GetComponent<Fenrir::Name>().Set("Wheel");
        wheel.GetComponent<Fenrir::Transform>().pos = at + settings.position;
        wheel.AddComponent<Fenrir::VehicleWheel>(settings);
        Fenrir::Entity tyre = Look("Tyre", Fenrir::Math::Vec3(0.0f),
                                   Fenrir::Math::Vec3(settings.width, 2.0f * settings.radius, 2.0f * settings.radius),
                                   Fenrir::Math::Vec3(0.1f), m_sphere);
        wheel.AddChild(tyre);
        Fenrir::Entity stripe =
            Look("Stripe", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(1.05f, 0.25f, 0.9f), Fenrir::Math::Vec3(0.95f));
        tyre.AddChild(stripe);
        return wheel;
    }

    // A static box: its collider the unit box, scaled by its Transform as its look is.
    Fenrir::Entity Static(const char* label, const Fenrir::Math::Vec3& position, const Fenrir::Math::Vec3& size,
                          const Fenrir::Math::Vec3& colour)
    {
        Fenrir::Entity box = Look(label, position, size, colour);
        box.AddComponent<Fenrir::Collider>();
        return box;
    }

    // A model (the unit cube by default) scaled to `size`, in a flat colour.
    Fenrir::Entity Look(const char* label, const Fenrir::Math::Vec3& position, const Fenrir::Math::Vec3& size,
                        const Fenrir::Math::Vec3& colour, const Model* model = nullptr)
    {
        Fenrir::Entity box = m_entities->CreateEntity();
        box.GetComponent<Fenrir::Name>().Set(label);
        box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, size);
        Material material{*m_flatShader, {}};
        material.properties["color"] = colour;
        box.AddComponent<Material>(material);
        box.AddComponent<Model>(model == nullptr ? *m_cube : *model);
        return box;
    }

    // Readings.

    static std::string NameOf(const Fenrir::Entity& entity)
    {
        return entity.IsValid() ? std::string(entity.GetComponent<Fenrir::Name>().Get()) : "?";
    }

    // The car's and each wheel's state in their names, every frame.
    void ShowReadings()
    {
        const Fenrir::VehicleState& state = m_car.chassis.GetComponent<Fenrir::Vehicle>().state;
        std::array<char, 256> text{};
        std::snprintf(text.data(), text.size(), "Car: %.0f km/h, %.0f RPM, gear %d%s, clutch %.2f, %d on ground",
                      static_cast<double>(state.forwardSpeed * 3.6f), static_cast<double>(state.engineRpm), state.gear,
                      state.shifting ? ", shifting" : "", static_cast<double>(state.clutch), state.wheelsOnGround);
        m_car.chassis.GetComponent<Fenrir::Name>().Set(text.data());
        for (std::size_t i = 0; i < m_car.wheels.size(); ++i)
        {
            const Fenrir::VehicleWheelState& wheel = m_car.wheels[i].GetComponent<Fenrir::VehicleWheel>().state;
            if (wheel.onGround)
            {
                std::snprintf(text.data(), text.size(), "Wheel %zu: %s, compression %.3f m, slip %.2f / %.1f°", i,
                              NameOf(wheel.groundCollider).c_str(), static_cast<double>(wheel.compression),
                              static_cast<double>(wheel.longitudinalSlip),
                              static_cast<double>(wheel.lateralSlip / degrees));
            }
            else
            {
                std::snprintf(text.data(), text.size(), "Wheel %zu: in air", i);
            }
            m_car.wheels[i].GetComponent<Fenrir::Name>().Set(text.data());
        }
    }

    // L: the car's whole state and each wheel's.
    void LogState(Fenrir::App& app) const
    {
        const Fenrir::VehicleState& s = m_car.chassis.GetComponent<Fenrir::Vehicle>().state;
        app.Logger()->Info("L (tick {}): car {:.2f} m/s, {:.0f} RPM, gear {}, clutch {:.2f}, shifting {}, {} wheels on "
                           "ground",
                           m_tick, s.forwardSpeed, s.engineRpm, s.gear, s.clutch, s.shifting, s.wheelsOnGround);
        for (std::size_t i = 0; i < m_car.wheels.size(); ++i)
        {
            const Fenrir::VehicleWheelState& w = m_car.wheels[i].GetComponent<Fenrir::VehicleWheel>().state;
            app.Logger()->Info("  wheel {}: on ground {}, body {}, collider {}, contact ({:.2f}, {:.2f}, {:.2f}), "
                               "normal ({:.2f}, {:.2f}, {:.2f}), ground velocity ({:.2f}, {:.2f}, {:.2f}), compression "
                               "{:.3f} m, {:.0f} N, slip {:.3f} / {:.3f} rad, spin {:.1f} rad/s, steer {:.3f} rad",
                               i, w.onGround, w.onGround ? NameOf(w.groundBody) : "none",
                               w.onGround ? NameOf(w.groundCollider) : "none", w.contactPoint.x, w.contactPoint.y,
                               w.contactPoint.z, w.contactNormal.x, w.contactNormal.y, w.contactNormal.z,
                               w.groundVelocity.x, w.groundVelocity.y, w.groundVelocity.z, w.compression,
                               w.suspensionForce, w.longitudinalSlip, w.lateralSlip, w.angularVelocity, w.steerAngle);
        }
    }

    static constexpr float degrees = std::numbers::pi_v<float> / 180.0f;
    static constexpr float stoppedSpeed = 0.5f; // m/s: below it, S reverses rather than brakes.
    static constexpr float reached = 6.0f;      // m: the autopilot moves on this close to a waypoint.
    static constexpr long shiftTicks = 30;      // The autopilot's least time between shifts by hand.
    static constexpr long clutchTicks = 10;     // How long it holds the clutch for each.
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

    const Model* m_cube;
    const Model* m_sphere;
    const Shader* m_flatShader;
    Fenrir::EntityList* m_entities = nullptr;
    Driven m_car;
    Fenrir::Entity m_camera;
    VehicleCamera m_followCamera;
    std::set<int> m_held;
    bool m_autopilot = false;
    bool m_manual = false;        // The gearbox mode M last chose; the autopilot drives each waypoint in its own.
    long m_shifted = -shiftTicks; // The tick the autopilot last shifted by hand.
    bool m_softSprings = false;
    bool m_bigWheels = false;
    std::size_t m_waypoint = 0;
    long m_tick = 0;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
};

#endif // FENRIR_PHYSICS
