#pragma once

// PROTOTYPE (prototype/fenrir-physics-vehicles-spike, never merged): "Spike: a car, a motorcycle and a tank driving a
// small track through the proposed model". F6 steps to it from Default and back.
//
// Three vehicles made only through FenrirPhysics's proposed public interface (Vehicles.hpp): a 1500 kg car towing a
// 300 kg trailer on a BallJoint, a 240 kg motorcycle, and a 4000 kg tank with nine wheels a side. Tab switches which
// one the keys drive; the others are parked with the handbrake. Each chassis is a dynamic RigidBody with an unscaled
// box Collider and a Vehicle; its look is a scaled child with no collider. Each wheel is its own entity with a
// VehicleWheel naming its chassis, drawn as a cube scaled to the wheel, posed by physics.
//
// The track, from the start pad at the origin, facing north (-Z): ten bumps from z = -25 to -43, a ramp at z = -70
// rising to 2 m, then open ground. East: an ice lane (friction 0.05) at x = 30 and a grass lane (0.3) at x = 40. West:
// a pile of loose 20 kg boxes at x = -30, and a 15° slope to park on at x = -55. The ground is tarmac (0.5).

#ifdef FENRIR_PHYSICS

#include <algorithm>
#include <array>
#include <cmath>
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
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Vehicles.hpp"
#include "Material.hpp"
#include "ModelLibrary.hpp"

class VehicleScene
{
  public:
    static constexpr const char* name = "Vehicles (spike)";

    static DemoScene Scene()
    {
        DemoScene scene{.name = name,
                        .familyKey = DemoFamilyKey::vehicles,
                        .keyHelp =
                            "W throttle, S brake then reverse, A/D steer (the tank turns on the spot when stopped), "
                            "Space handbrake, Tab switches car (with trailer) / motorcycle / tank, T teleports it to "
                            "the start, C frees the camera. 1 toggles soft springs and 2 big wheels on the driven "
                            "vehicle (live wheel edits), 3 toggles the motorcycle's max lean 45°/20° (a rebuild), 4 "
                            "toggles the vehicle's enabled, 5 its interpolation, 6 the motorcycle's lean controller, "
                            "M manual gearbox (E/Q shift up/down, Shift holds the clutch), L logs its state, P drives "
                            "the car round the track by itself. North: bumps then a ramp. East: ice and grass lanes. "
                            "West: loose boxes and a 15° slope to park on. F6 goes on.",
                        .smokeKeys = {GLFW_KEY_P}};
        scene.smokeKeys.insert(scene.smokeKeys.end(), 40, GLFW_KEY_L);
        scene.smokeKeys.push_back(GLFW_KEY_P);
        for (const int key :
             {GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_5, GLFW_KEY_M, GLFW_KEY_E, GLFW_KEY_Q, GLFW_KEY_M, GLFW_KEY_4,
              GLFW_KEY_T, GLFW_KEY_C, GLFW_KEY_TAB, GLFW_KEY_3, GLFW_KEY_6, GLFW_KEY_TAB, GLFW_KEY_TAB})
        {
            scene.smokeKeys.insert(scene.smokeKeys.end(), {key, GLFW_KEY_L, key});
        }
        scene.smokeKeys.push_back(GLFW_KEY_L);
        return scene;
    }

    VehicleScene(const Model& cube, const Shader& flatShader) : m_cube(&cube), m_flatShader(&flatShader)
    {
    }

    void Init(Fenrir::App& app)
    {
        m_entities = &app.CreateScene(name).GetEntityList();
        BuildTrack();
        m_car = MakeCar(Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
        m_trailer = MakeTrailer();
        m_bike = MakeMotorcycle(Fenrir::Math::Vec3(6.0f, 1.0f, 0.0f));
        m_tank = MakeTank(Fenrir::Math::Vec3(-9.0f, 1.5f, 0.0f));
        m_camera = m_entities->CreateEntity();
        m_camera.GetComponent<Fenrir::Name>().Set("Vehicle camera");
        m_camera.AddComponent<DemoCamera>();
    }

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
        FollowCamera(static_cast<float>(app.GetTime().deltaTime));
        ShowReadings(app);
    }

