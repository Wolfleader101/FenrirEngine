// Motorcycle (C++): a motorcycle (ADR-0015) on an open stretch of tarmac, built in C++.
//
// The motorcycle is Jolt's sample bike: a 240 kg chassis, a dynamic RigidBody with an unscaled box Collider and a
// Vehicle whose kind is the default MotorcycleVehicle, its look a scaled child with no collider. Its two wheels are
// VehicleWheels on wheel entities of their own: the front on a 30 degree fork caster that steers 30 degrees, and the
// rear, which one VehicleDifferential naming only it drives, so it rides rear-wheel drive like the sample (with no
// differential a motorcycle drives both wheels). The box's roll inertia is 10.4 kg m², light enough that the backend's
// own lean damping would diverge; physics caps it privately.
//
// Its lean controller keeps it standing at rest and leans it into turns, and steering is limited by speed so the lean
// stays under its max lean. 3 changes the max lean between 45 and 20 degrees: a rebuild that keeps the engine speed,
// gear and wheel spin. 6 switches the lean controller off, and the bike falls over, and on again; a fallen bike stays
// down, and T stands it back up at the start, a Transform write that keeps its engine and wheels turning. 5 switches
// the chassis's interpolation off and on: the lean is the chassis's pose, so it blends with it, and the wheels follow.
// Its readout, in its name and shown as screen text, adds its lean to the speed, RPM and gear.
//
// W or Up drives, S or Down brakes and then reverses once stopped, A/D or Left/Right steer, and Space pulls the
// handbrake. P rides a loop by itself, two long straights and a hairpin at each end, leaning into each. C frees the
// chase camera (it stays where it is, and the arrows still drive; F3 is the free-fly camera) and snaps it back; L logs
// the bike's state.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <numbers>
#include <optional>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Vehicles.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "PhysicsScenes.hpp"
#include "SceneLooks.hpp"
#include "VehicleCamera.hpp"

namespace PhysicsDemo
{
    namespace
    {
        using Fenrir::Key;
        using Fenrir::Math::Vec2;
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Motorcycle (C++)";

        class MotorcycleScene
        {
          public:
            void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
            {
                // A new entry starts the scene afresh.
                *this = MotorcycleScene();
                m_entities = &scene.GetEntityList();
                m_looks.emplace(app, scene);
                BuildTarmac();
                MakeMotorcycle();
                m_camera = m_entities->CreateEntity();
                m_camera.GetComponent<Fenrir::Name>().Set("Motorcycle camera");
                m_camera.AddComponent<Fenrir::Camera>();
                m_followCamera.Update(m_camera.GetComponent<Fenrir::Transform>(), Fenrir::GetWorldTransform(m_bike),
                                      Vec2(0.0f), 0.0f);
            }

            // Update, every frame while the scene is active: keys, the camera, and the bike's readings.
            void Update(Fenrir::App& app)
            {
                if (!IsActive(app))
                {
                    return;
                }
                const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
                for (const Key key : {Key::C, Key::L, Key::P, Key::Num3, Key::Num6, Key::Num5, Key::T})
                {
                    if (keyboard.JustPressed(key))
                    {
                        OnKey(app, key);
                    }
                }
                m_followCamera.Update(m_camera.GetComponent<Fenrir::Transform>(), Fenrir::GetWorldTransform(m_bike),
                                      Vec2(0.0f), static_cast<float>(app.GetTime().deltaTime));
                ShowReadings(app.GetActiveScene());
            }

            // Tick, while the scene is active: game code reads the bike's state and writes its driver input.
            void Tick(Fenrir::App& app)
            {
                if (IsActive(app))
                {
                    Drive(app);
                }
            }

          private:
            [[nodiscard]] bool IsActive(Fenrir::App& app) const
            {
                return m_entities != nullptr && app.GetActiveScene().GetName() == SceneName;
            }

