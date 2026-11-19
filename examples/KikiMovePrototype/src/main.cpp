// PROTOTYPE, throwaway: how does Kiki move? (.scratch/flightless-frenzy/issues/05-how-kiki-moves.md)
//
// A grey-box course on a FenrirPhysics CharacterBody (ADR-0014) for judging the feel of running, the jump, the extra
// jump, the glide, the speed boost and the camera, with every number on the demo panel while it runs. Not game code:
// the game is C# in games/FlightlessFrenzy and leans on no demo code; this is fastest-to-write C++ on the demo
// scaffold. See this folder's README.md for the controls, the defaults and the course's measurements.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirDemoScaffold/DemoScaffold.hpp"
#include "FenrirDemoScaffold/DemoUI.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirLogger/ConsoleLogger.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirWindow/Window.hpp"

namespace
{
    using Fenrir::Math::Vec3;

    constexpr float Pi = std::numbers::pi_v<float>;
    constexpr float Deg = Pi / 180.0f;

    // Kiki's capsule: 1.0 m tall (feet to crown), 0.6 m across. Its Transform is the capsule's centre.
    constexpr float CapsuleHalfHeight = 0.2f;
    constexpr float CapsuleRadius = 0.3f;
    constexpr float FootToCentre = CapsuleHalfHeight + CapsuleRadius; // 0.5 m
    constexpr float KikiHeight = 2.0f * FootToCentre;                 // 1.0 m

    /** @brief Every number the feel is made of: the demo panel's switch rows bind to these, P copies them out. */
    struct Tuning
    {
        // Running.
        float runSpeed = 7.0f;      // m/s, top speed on the ground
        float groundAccel = 60.0f;  // m/s^2, speeding up towards the stick's speed
        float groundDecel = 80.0f;  // m/s^2, slowing with no input, and turning against the run
        float turnSpeed = 900.0f;   // deg/s, how fast the capsule's facing (its beak) follows its velocity
        bool sprintAllowed = false; // Shift / right shoulder runs faster when on (is there a sprint, or one speed?)
        float sprintMultiplier = 1.4f;

        // The jump.
        float jumpHeight = 2.0f;            // m, the held jump's rise (feet) at the default gravity
        float timeToApex = 0.38f;           // s, a held jump's time to the top
        float fallGravityMultiplier = 1.8f; // falling gravity = rising gravity x this
        float jumpCutMultiplier = 3.0f;     // rising gravity x this once jump is let go: the tapped jump is lower
        float maxFallSpeed = 30.0f;         // m/s
        float airAccel = 30.0f;             // m/s^2, air control with input
        float airDecel = 8.0f;              // m/s^2, air drag with no input
        float coyoteTime = 0.12f;           // s, a jump still works this long after walking off a ledge
        float jumpBuffer = 0.12f;           // s, a press this long before landing jumps on landing

        // Power-ups (timed pickups in the game; switches here).
        bool extraJumpOn = false;
        int extraJumps = 1;
        float extraJumpHeight = 1.5f; // m, the extra jump's rise from where it starts
        bool glideOn = false;
        bool glideOnHold = false;           // also glide whenever jump is held while falling, not only on an air press
        float glideFallSpeed = 2.5f;        // m/s, the steady sink while gliding
        float glideBrake = 40.0f;           // m/s^2, how fast a fall slows to the glide's sink
        float glideSpeedMultiplier = 1.15f; // horizontal top speed while gliding = run speed x this
        bool speedBoostOn = false;
        float speedBoostMultiplier = 1.5f;

        // The camera.
        int cameraMode = 0;                // 0 third person, 1 first person
        float camDistance = 6.0f;          // m, from the pivot
        float camPivotHeight = 1.0f;       // m, the pivot above the capsule's centre
        float camPitchMin = -70.0f;        // deg, looking down (negative) ...
        float camPitchMax = 30.0f;         // deg, ... and up from below
        float camStartPitch = -20.0f;      // deg, after R
        bool camCollision = true;          // pull in against walls, floors and ceilings
        float camRadius = 0.25f;           // m, the sphere cast for the pull-in
        float camMinDistance = 0.6f;       // m
        float camZoomOutSpeed = 6.0f;      // m/s, easing back out after a pull-in (pulling in is instant)
        float camFollowHorizontal = 20.0f; // 1/s, the pivot's follow sharpness across (higher is tighter)
        float camFollowVertical = 8.0f;    // 1/s, and up and down, so jumps do not shake the view
        float fovThird = 60.0f;            // deg, vertical
        float fovFirst = 75.0f;            // deg, vertical
        float eyeHeight = 0.3f;            // m, first person's eye above the capsule's centre
        float mouseSensitivity = 0.12f;    // deg per mouse count
        float padLookSpeed = 200.0f;       // deg/s at full right stick
        float keyTurnSpeed = 120.0f;       // deg/s for Q/E
        bool invertY = false;

        // Derived: rising gravity and launch speed from the height and the time to the top.
        [[nodiscard]] float RiseGravity() const
        {
            return 2.0f * jumpHeight / (timeToApex * timeToApex);
        }
        [[nodiscard]] float JumpSpeed() const
        {
            return 2.0f * jumpHeight / timeToApex;
        }
        [[nodiscard]] float TappedHeight() const
        {
            const float v = JumpSpeed();
            return v * v / (2.0f * RiseGravity() * jumpCutMultiplier);
        }
        [[nodiscard]] float ExtraJumpSpeed() const
        {
            return std::sqrt(2.0f * RiseGravity() * extraJumpHeight);
        }

        /** @brief Every value as `name = value` lines, to paste back into a ticket. */
        [[nodiscard]] std::string Dump() const
        {
            std::string out = "Kiki movement prototype values\n";
            const auto f = [&out](const char* name, float value) { out += std::format("{} = {:.3f}\n", name, value); };
            const auto b = [&out](const char* name, bool value) { out += std::format("{} = {}\n", name, value); };
            out += "[run]\n";
            f("runSpeed", runSpeed);
            f("groundAccel", groundAccel);
            f("groundDecel", groundDecel);
            f("turnSpeed", turnSpeed);
            b("sprintAllowed", sprintAllowed);
            f("sprintMultiplier", sprintMultiplier);
            out += "[jump]\n";
            f("jumpHeight", jumpHeight);
            f("timeToApex", timeToApex);
            f("fallGravityMultiplier", fallGravityMultiplier);
            f("jumpCutMultiplier", jumpCutMultiplier);
            f("maxFallSpeed", maxFallSpeed);
            f("airAccel", airAccel);
            f("airDecel", airDecel);
            f("coyoteTime", coyoteTime);
            f("jumpBuffer", jumpBuffer);
            f("derived.riseGravity", RiseGravity());
            f("derived.fallGravity", RiseGravity() * fallGravityMultiplier);
            f("derived.jumpSpeed", JumpSpeed());
            f("derived.tappedHeight", TappedHeight());
            out += "[power-ups]\n";
            b("extraJumpOn", extraJumpOn);
            out += std::format("extraJumps = {}\n", extraJumps);
            f("extraJumpHeight", extraJumpHeight);
            b("glideOn", glideOn);
            b("glideOnHold", glideOnHold);
            f("glideFallSpeed", glideFallSpeed);
            f("glideBrake", glideBrake);
            f("glideSpeedMultiplier", glideSpeedMultiplier);
            b("speedBoostOn", speedBoostOn);
            f("speedBoostMultiplier", speedBoostMultiplier);
            out += "[camera]\n";
            out += std::format("cameraMode = {}\n", cameraMode == 0 ? "third person" : "first person");
            f("camDistance", camDistance);
            f("camPivotHeight", camPivotHeight);
            f("camPitchMin", camPitchMin);
            f("camPitchMax", camPitchMax);
            f("camStartPitch", camStartPitch);
            b("camCollision", camCollision);
            f("camRadius", camRadius);
            f("camMinDistance", camMinDistance);
            f("camZoomOutSpeed", camZoomOutSpeed);
            f("camFollowHorizontal", camFollowHorizontal);
            f("camFollowVertical", camFollowVertical);
            f("fovThird", fovThird);
            f("fovFirst", fovFirst);
            f("eyeHeight", eyeHeight);
            f("mouseSensitivity", mouseSensitivity);
            f("padLookSpeed", padLookSpeed);
            f("keyTurnSpeed", keyTurnSpeed);
            b("invertY", invertY);
            return out;
        }
    };