    void Tick(Fenrir::App& app)
    {
        if (app.GetActiveScene().GetName() != name)
        {
            return;
        }
        ++m_tick;
        for (Driven* vehicle : {&m_car, &m_bike, &m_tank})
        {
            Fenrir::Vehicle& component = vehicle->chassis.GetComponent<Fenrir::Vehicle>();
            if (vehicle != &Current())
            {
                const int gear = component.input.gear;
                component.input = Fenrir::VehicleInput{.handbrake = 1.0f, .gear = gear};
                continue;
            }
            Drive(app, *vehicle, component);
        }
    }

  private:
    struct Driven
    {
        const char* label = "";
        Fenrir::Entity chassis;
        std::vector<Fenrir::Entity> wheels;
        Fenrir::Math::Vec3 start = Fenrir::Math::Vec3(0.0f);
        // The direction the keys last asked for: S brakes until stopped, then reverses.
        float wantedDirection = 1.0f;
    };

    Driven& Current()
    {
        return m_driven == 0 ? m_car : (m_driven == 1 ? m_bike : m_tank);
    }

    void OnKey(Fenrir::App& app, int key)
    {
        Driven& driven = Current();
        Fenrir::Vehicle& vehicle = driven.chassis.GetComponent<Fenrir::Vehicle>();
        if (key == GLFW_KEY_TAB)
        {
            m_driven = (m_driven + 1) % 3;
            app.Logger()->Info("Tab: driving the {}", Current().label);
        }
        else if (key == GLFW_KEY_T)
        {
            Fenrir::Transform& transform = driven.chassis.GetComponent<Fenrir::Transform>();
            transform.pos = driven.start;
            transform.rot = identity;
            Fenrir::RigidBody& body = driven.chassis.GetComponent<Fenrir::RigidBody>();
            body.linearVelocity = Fenrir::Math::Vec3(0.0f);
            body.angularVelocity = Fenrir::Math::Vec3(0.0f);
            if (&driven == &m_car)
            {
                PlaceTrailer();
            }
            app.Logger()->Info("T: {} teleported to the start", driven.label);
        }
        else if (key == GLFW_KEY_C)
        {
            m_following = !m_following;
            app.Logger()->Info(m_following ? "C: camera follows" : "C: camera free (WASD flies it)");
        }
        else if (key == GLFW_KEY_1)
        {
            m_softSprings = !m_softSprings;
            for (Fenrir::Entity& wheel : driven.wheels)
            {
                wheel.GetComponent<Fenrir::VehicleWheel>().suspension.spring.frequency = m_softSprings ? 0.7f : 1.5f;
            }
            app.Logger()->Info(m_softSprings ? "1: soft springs (0.7 Hz)" : "1: springs back to 1.5 Hz");
        }
        else if (key == GLFW_KEY_2)
        {
            m_bigWheels = !m_bigWheels;
            for (Fenrir::Entity& wheel : driven.wheels)
            {
                Fenrir::VehicleWheel& settings = wheel.GetComponent<Fenrir::VehicleWheel>();
                settings.radius *= m_bigWheels ? 1.5f : 1.0f / 1.5f;
                Fenrir::Transform& look = wheel.GetComponent<Fenrir::Transform>();
                look.scale = Fenrir::Math::Vec3(settings.width, 2.0f * settings.radius, 2.0f * settings.radius);
            }
            app.Logger()->Info(m_bigWheels ? "2: wheel radius x1.5" : "2: wheels back to size");
        }
        else if (key == GLFW_KEY_3)
        {
            Fenrir::Vehicle& bike = m_bike.chassis.GetComponent<Fenrir::Vehicle>();
            float& maxAngle = bike.kind.As<Fenrir::MotorcycleVehicle>().lean.maxAngle;
            const float before = maxAngle;
            maxAngle = before > 0.5f ? 20.0f * degrees : 45.0f * degrees;
            app.Logger()->Info(
                "3: motorcycle max lean {:.0f}° -> {:.0f}° (a rebuild) at {:.0f} RPM, gear {}, rear spin "
                "{:.1f} rad/s",
                before / degrees, maxAngle / degrees, bike.state.engineRpm, bike.state.gear,
                m_bike.wheels[1].GetComponent<Fenrir::VehicleWheel>().state.angularVelocity);
            m_logRebuild = 2;
        }
        else if (key == GLFW_KEY_4)
        {
            vehicle.enabled = !vehicle.enabled;
            app.Logger()->Info("4: {} {}", driven.label, vehicle.enabled ? "enabled" : "disabled (a plain rigid body)");
        }
        else if (key == GLFW_KEY_5)
        {
            bool& interpolate = driven.chassis.GetComponent<Fenrir::RigidBody>().interpolate;
            interpolate = !interpolate;
            app.Logger()->Info(interpolate ? "5: interpolation on" : "5: interpolation off");
        }
        else if (key == GLFW_KEY_6)
        {
            bool& lean =
                m_bike.chassis.GetComponent<Fenrir::Vehicle>().kind.As<Fenrir::MotorcycleVehicle>().lean.enabled;
            lean = !lean;
            app.Logger()->Info(lean ? "6: motorcycle lean controller on"
                                    : "6: lean controller off: the bike falls over");
        }
        else if (key == GLFW_KEY_M)
        {
            Fenrir::VehicleTransmission& transmission = TransmissionOf(vehicle.kind);
            const bool manual = transmission.mode == Fenrir::TransmissionMode::Automatic;
            transmission.mode = manual ? Fenrir::TransmissionMode::Manual : Fenrir::TransmissionMode::Automatic;
            vehicle.input.gear = manual ? std::max(1, vehicle.state.gear) : 0;
            app.Logger()->Info(manual ? "M: manual gearbox, gear {}" : "M: automatic gearbox", vehicle.input.gear);
        }
        else if (key == GLFW_KEY_E || key == GLFW_KEY_Q)
        {
            vehicle.input.gear += key == GLFW_KEY_E ? 1 : -1;
            app.Logger()->Info("{}: gear {} asked", key == GLFW_KEY_E ? "E" : "Q", vehicle.input.gear);
        }
        else if (key == GLFW_KEY_L)
        {
            LogState(app, driven);
        }
        else if (key == GLFW_KEY_P)
        {
            m_autopilot = !m_autopilot;
            m_waypoint = 0;
            if (m_autopilot)
            {
                m_driven = 0;
            }
            app.Logger()->Info(m_autopilot ? "P: the car drives the track by itself" : "P: autopilot off");
        }
    }

