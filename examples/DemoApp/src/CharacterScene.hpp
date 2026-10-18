#pragma once

// PROTOTYPE (prototype/fenrir-physics-character-spike, ticket 37): the Characters (spike) scene, a character body
// (ADR-0014) walked from the keyboard through a small level. F5 steps to it from Default and back.
//
// The character is a capsule collider with a CharacterBody on its own entity; its look is two child cubes with no
// collider (a body and a nose showing where it faces). Game code in Tick does what the spec asks of it: sets the
// horizontal velocity from the keys, adds gravity, jumps only from walkable ground, and turns the character to face
// where it walks. Physics moves it in PostTick before the step and writes velocity back trimmed by what it hit.
//
// The level, around the start (a white pad): a walkable 30 degree ramp to the right (east), a 55 degree slope too
// steep to climb to the left (west), a flight of 0.3 m stairs ahead (north) up to a ledge, a lift beside the ledge
// going up and down, a shuttle going sideways in the north-west, five 1 kg boxes behind the start (south), and a low
// ceiling south-west to jump into.
//
// The character entity's name shows its state every frame: ground, velocity, carried velocity, and how often its
// ground state has changed in the last second. Each change of ground state is logged with its tick, to see flicker.

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

#include "DemoScenes.hpp"
#include "Events.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirCamera/Camera.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "Material.hpp"
#include "ModelLibrary.hpp"

class CharacterScene
{
  public:
    static constexpr const char* name = "Characters (spike)";

    /** @brief The scene as the switcher steps to it: F5 from Default and back. */
    static DemoScene Scene()
    {
        return DemoScene{
            .name = name,
            .familyKey = DemoFamilyKey::characterController,
            .keyHelp =
                "WASD or the arrows walk (relative to the camera), Shift runs, Space jumps, Q and E turn the "
                "camera. Ramp east, too-steep slope west, stairs north to a ledge with a lift, a shuttle "
                "north-west, boxes south, a low ceiling south-west. T teleports to the start, 1 toggles "
                "stopOnSlope, 2 toggles interpolation, 3 pauses the platforms, 4 disables the character, 5 "
                "toggles stairs and floor snap, L logs the state, P walks a route by itself, C frees the camera. "
                "F5 goes on.",
            .smokeKeys = {GLFW_KEY_P, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L,
                          GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L,
                          GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L,
                          GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_P, GLFW_KEY_1, GLFW_KEY_1,
                          GLFW_KEY_2, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_4, GLFW_KEY_5,
                          GLFW_KEY_5, GLFW_KEY_T, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L,
                          GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L, GLFW_KEY_L}};
    }

    CharacterScene(const Model& cube, const Shader& flatShader, Fenrir::Camera& camera)
        : m_cube(&cube), m_flatShader(&flatShader), m_camera(&camera)
    {
    }