            void OnKey(Fenrir::App& app, Key key)
            {
                Fenrir::Vehicle& vehicle = m_bike.GetComponent<Fenrir::Vehicle>();
                Fenrir::MotorcycleLean& lean = vehicle.kind.As<Fenrir::MotorcycleVehicle>().lean;
                if (key == Key::C)
                {
                    m_followCamera.Toggle();
                    app.Logger()->Info(m_followCamera.Following()
                                           ? "C: the camera chases the motorcycle"
                                           : "C: camera free: it stays where it is (F3 flies the free-fly camera), "
                                             "the arrows ride");
                }
                else if (key == Key::L)
                {
                    LogState(app);
                }
                else if (key == Key::P)
                {
                    m_autopilot = !m_autopilot;
                    m_waypoint = 0;
                    app.Logger()->Info(m_autopilot ? "P: autopilot: riding the loop" : "P: autopilot off");
                }
                else if (key == Key::Num3)
                {
                    const float before = lean.maxAngle;
                    lean.maxAngle = before > 30.0f * degrees ? 20.0f * degrees : 45.0f * degrees;
                    app.Logger()->Info("3: max lean {:.0f} -> {:.0f} degrees, a rebuild, at {:.0f} RPM in gear {}",
                                       before / degrees, lean.maxAngle / degrees, vehicle.state.engineRpm,
                                       vehicle.state.gear);
                }
                else if (key == Key::Num6)
                {
                    lean.enabled = !lean.enabled;
                    app.Logger()->Info(lean.enabled ? "6: the lean controller is on"
                                                    : "6: the lean controller is off: the motorcycle falls over");
                }
                else if (key == Key::Num5)
                {
                    bool& interpolate = m_bike.GetComponent<Fenrir::RigidBody>().interpolate;
                    interpolate = !interpolate;
                    app.Logger()->Info(
                        interpolate
                            ? "5: the motorcycle is interpolated, its lean and wheels with it"
                            : "5: the motorcycle is not interpolated: it and its wheels are drawn at step poses");
                }
                else if (key == Key::T)
                {
                    Respawn(app);
                }
            }

            // The keys as normalised input, or the autopilot's while it rides. While the camera is free, WASD does
            // nothing, so only the arrows ride.
            void Drive(Fenrir::App& app)
            {
                const bool wasd = m_followCamera.Following();
                const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
                const auto held = [&keyboard](Key key) { return keyboard.IsDown(key); };
                float throttle = held(Key::Up) || (wasd && held(Key::W)) ? 1.0f : 0.0f;
                float back = held(Key::Down) || (wasd && held(Key::S)) ? 1.0f : 0.0f;
                float steer = 0.0f;
                steer += held(Key::Right) || (wasd && held(Key::D)) ? 1.0f : 0.0f;
                steer -= held(Key::Left) || (wasd && held(Key::A)) ? 1.0f : 0.0f;
                if (m_autopilot)
                {
                    Autopilot(app, throttle, back, steer);
                }

                Fenrir::Vehicle& vehicle = m_bike.GetComponent<Fenrir::Vehicle>();
                vehicle.input.steer = steer;
                vehicle.input.handbrake = held(Key::Space) ? 1.0f : 0.0f;
                // Asked to go the other way, it brakes until it has all but stopped, then rides that way.
                const float speed = vehicle.state.forwardSpeed;
                float forward = throttle - back;
                float brake = 0.0f;
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
                vehicle.input.forward = forward;
                vehicle.input.brake = brake;
            }

