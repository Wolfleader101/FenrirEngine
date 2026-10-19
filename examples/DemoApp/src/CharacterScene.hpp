#pragma once

// The Character (C++) scene: a character body (ADR-0014) walked from the keyboard through a small level, built in C++.
// F5 steps to it from Default and back.
//
// The character is a capsule Collider with a CharacterBody on its own entity; its look is a child box with no collider,
// so not part of it. Game code in Tick does what the spec asks of it: steers the horizontal velocity physics wrote back
// towards the keys' walk by at most its acceleration, adds gravity, and jumps only from walkable ground. Physics moves
// it in PostTick before the step and writes velocity back trimmed by what it touched, and its state beside it.
//
// The level is built one function per piece, each with its own leg of the autopilot route, so later pieces add a
// function, a leg and their keys without touching the others': flat ground with a white start pad, a wall to the
// south-east to walk into (onWall), and a low ceiling to the south-west to jump into (onCeiling).
//
// The camera is a CharacterCamera: C++ game code that follows the character and writes the scene's DemoCamera, which
// DemoCameras copies into the app's camera. C frees the camera (WASD flies it, the arrows still walk) and snaps it
// back.
//
// The character's name shows its readings every frame: ground state and what it stands on, velocity, moved velocity,
// onWall and onCeiling while set, how many times it has landed, and how often its ground state changed in the last
// second. Each ground change is logged with its tick; L logs the whole state.
//
// T teleports the character to just above the start by writing its Transform's position, and the autopilot does so at
// the end of each route. 2 toggles its interpolation, which a 60 Hz display with vsync barely shows.

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
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "Material.hpp"
#include "ModelLibrary.hpp"

/**
 * @brief A follow camera, as game code writes one: it orbits the character at a fixed distance and pitch, Q and E turn
 * it, and it writes where it is into a DemoCamera. Switched off, it leaves the DemoCamera inactive.
 */
class CharacterCamera
{
  public:
    /** @brief Degrees about world +Y, as DemoCamera's: the way the camera looks, and so what "forward" walks. */
    [[nodiscard]] float Yaw() const
    {
        return m_yaw;
    }

    [[nodiscard]] bool Following() const
    {
        return m_following;
    }

    void Toggle()
    {
        m_following = !m_following;
    }

    /** @brief Each frame: turns by Q and E held, and writes the camera looking at `target` into `camera`. */
    void Update(DemoCamera& camera, const Fenrir::Math::Vec3& target, bool turnLeft, bool turnRight, float dt)
    {
        camera.Active = m_following ? 1 : 0;
        if (!m_following)
        {
            return;
        }
        m_yaw += (turnRight ? turnSpeed * dt : 0.0f) - (turnLeft ? turnSpeed * dt : 0.0f);
        const float yaw = m_yaw * std::numbers::pi_v<float> / 180.0f;
        const float pitch = pitchDegrees * std::numbers::pi_v<float> / 180.0f;
        const Fenrir::Math::Vec3 front(std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                                       std::cos(pitch) * std::sin(yaw));
        camera.Position = target - front * distance;
        camera.Yaw = m_yaw;
        camera.Pitch = pitchDegrees;
    }

  private:
    static constexpr float distance = 9.0f;
    static constexpr float pitchDegrees = -25.0f;
    static constexpr float turnSpeed = 90.0f; // Degrees per second.

    float m_yaw = -90.0f;
    bool m_following = true;
};

class CharacterScene
{
  public:
    static constexpr const char* name = "Character (C++)";