    // PostInit: the cube and the flat shader are loaded in Init.
    void Init(Fenrir::App& app)
    {
        Fenrir::EntityList& entities = app.CreateScene(name).GetEntityList();
        m_entities = &entities;

        Fenrir::Entity ground = Box("Ground", Fenrir::Math::Vec3(0.0f, -0.5f, 0.0f),
                                    Fenrir::Math::Vec3(40.0f, 1.0f, 40.0f), Fenrir::Math::Vec3(0.45f));
        ground.AddComponent<Fenrir::Collider>();
        Box("Start", Fenrir::Math::Vec3(start.x, 0.005f, start.z), Fenrir::Math::Vec3(1.2f, 0.01f, 1.2f),
            Fenrir::Math::Vec3(0.95f));

        // East: 30 degrees, rising to the east, its foot at x = 3.
        Slope("Ramp (30 degrees, walkable)", 3.0f, 30.0f, 1.0f, Fenrir::Math::Vec3(0.3f, 0.7f, 0.3f));
        // West: 55 degrees, rising to the west, its foot at x = -3.
        Slope("Steep slope (55 degrees)", -3.0f, 55.0f, -1.0f, Fenrir::Math::Vec3(0.8f, 0.3f, 0.25f));

        // North: ten steps of 0.3 m, 0.4 m deep, from z = -3, up to a ledge 3 m high.
        for (int i = 0; i < stepCount; ++i)
        {
            const float rise = stepRise * static_cast<float>(i + 1);
            Box(("Step " + std::to_string(i)).c_str(),
                Fenrir::Math::Vec3(0.0f, rise / 2.0f, -3.0f - stepDepth * (static_cast<float>(i) + 0.5f)),
                Fenrir::Math::Vec3(2.0f, rise, stepDepth), Fenrir::Math::Vec3(i % 2 == 0 ? 0.6f : 0.68f))
                .AddComponent<Fenrir::Collider>();
        }
        const float ledgeTop = stepRise * stepCount;
        const float ledgeNorth = -3.0f - stepDepth * stepCount;
        Box("Ledge", Fenrir::Math::Vec3(0.0f, ledgeTop / 2.0f, ledgeNorth - 2.0f),
            Fenrir::Math::Vec3(4.0f, ledgeTop, 4.0f), Fenrir::Math::Vec3(0.6f))
            .AddComponent<Fenrir::Collider>();

        // East of the ledge: a lift between just above the ground and the ledge's top, 1.5 m/s.
        m_lift = Platform("Lift (up and down)", Fenrir::Math::Vec3(3.5f, liftBottom, ledgeNorth - 2.0f),
                          Fenrir::Math::Vec3(0.0f, 1.5f, 0.0f), (ledgeTop - liftBottom) / 1.5f,
                          Fenrir::Math::Vec3(0.95f, 0.55f, 0.15f));
        // North-west: a shuttle 0.3 m up (a step onto it), 2 m/s for 4 s each way.
        m_shuttle = Platform("Shuttle (sideways)", Fenrir::Math::Vec3(-6.0f, 0.3f, -8.0f),
                             Fenrir::Math::Vec3(-2.0f, 0.0f, 0.0f), 4.0f, Fenrir::Math::Vec3(0.15f, 0.55f, 0.95f));

        // South: five 1 kg boxes, two stacked.
        static const std::array<Fenrir::Math::Vec3, 5> boxes = {
            Fenrir::Math::Vec3(-1.5f, 0.4f, 4.0f), Fenrir::Math::Vec3(0.0f, 0.4f, 4.5f),
            Fenrir::Math::Vec3(1.5f, 0.4f, 4.0f), Fenrir::Math::Vec3(0.0f, 0.4f, 6.0f),
            Fenrir::Math::Vec3(0.0f, 1.2f, 6.0f)};
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            Fenrir::Entity box = Box(("Box " + std::to_string(i)).c_str(), boxes[i], Fenrir::Math::Vec3(0.8f),
                                     Fenrir::Math::Vec3(0.9f, 0.2f + 0.15f * static_cast<float>(i), 0.2f));
            box.AddComponent<Fenrir::RigidBody>();
            box.AddComponent<Fenrir::Collider>();
        }

        // South-west: a ceiling 2.3 m up, 0.7 m over the character's head.
        Box("Low ceiling", Fenrir::Math::Vec3(-5.0f, 2.5f, 5.0f), Fenrir::Math::Vec3(3.0f, 0.4f, 3.0f),
            Fenrir::Math::Vec3(0.5f, 0.5f, 0.6f))
            .AddComponent<Fenrir::Collider>();
        Box("Ceiling post", Fenrir::Math::Vec3(-6.6f, 1.15f, 6.6f), Fenrir::Math::Vec3(0.2f, 2.3f, 0.2f),
            Fenrir::Math::Vec3(0.5f))
            .AddComponent<Fenrir::Collider>();