    Fenrir::Math::Quat Identity()
    {
        return Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    Vec3 MoveTowards(const Vec3& from, const Vec3& to, float most)
    {
        const Vec3 change = to - from;
        const float length = Fenrir::Math::Magnitude(change);
        return length <= most || length == 0.0f ? to : from + change * (most / length);
    }

    float MoveTowards(float from, float to, float most)
    {
        return std::abs(to - from) <= most ? to : from + (to > from ? most : -most);
    }

    // Looking along -Z at yaw 0 and pitch 0; positive yaw turns left, positive pitch looks up (FenrirDemo::YawPitch).
    Vec3 Forward(float yawDeg, float pitchDeg)
    {
        const float y = yawDeg * Deg;
        const float p = pitchDeg * Deg;
        return Vec3(-std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p));
    }

    float SrgbToLinear(float encoded)
    {
        return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }

    /** @brief A teleport spot: where the feet go, and which way the camera looks. */
    struct Spot
    {
        const char* name;
        Vec3 feet;
        float yaw;
    };

    // The course. Every height is of a top surface above the floor (y = 0); every gap is edge to edge at equal heights.
    constexpr std::array<float, 10> LedgeHeights = {0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f};
    constexpr std::array<float, 10> GapLengths = {2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 12.0f};
    constexpr float GapRunwayHeight = 1.0f;
    constexpr std::array<float, 5> RampAngles = {15.0f, 30.0f, 40.0f, 45.0f, 50.0f};
    constexpr float RampTop = 2.5f;
    constexpr float LowCeiling = 1.4f;    // underside: 0.4 m over Kiki's crown
    constexpr float MidCeiling = 2.2f;    // underside: 1.2 m over Kiki's crown, under a jump's top
    constexpr float OverheadUnder = 3.5f; // an overhead platform's underside
    constexpr float TowerStep = 1.5f;
    constexpr int TowerSteps = 7; // 1.5 m to 10.5 m

    class KikiMove
    {
      public:
        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            m_scene = &scene;
            m_entities = &scene.GetEntityList();
            m_store = &app.GetResource<Fenrir::AssetStore>();
            m_materials.clear();
            m_labels.clear();
            LoadLooks(app, scene);
            BuildCourse();
            MakeKiki();
            m_camera = m_entities->CreateEntity();
            m_camera.GetComponent<Fenrir::Name>().Set("Camera");
            m_camera.AddComponent<Fenrir::Camera>();
            ResetToStart();
            AddPanel(app);
            m_spawned = true;
            app.Logger()->Info(
                "KikiMovePrototype: course spawned. Click the scene to capture the mouse, Esc frees it.");
        }

