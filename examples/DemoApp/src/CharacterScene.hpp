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
// south-east to walk into (onWall), a low ceiling to the south-west to jump into (onCeiling), a crouch tunnel to the
// north with a loose box over a gap in its roof and a trigger at its far end, a ledge to the north-west only a
// crouch-jump clears, and to the north-east a lift (up and down 3 m at 1.5 m/s) and a shuttle (to and fro along z at
// 2 m/s).
//
// The platforms are kinematic bodies game code moves in Tick, and physics alone makes the character ride them: the
// readings name the platform ("on ground: Lift"), and jumping or walking off one keeps its velocity as carried velocity
// until landing. The lift turns at once at the bottom, where the character, which trails a descending lift by a tick of
// its travel, is in the air for a few ticks (the spec's stated limit). 3 pauses and resumes the platforms.
//
// Crouching is game code editing the character's Collider once: X toggles it, and G scales the character by 1.5 and
// back. Physics applies a shape edit only once it fits, keeping the feet in place on the ground and tucking them up in
// the air; until then the character keeps its old shape and its state says shapeBlocked. The character's look shows the
// shape in use.
//
// The camera is a CharacterCamera: C++ game code that follows the character and writes the scene's DemoCamera, which
// DemoCameras copies into the app's camera. C frees the camera (WASD flies it, the arrows still walk) and snaps it
// back.
//
// The character's name shows its readings every frame: ground state and what it stands on, velocity, moved velocity,
// carried velocity while it has some, onWall, onCeiling and shapeBlocked while set, how many times it has landed, and
// how often its ground state changed in the last second. Each ground change is logged with its tick; L logs the whole
// state.
//
// T teleports the character to just above the start by writing its Transform's position, and the autopilot does so at
// the end of each route. 2 toggles its interpolation, which a 60 Hz display with vsync barely shows.
//
// 4 toggles the CharacterBody's enabled: disabled, nothing sees it (the loose box falls through it), game code stops
// driving it, and its state reads as the defaults; enabled again, it carries on where it stands. M swaps its Collider
// to a mesh and back: a character cannot use a mesh, so it goes inactive with one warning, and is a character again
// once the capsule is back.
//
// Other bodies: east of the wall, boxes of 1, 20 and 100 kg to walk into (the 1 kg one keeps pace, the 20 kg one on
// slippery feet speeds up slowly, the 100 kg one does not move), each named with its mass and speed; to the west, a
// seesaw hinged at its middle that the character's weight tips, named with its angle; to the south, a kinematic paddle
// sweeping a lane that shoves the character at its own speed, which 3 pauses with the platforms. B throws a ball at the
// character, which bounces off without moving it, and H drops a 1 kg box on its head, which it does not carry. The
// autopilot resets the boxes each time round.