        MakeCharacter();
    }

    // Update, while the scene is active: keys, the character's readings in its name, and ground-state changes logged.
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
        if (active)
        {
            ShowReadings(app);
        }
    }

    // Tick: game code drives the character and moves the platforms. Platforms move only while the scene is active.
    void Tick(Fenrir::App& app)
    {
        if (app.GetActiveScene().GetName() != name)
        {
            return;
        }
        const float dt = static_cast<float>(app.GetTime().tickRate);
        ++m_tick;
        if (!m_platformsPaused)
        {
            m_platformTime += dt;
            MovePlatform(m_lift);
            MovePlatform(m_shuttle);
        }

        Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        if (world == nullptr)
        {
            return;
        }
        const Fenrir::CharacterState state = world->GetCharacterState(m_character);
        LogGroundChange(app, state.ground);

        Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        const Fenrir::Math::Vec3 walk = m_autopilot ? AutopilotWalk() : KeyboardWalk();
        body.velocity.x = walk.x;
        body.velocity.z = walk.z;
        body.velocity.y += world->Gravity().y * dt;
        if ((m_jump || (m_autopilot && m_autoJump)) && state.ground == Fenrir::CharacterGround::OnGround)
        {
            body.velocity.y = jumpSpeed;
        }
        m_jump = false;
        m_autoJump = false;

        // Turned to face where it walks: game code's job (ADR-0014), blended between steps like its position.
        if (Fenrir::Math::Magnitude(walk) > 0.1f)
        {
            const float yaw = std::atan2(-walk.x, -walk.z);
            m_character.GetComponent<Fenrir::Transform>().rot =
                Fenrir::Math::AngleAxis(yaw, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
        }
    }

    // Update, after the camera controller and before the renderer: the camera follows the character's shown pose.
    void FollowCamera(Fenrir::App& app)
    {
        if (app.GetActiveScene().GetName() != name || !m_follow)
        {
            return;
        }
        const float turn = static_cast<float>(app.GetTime().deltaTime) * 90.0f;
        m_cameraYaw += (m_held.contains(GLFW_KEY_E) ? turn : 0.0f) - (m_held.contains(GLFW_KEY_Q) ? turn : 0.0f);
        const Fenrir::Math::Vec3 target =
            Fenrir::GetWorldTransform(m_character).pos + Fenrir::Math::Vec3(0.0f, 0.6f, 0.0f);
        const float yaw = m_cameraYaw * std::numbers::pi_v<float> / 180.0f;
        const float pitch = cameraPitch * std::numbers::pi_v<float> / 180.0f;
        const Fenrir::Math::Vec3 front(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                                       std::cos(pitch) * std::sin(yaw));
        m_camera->pos = target - front * cameraDistance;
        m_camera->yaw = m_cameraYaw;
        m_camera->pitch = cameraPitch;
        m_camera->Update();
    }

  private:
    template <typename... Args>
    static std::string Format(const char* format, Args... args)
    {
        std::array<char, 256> text{};
        std::snprintf(text.data(), text.size(), format, args...);
        return text.data();
    }

    static std::string Describe(const Fenrir::Math::Vec3& v)
    {
        return Format("(%.2f, %.2f, %.2f)", static_cast<double>(v.x), static_cast<double>(v.y),
                      static_cast<double>(v.z));
    }

    static const char* NameOf(Fenrir::CharacterGround ground)
    {
        static constexpr std::array<const char*, 4> names = {"on ground", "on steep ground", "not supported", "in air"};
        return names[static_cast<std::size_t>(ground)];
    }

    void OnKey(Fenrir::App& app, int key)
    {
        Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        if (key == GLFW_KEY_SPACE)
        {
            m_jump = true;
        }
        else if (key == GLFW_KEY_T)
        {
            // Writing the Transform is a teleport: no sweep, the shown pose snaps, velocity is kept.
            m_character.GetComponent<Fenrir::Transform>().pos = start + Fenrir::Math::Vec3(0.0f, footToCentre, 0.0f);
            app.Logger()->Info("Character teleported to the start: it snaps there, keeping its velocity {}",
                               Describe(body.velocity));
        }
        else if (key == GLFW_KEY_1)
        {
            body.stopOnSlope = !body.stopOnSlope;
            app.Logger()->Info(body.stopOnSlope ? "stopOnSlope on: it stands still on the ramp"
                                                : "stopOnSlope off: it slides down the ramp (slowly: Tick sets its "
                                                  "horizontal speed from the keys each tick)");
        }
        else if (key == GLFW_KEY_2)
        {
            body.interpolate = !body.interpolate;
            app.Logger()->Info(body.interpolate ? "Character interpolated between steps"
                                                : "Character shown at its step poses (watch for judder above 60 Hz)");
        }
        else if (key == GLFW_KEY_3)
        {
            m_platformsPaused = !m_platformsPaused;
            app.Logger()->Info(m_platformsPaused ? "Platforms paused" : "Platforms moving");
        }
        else if (key == GLFW_KEY_4)
        {
            body.enabled = !body.enabled;
            app.Logger()->Info(body.enabled ? "Character enabled: its contacts are found again where it is"
                                            : "Character disabled: it does not move, and boxes pass through it");
        }
        else if (key == GLFW_KEY_5)
        {
            const bool on = body.stepHeight == 0.0f;
            body.stepHeight = on ? 0.4f : 0.0f;
            body.floorSnap = on ? 0.5f : 0.0f;
            app.Logger()->Info(on ? "Stairs (0.4 m) and floor snap (0.5 m) on"
                                  : "Stairs and floor snap off: the 0.3 m steps block it, and it hops down slopes");
        }
        else if (key == GLFW_KEY_L)
        {
            LogState(app);
        }
        else if (key == GLFW_KEY_P)
        {
            m_autopilot = !m_autopilot;
            m_waypoint = 0;
            app.Logger()->Info(m_autopilot ? "Autopilot: walking the route (ramp, stairs, ledge, lift, boxes, shuttle)"
                                           : "Autopilot off");
        }
        else if (key == GLFW_KEY_C)
        {
            m_follow = !m_follow;
            app.Logger()->Info(m_follow ? "Camera follows the character" : "Camera free: WASD moves it, F1 locks it");
        }
    }

    // The keys' walk, relative to where the camera looks, on the ground plane. While the camera is free, WASD is the
    // camera's, so only the arrows walk.
    Fenrir::Math::Vec3 KeyboardWalk() const
    {
        const auto held = [this](int key) { return m_held.contains(key); };
        float forward = 0.0f;
        float right = 0.0f;
        forward += (held(GLFW_KEY_UP) || (m_follow && held(GLFW_KEY_W))) ? 1.0f : 0.0f;
        forward -= (held(GLFW_KEY_DOWN) || (m_follow && held(GLFW_KEY_S))) ? 1.0f : 0.0f;
        right += (held(GLFW_KEY_RIGHT) || (m_follow && held(GLFW_KEY_D))) ? 1.0f : 0.0f;
        right -= (held(GLFW_KEY_LEFT) || (m_follow && held(GLFW_KEY_A))) ? 1.0f : 0.0f;
        if (forward == 0.0f && right == 0.0f)
        {
            return Fenrir::Math::Vec3(0.0f);
        }
        const float yaw = m_cameraYaw * std::numbers::pi_v<float> / 180.0f;
        const Fenrir::Math::Vec3 ahead(std::cos(yaw), 0.0f, std::sin(yaw));
        const Fenrir::Math::Vec3 side(-ahead.z, 0.0f, ahead.x);
        const float speed = held(GLFW_KEY_LEFT_SHIFT) ? runSpeed : walkSpeed;
        return Fenrir::Math::Normalized(ahead * forward + side * right) * speed;
    }

    struct Waypoint
    {
        Fenrir::Math::Vec3 at;
        // Jump on arriving.
        bool jump = false;
        // Seconds to stand once there.
        float wait = 0.0f;
    };

    // The autopilot's route: the walk to its next waypoint, jumping or waiting where it says.
    Fenrir::Math::Vec3 AutopilotWalk()
    {
        // Up the ramp and back, up the stairs to the ledge, onto the lift for a ride down and up, back down the stairs,
        // into the boxes, jumping under the ceiling, and onto the shuttle for a ride.
        static const std::vector<Waypoint> route = {
            {Fenrir::Math::Vec3(2.0f, 0.0f, 0.0f)},
            {Fenrir::Math::Vec3(6.5f, 0.0f, 0.0f), false, 2.0f},
            {Fenrir::Math::Vec3(0.0f, 0.0f, -1.5f)},
            {Fenrir::Math::Vec3(0.0f, 0.0f, -8.0f)},
            {Fenrir::Math::Vec3(3.5f, 0.0f, -9.0f), false, 6.0f},
            {Fenrir::Math::Vec3(1.0f, 0.0f, -9.0f)},
            {Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f)},
            {Fenrir::Math::Vec3(0.0f, 0.0f, 7.0f)},
            {Fenrir::Math::Vec3(-5.0f, 0.0f, 5.0f), true, 1.0f},
            {Fenrir::Math::Vec3(-6.0f, 0.0f, -5.0f)},
            {Fenrir::Math::Vec3(-6.0f, 0.0f, -8.0f), true, 5.0f},
            {Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f), false, 1.0f},
        };
        const Fenrir::Math::Vec3 at = Fenrir::GetWorldTransform(m_character).pos;
        const Waypoint& next = route[m_waypoint % route.size()];
        Fenrir::Math::Vec3 to = next.at - at;
        to.y = 0.0f;
        if (Fenrir::Math::Magnitude(to) > 0.3f)
        {
            m_waited = 0.0f;
            return Fenrir::Math::Normalized(to) * walkSpeed;
        }
        if (m_waited == 0.0f && next.jump)
        {
            m_autoJump = true;
        }
        m_waited += 1.0f / 60.0f;
        if (m_waited >= next.wait)
        {
            ++m_waypoint;
            m_waited = 0.0f;
        }
        return Fenrir::Math::Vec3(0.0f);
    }

    void LogGroundChange(Fenrir::App& app, Fenrir::CharacterGround ground)
    {
        m_changes.erase(m_changes.begin(),
                        std::lower_bound(m_changes.begin(), m_changes.end(), m_tick > 60 ? m_tick - 60 : 0));
        if (ground == m_lastGround)
        {
            return;
        }
        m_changes.push_back(m_tick);
        app.Logger()->Info("Tick {}: character {} -> {}", m_tick, NameOf(m_lastGround), NameOf(ground));
        m_lastGround = ground;
    }

    void ShowReadings(Fenrir::App& app) const
    {
        const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        if (world == nullptr)
        {
            return;
        }
        const Fenrir::CharacterState state = world->GetCharacterState(m_character);
        const Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        const std::string reading = std::string("Character: ") + NameOf(state.ground) + ", v " +
                                    Describe(body.velocity) + ", carried " + Describe(state.carriedVelocity) + ", " +
                                    std::to_string(m_changes.size()) + " ground changes/s";
        m_character.GetComponent<Fenrir::Name>().Set(reading.c_str());
    }

    void LogState(Fenrir::App& app) const
    {
        const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        if (world == nullptr)
        {
            return;
        }
        const Fenrir::CharacterState state = world->GetCharacterState(m_character);
        const Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        const std::string on =
            state.groundBody.IsValid() ? state.groundBody.GetComponent<Fenrir::Name>().Get() : "nothing";
        app.Logger()->Info(
            "Tick {}: at {}, {} on {} (normal {}), velocity {}, moved at {}, ground velocity {}, carried {}", m_tick,
            Describe(Fenrir::GetWorldTransform(m_character).pos), NameOf(state.ground), on,
            Describe(state.groundNormal), Describe(body.velocity), Describe(state.movedVelocity),
            Describe(state.groundVelocity), Describe(state.carriedVelocity));
    }

    // A unit cube model scaled to `size`, in a flat colour. Its collider, if any, is the default unit box, scaled too.
    Fenrir::Entity Box(const char* boxName, const Fenrir::Math::Vec3& position, const Fenrir::Math::Vec3& size,
                       const Fenrir::Math::Vec3& colour, const Fenrir::Math::Quat& rotation = identity) const
    {
        Fenrir::Entity box = m_entities->CreateEntity();
        box.GetComponent<Fenrir::Name>().Set(boxName);
        box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, rotation, size);
        Material material{*m_flatShader, {}};
        material.properties["color"] = colour;
        box.AddComponent<Material>(material);
        box.AddComponent<Model>(*m_cube);
        return box;
    }

    // A slab 6 m long and 4 m wide tilted by `degrees`, its foot on the ground at x = `foot`, rising away from the
    // start along `direction` (1 east, -1 west).
    void Slope(const char* slopeName, float foot, float degrees, float direction,
               const Fenrir::Math::Vec3& colour) const
    {
        const float angle = degrees * std::numbers::pi_v<float> / 180.0f;
        const float length = 6.0f;
        const float thickness = 0.4f;
        // The slab's top face runs from the foot up the slope; its centre is half a length along and half a
        // thickness below.
        const Fenrir::Math::Vec3 along(direction * std::cos(angle), std::sin(angle), 0.0f);
        const Fenrir::Math::Vec3 down(direction * std::sin(angle), -std::cos(angle), 0.0f);
        const Fenrir::Math::Vec3 centre =
            Fenrir::Math::Vec3(foot, 0.0f, 0.0f) + along * (length / 2.0f) + down * (thickness / 2.0f);
        const Fenrir::Math::Quat tilt =
            Fenrir::Math::AngleAxis(direction * angle, Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
        Box(slopeName, centre, Fenrir::Math::Vec3(length, thickness, 4.0f), colour, tilt)
            .AddComponent<Fenrir::Collider>();
    }

    struct Moving
    {
        Fenrir::Entity entity;
        Fenrir::Math::Vec3 home;
        Fenrir::Math::Vec3 velocity;
        float seconds;
    };

    // A kinematic platform 3 m square and 0.2 m thick, its top at `top`, moved by MovePlatform at `velocity` for
    // `seconds` and back, over and over.
    Moving Platform(const char* platformName, const Fenrir::Math::Vec3& top, const Fenrir::Math::Vec3& velocity,
                    float seconds, const Fenrir::Math::Vec3& colour) const
    {
        const Fenrir::Math::Vec3 centre = top - Fenrir::Math::Vec3(0.0f, 0.1f, 0.0f);
        Fenrir::Entity platform = Box(platformName, centre, Fenrir::Math::Vec3(3.0f, 0.2f, 3.0f), colour);
        platform.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.motionType = Fenrir::MotionType::Kinematic});
        platform.AddComponent<Fenrir::Collider>();
        return Moving{platform, centre, velocity, seconds};
    }

    // At a constant speed out and back: a triangle wave, so riding it shows the hover and the turns plainly.
    void MovePlatform(const Moving& platform) const
    {
        const float cycle = std::fmod(m_platformTime, 2.0f * platform.seconds);
        const float out = cycle < platform.seconds ? cycle : 2.0f * platform.seconds - cycle;
        platform.entity.GetComponent<Fenrir::Transform>().pos = platform.home + platform.velocity * out;
    }

    void MakeCharacter()
    {
        m_character = m_entities->CreateEntity();
        m_character.GetComponent<Fenrir::Name>().Set("Character");
        m_character.GetComponent<Fenrir::Transform>().pos = start + Fenrir::Math::Vec3(0.0f, footToCentre + 0.5f, 0.0f);
        m_character.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f}});
        m_character.AddComponent<Fenrir::CharacterBody>();

        // Its look: children with no collider, so not part of it (ADR-0014: no compound characters).
        Fenrir::Entity look = Box("Character body", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(0.6f, 1.6f, 0.6f),
                                  Fenrir::Math::Vec3(0.95f, 0.85f, 0.3f));
        m_character.AddChild(look);
        Fenrir::Entity nose = Box("Character nose", Fenrir::Math::Vec3(0.0f, 0.5f, -0.35f), Fenrir::Math::Vec3(0.2f),
                                  Fenrir::Math::Vec3(0.9f, 0.2f, 0.2f));
        m_character.AddChild(nose);
    }

    static constexpr float footToCentre = 0.8f;
    static constexpr float liftBottom = 0.02f;
    static constexpr float walkSpeed = 4.0f;
    static constexpr float runSpeed = 8.0f;
    static constexpr float jumpSpeed = 5.0f;
    static constexpr int stepCount = 10;
    static constexpr float stepRise = 0.3f;
    static constexpr float stepDepth = 0.4f;
    static constexpr float cameraDistance = 9.0f;
    static constexpr float cameraPitch = -25.0f;
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    inline static const Fenrir::Math::Vec3 start = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);

    const Model* m_cube;
    const Shader* m_flatShader;
    Fenrir::Camera* m_camera;
    Fenrir::EntityList* m_entities = nullptr;
    Fenrir::Entity m_character;
    Moving m_lift;
    Moving m_shuttle;
    float m_platformTime = 0.0f;
    bool m_platformsPaused = false;
    std::set<int> m_held;
    bool m_jump = false;
    bool m_follow = true;
    float m_cameraYaw = -90.0f;
    bool m_autopilot = false;
    bool m_autoJump = false;
    std::size_t m_waypoint = 0;
    float m_waited = 0.0f;
    std::size_t m_tick = 0;
    Fenrir::CharacterGround m_lastGround = Fenrir::CharacterGround::InAir;
    std::vector<std::size_t> m_changes;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
};

#endif // FENRIR_PHYSICS