        // Tick: the character's movement, from the state its last move left.
        void Tick(Fenrir::App& app)
        {
            if (!m_spawned)
            {
                return;
            }
            const float dt = static_cast<float>(app.GetTime().tickRate);
            m_seconds += dt;
            Fenrir::CharacterBody& body = m_kiki.GetComponent<Fenrir::CharacterBody>();
            const Tuning& t = m_tuning;

            // Input: stick or keys, relative to the camera's yaw; jump pressed and held.
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            const auto& pads = app.GetResource<Fenrir::Gamepads>();
            const std::optional<Fenrir::GamepadId> pad = FirstPad(pads);
            float forward = 0.0f;
            float right = 0.0f;
            forward += keyboard.IsDown(Fenrir::Key::W) || keyboard.IsDown(Fenrir::Key::Up) ? 1.0f : 0.0f;
            forward -= keyboard.IsDown(Fenrir::Key::S) || keyboard.IsDown(Fenrir::Key::Down) ? 1.0f : 0.0f;
            right += keyboard.IsDown(Fenrir::Key::D) || keyboard.IsDown(Fenrir::Key::Right) ? 1.0f : 0.0f;
            right -= keyboard.IsDown(Fenrir::Key::A) || keyboard.IsDown(Fenrir::Key::Left) ? 1.0f : 0.0f;
            bool jumpPressed = keyboard.JustPressed(Fenrir::Key::Space);
            bool jumpHeld = keyboard.IsDown(Fenrir::Key::Space) || m_holdJumpForTest;
            bool sprintHeld = keyboard.IsDown(Fenrir::Key::LeftShift);
            if (pad)
            {
                const float sx = pads.Axis(*pad, Fenrir::GamepadAxis::LeftX);
                const float sy = pads.Axis(*pad, Fenrir::GamepadAxis::LeftY);
                if (std::sqrt(sx * sx + sy * sy) > StickDeadzone)
                {
                    right += sx;
                    forward -= sy; // SDL's stick y is down-positive
                }
                jumpPressed = jumpPressed || pads.JustPressed(*pad, Fenrir::GamepadButton::South);
                jumpHeld = jumpHeld || pads.IsDown(*pad, Fenrir::GamepadButton::South);
                sprintHeld = sprintHeld || pads.IsDown(*pad, Fenrir::GamepadButton::RightShoulder);
            }
            Vec3 wish = Forward(m_yaw, 0.0f) * forward + Forward(m_yaw - 90.0f, 0.0f) * right;
            const float wishLength = Fenrir::Math::Magnitude(wish);
            if (wishLength > 1.0f)
            {
                wish =
                    wish * (1.0f / wishLength); // the stick's tilt is a speed below 1, the keys' diagonal is not more
            }

            const bool grounded = body.state.ground == Fenrir::GroundState::OnGround;
            const Vec3 feet = m_kiki.GetComponent<Fenrir::Transform>().pos - Vec3(0.0f, FootToCentre, 0.0f);
            TrackJump(app, grounded, feet);

            // Forgiveness windows.
            if (grounded)
            {
                m_coyote = t.coyoteTime;
                m_extraUsed = 0;
                m_gliding = false;
                m_jumping = false;
            }
            else
            {
                m_coyote -= dt;
            }
            m_buffer = jumpPressed ? t.jumpBuffer : m_buffer - dt;

            // Jumps. The launch tick takes half a tick's gravity, so a held jump peaks at its set height exactly.
            bool launched = false;
            if (m_buffer > 0.0f && m_coyote > 0.0f)
            {
                body.velocity.y = t.JumpSpeed();
                m_coyote = 0.0f;
                m_buffer = 0.0f;
                m_jumping = true;
                m_gliding = false;
                launched = true;
                ++m_jumpCount;
            }
            else if (jumpPressed && !grounded)
            {
                if (t.extraJumpOn && m_extraUsed < t.extraJumps)
                {
                    body.velocity.y = t.ExtraJumpSpeed();
                    ++m_extraUsed;
                    m_buffer = 0.0f;
                    m_jumping = true;
                    m_gliding = false;
                    launched = true;
                }
                else if (t.glideOn)
                {
                    m_gliding = true;
                    m_buffer = 0.0f; // a glide press is not also a buffered jump on landing
                }
            }
            if (t.glideOn && t.glideOnHold && !grounded && jumpHeld && body.velocity.y < 0.0f &&
                !(t.extraJumpOn && m_extraUsed < t.extraJumps))
            {
                m_gliding = true;
            }
            if (!t.glideOn || !jumpHeld || grounded)
            {
                m_gliding = false;
            }

            // Horizontal: steered from the velocity physics wrote back, so what it trimmed against walls stays trimmed.
            float topSpeed = t.runSpeed * (t.speedBoostOn ? t.speedBoostMultiplier : 1.0f) *
                             (t.sprintAllowed && sprintHeld ? t.sprintMultiplier : 1.0f);
            if (m_gliding)
            {
                topSpeed *= t.glideSpeedMultiplier;
            }
            const Vec3 target = wish * topSpeed;
            Vec3 horizontal(body.velocity.x, 0.0f, body.velocity.z);
            float accel = 0.0f;
            const bool hasInput = Fenrir::Math::MagnitudeSq(wish) > 0.0f;
            if (grounded)
            {
                accel = !hasInput                                      ? t.groundDecel
                        : Fenrir::Math::Dot(horizontal, target) < 0.0f ? std::max(t.groundAccel, t.groundDecel)
                                                                       : t.groundAccel;
            }
            else
            {
                accel = hasInput ? t.airAccel : t.airDecel;
            }
            horizontal = MoveTowards(horizontal, target, accel * dt);
            body.velocity.x = horizontal.x;
            body.velocity.z = horizontal.z;

            // Vertical: gravity is game code's. Rising, falling and a let-go jump each have their own.
            if (m_jumping && body.velocity.y <= 0.0f)
            {
                m_jumping = false;
            }
            float gravity = t.RiseGravity();
            if (body.velocity.y > 0.0f)
            {
                if (m_jumping && !jumpHeld)
                {
                    gravity *= t.jumpCutMultiplier;
                }
            }
            else
            {
                gravity *= t.fallGravityMultiplier;
            }
            body.velocity.y -= gravity * dt * (launched ? 0.5f : 1.0f);
            if (m_gliding && body.velocity.y < -t.glideFallSpeed)
            {
                // Undo this tick's gravity past the sink and brake down to it.
                body.velocity.y = std::min(-t.glideFallSpeed, MoveTowards(body.velocity.y + gravity * dt,
                                                                          -t.glideFallSpeed, t.glideBrake * dt));
            }
            body.velocity.y = std::max(body.velocity.y, -t.maxFallSpeed);

            // Facing follows the velocity.
            if (Fenrir::Math::Magnitude(horizontal) > 0.3f)
            {
                const float want = std::atan2(-horizontal.x, -horizontal.z) / Deg;
                float delta = std::remainder(want - m_facing, 360.0f);
                const float most = t.turnSpeed * dt;
                delta = std::clamp(delta, -most, most);
                m_facing += delta;
                m_pivot.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(m_facing * Deg, Vec3(0, 1, 0));
            }
        }

        // Update: switches, the mouse, and the camera, from the character's interpolated position.
        void Update(Fenrir::App& app)
        {
            if (!m_spawned)
            {
                return;
            }
            const float dt = std::min(static_cast<float>(app.GetTime().deltaTime), 0.1f);
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            const auto& mouse = app.GetResource<Fenrir::Mouse>();
            const auto& pads = app.GetResource<Fenrir::Gamepads>();
            const std::optional<Fenrir::GamepadId> pad = FirstPad(pads);
            Tuning& t = m_tuning;

            for ([[maybe_unused]] const FenrirDemo::DemoSceneClickEvent& click : m_clicks.Read(app))
            {
                SetCaptured(app, true);
            }
            if (keyboard.JustPressed(Fenrir::Key::Escape))
            {
                SetCaptured(app, false);
            }

            const auto pressed = [&](Fenrir::Key key, Fenrir::GamepadButton button) {
                return keyboard.JustPressed(key) || (pad && pads.JustPressed(*pad, button));
            };
            if (pressed(Fenrir::Key::V, Fenrir::GamepadButton::North))
            {
                t.cameraMode = 1 - t.cameraMode;
                Log(app, std::format("camera: {}", t.cameraMode == 0 ? "third person" : "first person"));
            }
            if (pressed(Fenrir::Key::Num1, Fenrir::GamepadButton::DPadUp))
            {
                t.extraJumpOn = !t.extraJumpOn;
                Log(app, std::format("extra jump {}", t.extraJumpOn ? "on" : "off"));
            }
            if (pressed(Fenrir::Key::Num2, Fenrir::GamepadButton::DPadRight))
            {
                t.glideOn = !t.glideOn;
                Log(app, std::format("glide {}", t.glideOn ? "on" : "off"));
            }
            if (pressed(Fenrir::Key::Num3, Fenrir::GamepadButton::DPadDown))
            {
                t.speedBoostOn = !t.speedBoostOn;
                Log(app, std::format("speed boost {}", t.speedBoostOn ? "on" : "off"));
            }
            if (keyboard.JustPressed(Fenrir::Key::Num4))
            {
                t.sprintAllowed = !t.sprintAllowed;
                Log(app, std::format("sprint {}", t.sprintAllowed ? "allowed" : "off"));
            }
            if (pressed(Fenrir::Key::R, Fenrir::GamepadButton::Back))
            {
                ResetToStart();
            }
            if (pressed(Fenrir::Key::T, Fenrir::GamepadButton::DPadLeft))
            {
                m_spot = (m_spot + 1) % Spots().size();
                TeleportTo(Spots()[m_spot]);
                Log(app, std::format("teleport: {}", Spots()[m_spot].name));
            }
            if (keyboard.JustPressed(Fenrir::Key::L))
            {
                m_labelsShown = !m_labelsShown;
            }
            if (keyboard.JustPressed(Fenrir::Key::P))
            {
                CopyValues(app);
            }
            if (keyboard.JustPressed(Fenrir::Key::Num0))
            {
                m_tuning = Tuning{};
                Log(app, "values reset to the defaults");
            }
            ShowLabels();
            DrawGrid();

            // Look: the captured mouse, the right stick, or Q/E.
            float lookYaw = 0.0f;
            float lookPitch = 0.0f;
            if (m_captured)
            {
                const Fenrir::Math::Vec2 motion = mouse.Motion();
                lookYaw -= motion.x * t.mouseSensitivity;
                lookPitch -= motion.y * t.mouseSensitivity;
            }
            if (pad)
            {
                const float rx = pads.Axis(*pad, Fenrir::GamepadAxis::RightX);
                const float ry = pads.Axis(*pad, Fenrir::GamepadAxis::RightY);
                if (std::sqrt(rx * rx + ry * ry) > StickDeadzone)
                {
                    lookYaw -= rx * t.padLookSpeed * dt;
                    lookPitch -= ry * t.padLookSpeed * dt;
                }
            }
            lookYaw += (keyboard.IsDown(Fenrir::Key::Q) ? t.keyTurnSpeed * dt : 0.0f) -
                       (keyboard.IsDown(Fenrir::Key::E) ? t.keyTurnSpeed * dt : 0.0f);
            m_yaw = std::remainder(m_yaw + lookYaw, 360.0f);
            const float pitchMin = t.cameraMode == 0 ? std::min(t.camPitchMin, t.camPitchMax) : -85.0f;
            const float pitchMax = t.cameraMode == 0 ? std::max(t.camPitchMin, t.camPitchMax) : 85.0f;
            m_pitch = std::clamp(m_pitch + (t.invertY ? -lookPitch : lookPitch), pitchMin, pitchMax);

            PlaceCamera(dt);
        }

