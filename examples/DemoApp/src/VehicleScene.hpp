#pragma once

// The Vehicles (C++) scene: a car (ADR-0015) driven from the keyboard round a small track, built in C++. F6 steps to
// it from Default and back.
//
// The car is a vehicle on its chassis: a dynamic RigidBody with an unscaled box Collider and a Vehicle, its look a
// scaled child with no collider. Each wheel is a VehicleWheel on its own unscaled wheel entity, not a child of the
// chassis, naming the chassis; physics poses the wheel entity, and its look is a child: a sphere scaled to the tyre,
// with a stripe across it to show its spin. Every setting is the default but the front wheels' steering, so it drives
// all four wheels through an automatic gearbox. Game code in Tick turns the keys into driver input; physics writes the
// car's state and each wheel's back after every step, and their names show it every frame.
//
// The track is built one function per piece, each with its own leg of the autopilot route, so later pieces add a
// function, a leg and their keys without touching the others': tarmac (friction 0.5) with a white start pad at the
// origin, facing north (-Z); ten bumps along the start straight from z = -25 to -43; and a ramp at z = -70 rising to
// 2 m, then a drop.
//
// The camera is a VehicleCamera: C++ game code that chases the car and writes the scene's DemoCamera, which
// DemoCameras copies into the app's camera. C frees the camera (WASD flies it, the arrows still drive) and snaps it
// back.
//
// A gamepad drives too, read from the scene's DemoGamepad, which DemoGamepads writes each frame. Beyond the deadzone a
// trigger or the left stick wins over its keys, its value going straight into the input; its buttons press the keys
// they stand for (padKeys), so a key's action is the pad's too. The right stick turns the chase camera round the car,
// and it swings back behind on release. Unplugged, the pad reads all zero, which lets go of everything it held.

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
#include "DemoGamepad.hpp"
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

    /**
     * @brief Each frame: swings round behind `target`'s forward (-Z) and writes the camera into `camera`. `look`, each
     * axis in [-1, 1] as a gamepad's stick, turns it round the target and tilts it; at zero it springs back behind.
     */
    void Update(DemoCamera& camera, const Fenrir::Transform& target, const Fenrir::Math::Vec2& look, float dt)
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
        // Pushed down, the stick looks down from higher up.
        const Fenrir::Math::Vec2 orbit(look.x * orbitYawDegrees, -look.y * orbitPitchDegrees);
        m_orbit += (orbit - m_orbit) * std::min(1.0f, orbitCatchUp * dt);
        const float yaw = (m_yaw + m_orbit.x) * degrees;
        const float pitch = (pitchDegrees + m_orbit.y) * degrees;
        const Fenrir::Math::Vec3 front(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                                       std::cos(pitch) * std::sin(yaw));
        camera.Position = target.pos + Fenrir::Math::Vec3(0.0f, height, 0.0f) - front * distance;
        camera.Yaw = m_yaw + m_orbit.x;
        camera.Pitch = pitchDegrees + m_orbit.y;
    }

  private:
    static constexpr float degrees = std::numbers::pi_v<float> / 180.0f;
    static constexpr float distance = 9.0f;
    static constexpr float height = 1.5f;
    static constexpr float pitchDegrees = -15.0f;
    static constexpr float catchUp = 3.0f;           // How much of its turn it makes up per second.
    static constexpr float orbitYawDegrees = 180.0f; // A full push sideways looks at the target from the front.
    static constexpr float orbitPitchDegrees = 20.0f;
    static constexpr float orbitCatchUp = 6.0f; // As catchUp, for following the stick and springing back.

    float m_yaw = -90.0f;
    Fenrir::Math::Vec2 m_orbit = Fenrir::Math::Vec2(0.0f); // Degrees of yaw and pitch the stick has turned it by.
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
                        .keyHelp =
                            "W or Up drives, S or Down brakes and then reverses once stopped, A/D or Left/Right "
                            "steer, Space pulls the handbrake, C frees the camera (WASD flies it, the arrows "
                            "still drive) and snaps it back. North of the start: ten bumps, then a ramp. L logs "
                            "the car's state, P drives the track by itself. F6 goes on. A gamepad drives too: "
                            "the right trigger drives, the left trigger brakes and then reverses, the left stick "
                            "steers, A pulls the handbrake, the right stick looks round the car.",
                        .smokeKeys = {GLFW_KEY_P}};
        // The autopilot drives every piece while L logs the state, then stops, and each key is tapped twice.
        scene.smokeKeys.insert(scene.smokeKeys.end(), 40, GLFW_KEY_L);
        scene.smokeKeys.push_back(GLFW_KEY_P);
        for (const int key : {GLFW_KEY_C, GLFW_KEY_SPACE})
        {
            scene.smokeKeys.insert(scene.smokeKeys.end(), {key, key});
        }
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

        m_camera = m_entities->CreateEntity();
        m_camera.GetComponent<Fenrir::Name>().Set("Vehicle camera");
        m_camera.AddComponent<DemoCamera>();

        m_gamepad = m_entities->CreateEntity();
        m_gamepad.GetComponent<Fenrir::Name>().Set("Gamepad");
        m_gamepad.AddComponent<DemoGamepad>();
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
            // DemoGamepads writes only the active scene's: let go of what the pad held, so nothing is stuck on return.
            m_gamepad.GetComponent<DemoGamepad>() = DemoGamepad{};
            m_padHeld.clear();
            return;
        }
        PressPadKeys(app);
        m_followCamera.Update(m_camera.GetComponent<DemoCamera>(), Fenrir::GetWorldTransform(m_car.chassis),
                              Beyond(m_gamepad.GetComponent<DemoGamepad>().RightStick),
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
    };

    // A pad button and the key it presses while held.
    struct PadKey
    {
        int button;
        int key;
    };

    // A key no ticket handles yet does nothing, so the bumpers (Q/E: shift down and up), Y (F: get out and in) and
    // Back (R: right the vehicle) work once their keys do.
    static constexpr std::array<PadKey, 5> padKeys = {
        PadKey{GLFW_GAMEPAD_BUTTON_A, GLFW_KEY_SPACE}, PadKey{GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, GLFW_KEY_Q},
        PadKey{GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, GLFW_KEY_E}, PadKey{GLFW_GAMEPAD_BUTTON_Y, GLFW_KEY_F},
        PadKey{GLFW_GAMEPAD_BUTTON_BACK, GLFW_KEY_R}};

    // Each frame: a pad button just pressed presses its key, as the keyboard's would; one let go (or unplugged)
    // releases it. The pad's held keys are kept apart from the keyboard's, so neither lets go of the other's.
    void PressPadKeys(Fenrir::App& app)
    {
        const DemoGamepad& pad = m_gamepad.GetComponent<DemoGamepad>();
        for (const PadKey& padKey : padKeys)
        {
            if (!pad.Held(padKey.button))
            {
                m_padHeld.erase(padKey.key);
            }
            else if (m_padHeld.insert(padKey.key).second)
            {
                OnKey(app, padKey.key);
            }
        }
    }

    // A stick with each axis inside the deadzone read as zero.
    static Fenrir::Math::Vec2 Beyond(const Fenrir::Math::Vec2& stick)
    {
        return {std::abs(stick.x) > deadzone ? stick.x : 0.0f, std::abs(stick.y) > deadzone ? stick.y : 0.0f};
    }

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
    }

    // The keys and the pad as normalised input, or the autopilot's while it drives. While the camera is free, WASD is
    // the camera's, so only the arrows drive.
    void Drive(Fenrir::App& app)
    {
        const bool wasd = m_followCamera.Following();
        const auto held = [this](int key) { return m_held.contains(key) || m_padHeld.contains(key); };
        float throttle = held(GLFW_KEY_UP) || (wasd && held(GLFW_KEY_W)) ? 1.0f : 0.0f;
        float back = held(GLFW_KEY_DOWN) || (wasd && held(GLFW_KEY_S)) ? 1.0f : 0.0f;
        float steer = 0.0f;
        steer += held(GLFW_KEY_RIGHT) || (wasd && held(GLFW_KEY_D)) ? 1.0f : 0.0f;
        steer -= held(GLFW_KEY_LEFT) || (wasd && held(GLFW_KEY_A)) ? 1.0f : 0.0f;
        // The pad wins beyond the deadzone. The left trigger is S's: it brakes, then reverses once stopped.
        const DemoGamepad& pad = m_gamepad.GetComponent<DemoGamepad>();
        throttle = pad.RightTrigger > deadzone ? pad.RightTrigger : throttle;
        back = pad.LeftTrigger > deadzone ? pad.LeftTrigger : back;
        steer = std::abs(pad.LeftStick.x) > deadzone ? pad.LeftStick.x : steer;
        if (m_autopilot)
        {
            Autopilot(app, throttle, back, steer);
        }

        Fenrir::Vehicle& vehicle = m_car.chassis.GetComponent<Fenrir::Vehicle>();
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
        vehicle.input.steer = steer;
        vehicle.input.handbrake = held(GLFW_KEY_SPACE) ? 1.0f : 0.0f;
    }

    // Steers for the next waypoint of the route, and holds its speed.
    void Autopilot(Fenrir::App& app, float& throttle, float& back, float& steer)
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

    // The route: each piece's leg in turn, over and over.
    static const std::vector<Waypoint>& Route()
    {
        static const std::vector<Waypoint> route = [] {
            std::vector<Waypoint> legs;
            for (const std::vector<Waypoint>& leg : {BumpsLeg(), RampLeg()})
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
    static constexpr float deadzone = 0.15f;    // A pad's trigger or stick axis this close to rest leaves the keys be.
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

    const Model* m_cube;
    const Model* m_sphere;
    const Shader* m_flatShader;
    Fenrir::EntityList* m_entities = nullptr;
    Driven m_car;
    Fenrir::Entity m_camera;
    Fenrir::Entity m_gamepad;
    VehicleCamera m_followCamera;
    std::set<int> m_held;
    std::set<int> m_padHeld; // The keys the pad's buttons hold (padKeys).
    bool m_autopilot = false;
    std::size_t m_waypoint = 0;
    long m_tick = 0;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
};

#endif // FENRIR_PHYSICS
