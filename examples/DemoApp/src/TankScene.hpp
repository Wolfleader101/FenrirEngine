#pragma once

// The Tank (C++) scene: a tracked vehicle (ADR-0015) on an open stretch of tarmac, built in C++. F6 steps to it from
// Motorcycle (C++), and on to Default.
//
// The tank is the spike's: a 4000 kg chassis, a dynamic RigidBody with an unscaled box Collider and a Vehicle whose
// kind is the default TrackedVehicle, its look a scaled child with no collider. Its eighteen wheels are VehicleWheels
// on wheel entities of their own, nine a side, each naming its side's track; the end ones are rigid and sit higher.
// Each track is driven at its own speed ratio, and its rearmost wheel is the driven one.
//
// Game code turns steering into track ratios: stopped, A/D pivot it on the spot, one track forward and the other
// back; moving, the inner track runs at 0.6 of the outer. 3 changes both tracks' grip (longitudinalFriction) between
// 4 and 1, a live edit: on 1 the tracks slip and it crawls. A row of 20 kg crates north of the start is there to push
// aside. T puts it back at the start, a Transform write that keeps its engine and tracks turning. 5 switches the
// chassis's interpolation off and on, and the wheels follow it. Its readout, in its name, adds its two track speeds to
// the speed, RPM and gear.
//
// W or Up drives, S or Down brakes and then reverses once stopped, A/D or Left/Right steer, and Space brakes. P drives
// a loop by itself, two long straights with a pivot on the spot at each end. C frees the chase camera (WASD flies it,
// the arrows still drive) and snaps it back; L logs the tank's state.

#ifdef FENRIR_PHYSICS

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <set>
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
#include "VehicleScene.hpp"

class TankScene
{
  public:
    static constexpr const char* name = "Tank (C++)";

    /** @brief The scene as the switcher steps to it: F6 from Motorcycle (C++), and on to Default. */
    static DemoScene Scene()
    {
        DemoScene scene{.name = name,
                        .familyKey = DemoFamilyKey::vehicles,
                        .keyHelp = "W or Up drives, S or Down brakes and then reverses once stopped, A/D or "
                                   "Left/Right steer (it pivots on the spot when stopped), Space brakes, C frees the "
                                   "camera (WASD flies it, the arrows still drive) and snaps it back. P drives a loop "
                                   "by itself, pivoting at each end, L logs the tank's state. 3 changes the tracks' "
                                   "grip between 4 and 1, 5 switches its interpolation off and on, T puts it back at "
                                   "the start. F6 goes on.",
                        .smokeKeys = {}};
        // F2 steps the app's debug lines from off to "vehicles" first, so the run draws every wheel throughout.
        scene.smokeKeys.insert(scene.smokeKeys.end(), 7, GLFW_KEY_F2);
        // The autopilot drives the loop, pivoting at its ends, while L logs the tank, its grip changed twice on the
        // way, then stops.
        scene.smokeKeys.push_back(GLFW_KEY_P);
        scene.smokeKeys.insert(scene.smokeKeys.end(), 14, GLFW_KEY_L);
        scene.smokeKeys.insert(scene.smokeKeys.end(), {GLFW_KEY_3, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_3});
        scene.smokeKeys.insert(scene.smokeKeys.end(), 14, GLFW_KEY_L);
        scene.smokeKeys.insert(scene.smokeKeys.end(), {GLFW_KEY_P, GLFW_KEY_L, GLFW_KEY_T, GLFW_KEY_L});
        for (const int key : {GLFW_KEY_C, GLFW_KEY_SPACE, GLFW_KEY_5})
        {
            scene.smokeKeys.insert(scene.smokeKeys.end(), {key, key});
        }
        scene.smokeKeys.push_back(GLFW_KEY_L);
        return scene;
    }

    TankScene(const Model& cube, const Model& sphere, const Shader& flatShader)
        : m_cube(&cube), m_sphere(&sphere), m_flatShader(&flatShader)
    {
    }

    // PostInit: the models and the flat shader are loaded in Init.
    void Init(Fenrir::App& app)
    {
        m_entities = &app.CreateScene(name).GetEntityList();
        BuildTarmac();
        MakeTank();
        m_camera = m_entities->CreateEntity();
        m_camera.GetComponent<Fenrir::Name>().Set("Tank camera");
        m_camera.AddComponent<DemoCamera>();
    }