      private:
        static constexpr float StickDeadzone = 0.15f;
        // KIKI_HOLD_JUMP=1 holds jump down, so a smoke run's tapped Space gives held jumps (for checking heights).
        bool m_holdJumpForTest = std::getenv("KIKI_HOLD_JUMP") != nullptr;

        static std::optional<Fenrir::GamepadId> FirstPad(const Fenrir::Gamepads& pads)
        {
            const std::vector<Fenrir::GamepadId> connected = pads.Connected();
            if (connected.empty())
            {
                return std::nullopt;
            }
            return connected.front();
        }

        static const std::vector<Spot>& Spots()
        {
            static const std::vector<Spot> spots = {
                {"start", Vec3(0.0f, 0.0f, 0.0f), 0.0f},
                {"ledges (west)", Vec3(-12.0f, 0.0f, -4.0f), 0.0f},
                {"gap runway (east)", Vec3(6.0f, GapRunwayHeight, -6.0f), 0.0f},
                {"ramps (south-west)", Vec3(-18.0f, 0.0f, 4.0f), 180.0f},
                {"wall and corridor (south-east)", Vec3(14.0f, 0.0f, 14.0f), 180.0f},
                {"low ceilings (south)", Vec3(-2.0f, 0.0f, 12.0f), 180.0f},
                {"tower top (north), 10.5 m", Vec3(-10.0f + 3.0f * (TowerSteps - 1), TowerStep * TowerSteps, -30.0f),
                 0.0f},
            };
            return spots;
        }

        void Log(Fenrir::App& app, const std::string& line)
        {
            app.Logger()->Info("KikiMovePrototype: {}", line);
            if (Fenrir::DebugDraw* draw = m_scene->GetResource<Fenrir::DebugDraw>())
            {
                draw->Message(1, line, Fenrir::Colour{1.0f, 1.0f, 0.6f, 1.0f}, {.duration = 2.0f, .size = 20.0f});
            }
        }

        void SetCaptured(Fenrir::App& app, bool captured)
        {
            if (captured == m_captured)
            {
                return;
            }
            m_captured = captured;
            Fenrir::Window::SetCursorMode(app, Fenrir::Window::MainWindow(),
                                          captured ? Fenrir::CursorMode::Captured : Fenrir::CursorMode::Normal);
        }

        void CopyValues(Fenrir::App& app)
        {
            const std::string text = m_tuning.Dump();
            app.Logger()->Info("KikiMovePrototype values:\n{}", text);
            const bool copied = Fenrir::Window::SetClipboardText(app, text);
            Log(app, copied ? "values copied to the clipboard and logged" : "values logged (clipboard refused)");
        }

        // The jump's record: takeoff to landing, for the highest ledge and the longest gap.
        void TrackJump(Fenrir::App& app, bool grounded, const Vec3& feet)
        {
            if (!grounded && !m_airborne)
            {
                m_airborne = true;
                m_takeoff = m_lastGroundFeet;
                m_takeoffTime = m_lastGroundTime;
                m_peak = feet.y;
                m_trail.clear();
                m_trail.push_back(m_lastGroundFeet);
            }
            if (m_airborne)
            {
                m_peak = std::max(m_peak, feet.y);
                if (m_trail.size() < 2000)
                {
                    m_trail.push_back(feet);
                }
            }
            if (grounded)
            {
                m_lastGroundFeet = feet;
                m_lastGroundTime = m_seconds;
            }
            if (grounded && m_airborne)
            {
                m_airborne = false;
                const Vec3 across(feet.x - m_takeoff.x, 0.0f, feet.z - m_takeoff.z);
                m_lastRise = m_peak - m_takeoff.y;
                m_lastDistance = Fenrir::Math::Magnitude(across);
                m_lastDrop = m_takeoff.y - feet.y;
                m_lastAirTime = m_seconds - m_takeoffTime;
                m_trail.push_back(feet);
                if (m_lastAirTime > 0.1f)
                {
                    app.Logger()->Info("KikiMovePrototype: air {:.2f} s, peak rise {:.2f} m, across {:.2f} m, "
                                       "landed {:.2f} m {} takeoff",
                                       m_lastAirTime, m_lastRise, m_lastDistance, std::abs(m_lastDrop),
                                       m_lastDrop >= 0.0f ? "below" : "above");
                }
                if (Fenrir::DebugDraw* draw = m_scene->GetResource<Fenrir::DebugDraw>())
                {
                    draw->Clear("Trail");
                    draw->Polyline(m_trail, false, Fenrir::Colour{1.0f, 0.5f, 0.1f, 1.0f},
                                   {.group = "Trail", .duration = Fenrir::DebugDraw::Forever, .thickness = 2.0f});
                }
            }
        }