    /** @brief The scene as the switcher steps to it: F5 from Default and back. */
    static DemoScene Scene()
    {
        DemoScene scene{.name = name,
                        .familyKey = DemoFamilyKey::characterController,
                        .keyHelp =
                            "WASD or the arrows walk (relative to the camera), Shift runs, Space jumps, Q and E "
                            "turn the camera, C frees the camera and snaps it back. A wall to the south-east, a "
                            "low ceiling to the south-west. T teleports to the start, 2 toggles interpolation. L "
                            "logs the state, P walks the route by itself. F5 goes on.",
                        .smokeKeys = {GLFW_KEY_P}};
        // The autopilot walks every piece while L logs the state, then stops, each key is tapped twice, and T ends it.
        scene.smokeKeys.insert(scene.smokeKeys.end(), 25, GLFW_KEY_L);
        scene.smokeKeys.push_back(GLFW_KEY_P);
        for (const int key :
             {GLFW_KEY_C, GLFW_KEY_SPACE, GLFW_KEY_Q, GLFW_KEY_E, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_T, GLFW_KEY_2})
        {
            scene.smokeKeys.insert(scene.smokeKeys.end(), {key, key});
        }
        scene.smokeKeys.push_back(GLFW_KEY_L);
        scene.smokeKeys.push_back(GLFW_KEY_T);
        return scene;
    }

    CharacterScene(const Model& cube, const Shader& flatShader) : m_cube(&cube), m_flatShader(&flatShader)
    {
    }

    // PostInit: the cube and the flat shader are loaded in Init.
    void Init(Fenrir::App& app)
    {
        m_entities = &app.CreateScene(name).GetEntityList();
        BuildGround();
        BuildWall();
        BuildLowCeiling();
        MakeCharacter();

        m_camera = m_entities->CreateEntity();
        m_camera.GetComponent<Fenrir::Name>().Set("Character camera");
        m_camera.AddComponent<DemoCamera>();
    }