    static Fenrir::VehicleTransmission& TransmissionOf(Fenrir::VehicleKind& kind)
    {
        if (kind.Is<Fenrir::WheeledVehicle>())
        {
            return kind.As<Fenrir::WheeledVehicle>().transmission;
        }
        if (kind.Is<Fenrir::MotorcycleVehicle>())
        {
            return kind.As<Fenrir::MotorcycleVehicle>().transmission;
        }
        return kind.As<Fenrir::TrackedVehicle>().transmission;
    }

    // Game code, in Tick: the keys (or the autopilot) as normalised input.
    void Drive(Fenrir::App& app, Driven& driven, Fenrir::Vehicle& vehicle)
    {
        const auto held = [this](int key) { return m_held.contains(key); };
        const bool keys = m_following; // a free camera flies on WASD
        float throttle = keys && (held(GLFW_KEY_W) || held(GLFW_KEY_UP)) ? 1.0f : 0.0f;
        float back = keys && (held(GLFW_KEY_S) || held(GLFW_KEY_DOWN)) ? 1.0f : 0.0f;
        float steer = 0.0f;
        steer += keys && (held(GLFW_KEY_D) || held(GLFW_KEY_RIGHT)) ? 1.0f : 0.0f;
        steer -= keys && (held(GLFW_KEY_A) || held(GLFW_KEY_LEFT)) ? 1.0f : 0.0f;
        if (m_autopilot && &driven == &m_car)
        {
            Autopilot(app, driven, throttle, back, steer);
        }

        // S brakes while moving forward and reverses once stopped; W does the opposite (Jolt's sample's logic).
        const float speed = vehicle.state.forwardSpeed;
        float forward = throttle - back;
        float brake = 0.0f;
        if (forward != 0.0f && forward * driven.wantedDirection < 0.0f)
        {
            if (std::abs(speed) > 0.5f && speed * forward < 0.0f)
            {
                brake = 1.0f;
                forward = 0.0f;
            }
            else
            {
                driven.wantedDirection = forward;
            }
        }
        const bool manual = TransmissionOf(vehicle.kind).mode == Fenrir::TransmissionMode::Manual;
        vehicle.input.forward = manual ? std::abs(forward) : forward;
        vehicle.input.brake = brake;
        vehicle.input.handbrake = held(GLFW_KEY_SPACE) ? 1.0f : 0.0f;
        vehicle.input.clutch = held(GLFW_KEY_LEFT_SHIFT) ? 1.0f : 0.0f;
        if (vehicle.kind.Is<Fenrir::TrackedVehicle>())
        {
            // Game code turns steering into track ratios: on the spot when stopped, a slower inner track when moving.
            vehicle.input.leftTrack = 1.0f;
            vehicle.input.rightTrack = 1.0f;
            if (steer != 0.0f)
            {
                const bool pivot = forward == 0.0f && brake == 0.0f && std::abs(speed) < 1.0f;
                float& inner = steer > 0.0f ? vehicle.input.rightTrack : vehicle.input.leftTrack;
                inner = pivot ? -1.0f : 0.6f;
                if (pivot)
                {
                    vehicle.input.forward = 1.0f;
                }
            }
        }
        else
        {
            vehicle.input.steer = steer;
        }
    }