        void PlaceCamera(float dt)
        {
            const Tuning& t = m_tuning;
            const Vec3 centre = Fenrir::GetWorldTransform(m_kiki).pos;
            auto& cam = m_camera.GetComponent<Fenrir::Camera>();
            auto& camTransform = m_camera.GetComponent<Fenrir::Transform>();
            const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(*m_scene);
            const Fenrir::QueryFilter filter{.ignoreBody = m_kiki};

            // The pivot follows the capsule, more loosely up and down.
            const float kh = 1.0f - std::exp(-t.camFollowHorizontal * dt);
            const float kv = 1.0f - std::exp(-t.camFollowVertical * dt);
            m_follow.x += (centre.x - m_follow.x) * kh;
            m_follow.z += (centre.z - m_follow.z) * kh;
            m_follow.y += (centre.y - m_follow.y) * kv;

            if (t.cameraMode == 1)
            {
                camTransform.pos = centre + Vec3(0.0f, t.eyeHeight, 0.0f);
                camTransform.rot = FenrirDemo::YawPitch(m_yaw * Deg, m_pitch * Deg);
                cam.projection = Fenrir::Perspective{.verticalFov = t.fovFirst * Deg, .near = 0.05f};
                SetKikiVisible(false);
                m_actualDistance = 0.0f;
                return;
            }

            // Third person: the pivot above the capsule, lowered under a ceiling; then back along the view, pulled in
            // against whatever is between.
            Vec3 pivot = m_follow + Vec3(0.0f, t.camPivotHeight, 0.0f);
            Vec3 back = Forward(m_yaw, m_pitch) * -1.0f;
            float wanted = t.camDistance;
            float allowed = wanted;
            if (t.camCollision && world != nullptr)
            {
                const Fenrir::SphereShape sphere{.radius = t.camRadius};
                Fenrir::Transform pose;
                pose.pos = m_follow;
                if (t.camPivotHeight > 0.0f)
                {
                    if (const auto hit =
                            world->ShapeCast(sphere, pose, Vec3(0.0f, 1.0f, 0.0f), t.camPivotHeight, filter))
                    {
                        pivot = m_follow + Vec3(0.0f, std::max(0.0f, hit->distance - 0.02f), 0.0f);
                    }
                }
                pose.pos = pivot;
                if (const auto hit = world->ShapeCast(sphere, pose, back, wanted, filter))
                {
                    allowed = std::max(std::min(t.camMinDistance, wanted), hit->distance - 0.02f);
                }
            }
            m_actualDistance =
                allowed < m_actualDistance ? allowed : MoveTowards(m_actualDistance, allowed, t.camZoomOutSpeed * dt);
            camTransform.pos = pivot + back * m_actualDistance;
            camTransform.rot = FenrirDemo::YawPitch(m_yaw * Deg, m_pitch * Deg);
            cam.projection = Fenrir::Perspective{.verticalFov = t.fovThird * Deg, .near = 0.05f};
            // Hidden when the camera is inside Kiki's head, so it does not fill the view.
            SetKikiVisible(Fenrir::Math::Magnitude(camTransform.pos - centre) > 0.9f);
        }

        void SetKikiVisible(bool visible)
        {
            for (Fenrir::Entity* part : {&m_body, &m_beak})
            {
                part->GetComponent<Fenrir::MeshRenderer>().visible = visible;
            }
        }

        void ResetToStart()
        {
            m_spot = 0;
            TeleportTo(Spots()[0]);
            m_pitch = m_tuning.camStartPitch;
        }

        void TeleportTo(const Spot& spot)
        {
            Fenrir::CharacterBody& body = m_kiki.GetComponent<Fenrir::CharacterBody>();
            m_kiki.GetComponent<Fenrir::Transform>().pos = spot.feet + Vec3(0.0f, FootToCentre + 0.02f, 0.0f);
            body.velocity = Vec3(0.0f);
            m_yaw = spot.yaw;
            m_facing = spot.yaw;
            m_pivot.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(m_facing * Deg, Vec3(0, 1, 0));
            m_follow = spot.feet + Vec3(0.0f, FootToCentre, 0.0f);
            m_actualDistance = m_tuning.camDistance;
            m_airborne = false;
            m_gliding = false;
            m_jumping = false;
            m_lastGroundFeet = spot.feet;
            m_lastGroundTime = m_seconds;
        }

        // Looks: the shared unit cube in flat colours, lit by a sun under the shared skybox.
        void LoadLooks(Fenrir::App& app, Fenrir::Scene& scene)
        {
            const auto cube = m_store->Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto lit = m_store->Load<Fenrir::Shader>("builtin:lit", scene);
            const auto skybox = m_store->Load<Fenrir::Image>("skybox/skybox.cubemap", scene);
            if (!cube.Ok() || !lit.Ok() || !skybox.Ok())
            {
                app.Logger()->Error("KikiMovePrototype: the cube, lit shader or skybox did not load");
            }
            m_cube = cube.handle;
            m_lit = lit.handle;
            Fenrir::Entity sun = m_entities->CreateEntity();
            sun.GetComponent<Fenrir::Name>().Set("Sun");
            sun.GetComponent<Fenrir::Transform>().rot =
                Fenrir::Math::AngleAxis(0.6f, Vec3(0.0f, 1.0f, 0.0f)) * Fenrir::Math::AngleAxis(-1.0f, Vec3(1, 0, 0));
            sun.AddComponent<Fenrir::DirectionalLight>();
            scene.AddResource<Fenrir::Sky>(Fenrir::CubemapSky{skybox.handle});
        }

        Fenrir::AssetHandle<Fenrir::Material> Material(const Vec3& srgb)
        {
            for (const auto& [made, material] : m_materials)
            {
                if (made == srgb)
                {
                    return material;
                }
            }
            const auto material = m_store->CreateMaterial(m_lit, *m_scene);
            m_store->SetColour(material, "baseColour",
                               Fenrir::Colour{SrgbToLinear(srgb.x), SrgbToLinear(srgb.y), SrgbToLinear(srgb.z), 1.0f});
            m_materials.emplace_back(srgb, material);
            return material;
        }