    // Update, while the scene is active and before DemoCameras: keys, the camera, and the tank's readings.
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
        m_followCamera.Update(m_camera.GetComponent<DemoCamera>(), Fenrir::GetWorldTransform(m_tank),
                              Fenrir::Math::Vec2(0.0f), static_cast<float>(app.GetTime().deltaTime));
        ShowReadings();
    }

    // Tick, while the scene is active: game code reads the tank's state and writes its driver input.
    void Tick(Fenrir::App& app)
    {
        if (app.GetActiveScene().GetName() == name)
        {
            Drive(app);
        }
    }

  private:
    void OnKey(Fenrir::App& app, int key)
    {
        Fenrir::TrackedVehicle& tracked = m_tank.GetComponent<Fenrir::Vehicle>().kind.As<Fenrir::TrackedVehicle>();
        if (key == GLFW_KEY_C)
        {
            m_followCamera.Toggle();
            app.Logger()->Info(m_followCamera.Following() ? "C: the camera chases the tank"
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
            app.Logger()->Info(m_autopilot ? "P: autopilot: driving the loop" : "P: autopilot off");
        }
        else if (key == GLFW_KEY_3)
        {
            const float grip = tracked.left.longitudinalFriction > 2.0f ? 1.0f : 4.0f;
            tracked.left.longitudinalFriction = grip;
            tracked.right.longitudinalFriction = grip;
            app.Logger()->Info("3: the tracks' grip is {:.0f}, a live edit{}", grip,
                               grip < 2.0f ? ": they slip, and it crawls" : "");
        }
        else if (key == GLFW_KEY_5)
        {
            bool& interpolate = m_tank.GetComponent<Fenrir::RigidBody>().interpolate;
            interpolate = !interpolate;
            app.Logger()->Info(interpolate
                                   ? "5: the tank is interpolated, its wheels with it"
                                   : "5: the tank is not interpolated: it and its wheels are drawn at step poses");
        }
        else if (key == GLFW_KEY_T)
        {
            Respawn(app);
        }
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
        if (m_autopilot)
        {
            Autopilot(app, throttle, back, steer);
        }

        Fenrir::Vehicle& vehicle = m_tank.GetComponent<Fenrir::Vehicle>();
        // Asked to go the other way, it brakes until it has all but stopped, then drives that way.
        const float speed = vehicle.state.forwardSpeed;
        float forward = throttle - back;
        float brake = held(GLFW_KEY_SPACE) ? 1.0f : 0.0f;
        if (forward * m_direction < 0.0f)
        {
            if (std::abs(speed) > stoppedSpeed && speed * forward < 0.0f)
            {
                brake = std::abs(forward);
                forward = 0.0f;
            }
            else
            {
                m_direction = forward > 0.0f ? 1.0f : -1.0f;
            }
        }
        // Steering as track ratios: stopped, the inner track runs backwards and the tank pivots on the spot (it needs
        // the throttle to turn its tracks); moving, the inner track runs slower.
        vehicle.input.leftTrack = 1.0f;
        vehicle.input.rightTrack = 1.0f;
        if (steer != 0.0f)
        {
            const bool pivot = forward == 0.0f && brake == 0.0f && std::abs(speed) < pivotSpeed;
            float& inner = steer > 0.0f ? vehicle.input.rightTrack : vehicle.input.leftTrack;
            inner = pivot ? -1.0f : innerRatio;
            if (pivot)
            {
                forward = std::abs(steer);
            }
        }
        vehicle.input.forward = forward;
        vehicle.input.brake = brake;
    }

    // Drives for the next point of the loop: facing well away from it, it stops and pivots towards it; otherwise it
    // steers for it at its cruising speed.
    void Autopilot(Fenrir::App& app, float& throttle, float& back, float& steer)
    {
        const std::vector<Fenrir::Math::Vec3>& loop = LoopLeg();
        const Fenrir::Math::Vec3& next = loop[m_waypoint % loop.size()];
        const Fenrir::Transform at = Fenrir::GetWorldTransform(m_tank);
        Fenrir::Math::Vec3 to = next - at.pos;
        to.y = 0.0f;
        if (Fenrir::Math::Magnitude(to) < reach)
        {
            ++m_waypoint;
            app.Logger()->Info("Autopilot: point {} reached", m_waypoint % loop.size());
            return;
        }
        const Fenrir::Math::Vec3 ahead = at.rot * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
        const Fenrir::Math::Vec3 right = at.rot * Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f);
        const float angle = std::atan2(Fenrir::Math::Dot(to, right), Fenrir::Math::Dot(to, ahead));
        const float speed = m_tank.GetComponent<Fenrir::Vehicle>().state.forwardSpeed;
        back = 0.0f;
        if (std::abs(angle) > pivotAngle)
        {
            // Stopped first, then round on the spot.
            steer = angle > 0.0f ? 1.0f : -1.0f;
            throttle = 0.0f;
            back = speed > pivotSpeed ? 1.0f : 0.0f;
            return;
        }
        steer = std::abs(angle) > 0.1f ? (angle > 0.0f ? 1.0f : -1.0f) : 0.0f;
        throttle = speed < cruise ? 0.8f : 0.0f;
    }

    // The ground: tarmac all round, with the start pad, a look only, at the origin facing north (-Z), and a row of
    // crates across the way north.
    void BuildTarmac()
    {
        Fenrir::Entity tarmac = Look("Tarmac", Fenrir::Math::Vec3(0.0f, -0.5f, -40.0f),
                                     Fenrir::Math::Vec3(160.0f, 1.0f, 200.0f), Fenrir::Math::Vec3(0.35f));
        tarmac.AddComponent<Fenrir::Collider>();
        tarmac.AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.friction = 0.5f});
        Look("Start", Fenrir::Math::Vec3(0.0f, 0.005f, 0.0f), Fenrir::Math::Vec3(4.0f, 0.01f, 7.0f),
             Fenrir::Math::Vec3(0.95f));
        for (int i = 0; i < 6; ++i)
        {
            Fenrir::Entity crate = Look("Crate", Fenrir::Math::Vec3(-5.0f + 2.0f * static_cast<float>(i), 0.5f, -30.0f),
                                        Fenrir::Math::Vec3(1.0f), Fenrir::Math::Vec3(0.6f, 0.45f, 0.25f));
            crate.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 20.0f});
            crate.AddComponent<Fenrir::Collider>();
        }
    }

    // The loop the autopilot drives: north up a 60 m straight through the crates, a pivot to face west, 25 m across,
    // a pivot to face south, and back down another straight to the start.
    static const std::vector<Fenrir::Math::Vec3>& LoopLeg()
    {
        static const std::vector<Fenrir::Math::Vec3> loop = {
            Fenrir::Math::Vec3(0.0f, 0.0f, -60.0f), Fenrir::Math::Vec3(-25.0f, 0.0f, -60.0f),
            Fenrir::Math::Vec3(-25.0f, 0.0f, 0.0f), Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f)};
        return loop;
    }

    // The spike's tank on the start pad, as the file's header describes.
    void MakeTank()
    {
        m_tank = m_entities->CreateEntity();
        m_tank.GetComponent<Fenrir::Name>().Set("Tank");
        m_tank.GetComponent<Fenrir::Transform>().pos = start;
        m_tank.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 4000.0f});
        const Fenrir::Math::Vec3 half(1.7f, 0.5f, 3.2f);
        m_tank.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{half}});
        m_tank.AddChild(
            Look("Chassis look", Fenrir::Math::Vec3(0.0f), 2.0f * half, Fenrir::Math::Vec3(0.35f, 0.45f, 0.2f)));
        m_tank.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::TrackedVehicle{}});
        for (const float x : {-1.7f, 1.7f})
        {
            for (int i = 0; i < wheelsASide; ++i)
            {
                const bool end = i == 0 || i == wheelsASide - 1;
                m_wheels.push_back(Wheel(Fenrir::VehicleWheel{
                    .vehicle = m_tank,
                    .position = Fenrir::Math::Vec3(x, end ? -0.6f : -0.8f, -2.8f + 0.7f * static_cast<float>(i)),
                    .track = x < 0.0f ? Fenrir::VehicleTrackSide::Left : Fenrir::VehicleTrackSide::Right,
                    .suspension = {.travel = end ? 0.0f : 0.2f, .spring = {.frequency = 1.0f, .damping = 0.5f}}}));
            }
        }
    }

    // T: the tank back on the start pad, level, facing north and still. A Transform write is a teleport: the engine's
    // speed and the tracks' spin carry on, and physics poses the wheels on the chassis at once.
    void Respawn(Fenrir::App& app)
    {
        Fenrir::Transform& at = m_tank.GetComponent<Fenrir::Transform>();
        at.pos = start;
        at.rot = identity;
        Fenrir::RigidBody& body = m_tank.GetComponent<Fenrir::RigidBody>();
        body.linearVelocity = Fenrir::Math::Vec3(0.0f);
        body.angularVelocity = Fenrir::Math::Vec3(0.0f);
        m_waypoint = 0;
        app.Logger()->Info("T: the tank is back at the start, its engine and tracks still turning");
    }

    // A wheel entity at its place on the tank at the start: unscaled, posed by physics, not the chassis's child. Its
    // look is a child sphere scaled to the wheel, with a white stripe across it to show its spin.
    Fenrir::Entity Wheel(const Fenrir::VehicleWheel& settings)
    {
        Fenrir::Entity wheel = m_entities->CreateEntity();
        wheel.GetComponent<Fenrir::Name>().Set("Wheel");
        wheel.GetComponent<Fenrir::Transform>().pos = start + settings.position;
        wheel.AddComponent<Fenrir::VehicleWheel>(settings);
        Fenrir::Entity tyre = Look("Road wheel", Fenrir::Math::Vec3(0.0f),
                                   Fenrir::Math::Vec3(settings.width, 2.0f * settings.radius, 2.0f * settings.radius),
                                   Fenrir::Math::Vec3(0.1f), m_sphere);
        wheel.AddChild(tyre);
        tyre.AddChild(Look("Stripe", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(1.05f, 0.25f, 0.9f),
                           Fenrir::Math::Vec3(0.95f)));
        return wheel;
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

    // The tank's state in its name, every frame, with its two track speeds.
    void ShowReadings()
    {
        const Fenrir::VehicleState& state = m_tank.GetComponent<Fenrir::Vehicle>().state;
        std::array<char, 160> text{};
        std::snprintf(text.data(), text.size(),
                      "Tank: %.0f km/h, %.0f RPM, gear %d%s, tracks %.1f / %.1f m/s, %d on ground",
                      static_cast<double>(state.forwardSpeed * 3.6f), static_cast<double>(state.engineRpm), state.gear,
                      state.shifting ? ", shifting" : "", static_cast<double>(state.leftTrackSpeed),
                      static_cast<double>(state.rightTrackSpeed), state.wheelsOnGround);
        m_tank.GetComponent<Fenrir::Name>().Set(text.data());
    }

    // L: the tank's state, and its two driven wheels' (each track's rearmost).
    void LogState(Fenrir::App& app) const
    {
        const Fenrir::Vehicle& vehicle = m_tank.GetComponent<Fenrir::Vehicle>();
        const Fenrir::VehicleState& s = vehicle.state;
        app.Logger()->Info("L: tank {:.2f} m/s, {:.0f} RPM, gear {}, shifting {}, {} wheels on ground, tracks {:.2f} / "
                           "{:.2f} m/s (ratios {:.1f} / {:.1f})",
                           s.forwardSpeed, s.engineRpm, s.gear, s.shifting, s.wheelsOnGround, s.leftTrackSpeed,
                           s.rightTrackSpeed, vehicle.input.leftTrack, vehicle.input.rightTrack);
        for (const std::size_t i : {std::size_t{wheelsASide - 1}, std::size_t{2 * wheelsASide - 1}})
        {
            const Fenrir::VehicleWheelState& w = m_wheels[i].GetComponent<Fenrir::VehicleWheel>().state;
            app.Logger()->Info("  {} driven wheel: on ground {}, compression {:.3f} m, spin {:.1f} rad/s",
                               i < wheelsASide ? "left" : "right", w.onGround, w.compression, w.angularVelocity);
        }
    }

    static constexpr int wheelsASide = 9;
    static constexpr float stoppedSpeed = 0.5f; // m/s: below it, S reverses rather than brakes.
    static constexpr float pivotSpeed = 1.0f;   // m/s: below it, steering with no throttle pivots on the spot.
    static constexpr float innerRatio = 0.6f;   // The inner track's ratio, turning on the move.
    static constexpr float cruise = 6.0f;       // m/s: the autopilot's speed on the straights.
    static constexpr float reach = 4.0f;        // m: the autopilot moves on this close to a point.
    static constexpr float pivotAngle = 0.6f;   // Radians: facing further from its point, the autopilot pivots.
    inline static const Fenrir::Math::Vec3 start = Fenrir::Math::Vec3(0.0f, 1.5f, 0.0f);
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

    const Model* m_cube;
    const Model* m_sphere;
    const Shader* m_flatShader;
    Fenrir::EntityList* m_entities = nullptr;
    Fenrir::Entity m_tank;
    std::vector<Fenrir::Entity> m_wheels; // Left then right, each front to back: entity order, so wheel order.
    Fenrir::Entity m_camera;
    VehicleCamera m_followCamera;
    std::set<int> m_held;
    bool m_autopilot = false;
    float m_direction = 1.0f; // The way the keys last drove it: S brakes while it goes forward, then reverses.
    std::size_t m_waypoint = 0;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
};

#endif // FENRIR_PHYSICS