    // The car's route: up the bumps, over the ramp, round to the ice and grass lanes, back to the start.
    void Autopilot(Fenrir::App& app, const Driven& driven, float& throttle, float& back, float& steer)
    {
        static const std::array<Fenrir::Math::Vec3, 9> route = {
            Fenrir::Math::Vec3(0.0f, 0.0f, -45.0f),   Fenrir::Math::Vec3(0.0f, 0.0f, -100.0f),
            Fenrir::Math::Vec3(25.0f, 0.0f, -110.0f), Fenrir::Math::Vec3(30.0f, 0.0f, -70.0f),
            Fenrir::Math::Vec3(30.0f, 0.0f, -15.0f),  Fenrir::Math::Vec3(40.0f, 0.0f, 10.0f),
            Fenrir::Math::Vec3(40.0f, 0.0f, -60.0f),  Fenrir::Math::Vec3(15.0f, 0.0f, 15.0f),
            Fenrir::Math::Vec3(0.0f, 0.0f, 5.0f)};
        const Fenrir::Transform at = Fenrir::GetWorldTransform(driven.chassis);
        Fenrir::Math::Vec3 to = route[m_waypoint % route.size()] - at.pos;
        to.y = 0.0f;
        if (Fenrir::Math::Magnitude(to) < 10.0f)
        {
            ++m_waypoint;
            app.Logger()->Info("Autopilot: waypoint {}", m_waypoint % route.size());
        }
        const Fenrir::Math::Vec3 forward = at.rot * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
        const Fenrir::Math::Vec3 right = at.rot * Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f);
        const float angle = std::atan2(Fenrir::Math::Dot(to, right), Fenrir::Math::Dot(to, forward));
        steer = std::clamp(2.0f * angle, -1.0f, 1.0f);
        const float speed = driven.chassis.GetComponent<Fenrir::Vehicle>().state.forwardSpeed;
        throttle = speed < (std::abs(angle) > 0.5f ? 8.0f : 16.0f) ? 0.7f : 0.0f;
        back = 0.0f;
    }

    // A chase camera behind the driven vehicle, writing the scene's DemoCamera.
    void FollowCamera(float dt)
    {
        DemoCamera& camera = m_camera.GetComponent<DemoCamera>();
        camera.Active = m_following ? 1 : 0;
        if (!m_following)
        {
            return;
        }
        const Fenrir::Transform at = Fenrir::GetWorldTransform(Current().chassis);
        Fenrir::Math::Vec3 forward = at.rot * Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
        forward.y = 0.0f;
        if (Fenrir::Math::Magnitude(forward) > 0.1f)
        {
            const float wanted = std::atan2(forward.z, forward.x) / degrees;
            float turn = wanted - m_cameraYaw;
            turn -= 360.0f * std::round(turn / 360.0f);
            m_cameraYaw += turn * std::min(1.0f, 3.0f * dt);
        }
        const float distance = &Current() == &m_tank ? 14.0f : 9.0f;
        const float yaw = m_cameraYaw * degrees;
        const float pitch = -15.0f * degrees;
        const Fenrir::Math::Vec3 front(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                                       std::cos(pitch) * std::sin(yaw));
        camera.Position = at.pos + Fenrir::Math::Vec3(0.0f, 1.5f, 0.0f) - front * distance;
        camera.Yaw = m_cameraYaw;
        camera.Pitch = -15.0f;
    }

    void ShowReadings(Fenrir::App& app)
    {
        const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        std::array<char, 256> text{};
        for (Driven* driven : {&m_car, &m_bike, &m_tank})
        {
            const Fenrir::Vehicle& vehicle = driven->chassis.GetComponent<Fenrir::Vehicle>();
            const Fenrir::VehicleState& s = vehicle.state;
            std::snprintf(
                text.data(), text.size(),
                "%s: %.0f km/h, gear %d%s, %.0f RPM, clutch %.2f, %d on ground, lean %.0f, tracks %.1f/%.1f%s%s",
                driven->label, static_cast<double>(s.forwardSpeed * 3.6f), s.gear, s.shifting ? " (shifting)" : "",
                static_cast<double>(s.engineRpm), static_cast<double>(s.clutch), s.wheelsOnGround,
                static_cast<double>(s.leanAngle / degrees), static_cast<double>(s.leftTrackSpeed),
                static_cast<double>(s.rightTrackSpeed),
                world != nullptr && world->IsSleeping(driven->chassis) ? ", asleep" : "",
                vehicle.enabled ? "" : ", disabled");
            driven->chassis.GetComponent<Fenrir::Name>().Set(text.data());
            for (std::size_t i = 0; i < driven->wheels.size(); ++i)
            {
                const Fenrir::VehicleWheelState& w = driven->wheels[i].GetComponent<Fenrir::VehicleWheel>().state;
                std::snprintf(text.data(), text.size(),
                              "Wheel %zu: %s, compression %.3f, %.0f N, slip %.2f / %.1f deg, spin %.1f, steer %.1f", i,
                              w.onGround ? GroundName(w.groundCollider).c_str() : "in air",
                              static_cast<double>(w.compression), static_cast<double>(w.suspensionForce),
                              static_cast<double>(w.longitudinalSlip), static_cast<double>(w.lateralSlip / degrees),
                              static_cast<double>(w.angularVelocity), static_cast<double>(w.steerAngle / degrees));
                driven->wheels[i].GetComponent<Fenrir::Name>().Set(text.data());
            }
        }
        if (m_logRebuild > 0 && --m_logRebuild == 0)
        {
            const Fenrir::Vehicle& bike = m_bike.chassis.GetComponent<Fenrir::Vehicle>();
            app.Logger()->Info("3: after the rebuild: {:.0f} RPM, gear {}, rear spin {:.1f} rad/s",
                               bike.state.engineRpm, bike.state.gear,
                               m_bike.wheels[1].GetComponent<Fenrir::VehicleWheel>().state.angularVelocity);
        }
    }

    static std::string GroundName(const Fenrir::Entity& entity)
    {
        return entity.IsValid() ? std::string(entity.GetComponent<Fenrir::Name>().Get()) : "?";
    }

    void LogState(Fenrir::App& app, const Driven& driven) const
    {
        const Fenrir::Vehicle& vehicle = driven.chassis.GetComponent<Fenrir::Vehicle>();
        app.Logger()->Info("L (tick {}): {}", m_tick, driven.chassis.GetComponent<Fenrir::Name>().Get());
        for (const Fenrir::Entity& wheel : driven.wheels)
        {
            const Fenrir::VehicleWheelState& w = wheel.GetComponent<Fenrir::VehicleWheel>().state;
            app.Logger()->Info("  {} | normal ({:.2f}, {:.2f}, {:.2f}), ground v ({:.2f}, {:.2f}, {:.2f})",
                               wheel.GetComponent<Fenrir::Name>().Get(), w.contactNormal.x, w.contactNormal.y,
                               w.contactNormal.z, w.groundVelocity.x, w.groundVelocity.y, w.groundVelocity.z);
        }
        (void)vehicle;
    }

    // Track pieces.

    void BuildTrack()
    {
        Fenrir::Entity ground = Static("Tarmac", Fenrir::Math::Vec3(0.0f, -0.5f, -30.0f),
                                       Fenrir::Math::Vec3(200.0f, 1.0f, 260.0f), Fenrir::Math::Vec3(0.35f));
        (void)ground;
        Look("Start", Fenrir::Math::Vec3(0.0f, 0.005f, 0.0f), Fenrir::Math::Vec3(4.0f, 0.01f, 6.0f),
             Fenrir::Math::Vec3(0.95f));

        // Bumps: square bars turned 45° about their length, 0.14 m high, every 2 m.
        for (int i = 0; i < 10; ++i)
        {
            Fenrir::Entity bump = Static("Bump", Fenrir::Math::Vec3(0.0f, 0.0f, -25.0f - 2.0f * static_cast<float>(i)),
                                         Fenrir::Math::Vec3(8.0f, 0.2f, 0.2f), Fenrir::Math::Vec3(0.8f, 0.7f, 0.2f));
            bump.GetComponent<Fenrir::Transform>().rot =
                Fenrir::Math::AngleAxis(0.25f * std::numbers::pi_v<float>, Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));
        }

        // The ramp: 10 m long, 6 m wide, rising north to 2 m, then a drop.
        const float rise = std::asin(2.0f / 10.0f);
        Fenrir::Entity ramp = Static("Ramp", Fenrir::Math::Vec3(0.0f, 1.0f - 0.25f, -70.0f),
                                     Fenrir::Math::Vec3(6.0f, 0.5f, 10.0f), Fenrir::Math::Vec3(0.6f, 0.45f, 0.3f));
        ramp.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(rise, Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));

        // Grip lanes: thin slabs whose PhysicsMaterial the wheels feel.
        Fenrir::Entity ice = Static("Ice (0.05)", Fenrir::Math::Vec3(30.0f, 0.02f, -40.0f),
                                    Fenrir::Math::Vec3(6.0f, 0.04f, 70.0f), Fenrir::Math::Vec3(0.75f, 0.9f, 1.0f));
        ice.AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.friction = 0.05f});
        Fenrir::Entity grass = Static("Grass (0.3)", Fenrir::Math::Vec3(40.0f, 0.02f, -40.0f),
                                      Fenrir::Math::Vec3(6.0f, 0.04f, 70.0f), Fenrir::Math::Vec3(0.3f, 0.6f, 0.25f));
        grass.AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.friction = 0.3f});

        // Loose boxes to drive through: a wall of 20 kg crates, 0.6 m on a side.
        for (int row = 0; row < 3; ++row)
        {
            for (int column = 0; column < 6; ++column)
            {
                Fenrir::Entity crate = Look("Crate",
                                            Fenrir::Math::Vec3(-30.0f + 0.65f * static_cast<float>(column),
                                                               0.3f + 0.6f * static_cast<float>(row), -20.0f),
                                            Fenrir::Math::Vec3(0.6f), Fenrir::Math::Vec3(0.75f, 0.5f, 0.25f));
                crate.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 20.0f});
                crate.AddComponent<Fenrir::Collider>();
            }
        }

        // A 15° slope to park on, rising north to 5 m, then a drop.
        const float slope = 15.0f * degrees;
        Fenrir::Entity hill =
            Static("Slope (15 deg)", Fenrir::Math::Vec3(-55.0f, 2.5f * std::sin(slope) * 4.0f - 0.25f, -20.0f),
                   Fenrir::Math::Vec3(10.0f, 0.5f, 20.0f), Fenrir::Math::Vec3(0.5f, 0.55f, 0.45f));
        hill.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(slope, Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f));
    }

    // A static box: an unscaled entity with a BoxShape, its look a scaled child.
    Fenrir::Entity Static(const char* label, const Fenrir::Math::Vec3& position, const Fenrir::Math::Vec3& size,
                          const Fenrir::Math::Vec3& colour)
    {
        Fenrir::Entity box = Look(label, position, size, colour);
        box.AddComponent<Fenrir::Collider>();
        return box;
    }

    // A scaled unit cube: the cube model and a unit box collider share its Transform's scale.
    Fenrir::Entity Look(const char* label, const Fenrir::Math::Vec3& position, const Fenrir::Math::Vec3& size,
                        const Fenrir::Math::Vec3& colour)
    {
        Fenrir::Entity box = m_entities->CreateEntity();
        box.GetComponent<Fenrir::Name>().Set(label);
        box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, size);
        Material material{*m_flatShader, {}};
        material.properties["color"] = colour;
        box.AddComponent<Material>(material);
        box.AddComponent<Model>(*m_cube);
        return box;
    }

    // A chassis: unscaled, with a box collider of `half` extents and a scaled look as its child.
    Fenrir::Entity Chassis(const char* label, const Fenrir::Math::Vec3& at, const Fenrir::Math::Vec3& half, float mass,
                           const Fenrir::Math::Vec3& colour)
    {
        Fenrir::Entity chassis = m_entities->CreateEntity();
        chassis.GetComponent<Fenrir::Name>().Set(label);
        chassis.GetComponent<Fenrir::Transform>() = Fenrir::Transform(at, identity, Fenrir::Math::Vec3(1.0f));
        chassis.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = mass});
        chassis.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{half}});
        Fenrir::Entity look = Look("Look", Fenrir::Math::Vec3(0.0f), 2.0f * half, colour);
        chassis.AddChild(look);
        look.GetComponent<Fenrir::Transform>().pos = Fenrir::Math::Vec3(0.0f);
        return chassis;
    }

    // A wheel entity: posed by physics, drawn as a cube the wheel's size, with a white stripe to show its spin.
    Fenrir::Entity Wheel(const Fenrir::VehicleWheel& wheel, const Fenrir::Math::Vec3& at)
    {
        Fenrir::Entity entity =
            Look("Wheel", at + wheel.position,
                 Fenrir::Math::Vec3(wheel.width, 2.0f * wheel.radius, 2.0f * wheel.radius), Fenrir::Math::Vec3(0.1f));
        entity.AddComponent<Fenrir::VehicleWheel>(wheel);
        Fenrir::Entity stripe = Look("Stripe", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(1.05f, 0.25f, 1.05f),
                                     Fenrir::Math::Vec3(0.95f));
        entity.AddChild(stripe);
        stripe.GetComponent<Fenrir::Transform>().pos = Fenrir::Math::Vec3(0.0f);
        return entity;
    }

    Driven MakeCar(const Fenrir::Math::Vec3& at)
    {
        Driven car{.label = "Car", .start = at};
        car.chassis =
            Chassis("Car", at, Fenrir::Math::Vec3(0.9f, 0.25f, 2.0f), 1500.0f, Fenrir::Math::Vec3(0.85f, 0.15f, 0.1f));
        car.chassis.AddComponent<Fenrir::Vehicle>();
        for (const float z : {-1.4f, 1.4f})
        {
            for (const float x : {-0.9f, 0.9f})
            {
                const bool front = z < 0.0f;
                car.wheels.push_back(Wheel(Fenrir::VehicleWheel{.vehicle = car.chassis,
                                                                .position = Fenrir::Math::Vec3(x, -0.55f, z),
                                                                .maxSteerAngle = front ? 0.5f : 0.0f,
                                                                .maxHandbrakeTorque = front ? 0.0f : 4000.0f},
                                           at));
            }
        }
        return car;
    }

    // An unpowered trailer: a Vehicle given no input stays in neutral, so its wheels only roll and brake. Joined to
    // the car's tow hitch by a BallJoint on the trailer's entity.
    Fenrir::Entity MakeTrailer()
    {
        const Fenrir::Math::Vec3 at = m_car.start + Fenrir::Math::Vec3(0.0f, 0.0f, trailerOffset);
        Fenrir::Entity trailer =
            Chassis("Trailer", at, Fenrir::Math::Vec3(0.8f, 0.3f, 1.0f), 300.0f, Fenrir::Math::Vec3(0.3f, 0.35f, 0.8f));
        trailer.AddComponent<Fenrir::Vehicle>();
        for (const float x : {-0.85f, 0.85f})
        {
            m_trailerWheels.push_back(Wheel(Fenrir::VehicleWheel{.vehicle = trailer,
                                                                 .position = Fenrir::Math::Vec3(x, -0.45f, 0.0f),
                                                                 .maxHandbrakeTorque = 0.0f},
                                            at));
        }
        Fenrir::Entity tongue =
            Look("Tongue", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(0.1f, 0.1f, 1.3f), Fenrir::Math::Vec3(0.2f));
        trailer.AddChild(tongue);
        tongue.GetComponent<Fenrir::Transform>().pos = Fenrir::Math::Vec3(0.0f, -0.1f, -1.6f);
        const Fenrir::Math::Vec3 hitch = m_car.start + Fenrir::Math::Vec3(0.0f, -0.1f, 2.3f);
        const Fenrir::JointFrames frames = Fenrir::FrameFromWorld(m_car.chassis, trailer, Fenrir::JointFrame{hitch});
        trailer.AddComponent<Fenrir::Joint>(Fenrir::Joint{.bodyA = m_car.chassis,
                                                          .bodyB = trailer,
                                                          .frameA = frames.frameA,
                                                          .frameB = frames.frameB,
                                                          .kind = Fenrir::BallJoint{}});
        return trailer;
    }

    void PlaceTrailer()
    {
        Fenrir::Transform& transform = m_trailer.GetComponent<Fenrir::Transform>();
        transform.pos = m_car.start + Fenrir::Math::Vec3(0.0f, 0.0f, trailerOffset);
        transform.rot = identity;
        Fenrir::RigidBody& body = m_trailer.GetComponent<Fenrir::RigidBody>();
        body.linearVelocity = Fenrir::Math::Vec3(0.0f);
        body.angularVelocity = Fenrir::Math::Vec3(0.0f);
    }

    // Jolt's motorcycle sample's numbers: a 30° fork caster, rear-wheel drive through one single-wheel differential.
    Driven MakeMotorcycle(const Fenrir::Math::Vec3& at)
    {
        Driven bike{.label = "Motorcycle", .start = at};
        bike.chassis = Chassis("Motorcycle", at, Fenrir::Math::Vec3(0.2f, 0.3f, 0.4f), 240.0f,
                               Fenrir::Math::Vec3(0.15f, 0.35f, 0.85f));
        bike.chassis.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::MotorcycleVehicle{}});
        for (const float z : {-0.75f, 0.75f})
        {
            const bool front = z < 0.0f;
            bike.wheels.push_back(Wheel(
                Fenrir::VehicleWheel{
                    .vehicle = bike.chassis,
                    .position = Fenrir::Math::Vec3(0.0f, -0.6f, z),
                    .radius = 0.31f,
                    .width = 0.05f,
                    .caster = front ? 30.0f * degrees : 0.0f,
                    .suspension = {.travel = 0.2f, .spring = {.frequency = front ? 1.5f : 2.0f, .damping = 0.5f}},
                    .maxSteerAngle = front ? 30.0f * degrees : 0.0f,
                    .maxBrakeTorque = front ? 500.0f : 250.0f,
                    .maxHandbrakeTorque = front ? 0.0f : 250.0f},
                at));
        }
        bike.chassis.AddComponent<Fenrir::VehicleDifferential>(
            Fenrir::VehicleDifferential{.rightWheel = bike.wheels[1], .ratio = 1.93f * 40.0f / 16.0f});
        return bike;
    }

    // Jolt's tank sample's numbers: nine wheels a side, the end ones higher.
    Driven MakeTank(const Fenrir::Math::Vec3& at)
    {
        Driven tank{.label = "Tank", .start = at};
        tank.chassis =
            Chassis("Tank", at, Fenrir::Math::Vec3(1.7f, 0.5f, 3.2f), 4000.0f, Fenrir::Math::Vec3(0.35f, 0.45f, 0.2f));
        tank.chassis.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::TrackedVehicle{}});
        for (const float x : {-1.7f, 1.7f})
        {
            for (int i = 0; i < 9; ++i)
            {
                const bool end = i == 0 || i == 8;
                const float z = -2.8f + 0.7f * static_cast<float>(i);
                tank.wheels.push_back(Wheel(
                    Fenrir::VehicleWheel{
                        .vehicle = tank.chassis,
                        .position = Fenrir::Math::Vec3(x, end ? -0.6f : -0.8f, z),
                        .track = x < 0.0f ? Fenrir::VehicleTrackSide::Left : Fenrir::VehicleTrackSide::Right,
                        .suspension = {.travel = end ? 0.0f : 0.2f, .spring = {.frequency = 1.0f, .damping = 0.5f}}},
                    at));
            }
        }
        return tank;
    }

    static constexpr float degrees = std::numbers::pi_v<float> / 180.0f;
    static constexpr float trailerOffset = 2.3f + 1.9f;
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

    const Model* m_cube;
    const Shader* m_flatShader;
    Fenrir::EntityList* m_entities = nullptr;
    Driven m_car;
    Driven m_bike;
    Driven m_tank;
    Fenrir::Entity m_trailer;
    std::vector<Fenrir::Entity> m_trailerWheels;
    Fenrir::Entity m_camera;
    int m_driven = 0;
    bool m_following = true;
    float m_cameraYaw = -90.0f;
    bool m_softSprings = false;
    bool m_bigWheels = false;
    bool m_autopilot = false;
    std::size_t m_waypoint = 0;
    int m_logRebuild = 0;
    long m_tick = 0;
    std::set<int> m_held;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
};

#endif