            // Steers for the next point of the loop and holds its speed, slower through a sharp turn.
            void Autopilot(Fenrir::App& app, float& throttle, float& back, float& steer)
            {
                const std::vector<Vec3>& loop = LoopLeg();
                const Vec3& next = loop.at(m_waypoint % loop.size());
                const Fenrir::Transform at = Fenrir::GetWorldTransform(m_bike);
                Vec3 to = next - at.pos;
                to.y = 0.0f;
                if (Fenrir::Math::Magnitude(to) < reach)
                {
                    ++m_waypoint;
                    app.Logger()->Info("Autopilot: point {} reached", m_waypoint % loop.size());
                    return;
                }
                const Vec3 ahead = at.rot * Vec3(0.0f, 0.0f, -1.0f);
                const Vec3 right = at.rot * Vec3(1.0f, 0.0f, 0.0f);
                const float angle = std::atan2(Fenrir::Math::Dot(to, right), Fenrir::Math::Dot(to, ahead));
                steer = std::clamp(2.0f * angle, -1.0f, 1.0f);
                const float wanted = std::abs(angle) > 0.5f ? 0.5f * cruise : cruise;
                throttle = m_bike.GetComponent<Fenrir::Vehicle>().state.forwardSpeed < wanted ? 0.7f : 0.0f;
                back = 0.0f;
            }

            // The ground: tarmac all round, with the start pad, a look only, at the origin facing north (-Z).
            void BuildTarmac()
            {
                Fenrir::Entity tarmac =
                    Look("Tarmac", Vec3(0.0f, -0.5f, -40.0f), Vec3(160.0f, 1.0f, 200.0f), Vec3(0.35f));
                tarmac.AddComponent<Fenrir::Collider>();
                tarmac.AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.friction = 0.5f});
                Look("Start", Vec3(0.0f, 0.005f, 0.0f), Vec3(2.0f, 0.01f, 3.0f), Vec3(0.95f));
            }

            // The loop the autopilot rides: north up a 70 m straight, a hairpin west, south down another, and a
            // hairpin back.
            static const std::vector<Vec3>& LoopLeg()
            {
                static const std::vector<Vec3> loop = {Vec3(0.0f, 0.0f, -15.0f),    Vec3(0.0f, 0.0f, -85.0f),
                                                       Vec3(-10.0f, 0.0f, -100.0f), Vec3(-25.0f, 0.0f, -85.0f),
                                                       Vec3(-25.0f, 0.0f, -15.0f),  Vec3(-12.0f, 0.0f, 0.0f)};
                return loop;
            }