    // Update, while the scene is active and before DemoCameras: keys, the camera, and the character's readings.
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
        m_followCamera.Update(m_camera.GetComponent<DemoCamera>(),
                              Fenrir::GetWorldTransform(m_character).pos + Fenrir::Math::Vec3(0.0f, 0.6f, 0.0f),
                              m_held.contains(GLFW_KEY_Q), m_held.contains(GLFW_KEY_E),
                              static_cast<float>(app.GetTime().deltaTime));
        ShowReadings();
    }

    // Tick, while the scene is active: game code reads the last move's state and drives the character.
    void Tick(Fenrir::App& app)
    {
        const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        if (app.GetActiveScene().GetName() != name || world == nullptr)
        {
            return;
        }
        const float dt = static_cast<float>(app.GetTime().tickRate);
        ++m_tick;
        Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        NoteGroundChange(app, body.state.ground);

        // Steered from the velocity physics wrote back, never set outright, so what physics trimmed stays trimmed and
        // a slide keeps its speed.
        const Fenrir::Math::Vec3 walk = m_autopilot ? AutopilotWalk(app, dt) : KeyboardWalk();
        Fenrir::Math::Vec3 horizontal(body.velocity.x, 0.0f, body.velocity.z);
        const Fenrir::Math::Vec3 change = walk - horizontal;
        const float most = acceleration * dt;
        const float length = Fenrir::Math::Magnitude(change);
        horizontal += length > most ? change * (most / length) : change;
        body.velocity.x = horizontal.x;
        body.velocity.z = horizontal.z;

        // Gravity is game code's: physics adds none.
        body.velocity.y += world->Gravity().y * dt;
        if (m_jump && body.state.ground == Fenrir::GroundState::OnGround)
        {
            body.velocity.y = jumpSpeed;
        }
        m_jump = false;
    }

  private:
    static std::string Describe(const Fenrir::Math::Vec3& v)
    {
        std::array<char, 64> text{};
        std::snprintf(text.data(), text.size(), "(%.2f, %.2f, %.2f)", static_cast<double>(v.x),
                      static_cast<double>(v.y), static_cast<double>(v.z));
        return text.data();
    }

    static const char* NameOf(Fenrir::GroundState ground)
    {
        static constexpr std::array<const char*, 4> names = {"in air", "on ground", "on steep ground", "touching"};
        return names[static_cast<std::size_t>(ground)];
    }

    static std::string NameOf(const Fenrir::Entity& entity)
    {
        return entity.IsValid() ? entity.GetComponent<Fenrir::Name>().Get() : "nothing";
    }

    void OnKey(Fenrir::App& app, int key)
    {
        if (key == GLFW_KEY_SPACE)
        {
            m_jump = true;
        }
        else if (key == GLFW_KEY_C)
        {
            m_followCamera.Toggle();
            app.Logger()->Info(m_followCamera.Following() ? "Camera follows the character"
                                                          : "Camera free: WASD flies it, the arrows walk");
        }
        else if (key == GLFW_KEY_L)
        {
            LogState(app);
        }
        else if (key == GLFW_KEY_T)
        {
            TeleportToStart(app);
        }
        else if (key == GLFW_KEY_2)
        {
            bool& interpolate = m_character.GetComponent<Fenrir::CharacterBody>().interpolate;
            interpolate = !interpolate;
            app.Logger()->Info(interpolate ? "2: interpolation on" : "2: interpolation off");
        }
        else if (key == GLFW_KEY_P)
        {
            m_autopilot = !m_autopilot;
            m_waypoint = 0;
            m_onWaypoint = 0.0f;
            app.Logger()->Info(m_autopilot ? "Autopilot: walking the route" : "Autopilot off");
        }
    }

    // A teleport: only the position is written, so physics puts it there with no sweep and finds its ground anew.
    // Half a metre up, so it is seen to drop onto the pad.
    void TeleportToStart(Fenrir::App& app)
    {
        m_character.GetComponent<Fenrir::Transform>().pos = start + Fenrir::Math::Vec3(0.0f, footToCentre + 0.5f, 0.0f);
        app.Logger()->Info("T: teleported to the start");
    }

    // The keys' walk on the ground plane, relative to where the camera looks. While the camera is free, WASD is the
    // camera's, so only the arrows walk.
    Fenrir::Math::Vec3 KeyboardWalk() const
    {
        const bool wasd = m_followCamera.Following();
        const auto held = [this](int key) { return m_held.contains(key); };
        float forward = 0.0f;
        float right = 0.0f;
        forward += (held(GLFW_KEY_UP) || (wasd && held(GLFW_KEY_W))) ? 1.0f : 0.0f;
        forward -= (held(GLFW_KEY_DOWN) || (wasd && held(GLFW_KEY_S))) ? 1.0f : 0.0f;
        right += (held(GLFW_KEY_RIGHT) || (wasd && held(GLFW_KEY_D))) ? 1.0f : 0.0f;
        right -= (held(GLFW_KEY_LEFT) || (wasd && held(GLFW_KEY_A))) ? 1.0f : 0.0f;
        if (forward == 0.0f && right == 0.0f)
        {
            return Fenrir::Math::Vec3(0.0f);
        }
        const float yaw = m_followCamera.Yaw() * std::numbers::pi_v<float> / 180.0f;
        const Fenrir::Math::Vec3 ahead(std::cos(yaw), 0.0f, std::sin(yaw));
        const Fenrir::Math::Vec3 side(-ahead.z, 0.0f, ahead.x);
        const float speed = held(GLFW_KEY_LEFT_SHIFT) ? runSpeed : walkSpeed;
        return Fenrir::Math::Normalized(ahead * forward + side * right) * speed;
    }

    // Level pieces, each with its leg of the autopilot route.

    struct Waypoint
    {
        Fenrir::Math::Vec3 at;
        bool jump = false;    // Jump on arriving.
        float wait = 0.0f;    // Seconds to stand once there.
        float giveUp = 10.0f; // Seconds to try for it: a waypoint behind a wall is walked into until then.
    };

    void BuildGround()
    {
        Box("Ground", Fenrir::Math::Vec3(0.0f, -0.5f, 0.0f), Fenrir::Math::Vec3(40.0f, 1.0f, 40.0f),
            Fenrir::Math::Vec3(0.45f))
            .AddComponent<Fenrir::Collider>();
        Box("Start", Fenrir::Math::Vec3(start.x, 0.005f, start.z), Fenrir::Math::Vec3(1.2f, 0.01f, 1.2f),
            Fenrir::Math::Vec3(0.95f));
    }

    // South-east: a wall 6 m long and 3 m high, its west face at x = 6.
    void BuildWall()
    {
        Box("Wall", Fenrir::Math::Vec3(6.5f, 1.5f, 5.0f), Fenrir::Math::Vec3(1.0f, 3.0f, 6.0f),
            Fenrir::Math::Vec3(0.55f, 0.4f, 0.3f))
            .AddComponent<Fenrir::Collider>();
    }

    // Walks into the wall and along it, then back to the start.
    static std::vector<Waypoint> WallLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(4.0f, 0.0f, 3.0f)},
                Waypoint{.at = Fenrir::Math::Vec3(8.0f, 0.0f, 4.0f), .giveUp = 1.5f},
                Waypoint{.at = Fenrir::Math::Vec3(8.0f, 0.0f, 7.0f), .giveUp = 1.5f},
                Waypoint{.at = start, .wait = 0.5f}};
    }

    // South-west: a ceiling 2.3 m up on a post, 0.7 m over the character's head: a jump under it hits it.
    void BuildLowCeiling()
    {
        Box("Low ceiling", Fenrir::Math::Vec3(-5.0f, 2.5f, 5.0f), Fenrir::Math::Vec3(3.0f, 0.4f, 3.0f),
            Fenrir::Math::Vec3(0.5f, 0.5f, 0.6f))
            .AddComponent<Fenrir::Collider>();
        Box("Ceiling post", Fenrir::Math::Vec3(-6.6f, 1.15f, 6.6f), Fenrir::Math::Vec3(0.2f, 2.3f, 0.2f),
            Fenrir::Math::Vec3(0.5f))
            .AddComponent<Fenrir::Collider>();
    }

    // Walks under the ceiling and jumps into it twice, then back to the start.
    static std::vector<Waypoint> LowCeilingLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(-5.0f, 0.0f, 5.0f), .jump = true, .wait = 1.0f},
                Waypoint{.at = Fenrir::Math::Vec3(-4.5f, 0.0f, 4.5f), .jump = true, .wait = 1.0f},
                Waypoint{.at = start, .wait = 0.5f}};
    }

    // The route: each piece's leg in turn, over and over, each time ending with a teleport to the start.
    static const std::vector<Waypoint>& Route()
    {
        static const std::vector<Waypoint> route = [] {
            std::vector<Waypoint> legs;
            for (const std::vector<Waypoint>& leg : {WallLeg(), LowCeilingLeg()})
            {
                legs.insert(legs.end(), leg.begin(), leg.end());
            }
            return legs;
        }();
        return route;
    }

    // The autopilot's walk towards its next waypoint, jumping and waiting where it says.
    Fenrir::Math::Vec3 AutopilotWalk(Fenrir::App& app, float dt)
    {
        const std::vector<Waypoint>& route = Route();
        const Waypoint& next = route[m_waypoint % route.size()];
        m_onWaypoint += dt;
        Fenrir::Math::Vec3 to = next.at - Fenrir::GetWorldTransform(m_character).pos;
        to.y = 0.0f;
        const bool there = Fenrir::Math::Magnitude(to) < 0.3f;
        if (!there && m_onWaypoint < next.giveUp)
        {
            m_waited = 0.0f;
            return Fenrir::Math::Normalized(to) * walkSpeed;
        }
        if (m_waited == 0.0f && next.jump)
        {
            m_jump = true;
        }
        m_waited += dt;
        if (m_waited >= next.wait)
        {
            ++m_waypoint;
            m_waited = 0.0f;
            m_onWaypoint = 0.0f;
            if (m_waypoint % route.size() == 0)
            {
                TeleportToStart(app);
            }
        }
        return Fenrir::Math::Vec3(0.0f);
    }

    // Each ground change, logged with its tick, and counted for the readings.
    void NoteGroundChange(Fenrir::App& app, Fenrir::GroundState ground)
    {
        m_changes.erase(m_changes.begin(),
                        std::lower_bound(m_changes.begin(), m_changes.end(), m_tick > 60 ? m_tick - 60 : 0));
        if (ground == m_lastGround)
        {
            return;
        }
        if (ground == Fenrir::GroundState::OnGround)
        {
            ++m_landings;
        }
        m_changes.push_back(m_tick);
        app.Logger()->Info("Tick {}: character {} -> {}", m_tick, NameOf(m_lastGround), NameOf(ground));
        m_lastGround = ground;
    }

    void ShowReadings() const
    {
        const Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        const Fenrir::CharacterState& state = body.state;
        std::string reading = std::string(NameOf(state.ground));
        if (state.groundBody.IsValid())
        {
            reading += ": " + NameOf(state.groundBody);
        }
        reading += ", v " + Describe(body.velocity) + ", moved " + Describe(state.movedVelocity);
        reading += state.onWall ? ", on wall" : "";
        reading += state.onCeiling ? ", on ceiling" : "";
        reading +=
            ", " + std::to_string(m_landings) + " landings, " + std::to_string(m_changes.size()) + " ground changes/s";
        m_character.GetComponent<Fenrir::Name>().Set(reading.c_str());
    }

    void LogState(Fenrir::App& app) const
    {
        const Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        const Fenrir::CharacterState& state = body.state;
        app.Logger()->Info("Tick {}: at {}, {} on {} ({}, at {}, normal {}), velocity {}, moved at {}, onWall {}, "
                           "onCeiling {}",
                           m_tick, Describe(Fenrir::GetWorldTransform(m_character).pos), NameOf(state.ground),
                           NameOf(state.groundBody), NameOf(state.groundCollider), Describe(state.groundPoint),
                           Describe(state.groundNormal), Describe(body.velocity), Describe(state.movedVelocity),
                           state.onWall, state.onCeiling);
    }

    // A unit cube model scaled to `size`, in a flat colour. Its collider, if any, is the default unit box, scaled too.
    Fenrir::Entity Box(const char* boxName, const Fenrir::Math::Vec3& position, const Fenrir::Math::Vec3& size,
                       const Fenrir::Math::Vec3& colour) const
    {
        Fenrir::Entity box = m_entities->CreateEntity();
        box.GetComponent<Fenrir::Name>().Set(boxName);
        box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, size);
        Material material{*m_flatShader, {}};
        material.properties["color"] = colour;
        box.AddComponent<Material>(material);
        box.AddComponent<Model>(*m_cube);
        return box;
    }

    void MakeCharacter()
    {
        m_character = m_entities->CreateEntity();
        m_character.GetComponent<Fenrir::Name>().Set("Character");
        m_character.GetComponent<Fenrir::Transform>().pos = start + Fenrir::Math::Vec3(0.0f, footToCentre + 0.5f, 0.0f);
        m_character.AddComponent<Fenrir::Collider>(
            Fenrir::Collider{Fenrir::CapsuleShape{.halfHeight = 0.5f, .radius = 0.3f}});
        m_character.AddComponent<Fenrir::CharacterBody>();

        // Its look: a child with no collider, so not part of it (ADR-0014: no compound characters).
        Fenrir::Entity look = Box("Character look", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(0.6f, 1.6f, 0.6f),
                                  Fenrir::Math::Vec3(0.95f, 0.85f, 0.3f));
        m_character.AddChild(look);
    }

    static constexpr float footToCentre = 0.8f;
    static constexpr float walkSpeed = 4.0f;
    static constexpr float runSpeed = 8.0f;
    static constexpr float acceleration = 30.0f; // m/s², the most the walk changes the velocity by.
    static constexpr float jumpSpeed = 5.0f;
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    inline static const Fenrir::Math::Vec3 start = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);

    const Model* m_cube;
    const Shader* m_flatShader;
    Fenrir::EntityList* m_entities = nullptr;
    Fenrir::Entity m_character;
    Fenrir::Entity m_camera;
    CharacterCamera m_followCamera;
    std::set<int> m_held;
    bool m_jump = false;
    bool m_autopilot = false;
    std::size_t m_waypoint = 0;
    float m_onWaypoint = 0.0f;
    float m_waited = 0.0f;
    std::size_t m_tick = 0;
    Fenrir::GroundState m_lastGround = Fenrir::GroundState::InAir;
    std::size_t m_landings = 0;
    std::vector<std::size_t> m_changes;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
};

#endif // FENRIR_PHYSICS