#ifdef FENRIR_PHYSICS

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Physics.hpp"
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
                            "turn the camera, C frees the camera and snaps it back, X toggles crouch, G scales the "
                            "character by 1.5 and back. A wall to the south-east, a low ceiling to the south-west, "
                            "a crouch tunnel to the north, a ledge only a crouch-jump clears to the north-west, a lift "
                            "and a shuttle to the north-east. Boxes of 1, 20 and 100 kg east of the wall, a seesaw to "
                            "the west, a paddle's lane to the south. B throws a ball at the character, H drops a box "
                            "on its head. T teleports to the start, 2 toggles interpolation, 3 pauses the platforms "
                            "and the paddle, 4 toggles the character on and off, M swaps its collider to a mesh "
                            "(inactive) and back. L logs the state, P walks the route by itself. F5 goes on.",
                        .smokeKeys = {GLFW_KEY_P}};
        // The autopilot walks every piece while L logs the state, then stops, each key is tapped twice, and T ends it.
        scene.smokeKeys.insert(scene.smokeKeys.end(), 75, GLFW_KEY_L);
        scene.smokeKeys.push_back(GLFW_KEY_P);
        for (const int key :
             {GLFW_KEY_C, GLFW_KEY_SPACE, GLFW_KEY_Q, GLFW_KEY_E, GLFW_KEY_LEFT_SHIFT, GLFW_KEY_X, GLFW_KEY_G,
              GLFW_KEY_T, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4, GLFW_KEY_M, GLFW_KEY_B, GLFW_KEY_H})
        {
            scene.smokeKeys.insert(scene.smokeKeys.end(), {key, key});
        }
        scene.smokeKeys.push_back(GLFW_KEY_L);
        scene.smokeKeys.push_back(GLFW_KEY_T);
        return scene;
    }

    CharacterScene(Fenrir::Physics& physics, const Model& cube, const Shader& flatShader)
        : m_physics(&physics), m_cube(&cube), m_flatShader(&flatShader)
    {
    }

    // PostInit: the cube and the flat shader are loaded in Init.
    void Init(Fenrir::App& app)
    {
        m_entities = &app.CreateScene(name).GetEntityList();
        BuildGround();
        BuildWall();
        BuildLowCeiling();
        BuildCrouchTunnel();
        BuildCrouchJumpLedge();
        BuildPlatforms();
        BuildPushBoxes();
        BuildSeesaw();
        BuildPaddleLane();
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
        ShowLook();
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
        MovePlatforms(dt);
        MovePaddle(dt);
        Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        if (!body.enabled || m_mesh)
        {
            return; // switched off or inactive: physics ignores it, so game code leaves it be too
        }
        NoteGroundChange(app, body.state.ground);
        // A crouch-jump crouches as soon as the jump has left the ground: the feet tuck up 0.6 m.
        if (m_crouchJump && body.state.ground == Fenrir::GroundState::InAir)
        {
            SetCrouched(app, true);
            m_crouchJump = false;
        }

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
        else if (key == GLFW_KEY_X)
        {
            SetCrouched(app, !m_crouched);
        }
        else if (key == GLFW_KEY_G)
        {
            m_grown = !m_grown;
            m_character.GetComponent<Fenrir::Transform>().scale = Fenrir::Math::Vec3(m_grown ? grownScale : 1.0f);
            app.Logger()->Info(m_grown ? "Grow by 1.5 (once there is room)" : "Back to size");
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
        else if (key == GLFW_KEY_3)
        {
            m_platformsPaused = !m_platformsPaused;
            app.Logger()->Info(m_platformsPaused ? "3: platforms and paddle paused" : "3: platforms and paddle moving");
        }
        else if (key == GLFW_KEY_4)
        {
            bool& enabled = m_character.GetComponent<Fenrir::CharacterBody>().enabled;
            enabled = !enabled;
            app.Logger()->Info(enabled ? "4: character enabled" : "4: character disabled: nothing sees it");
        }
        else if (key == GLFW_KEY_M)
        {
            SwapToMesh(app, !m_mesh);
        }
        else if (key == GLFW_KEY_B)
        {
            ThrowBall(app);
        }
        else if (key == GLFW_KEY_H)
        {
            DropBoxOnHead(app);
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

    enum class Crouch : std::uint8_t
    {
        Keep,
        Down,
        Up
    };

    struct Waypoint
    {
        Fenrir::Math::Vec3 at;
        bool jump = false;            // Jump on arriving.
        bool boardLift = false;       // Wait on arriving until the lift is at the bottom.
        bool boardShuttle = false;    // Wait on arriving until the shuttle is at its south end.
        bool ride = false;            // Once there, stand for the wait wherever the ground carries it.
        bool crouchJump = false;      // Jump on arriving, and crouch once in the air.
        Crouch crouch = Crouch::Keep; // Crouch or stand on arriving.
        float wait = 0.0f;            // Seconds to stand once there.
        float giveUp = 10.0f;         // Seconds to try for it: a waypoint behind a wall is walked into until then.
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

    // North: a tunnel 8 m long, 1.6 m wide and 1.3 m high, between a crouched head (1.0 m) and a standing one (1.6 m).
    // Halfway along, its roof has a 1 m gap with a loose box lying over it, which still blocks standing up. A trigger
    // fills the space just past its far end, clear of the ground and the tunnel, where a stand-up waiting since inside
    // fits: triggers never block.
    void BuildCrouchTunnel()
    {
        const Fenrir::Math::Vec3 colour(0.35f, 0.5f, 0.65f);
        for (const float x : {-1.0f, 1.0f})
        {
            Box("Tunnel wall", Fenrir::Math::Vec3(x, 0.75f, -7.0f), Fenrir::Math::Vec3(0.4f, 1.5f, 8.0f), colour)
                .AddComponent<Fenrir::Collider>();
        }
        // The roof either side of the gap from z = -6.5 to -7.5: its underside 1.3 m up, its top 1.5 m.
        for (const float z : {-4.75f, -9.25f})
        {
            Box("Tunnel roof", Fenrir::Math::Vec3(0.0f, 1.4f, z), Fenrir::Math::Vec3(2.4f, 0.2f, 3.5f), colour)
                .AddComponent<Fenrir::Collider>();
        }
        Fenrir::Entity box = Box("Roof-gap box", Fenrir::Math::Vec3(0.0f, 1.65f, -7.0f),
                                 Fenrir::Math::Vec3(1.6f, 0.3f, 1.6f), Fenrir::Math::Vec3(0.8f, 0.55f, 0.3f));
        box.AddComponent<Fenrir::RigidBody>();
        box.AddComponent<Fenrir::Collider>();
        Fenrir::Entity trigger = Box("Tunnel trigger", Fenrir::Math::Vec3(0.0f, 1.35f, -12.1f),
                                     Fenrir::Math::Vec3(2.0f, 2.5f, 2.0f), Fenrir::Math::Vec3(0.3f, 0.7f, 0.35f));
        trigger.AddComponent<Fenrir::Collider>();
        trigger.AddComponent<Fenrir::Trigger>();
    }

    // Crouches into the tunnel and stands inside it, so shapeBlocked shows until the exit, then walks back around it.
    static std::vector<Waypoint> CrouchTunnelLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, -1.5f), .crouch = Crouch::Down},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, -5.0f), .crouch = Crouch::Up},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, -12.5f), .wait = 0.5f},
                Waypoint{.at = Fenrir::Math::Vec3(3.0f, 0.0f, -12.5f)},
                Waypoint{.at = Fenrir::Math::Vec3(3.0f, 0.0f, -1.5f)},
                Waypoint{.at = start, .wait = 0.5f}};
    }

    // North-west: a ledge 1.6 m high, its east face at x = -6.5. A standing jump lifts the feet 1.3 m; crouching in
    // the air tucks them up 0.6 m more.
    void BuildCrouchJumpLedge()
    {
        Box("Crouch-jump ledge", Fenrir::Math::Vec3(-8.0f, 0.8f, -6.0f), Fenrir::Math::Vec3(3.0f, 1.6f, 3.0f),
            Fenrir::Math::Vec3(0.6f, 0.45f, 0.55f))
            .AddComponent<Fenrir::Collider>();
    }

    // Crouch-jumps onto the ledge, stands up on it, and walks off its far side back to the start.
    static std::vector<Waypoint> CrouchJumpLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(-5.0f, 0.0f, -6.0f), .crouchJump = true},
                Waypoint{.at = Fenrir::Math::Vec3(-8.0f, 0.0f, -6.0f), .crouch = Crouch::Up, .wait = 1.0f},
                Waypoint{.at = Fenrir::Math::Vec3(-8.0f, 0.0f, -9.0f)}, Waypoint{.at = start, .wait = 0.5f}};
    }

    // East of the wall, clear of the platforms: boxes of 1, 20 and 100 kg, a metre on a side, each in its own row,
    // pushed east. The 20 kg one has slippery feet
    // (friction 0.2 against the ground's 0.5), so the push's 100 N beats its friction and it speeds up slowly; the
    // 100 kg one's friction is far more than 100 N, so it does not move.
    void BuildPushBoxes()
    {
        const std::array<float, 3> masses = {1.0f, 20.0f, 100.0f};
        for (std::size_t i = 0; i < m_pushBoxes.size(); ++i)
        {
            Fenrir::Entity box = Box("Push box", PushBoxHome(i), Fenrir::Math::Vec3(1.0f),
                                     Fenrir::Math::Vec3(0.8f, 0.6f - 0.2f * static_cast<float>(i), 0.35f));
            box.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = masses[i]});
            box.AddComponent<Fenrir::Collider>();
            if (masses[i] == 20.0f)
            {
                box.AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.friction = 0.2f});
            }
            m_pushBoxes[i] = box;
            m_pushMasses[i] = masses[i];
        }
    }

    static Fenrir::Math::Vec3 PushBoxHome(std::size_t i)
    {
        return Fenrir::Math::Vec3(10.0f, 0.5f, 3.5f + 2.0f * static_cast<float>(i));
    }

    // Puts the boxes back where they started, at rest.
    void ResetPushBoxes()
    {
        for (std::size_t i = 0; i < m_pushBoxes.size(); ++i)
        {
            m_pushBoxes[i].GetComponent<Fenrir::Transform>().pos = PushBoxHome(i);
            m_pushBoxes[i].GetComponent<Fenrir::Transform>().rot = identity;
            Fenrir::RigidBody& body = m_pushBoxes[i].GetComponent<Fenrir::RigidBody>();
            body.linearVelocity = Fenrir::Math::Vec3(0.0f);
            body.angularVelocity = Fenrir::Math::Vec3(0.0f);
        }
    }

    // Goes round the wall's north end and walks into each box in turn for a few seconds, then back.
    static std::vector<Waypoint> PushBoxesLeg()
    {
        std::vector<Waypoint> leg = {Waypoint{.at = Fenrir::Math::Vec3(5.0f, 0.0f, 1.0f)},
                                     Waypoint{.at = Fenrir::Math::Vec3(8.5f, 0.0f, 1.0f)}};
        for (std::size_t i = 0; i < 3; ++i)
        {
            const float z = PushBoxHome(i).z;
            leg.push_back(Waypoint{.at = Fenrir::Math::Vec3(8.5f, 0.0f, z)});
            leg.push_back(Waypoint{.at = Fenrir::Math::Vec3(16.0f, 0.0f, z), .giveUp = 3.0f});
            leg.push_back(Waypoint{.at = Fenrir::Math::Vec3(8.5f, 0.0f, z)});
        }
        leg.push_back(Waypoint{.at = Fenrir::Math::Vec3(8.5f, 0.0f, 1.0f)});
        leg.push_back(Waypoint{.at = Fenrir::Math::Vec3(5.0f, 0.0f, 1.0f)});
        leg.push_back(Waypoint{.at = start, .wait = 0.5f});
        return leg;
    }

    // West: a 4 m plank, 20 kg, hinged at its middle 0.6 m up to the world about Z, lying with its east end on the
    // ground. Walked up from the east, it tips west once the character is past the hinge.
    void BuildSeesaw()
    {
        const Fenrir::Math::Vec3 hinge(-6.0f, 0.6f, 0.0f);
        // Its east end down: 0.55 m over 2 m.
        const float tilt = std::asin(0.55f / 2.0f);
        m_seesaw = Box("Seesaw", hinge, Fenrir::Math::Vec3(4.0f, 0.1f, 1.2f), Fenrir::Math::Vec3(0.7f, 0.5f, 0.3f));
        m_seesaw.GetComponent<Fenrir::Transform>().rot =
            Fenrir::Math::AngleAxis(-tilt, Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
        m_seesaw.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 20.0f});
        m_seesaw.AddComponent<Fenrir::Collider>();
        // A hinge turns about its frame's X axis: here the world's Z.
        const Fenrir::Math::Quat alongZ =
            Fenrir::Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
        const Fenrir::JointFrames frames =
            Fenrir::FrameFromWorld(m_seesaw, Fenrir::Entity(), Fenrir::JointFrame{hinge, alongZ});
        m_seesaw.AddComponent<Fenrir::Joint>(Fenrir::Joint{.bodyA = m_seesaw,
                                                           .bodyB = Fenrir::Entity(),
                                                           .frameA = frames.frameA,
                                                           .frameB = frames.frameB,
                                                           .kind = Fenrir::HingeJoint{}});
        // Its stand, a look only, under the hinge.
        Box("Seesaw stand", Fenrir::Math::Vec3(hinge.x, 0.25f, 0.0f), Fenrir::Math::Vec3(0.2f, 0.5f, 1.4f),
            Fenrir::Math::Vec3(0.4f));
    }

    // Walks up the low end past the hinge, tipping it, and back down the other way.
    static std::vector<Waypoint> SeesawLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(-2.5f, 0.0f, 0.0f)},
                Waypoint{.at = Fenrir::Math::Vec3(-7.5f, 0.0f, 0.0f), .wait = 1.5f, .giveUp = 4.0f},
                Waypoint{.at = Fenrir::Math::Vec3(-2.5f, 0.0f, 0.0f), .wait = 1.0f, .giveUp = 4.0f},
                Waypoint{.at = start, .wait = 0.5f}};
    }

    // South: a lane past the wall and the low ceiling, swept east and west by a kinematic paddle.
    void BuildPaddleLane()
    {
        Box("Paddle lane", Fenrir::Math::Vec3(0.0f, 0.005f, paddleLaneZ), Fenrir::Math::Vec3(10.0f, 0.01f, 2.4f),
            Fenrir::Math::Vec3(0.6f, 0.6f, 0.65f));
        m_paddle = Box("Paddle", Fenrir::Math::Vec3(-paddleReach, 0.6f, paddleLaneZ),
                       Fenrir::Math::Vec3(0.3f, 1.2f, 2.0f), Fenrir::Math::Vec3(0.3f, 0.45f, 0.75f));
        m_paddle.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.motionType = Fenrir::MotionType::Kinematic});
        m_paddle.AddComponent<Fenrir::Collider>();
    }

    // Game code moves a kinematic body by writing its Transform in Tick: back and forth across the lane.
    void MovePaddle(float dt)
    {
        if (m_platformsPaused)
        {
            return;
        }
        Fenrir::Math::Vec3& at = m_paddle.GetComponent<Fenrir::Transform>().pos;
        at.x += m_paddleHeading * paddleSpeed * dt;
        if (std::abs(at.x) >= paddleReach)
        {
            at.x = std::clamp(at.x, -paddleReach, paddleReach);
            m_paddleHeading = -m_paddleHeading;
        }
    }

    // Stands in the lane while the paddle shoves it (walking to it for 4 s at most, so it does not walk back against
    // the paddle once shoved), then walks back.
    static std::vector<Waypoint> PaddleLaneLeg()
    {
        return {Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, paddleLaneZ), .wait = 6.0f, .giveUp = 4.0f},
                Waypoint{.at = Fenrir::Math::Vec3(0.0f, 0.0f, 1.5f)}, Waypoint{.at = start, .wait = 0.5f}};
    }

    // B: a ball thrown at the character from 4 m ahead of it, the way the camera looks, at chest height.
    void ThrowBall(Fenrir::App& app)
    {
        if (!m_ball.IsValid())
        {
            m_ball =
                Box("Ball", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(0.3f), Fenrir::Math::Vec3(0.9f, 0.2f, 0.2f));
            m_ball.AddComponent<Fenrir::RigidBody>();
            m_ball.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::SphereShape{.radius = 0.5f}});
            m_ball.AddComponent<Fenrir::PhysicsMaterial>(Fenrir::PhysicsMaterial{.restitution = 0.6f});
        }
        const float yaw = m_followCamera.Yaw() * std::numbers::pi_v<float> / 180.0f;
        const Fenrir::Math::Vec3 ahead(std::cos(yaw), 0.0f, std::sin(yaw));
        m_ball.GetComponent<Fenrir::Transform>().pos =
            Fenrir::GetWorldTransform(m_character).pos + ahead * 4.0f + Fenrir::Math::Vec3(0.0f, 0.3f, 0.0f);
        // 10 m/s at it, and a little up against the drop on the way.
        m_ball.GetComponent<Fenrir::RigidBody>().linearVelocity = -ahead * 10.0f + Fenrir::Math::Vec3(0.0f, 2.0f, 0.0f);
        app.Logger()->Info("B: ball thrown");
    }

    // H: a 1 kg box dropped onto the character's head.
    void DropBoxOnHead(Fenrir::App& app)
    {
        if (!m_headBox.IsValid())
        {
            m_headBox = Box("Head box", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(0.4f),
                            Fenrir::Math::Vec3(0.3f, 0.75f, 0.4f));
            m_headBox.AddComponent<Fenrir::RigidBody>();
            m_headBox.AddComponent<Fenrir::Collider>();
        }
        m_headBox.GetComponent<Fenrir::Transform>().pos =
            Fenrir::GetWorldTransform(m_character).pos + Fenrir::Math::Vec3(0.0f, 1.6f, 0.0f);
        m_headBox.GetComponent<Fenrir::Transform>().rot = identity;
        m_headBox.GetComponent<Fenrir::RigidBody>().linearVelocity = Fenrir::Math::Vec3(0.0f);
        app.Logger()->Info("H: box dropped on the head");
    }

    // North-east: a lift 2 m square at 1.5 m/s, its top flush with the ground at the bottom, and a shuttle 2 m square
    // and 0.3 m high (a step up) going to and fro along z at 2 m/s, from z = -1 to -9. Each waits at its south end, or
    // the bottom, for boarding. The lift goes up 3 m, down to 1.5 m, at once up again (the turn where the character is
    // off it for a few ticks), and back down.
    void BuildPlatforms()
    {
        m_lift =
            Box("Lift", LiftCentre(0.0f), Fenrir::Math::Vec3(2.0f, 0.2f, 2.0f), Fenrir::Math::Vec3(0.3f, 0.6f, 0.4f));
        m_lift.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.motionType = Fenrir::MotionType::Kinematic});
        m_lift.AddComponent<Fenrir::Collider>();
        m_shuttle = Box("Shuttle", ShuttleCentre(0.0f), Fenrir::Math::Vec3(2.0f, 0.3f, 2.0f),
                        Fenrir::Math::Vec3(0.65f, 0.4f, 0.6f));
        m_shuttle.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.motionType = Fenrir::MotionType::Kinematic});
        m_shuttle.AddComponent<Fenrir::Collider>();
    }

    // A platform's path: how far along it is at each second of its cycle, straight between them.
    struct Stop
    {
        float second;
        float along;
    };

    template <std::size_t N>
    static float Along(const std::array<Stop, N>& path, float seconds)
    {
        const float into = std::fmod(seconds, path.back().second);
        std::size_t i = 1;
        while (path[i].second < into)
        {
            ++i;
        }
        const Stop& from = path[i - 1];
        const Stop& to = path[i];
        return from.along + (to.along - from.along) * (into - from.second) / (to.second - from.second);
    }

    // The lift's height (its top's) at 1.5 m/s: waits, up 3 m, down 1.5 m, up again, down to the bottom.
    static constexpr std::array<Stop, 6> liftPath = {Stop{0.0f, 0.0f}, Stop{1.5f, 0.0f}, Stop{3.5f, 3.0f},
                                                     Stop{4.5f, 1.5f}, Stop{5.5f, 3.0f}, Stop{7.5f, 0.0f}};
    // The shuttle's way north at 2 m/s: waits at the south end, north 8 m, waits, and back.
    static constexpr std::array<Stop, 5> shuttlePath = {Stop{0.0f, 0.0f}, Stop{1.5f, 0.0f}, Stop{5.5f, 8.0f},
                                                        Stop{6.5f, 8.0f}, Stop{10.5f, 0.0f}};

    static Fenrir::Math::Vec3 LiftCentre(float seconds)
    {
        return Fenrir::Math::Vec3(8.0f, Along(liftPath, seconds) - 0.1f, -3.0f);
    }

    static Fenrir::Math::Vec3 ShuttleCentre(float seconds)
    {
        return Fenrir::Math::Vec3(11.0f, 0.15f, -1.0f - Along(shuttlePath, seconds));
    }

    // Game code moving the platforms: their Transforms written each tick, so physics moves them kinematically.
    void MovePlatforms(float dt)
    {
        if (m_platformsPaused)
        {
            return;
        }
        m_platformSeconds += dt;
        m_lift.GetComponent<Fenrir::Transform>().pos = LiftCentre(m_platformSeconds);
        m_shuttle.GetComponent<Fenrir::Transform>().pos = ShuttleCentre(m_platformSeconds);
    }

    // Whether the autopilot, arrived at `waypoint`, may walk on: a platform it waits for has just begun its wait, so
    // there is time to walk on.
    [[nodiscard]] bool Boardable(const Waypoint& waypoint) const
    {
        if (waypoint.boardLift)
        {
            return std::fmod(m_platformSeconds, liftPath.back().second) < 0.5f;
        }
        if (waypoint.boardShuttle)
        {
            return std::fmod(m_platformSeconds, shuttlePath.back().second) < 0.5f;
        }
        return true;
    }

    // Boards the lift, rides it up, down and up again, and jumps off its north edge while it rises, carrying its
    // speed; then boards the shuttle, rides it north, and walks off its west side (stuck to the floor below by
    // floorSnap, so still on the ground, with nothing carried).
    static std::vector<Waypoint> PlatformsLeg()
    {
        return {
            Waypoint{.at = Fenrir::Math::Vec3(8.0f, 0.0f, -0.5f), .boardLift = true, .giveUp = 15.0f},
            Waypoint{.at = Fenrir::Math::Vec3(8.0f, 0.0f, -3.0f), .ride = true, .wait = 3.8f},
            Waypoint{.at = Fenrir::Math::Vec3(8.0f, 0.0f, -3.7f), .jump = true},
            Waypoint{.at = Fenrir::Math::Vec3(8.0f, 0.0f, -6.5f), .wait = 0.5f},
            // Between the lift and the shuttle's lane, round to the shuttle's south end.
            Waypoint{.at = Fenrir::Math::Vec3(9.5f, 0.0f, -6.5f)}, Waypoint{.at = Fenrir::Math::Vec3(9.5f, 0.0f, 1.0f)},
            Waypoint{.at = Fenrir::Math::Vec3(11.0f, 0.0f, 0.8f), .boardShuttle = true, .giveUp = 15.0f},
            Waypoint{.at = Fenrir::Math::Vec3(11.0f, 0.0f, -1.0f), .ride = true, .wait = 2.5f},
            Waypoint{.at = Fenrir::Math::Vec3(8.5f, 0.0f, -5.5f), .wait = 0.5f}, Waypoint{.at = start, .wait = 0.5f}};
    }

    // The route: each piece's leg in turn, over and over, each time ending with a teleport to the start.
    static const std::vector<Waypoint>& Route()
    {
        static const std::vector<Waypoint> route = [] {
            std::vector<Waypoint> legs;
            for (const std::vector<Waypoint>& leg : {WallLeg(), LowCeilingLeg(), CrouchTunnelLeg(), CrouchJumpLeg(),
                                                     PlatformsLeg(), PushBoxesLeg(), SeesawLeg(), PaddleLaneLeg()})
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
        const bool there = Fenrir::Math::Magnitude(to) < 0.3f || (next.ride && m_waited > 0.0f);
        if (there && !Boardable(next) && m_onWaypoint < next.giveUp)
        {
            return Fenrir::Math::Vec3(0.0f);
        }
        if (!there && m_onWaypoint < next.giveUp)
        {
            m_waited = 0.0f;
            return Fenrir::Math::Normalized(to) * walkSpeed;
        }
        if (m_waited == 0.0f)
        {
            m_jump = m_jump || next.jump || next.crouchJump;
            m_crouchJump = m_crouchJump || next.crouchJump;
            if (next.crouch != Crouch::Keep)
            {
                SetCrouched(app, next.crouch == Crouch::Down);
            }
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
                ResetPushBoxes();
            }
        }
        return Fenrir::Math::Vec3(0.0f);
    }

    // Game code's crouch: the Collider written once. Physics stands the character up once there is room.
    void SetCrouched(Fenrir::App& app, bool crouched)
    {
        if (crouched == m_crouched)
        {
            return;
        }
        m_crouched = crouched;
        if (!m_mesh)
        {
            m_character.GetComponent<Fenrir::Collider>().shape = crouched ? crouchedShape : standingShape;
        }
        app.Logger()->Info(crouched ? "Crouch" : "Stand up (once there is room)");
    }

    // A mesh Collider makes the character inactive, with one warning from physics; the capsule makes it one again.
    void SwapToMesh(Fenrir::App& app, bool mesh)
    {
        m_mesh = mesh;
        Fenrir::CollisionShape& shape = m_character.GetComponent<Fenrir::Collider>().shape;
        if (mesh)
        {
            shape = Fenrir::MeshShape{m_meshCollider};
        }
        else
        {
            shape = m_crouched ? crouchedShape : standingShape;
        }
        app.Logger()->Info(mesh ? "M: mesh collider, the character is inactive" : "M: capsule collider again");
    }

    // The look follows the shape in use: the one asked for, or the other while the change waits for room.
    void ShowLook() const
    {
        const bool blocked = m_character.GetComponent<Fenrir::CharacterBody>().state.shapeBlocked;
        const bool crouched = blocked ? !m_crouched : m_crouched;
        const Fenrir::CapsuleShape& shape = crouched ? crouchedShape : standingShape;
        m_look.GetComponent<Fenrir::Transform>().scale =
            Fenrir::Math::Vec3(0.6f, 2.0f * (shape.halfHeight + shape.radius), 0.6f);
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
        if (state.carriedVelocity != Fenrir::Math::Vec3(0.0f))
        {
            reading += ", carried " + Describe(state.carriedVelocity);
        }
        reading += state.onWall ? ", on wall" : "";
        reading += state.onCeiling ? ", on ceiling" : "";
        reading += state.shapeBlocked ? ", shape blocked" : "";
        reading += body.enabled ? "" : ", disabled";
        reading += m_mesh ? ", inactive (mesh)" : "";
        reading +=
            ", " + std::to_string(m_landings) + " landings, " + std::to_string(m_changes.size()) + " ground changes/s";
        m_character.GetComponent<Fenrir::Name>().Set(reading.c_str());

        std::array<char, 64> text{};
        for (std::size_t i = 0; i < m_pushBoxes.size(); ++i)
        {
            const Fenrir::Math::Vec3 v = m_pushBoxes[i].GetComponent<Fenrir::RigidBody>().linearVelocity;
            std::snprintf(text.data(), text.size(), "Push box %.0f kg: %.2f m/s", static_cast<double>(m_pushMasses[i]),
                          static_cast<double>(Fenrir::Math::Magnitude(Fenrir::Math::Vec3(v.x, 0.0f, v.z))));
            m_pushBoxes[i].GetComponent<Fenrir::Name>().Set(text.data());
        }
        // The plank's turn about the hinge, positive with its west end down.
        const Fenrir::Math::Vec3 along = Fenrir::GetWorldTransform(m_seesaw).rot * Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f);
        std::snprintf(text.data(), text.size(), "Seesaw: %.1f deg",
                      static_cast<double>(std::asin(along.y) * 180.0f / std::numbers::pi_v<float>));
        m_seesaw.GetComponent<Fenrir::Name>().Set(text.data());
    }

    void LogState(Fenrir::App& app) const
    {
        const Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
        const Fenrir::CharacterState& state = body.state;
        app.Logger()->Info("Tick {}: at {}, {} on {} ({}, at {}, normal {}, moving at {}, turning at {}), velocity {}, "
                           "moved at {}, carried {}, onWall {}, onCeiling {}, shapeBlocked {}",
                           m_tick, Describe(Fenrir::GetWorldTransform(m_character).pos), NameOf(state.ground),
                           NameOf(state.groundBody), NameOf(state.groundCollider), Describe(state.groundPoint),
                           Describe(state.groundNormal), Describe(state.groundVelocity),
                           Describe(state.groundAngularVelocity), Describe(body.velocity),
                           Describe(state.movedVelocity), Describe(state.carriedVelocity), state.onWall,
                           state.onCeiling, state.shapeBlocked);
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
        m_character.AddComponent<Fenrir::Collider>(Fenrir::Collider{standingShape});
        m_character.AddComponent<Fenrir::CharacterBody>();

        // For M: a box the character's size as triangles, which a character cannot use.
        std::array<Fenrir::Math::Vec3, 8> corners{};
        for (std::size_t i = 0; i < corners.size(); ++i)
        {
            corners[i] = Fenrir::Math::Vec3((i & 1u) ? 0.3f : -0.3f, (i & 2u) ? footToCentre : -footToCentre,
                                            (i & 4u) ? 0.3f : -0.3f);
        }
        static constexpr std::array<std::uint32_t, 36> triangles = {
            0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
        m_meshCollider = m_physics->CreateCollisionMesh(corners, triangles);

        // Its look: a child with no collider, so not part of it (ADR-0014: no compound characters).
        m_look = Box("Character look", Fenrir::Math::Vec3(0.0f), Fenrir::Math::Vec3(0.6f, 1.6f, 0.6f),
                     Fenrir::Math::Vec3(0.95f, 0.85f, 0.3f));
        m_character.AddChild(m_look);
    }

    static constexpr float footToCentre = 0.8f;
    static constexpr float walkSpeed = 4.0f;
    static constexpr float runSpeed = 8.0f;
    static constexpr float acceleration = 30.0f; // m/s², the most the walk changes the velocity by.
    static constexpr float jumpSpeed = 5.0f;
    static constexpr float grownScale = 1.5f;
    static constexpr float liftSpeed = 1.5f;
    static constexpr float shuttleSpeed = 2.0f;
    static constexpr Fenrir::CapsuleShape standingShape{.halfHeight = 0.5f, .radius = 0.3f};
    static constexpr Fenrir::CapsuleShape crouchedShape{.halfHeight = 0.2f, .radius = 0.3f};
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    inline static const Fenrir::Math::Vec3 start = Fenrir::Math::Vec3(0.0f, 0.0f, 0.0f);
    static constexpr float paddleLaneZ = 11.0f;
    static constexpr float paddleReach = 4.5f; // The paddle sweeps from x = -4.5 to 4.5.
    static constexpr float paddleSpeed = 1.5f;

    Fenrir::Physics* m_physics;
    const Model* m_cube;
    const Shader* m_flatShader;
    Fenrir::CollisionMesh m_meshCollider;
    bool m_mesh = false;
    Fenrir::EntityList* m_entities = nullptr;
    Fenrir::Entity m_character;
    Fenrir::Entity m_look;
    Fenrir::Entity m_camera;
    Fenrir::Entity m_lift;
    Fenrir::Entity m_shuttle;
    float m_platformSeconds = 0.0f;
    bool m_platformsPaused = false;
    std::array<Fenrir::Entity, 3> m_pushBoxes;
    std::array<float, 3> m_pushMasses{};
    Fenrir::Entity m_seesaw;
    Fenrir::Entity m_paddle;
    float m_paddleHeading = 1.0f;
    Fenrir::Entity m_ball;
    Fenrir::Entity m_headBox;
    CharacterCamera m_followCamera;
    std::set<int> m_held;
    bool m_jump = false;
    bool m_crouchJump = false;
    bool m_crouched = false;
    bool m_grown = false;
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