            // Jolt's sample motorcycle on the start pad, as the file's header describes.
            void MakeMotorcycle()
            {
                m_bike = m_entities->CreateEntity();
                m_bike.GetComponent<Fenrir::Name>().Set("Motorcycle");
                m_bike.GetComponent<Fenrir::Transform>().pos = start;
                m_bike.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 240.0f});
                const Vec3 half(0.2f, 0.3f, 0.4f);
                m_bike.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{half}});
                m_bike.AddChild(Look("Chassis look", Vec3(0.0f), 2.0f * half, Vec3(0.15f, 0.35f, 0.85f)));
                m_bike.AddComponent<Fenrir::Vehicle>(Fenrir::Vehicle{.kind = Fenrir::MotorcycleVehicle{}});
                for (const float z : {-0.75f, 0.75f})
                {
                    const bool front = z < 0.0f;
                    m_wheels.push_back(Wheel(Fenrir::VehicleWheel{
                        .vehicle = m_bike,
                        .position = Vec3(0.0f, -0.6f, z),
                        .radius = 0.31f,
                        .width = 0.05f,
                        .caster = front ? 30.0f * degrees : 0.0f,
                        .suspension = {.travel = 0.2f, .spring = {.frequency = front ? 1.5f : 2.0f, .damping = 0.5f}},
                        .maxSteerAngle = front ? 30.0f * degrees : 0.0f,
                        .maxBrakeTorque = front ? 500.0f : 250.0f,
                        .maxHandbrakeTorque = front ? 0.0f : 250.0f}));
                }
                Fenrir::Entity differential = m_entities->CreateEntity();
                differential.GetComponent<Fenrir::Name>().Set("Rear differential");
                differential.AddComponent<Fenrir::VehicleDifferential>(
                    Fenrir::VehicleDifferential{.rightWheel = m_wheels.at(1), .ratio = 1.93f * 40.0f / 16.0f});
            }

            // T: the bike back on the start pad, upright, facing north and still. A Transform write is a teleport: the
            // engine's speed and the wheels' spin carry on, and physics poses the wheels on the chassis at once.
            void Respawn(Fenrir::App& app)
            {
                Fenrir::Transform& at = m_bike.GetComponent<Fenrir::Transform>();
                at.pos = start;
                at.rot = identity;
                Fenrir::RigidBody& body = m_bike.GetComponent<Fenrir::RigidBody>();
                body.linearVelocity = Vec3(0.0f);
                body.angularVelocity = Vec3(0.0f);
                m_waypoint = 0;
                app.Logger()->Info("T: the motorcycle stands at the start, its engine and wheels still turning");
            }

            // A wheel entity at its place on the bike at the start: unscaled, posed by physics, not the chassis's
            // child. Its look is a child sphere scaled to the tyre, with a white stripe across it to show its spin.
            Fenrir::Entity Wheel(const Fenrir::VehicleWheel& settings)
            {
                Fenrir::Entity wheel = m_entities->CreateEntity();
                wheel.GetComponent<Fenrir::Name>().Set("Wheel");
                wheel.GetComponent<Fenrir::Transform>().pos = start + settings.position;
                wheel.AddComponent<Fenrir::VehicleWheel>(settings);
                Fenrir::Entity tyre =
                    Look("Tyre", Vec3(0.0f), Vec3(settings.width, 2.0f * settings.radius, 2.0f * settings.radius),
                         Vec3(0.1f), true);
                wheel.AddChild(tyre);
                tyre.AddChild(Look("Stripe", Vec3(0.0f), Vec3(1.05f, 0.25f, 0.9f), Vec3(0.95f)));
                return wheel;
            }

            // The unit cube (or sphere) scaled to `size`, in a flat colour.
            Fenrir::Entity Look(const char* label, const Vec3& position, const Vec3& size, const Vec3& colour,
                                bool sphere = false)
            {
                Fenrir::Entity box = m_entities->CreateEntity();
                box.GetComponent<Fenrir::Name>().Set(label);
                box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, size);
                if (sphere)
                {
                    m_looks->Sphere(box, colour);
                }
                else
                {
                    m_looks->Cube(box, colour);
                }
                return box;
            }

            // The bike's state in its name, every frame, with its lean, shown as screen text.
            void ShowReadings(Fenrir::Scene& scene)
            {
                const Fenrir::VehicleState& state = m_bike.GetComponent<Fenrir::Vehicle>().state;
                std::array<char, 160> text{};
                std::snprintf(text.data(), text.size(),
                              "Motorcycle: %.0f km/h, %.0f RPM, gear %d%s, lean %.0f°, %d on ground",
                              static_cast<double>(state.forwardSpeed * 3.6f), static_cast<double>(state.engineRpm),
                              state.gear, state.shifting ? ", shifting" : "",
                              static_cast<double>(state.leanAngle / degrees), state.wheelsOnGround);
                m_bike.GetComponent<Fenrir::Name>().Set(text.data());
                const std::array<Fenrir::Entity, 1> shown = {m_bike};
                ShowReadouts(scene, shown);
            }

            // L: the bike's state and each wheel's.
            void LogState(Fenrir::App& app) const
            {
                const Fenrir::VehicleState& s = m_bike.GetComponent<Fenrir::Vehicle>().state;
                app.Logger()->Info(
                    "L: motorcycle {:.2f} m/s, {:.0f} RPM, gear {}, shifting {}, {} wheels on ground, lean "
                    "{:.1f} degrees",
                    s.forwardSpeed, s.engineRpm, s.gear, s.shifting, s.wheelsOnGround, s.leanAngle / degrees);
                for (std::size_t i = 0; i < m_wheels.size(); ++i)
                {
                    const Fenrir::VehicleWheelState& w = m_wheels.at(i).GetComponent<Fenrir::VehicleWheel>().state;
                    app.Logger()->Info(
                        "  {} wheel: on ground {}, compression {:.3f} m, slip {:.3f} / {:.3f} rad, spin {:.1f} "
                        "rad/s, steer {:.1f} degrees",
                        i == 0 ? "front" : "rear", w.onGround, w.compression, w.longitudinalSlip, w.lateralSlip,
                        w.angularVelocity, w.steerAngle / degrees);
                }
            }

            static constexpr float degrees = std::numbers::pi_v<float> / 180.0f;
            static constexpr float stoppedSpeed = 0.5f; // m/s: below it, S reverses rather than brakes.
            static constexpr float cruise = 12.0f;      // m/s: the autopilot's speed on the straights.
            static constexpr float reach = 6.0f;        // m: the autopilot moves on this close to a point.
            inline static const Vec3 start = Vec3(0.0f, 1.0f, 0.0f);
            inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

            Fenrir::EntityList* m_entities = nullptr;
            std::optional<SceneLooks> m_looks;
            Fenrir::Entity m_bike;
            std::vector<Fenrir::Entity> m_wheels; // Front, rear: entity order, so wheel order.
            Fenrir::Entity m_camera;
            VehicleCamera m_followCamera;
            bool m_autopilot = false;
            float m_direction = 1.0f; // The way the keys last rode it: S brakes while it goes forward, then reverses.
            std::size_t m_waypoint = 0;
        };
    } // namespace

    FenrirDemo::DemoScene Motorcycle(Fenrir::App& app)
    {
        auto motorcycle = std::make_shared<MotorcycleScene>();
        app.AddSequentialSystem(Fenrir::SchedulePriority::Tick,
                                [motorcycle](Fenrir::App& running) { motorcycle->Tick(running); });
        app.AddSequentialSystem(Fenrir::SchedulePriority::Update,
                                [motorcycle](Fenrir::App& running) { motorcycle->Update(running); });

        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp = "W or Up drives, S or Down brakes and then reverses once stopped, A/D or Left/Right steer, "
                        "Space pulls the handbrake, C frees the camera (it stays where it is, the arrows still drive) "
                        "and snaps it back. P rides a loop by itself, L logs the bike's state. 3 changes the max lean "
                        "between 45 and 20 degrees (a rebuild keeping RPM and gear), 6 switches the lean controller "
                        "off (it falls over) and on, 5 switches its interpolation off and on, T stands it up at the "
                        "start.";
        // F2 steps the app's debug lines from off to "vehicles" first, so the run draws both wheels throughout.
        scene.keys.insert(scene.keys.end(), 7, Key::F2);
        // The autopilot rides the loop while L logs the bike, its max lean changed twice on the way, then stops.
        scene.keys.push_back(Key::P);
        scene.keys.insert(scene.keys.end(), 12, Key::L);
        scene.keys.insert(scene.keys.end(), {Key::Num3, Key::L, Key::L, Key::Num3});
        scene.keys.insert(scene.keys.end(), 12, Key::L);
        // Stopped, the lean controller off (it falls over) and on, then stood up again; and the other keys twice.
        scene.keys.insert(scene.keys.end(),
                          {Key::P, Key::L, Key::L, Key::Num6, Key::L, Key::L, Key::L, Key::Num6, Key::T, Key::L});
        for (const Key key : {Key::C, Key::Space, Key::Num5})
        {
            scene.keys.insert(scene.keys.end(), {key, key});
        }
        scene.keys.push_back(Key::L);
        scene.spawn = [motorcycle](Fenrir::App& running, Fenrir::Scene& spawned) {
            motorcycle->Spawn(running, spawned);
        };
        return scene;
    }
} // namespace PhysicsDemo