        // A box of `size` centred on `centre`, solid unless `solid` is false.
        Fenrir::Entity Box(const char* name, const Vec3& centre, const Vec3& size, const Vec3& colour,
                           const Fenrir::Math::Quat& rotation = Identity(), bool solid = true)
        {
            Fenrir::Entity box = m_entities->CreateEntity();
            box.GetComponent<Fenrir::Name>().Set(name);
            auto& transform = box.GetComponent<Fenrir::Transform>();
            transform.pos = centre;
            transform.rot = rotation;
            transform.scale = size;
            box.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{m_cube, Material(colour)});
            if (solid)
            {
                box.AddComponent<Fenrir::Collider>();
            }
            return box;
        }

        // A box from the floor (or `bottom`) up to `top`, over x in [x0, x1] and z in [z0, z1].
        Fenrir::Entity Block(const char* name, float x0, float x1, float z0, float z1, float top, const Vec3& colour,
                             float bottom = 0.0f)
        {
            return Box(name, Vec3((x0 + x1) * 0.5f, (bottom + top) * 0.5f, (z0 + z1) * 0.5f),
                       Vec3(x1 - x0, top - bottom, z1 - z0), colour);
        }

        void Label(const Vec3& at, std::string text)
        {
            m_labels.emplace_back(at, std::move(text));
        }

        void ShowLabels()
        {
            Fenrir::DebugDraw* draw = m_scene->GetResource<Fenrir::DebugDraw>();
            if (draw == nullptr || !m_labelsShown)
            {
                return;
            }
            const Vec3 eye = m_camera.GetComponent<Fenrir::Transform>().pos;
            for (const auto& [at, text] : m_labels)
            {
                // World text is never hidden behind walls, so only the near ones show.
                if (Fenrir::Math::Magnitude(at - eye) < 28.0f)
                {
                    draw->WorldText(at, text, Fenrir::Colour{1.0f, 1.0f, 1.0f, 1.0f}, {.size = 15.0f});
                }
            }
        }

        // A 1 m grid on the floor to judge distance by, every 5 m bolder; redrawn each frame.
        void DrawGrid()
        {
            Fenrir::DebugDraw* draw = m_scene->GetResource<Fenrir::DebugDraw>();
            if (draw == nullptr || !m_labelsShown)
            {
                return;
            }
            draw->Grid(Vec3(0.0f, 0.02f, 0.0f), Identity(), 120, 1.0f, Fenrir::Colour{0.3f, 0.3f, 0.3f, 1.0f});
            draw->Grid(Vec3(0.0f, 0.025f, 0.0f), Identity(), 24, 5.0f, Fenrir::Colour{0.05f, 0.05f, 0.05f, 1.0f},
                       {.thickness = 2.0f});
        }

        void BuildCourse()
        {
            const Vec3 floorColour(0.62f, 0.62f, 0.60f);
            const Vec3 ledgeColour(0.45f, 0.62f, 0.80f);
            const Vec3 gapColour(0.85f, 0.62f, 0.35f);
            const Vec3 rampColour(0.55f, 0.75f, 0.50f);
            const Vec3 wallColour(0.70f, 0.55f, 0.50f);
            const Vec3 ceilingColour(0.70f, 0.60f, 0.80f);
            const Vec3 towerColour(0.80f, 0.78f, 0.45f);

            Box("Floor", Vec3(0.0f, -0.5f, 0.0f), Vec3(140.0f, 1.0f, 140.0f), floorColour);
            Box("Start pad", Vec3(0.0f, 0.005f, 0.0f), Vec3(1.5f, 0.01f, 1.5f), Vec3(0.95f), Identity(), false);
            Label(Vec3(0.0f, 0.3f, 0.0f), "start (R)");

            // West: ledges, each 2.5 m wide and 3 m deep, front faces on z = -10, 1.5 m apart.
            for (std::size_t i = 0; i < LedgeHeights.size(); ++i)
            {
                const float x = -6.0f - 4.0f * static_cast<float>(i);
                Block("Ledge", x - 1.25f, x + 1.25f, -13.0f, -10.0f, LedgeHeights[i], ledgeColour);
                Label(Vec3(x, LedgeHeights[i] + 0.4f, -10.5f), std::format("ledge {:.1f} m", LedgeHeights[i]));
            }

            // East: a runway 1 m up (four 0.25 m steps up its west end), and north of it a landing pad per gap.
            constexpr float runwayX0 = 4.0f;
            constexpr float runwayX1 = 44.0f;
            constexpr float runwayZ0 = -10.0f; // its north edge, where every gap starts
            constexpr float runwayZ1 = -4.0f;
            Block("Gap runway", runwayX0, runwayX1, runwayZ0, runwayZ1, GapRunwayHeight, gapColour);
            for (int k = 1; k <= 4; ++k)
            {
                const float x0 = runwayX0 - 0.5f * static_cast<float>(5 - k);
                Block("Runway step", x0, x0 + 0.5f, runwayZ0, runwayZ1, 0.25f * static_cast<float>(k), gapColour);
            }
            Label(Vec3(runwayX0 + 1.0f, GapRunwayHeight + 0.6f, (runwayZ0 + runwayZ1) * 0.5f), "gap runway 1.0 m");
            for (std::size_t i = 0; i < GapLengths.size(); ++i)
            {
                const float x = 6.0f + 4.0f * static_cast<float>(i);
                const float nearEdge = runwayZ0 - GapLengths[i];
                Block("Gap landing", x - 1.25f, x + 1.25f, nearEdge - 4.0f, nearEdge, GapRunwayHeight, gapColour);
                Label(Vec3(x, GapRunwayHeight + 0.4f, nearEdge - 0.5f), std::format("gap {:.0f} m", GapLengths[i]));
                // A line across the runway's edge in front of each lane, where its gap is measured from.
                Label(Vec3(x, GapRunwayHeight + 0.4f, runwayZ0 + 0.5f), std::format("{:.0f} m ->", GapLengths[i]));
            }

            // South-west: ramps rising southwards to a 2.5 m top, each 3 m wide, with a landing behind.
            for (std::size_t i = 0; i < RampAngles.size(); ++i)
            {
                const float x = -12.0f - 6.0f * static_cast<float>(i);
                const float a = RampAngles[i] * Deg;
                const float length = RampTop / std::sin(a);
                const float run = RampTop / std::tan(a);
                constexpr float z0 = 8.0f; // where each ramp meets the floor
                constexpr float thickness = 0.3f;
                // The slab's top face runs from (z0, 0) to (z0 + run, RampTop): its centre sits half a thickness below.
                const Vec3 mid(x, RampTop * 0.5f - 0.5f * thickness * std::cos(a),
                               z0 + run * 0.5f + 0.5f * thickness * std::sin(a));
                Box("Ramp", mid, Vec3(3.0f, thickness, length), rampColour,
                    Fenrir::Math::AngleAxis(-a, Vec3(1.0f, 0.0f, 0.0f)));
                Block("Ramp top", x - 1.5f, x + 1.5f, z0 + run, z0 + run + 2.0f, RampTop, rampColour);
                Label(Vec3(x, 0.6f, z0 - 0.5f),
                      std::format("ramp {:.0f} deg{}", RampAngles[i], RampAngles[i] > 45.0f ? " (too steep)" : ""));
            }

            // South-east: a long high wall, an inside corner, and a 2 m corridor.
            Block("Wall", 4.0f, 24.0f, 20.0f, 21.0f, 8.0f, wallColour);
            Block("Corner wall", 23.0f, 24.0f, 12.0f, 20.0f, 8.0f, wallColour);
            Label(Vec3(14.0f, 1.5f, 19.5f), "wall 8 m");
            Block("Corridor west", 27.0f, 28.0f, 4.0f, 20.0f, 4.0f, wallColour);
            Block("Corridor east", 30.0f, 31.0f, 4.0f, 20.0f, 4.0f, wallColour);
            Label(Vec3(29.0f, 1.0f, 4.5f), "corridor 2 m wide, 4 m walls");

            // South: ceilings. A 0.4 m clearance over Kiki's crown, a 1.2 m one (under a full jump), and a platform
            // overhead to walk under and look up at.
            Block("Low ceiling west wall", -7.0f, -6.0f, 12.0f, 20.0f, LowCeiling + 0.3f, ceilingColour);
            Block("Low ceiling east wall", 2.0f, 3.0f, 12.0f, 20.0f, LowCeiling + 0.3f, ceilingColour);
            Block("Low ceiling", -7.0f, 3.0f, 14.0f, 20.0f, LowCeiling + 0.3f, ceilingColour, LowCeiling);
            Label(Vec3(-2.0f, 0.8f, 13.5f), std::format("ceiling {:.1f} m (Kiki 1.0 m)", LowCeiling));
            Block("Mid ceiling", -7.0f, 3.0f, 22.0f, 28.0f, MidCeiling + 0.3f, ceilingColour, MidCeiling);
            Block("Mid ceiling west post", -7.0f, -6.0f, 22.0f, 28.0f, MidCeiling, ceilingColour);
            Block("Mid ceiling east post", 2.0f, 3.0f, 22.0f, 28.0f, MidCeiling, ceilingColour);
            Label(Vec3(-2.0f, 0.8f, 21.5f), std::format("ceiling {:.1f} m", MidCeiling));
            Block("Overhead platform", -7.0f, 3.0f, 32.0f, 40.0f, OverheadUnder + 0.5f, ceilingColour, OverheadUnder);
            Block("Overhead post", -7.0f, -6.0f, 39.0f, 40.0f, OverheadUnder, ceilingColour);
            Block("Overhead post", 2.0f, 3.0f, 39.0f, 40.0f, OverheadUnder, ceilingColour);
            Label(Vec3(-2.0f, 0.8f, 31.5f),
                  std::format("overhead {:.1f} m under, {:.1f} m top", OverheadUnder, OverheadUnder + 0.5f));

            // North: a tower of 3 m blocks, each 1.5 m above the last, to 10.5 m; open floor beyond for gliding off.
            for (int i = 0; i < TowerSteps; ++i)
            {
                const float x = -10.0f + 3.0f * static_cast<float>(i);
                const float top = TowerStep * static_cast<float>(i + 1);
                Block("Tower step", x - 1.5f, x + 1.5f, -31.5f, -28.5f, top, towerColour);
                Label(Vec3(x, top + 0.4f, -28.8f), std::format("{:.1f} m", top));
            }
        }

        void MakeKiki()
        {
            m_kiki = m_entities->CreateEntity();
            m_kiki.GetComponent<Fenrir::Name>().Set("Kiki");
            m_kiki.AddComponent<Fenrir::Collider>(
                Fenrir::Collider{Fenrir::CapsuleShape{.halfHeight = CapsuleHalfHeight, .radius = CapsuleRadius}});
            Fenrir::CharacterBody body;
            body.stepHeight = 0.3f;
            body.floorSnap = 0.3f;
            m_kiki.AddComponent<Fenrir::CharacterBody>(body);

            // Its look, on children with no collider: a body box and a beak showing which way it faces.
            m_pivot = m_entities->CreateEntity();
            m_pivot.GetComponent<Fenrir::Name>().Set("Kiki facing");
            m_kiki.AddChild(m_pivot);
            m_body = Box("Kiki body", Vec3(0.0f), Vec3(0.6f, KikiHeight, 0.6f), Vec3(0.55f, 0.38f, 0.22f), Identity(),
                         false);
            m_beak = Box("Kiki beak", Vec3(0.0f, 0.2f, -0.45f), Vec3(0.08f, 0.08f, 0.4f), Vec3(0.95f, 0.85f, 0.55f),
                         Identity(), false);
            m_pivot.AddChild(m_body);
            m_pivot.AddChild(m_beak);
        }

        void AddPanel(Fenrir::App& app)
        {
            FenrirDemo::DemoUI* kit = app.TryGetResource<FenrirDemo::DemoUI>();
            if (kit == nullptr)
            {
                return;
            }
            Tuning& t = m_tuning;
            const auto f2 = FenrirDemo::DemoRangeOptions{.decimals = 2};
            const auto f0 = FenrirDemo::DemoRangeOptions{.decimals = 0};

            FenrirDemo::DemoSection readings = kit->SceneSection("Readings");
            readings.Readout("Ground", [this] { return GroundName(); });
            readings.Readout("Speed across", [this] {
                const Vec3 v = m_kiki.GetComponent<Fenrir::CharacterBody>().state.movedVelocity;
                return std::format("{:.2f} m/s", std::sqrt(v.x * v.x + v.z * v.z));
            });
            readings.Readout("Speed up", [this] {
                return std::format("{:.2f} m/s", m_kiki.GetComponent<Fenrir::CharacterBody>().state.movedVelocity.y);
            });
            readings.Readout("Feet height", [this] {
                return std::format("{:.2f} m", m_kiki.GetComponent<Fenrir::Transform>().pos.y - FootToCentre);
            });
            readings.Readout("Air state", [this] {
                std::string s = m_gliding ? "gliding" : m_jumping ? "jump rising" : m_airborne ? "airborne" : "-";
                if (m_tuning.extraJumpOn)
                {
                    s += std::format(", extra jumps left {}", std::max(0, m_tuning.extraJumps - m_extraUsed));
                }
                return s;
            });
            readings.Readout("Last jump", [this] {
                return std::format("rise {:.2f} m, across {:.2f} m, {} {:.2f} m, {:.2f} s", m_lastRise, m_lastDistance,
                                   m_lastDrop >= 0.0f ? "down" : "up", std::abs(m_lastDrop), m_lastAirTime);
            });
            readings.Readout("Derived jump", [this] {
                return std::format("g up {:.1f}, g down {:.1f} m/s2, launch {:.2f} m/s, tapped {:.2f} m",
                                   m_tuning.RiseGravity(), m_tuning.RiseGravity() * m_tuning.fallGravityMultiplier,
                                   m_tuning.JumpSpeed(), m_tuning.TappedHeight());
            });
            readings.Readout("Camera", [this] {
                return std::format("{}, yaw {:.0f}, pitch {:.0f}, distance {:.2f} m",
                                   m_tuning.cameraMode == 0 ? "third" : "first", m_yaw, m_pitch, m_actualDistance);
            });
            readings.Readout("Gamepad", [&app] {
                const auto connected = app.GetResource<Fenrir::Gamepads>().Connected();
                return connected.empty() ? std::string("none") : std::format("{} connected", connected.size());
            });
            readings.Button("Copy values to clipboard (P)", [this, &app] { CopyValues(app); });
            readings.Button("Reset values to defaults (0)", [this] { m_tuning = Tuning{}; });

            FenrirDemo::DemoSection power = kit->SceneSection("Power-ups");
            power.Toggle("Extra jump (1)", &t.extraJumpOn);
            power.Range("Extra jumps", &t.extraJumps, 1, 3);
            power.Range("Extra jump height m", &t.extraJumpHeight, 0.3f, 4.0f, f2);
            power.Toggle("Glide (2)", &t.glideOn, "Press jump in the air (after any extra jumps) and hold it");
            power.Toggle("Glide on held jump", &t.glideOnHold, "Also glide whenever jump is held while falling");
            power.Range("Glide sink m/s", &t.glideFallSpeed, 0.5f, 8.0f, f2);
            power.Range("Glide brake m/s2", &t.glideBrake, 5.0f, 150.0f, f0);
            power.Range("Glide speed x run", &t.glideSpeedMultiplier, 0.5f, 2.0f, f2);
            power.Toggle("Speed boost (3)", &t.speedBoostOn);
            power.Range("Speed boost x", &t.speedBoostMultiplier, 1.0f, 2.5f, f2);

            FenrirDemo::DemoSection run = kit->SceneSection("Run");
            run.Range("Top speed m/s", &t.runSpeed, 2.0f, 16.0f, f2);
            run.Range("Acceleration m/s2", &t.groundAccel, 5.0f, 200.0f, f0);
            run.Range("Stopping m/s2", &t.groundDecel, 5.0f, 200.0f, f0, "Also used turning against the run");
            run.Range("Facing turn deg/s", &t.turnSpeed, 90.0f, 2000.0f, f0);
            run.Toggle("Sprint allowed (4)", &t.sprintAllowed, "Shift or right shoulder");
            run.Range("Sprint x", &t.sprintMultiplier, 1.0f, 2.0f, f2);

            FenrirDemo::DemoSection jump = kit->SceneSection("Jump");
            jump.Range("Held height m", &t.jumpHeight, 0.5f, 5.0f, f2);
            jump.Range("Time to top s", &t.timeToApex, 0.15f, 0.8f, f2);
            jump.Range("Fall gravity x", &t.fallGravityMultiplier, 1.0f, 4.0f, f2);
            jump.Range("Let-go gravity x", &t.jumpCutMultiplier, 1.0f, 8.0f, f2, "Rising gravity once jump is let go");
            jump.Range("Max fall m/s", &t.maxFallSpeed, 5.0f, 60.0f, f0);
            jump.Range("Air control m/s2", &t.airAccel, 0.0f, 100.0f, f0);
            jump.Range("Air drag m/s2", &t.airDecel, 0.0f, 100.0f, f0);
            jump.Range("Coyote time s", &t.coyoteTime, 0.0f, 0.4f, f2);
            jump.Range("Jump buffer s", &t.jumpBuffer, 0.0f, 0.4f, f2);

            FenrirDemo::DemoSection camera = kit->SceneSection("Camera");
            camera.Choice("Camera (V)", &t.cameraMode, {"Third person", "First person"});
            camera.Range("Distance m", &t.camDistance, 1.5f, 15.0f, f2);
            camera.Range("Pivot height m", &t.camPivotHeight, 0.0f, 2.5f, f2);
            camera.Range("Pitch down limit deg", &t.camPitchMin, -89.0f, 0.0f, f0);
            camera.Range("Pitch up limit deg", &t.camPitchMax, -30.0f, 89.0f, f0);
            camera.Toggle("Pull in against walls", &t.camCollision);
            camera.Range("Pull-in radius m", &t.camRadius, 0.05f, 1.0f, f2);
            camera.Range("Closest m", &t.camMinDistance, 0.1f, 3.0f, f2);
            camera.Range("Ease out m/s", &t.camZoomOutSpeed, 0.5f, 40.0f, f2);
            camera.Range("Follow across 1/s", &t.camFollowHorizontal, 1.0f, 60.0f, f0);
            camera.Range("Follow up-down 1/s", &t.camFollowVertical, 1.0f, 60.0f, f0);
            camera.Range("FOV third deg", &t.fovThird, 35.0f, 100.0f, f0);
            camera.Range("FOV first deg", &t.fovFirst, 50.0f, 110.0f, f0);
            camera.Range("Eye height m", &t.eyeHeight, 0.0f, 0.5f, f2);
            camera.Range("Mouse deg/count", &t.mouseSensitivity, 0.02f, 0.5f,
                         FenrirDemo::DemoRangeOptions{.decimals = 3});
            camera.Range("Stick look deg/s", &t.padLookSpeed, 45.0f, 400.0f, f0);
            camera.Toggle("Invert Y", &t.invertY);
        }

        [[nodiscard]] std::string GroundName() const
        {
            static constexpr std::array<const char*, 4> names = {"in air", "on ground", "on steep ground", "touching"};
            const auto& state = m_kiki.GetComponent<Fenrir::CharacterBody>().state;
            std::string s = names.at(static_cast<std::size_t>(state.ground));
            if (state.onWall)
            {
                s += ", wall";
            }
            if (state.onCeiling)
            {
                s += ", ceiling";
            }
            return s;
        }

        Fenrir::Scene* m_scene = nullptr;
        Fenrir::EntityList* m_entities = nullptr;
        Fenrir::AssetStore* m_store = nullptr;
        Fenrir::AssetHandle<Fenrir::Mesh> m_cube;
        Fenrir::AssetHandle<Fenrir::Shader> m_lit;
        std::vector<std::pair<Vec3, Fenrir::AssetHandle<Fenrir::Material>>> m_materials;
        std::vector<std::pair<Vec3, std::string>> m_labels;
        bool m_labelsShown = true;
        bool m_spawned = false;

        Tuning m_tuning;
        Fenrir::Entity m_kiki;
        Fenrir::Entity m_pivot;
        Fenrir::Entity m_body;
        Fenrir::Entity m_beak;
        Fenrir::Entity m_camera;
        Fenrir::EventReader<FenrirDemo::DemoSceneClickEvent> m_clicks;
        bool m_captured = false;
        std::size_t m_spot = 0;

        // Movement state.
        float m_seconds = 0.0f;
        float m_coyote = 0.0f;
        float m_buffer = 0.0f;
        int m_extraUsed = 0;
        bool m_jumping = false;
        bool m_gliding = false;
        int m_jumpCount = 0;
        float m_facing = 0.0f;

        // The jump record.
        bool m_airborne = false;
        Vec3 m_takeoff = Vec3(0.0f);
        Vec3 m_lastGroundFeet = Vec3(0.0f);
        float m_lastGroundTime = 0.0f;
        float m_takeoffTime = 0.0f;
        float m_peak = 0.0f;
        float m_lastRise = 0.0f;
        float m_lastDistance = 0.0f;
        float m_lastDrop = 0.0f;
        float m_lastAirTime = 0.0f;
        std::vector<Vec3> m_trail;

        // Camera state.
        float m_yaw = 0.0f;
        float m_pitch = -20.0f;
        Vec3 m_follow = Vec3(0.0f);
        float m_actualDistance = 6.0f;
    };
} // namespace

int main()
{
    Fenrir::App app(std::make_unique<Fenrir::ConsoleLogger>());
    // Before the scaffold, so it ends after it (the scaffold destroys the scene and its physics world).
    Fenrir::Physics physics = Fenrir::Physics::Install(app);

    auto kiki = std::make_shared<KikiMove>();
    app.AddSequentialSystem(Fenrir::SchedulePriority::Tick, [kiki](Fenrir::App& running) { kiki->Tick(running); });
    app.AddSequentialSystem(Fenrir::SchedulePriority::Update, [kiki](Fenrir::App& running) { kiki->Update(running); });

    FenrirDemo::DemoScene scene;
    scene.name = "Grey box";
    scene.keyRows = {{"Click scene / Esc", "capture the mouse to look / free it"},
                     {"WASD/arrows, left stick", "run, relative to the camera"},
                     {"Space, pad South (A)", "jump (hold for height); in the air: extra jump, then glide"},
                     {"Shift, right shoulder", "sprint (when allowed, 4)"},
                     {"Mouse, right stick, Q/E", "look"},
                     {"V, pad North (Y)", "third / first person"},
                     {"1, D-pad up", "extra jump on/off"},
                     {"2, D-pad right", "glide on/off"},
                     {"3, D-pad down", "speed boost on/off"},
                     {"4", "sprint allowed on/off"},
                     {"T, D-pad left", "teleport to the next station"},
                     {"R, pad Back", "back to the start"},
                     {"L", "labels and floor grid on/off"},
                     {"P", "copy every value to the clipboard (and log it)"},
                     {"0", "reset every value to its default"}};
    using Fenrir::Key;
    scene.keys = {Key::W,    Key::Space, Key::Num1, Key::Space, Key::Space, Key::Num2, Key::Space, Key::V,
                  Key::Num3, Key::T,     Key::T,    Key::T,     Key::V,     Key::P,    Key::Num0,  Key::R};
    scene.spawn = [kiki](Fenrir::App& running, Fenrir::Scene& spawned) { kiki->Spawn(running, spawned); };

    FenrirDemo::DemoConfig config;
    config.appName = "KikiMovePrototype";
    config.scenes = {std::move(scene)};
    config.msaa = 4;
    FenrirDemo::DemoScaffold demo = FenrirDemo::DemoScaffold::Install(app, std::move(config));
    return demo.Run();
}
