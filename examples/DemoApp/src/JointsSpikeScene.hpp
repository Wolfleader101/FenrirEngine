#pragma once

// PROTOTYPE (joints spike, ticket 27): the "Joints (spike)" scene. A door on a hinge (limit, friction, motor) hung from
// a static frame, and a chain of boxes hanging from a point fixed to the world, made through the proposed public joint
// interface. F4 switches to it and back. Keys (only in this scene) drive the hand checks; each logs what it did. The
// joints' frames are drawn as axes: frameA long (red, green, blue), frameB short (pink, yellow, cyan); at the rest
// pose and while the joint holds they coincide. Never merged.

#ifdef FENRIR_PHYSICS

#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "Events.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "GLRenderer.hpp"
#include "Material.hpp"
#include "ModelLibrary.hpp"

class JointsSpikeScene
{
  public:
    static constexpr const char* sceneName = "Joints (spike)";

    JointsSpikeScene(GLRenderer& renderer, const Model& cube, const Shader& flatShader)
        : m_renderer(&renderer), m_cube(&cube), m_flatShader(&flatShader)
    {
    }

    // PostInit: the cube and the flat shader are loaded in Init. The scene stays inactive until F4.
    void Init(Fenrir::App& app)
    {
        m_firstScene = app.GetActiveScene().GetName();
        Fenrir::EntityList& entities = app.CreateScene(sceneName).GetEntityList();
        m_entities = &entities;

        // Static, its top face at y = -3.75.
        Fenrir::Entity ground = Box(entities, "Ground", Fenrir::Math::Vec3(0.0f, -4.0f, -12.0f),
                                    Fenrir::Math::Vec3(16.0f, 0.5f, 16.0f), Fenrir::Math::Vec3(0.45f));
        ground.AddComponent<Fenrir::Collider>();

        MakeSpare(entities);
        MakeDoor(entities);
        MakeChain(entities);

        // For a smoke run without a keyboard: start in this scene, and with "keys" press every key in turn.
        if (const char* start = std::getenv("FENRIR_JOINTS_SPIKE"))
        {
            app.ChangeActiveScene(sceneName);
            m_autoKeys = std::string(start) == "keys";
        }
    }

    // PreUpdate: F4 swaps between the first scene and this one.
    void SwitchOnF4(Fenrir::App& app)
    {
        for (const KeyboardKeyEvent& event : m_switchKeys.Read(app))
        {
            constexpr int keyF4 = 293; // GLFW_KEY_F4
            if (event.key != keyF4 || event.state != InputState::Pressed)
            {
                continue;
            }
            const bool inFirst = app.GetActiveScene().GetName() == m_firstScene;
            const std::string next = inFirst ? sceneName : m_firstScene;
            app.ChangeActiveScene(next);
            app.Logger()->Info("Scene: {}", next);
            if (inFirst)
            {
                app.Logger()->Info("Joints spike keys: 1 push door, 2 cycle door motor, 3 destroy link 3, 4 teleport "
                                   "door alone, 5 teleport door and frame, 6 scale link 5, 7 add/remove a lump on "
                                   "link 6, 8 reshape link 6, 9 toggle collideConnected on the chain, 0 log state, B "
                                   "toggle a 15 N breakForce on chain joint 1, R repair disabled joints, P remove the "
                                   "spare joint, L toggle door limits");
            }
        }
    }

    // Tick: the hand checks' keys, and the JointBroken events.
    void Keys(Fenrir::App& app)
    {
        if (app.GetActiveScene().GetName() != sceneName)
        {
            m_keys.Read(app); // read and dropped: keys pressed in another scene are not for this one
            m_broken.Read(app);
            return;
        }
        Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        for (const Fenrir::JointBroken& broken : m_broken.Read(app))
        {
            app.Logger()->Info("JointBroken: joint {} (bodies {} and {}), force {:.1f} N, torque {:.1f} N·m",
                               broken.joint.GetId(), broken.bodyA.GetId(), broken.bodyB.GetId(),
                               Fenrir::Math::Magnitude(broken.force), Fenrir::Math::Magnitude(broken.torque));
        }
        for (const KeyboardKeyEvent& event : m_keys.Read(app))
        {
            if (event.state == InputState::Pressed && world != nullptr)
            {
                OnKey(app, *world, event.key);
            }
        }
        if (m_autoKeys && world != nullptr)
        {
            // One key a second, after two seconds of settling.
            static constexpr std::string_view sequence = "0120345678906789B0R0BR9P0L1203";
            const int tick = m_ticks++;
            if (tick >= 120 && tick % 60 == 0 && static_cast<std::size_t>(tick / 60 - 2) < sequence.size())
            {
                const char key = sequence[static_cast<std::size_t>(tick / 60 - 2)];
                app.Logger()->Info("[auto] key {}", key);
                OnKey(app, *world, key);
            }
        }
    }

    // Update, after the renderer: each joint's two frames as axes, where the bodies are drawn.
    void DrawFrames(Fenrir::App& app)
    {
        if (app.GetActiveScene().GetName() != sceneName)
        {
            return;
        }
        m_lines.clear();
        const Fenrir::ComponentType joints[] = {Fenrir::ComponentType::Native<Fenrir::Joint>()};
        m_entities->ForEachWith(joints, [this](std::uint32_t id) {
            const Fenrir::Joint& joint = m_entities->GetEntity(id).GetComponent<Fenrir::Joint>();
            if (!joint.bodyA.IsValid())
            {
                return;
            }
            const auto colour = [&joint](float r, float g, float b) {
                // A disabled joint's axes are dimmed.
                return joint.enabled ? Fenrir::Math::Vec3(r, g, b) : Fenrir::Math::Vec3(r, g, b) * 0.3f;
            };
            const Fenrir::JointFrame a = InWorld(joint.bodyA, joint.frameA);
            Axes(a, 0.35f, colour(1.0f, 0.2f, 0.2f), colour(0.2f, 1.0f, 0.2f), colour(0.3f, 0.4f, 1.0f));
            const bool world = joint.bodyB == Fenrir::Entity();
            if (world || joint.bodyB.IsValid())
            {
                const Fenrir::JointFrame b = world ? joint.frameB : InWorld(joint.bodyB, joint.frameB);
                Axes(b, 0.2f, colour(1.0f, 0.5f, 0.9f), colour(1.0f, 1.0f, 0.3f), colour(0.3f, 1.0f, 1.0f));
            }
        });
        m_renderer->DrawLines(m_lines);
    }

  private:
    static constexpr int linkCount = 8;
    static constexpr float linkLength = 0.5f;
    inline static const Fenrir::Math::Vec3 hook = Fenrir::Math::Vec3(1.5f, 2.5f, -12.0f);
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

    // A unit cube model scaled to `size`, in a flat colour. Its collider, if any, is the default unit box, scaled too.
    Fenrir::Entity Box(Fenrir::EntityList& entities, const char* name, const Fenrir::Math::Vec3& position,
                       const Fenrir::Math::Vec3& size, const Fenrir::Math::Vec3& colour)
    {
        Fenrir::Entity box = entities.CreateEntity();
        box.GetComponent<Fenrir::Name>().Set(name);
        box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, size);
        Material material{*m_flatShader, {}};
        material.properties["color"] = colour;
        box.AddComponent<Material>(material);
        box.AddComponent<Model>(*m_cube);
        return box;
    }

    // Two small boxes on the ground, ball-jointed: the first joint made, so the lowest joint entity. Removing it (P)
    // makes Jolt move its last constraint into its slot; the solve order must not change for that.
    void MakeSpare(Fenrir::EntityList& entities)
    {
        Fenrir::Entity left = Box(entities, "Spare left", Fenrir::Math::Vec3(-7.0f, -3.5f, -12.0f),
                                  Fenrir::Math::Vec3(0.5f), Fenrir::Math::Vec3(0.6f));
        left.AddComponent<Fenrir::RigidBody>();
        left.AddComponent<Fenrir::Collider>();
        Fenrir::Entity right = Box(entities, "Spare right", Fenrir::Math::Vec3(-6.4f, -3.5f, -12.0f),
                                   Fenrir::Math::Vec3(0.5f), Fenrir::Math::Vec3(0.6f));
        right.AddComponent<Fenrir::RigidBody>();
        right.AddComponent<Fenrir::Collider>();
        m_spare = entities.CreateEntity();
        m_spare.GetComponent<Fenrir::Name>().Set("Spare joint");
        const Fenrir::JointFrames frames =
            Fenrir::FrameFromWorld(left, right, Fenrir::JointFrame{Fenrir::Math::Vec3(-6.7f, -3.5f, -12.0f)});
        m_spare.AddComponent<Fenrir::Joint>(Fenrir::Joint{.bodyA = left,
                                                          .bodyB = right,
                                                          .frameA = frames.frameA,
                                                          .frameB = frames.frameB,
                                                          .kind = Fenrir::BallJoint{}});
    }

    // A static frame (two posts and a lintel under a "Door frame" entity) and a dynamic door beneath it, hinged to the
    // left post on its left edge. The hinge lives on the door's own entity. The post is scaled (0.2, 2.2, 0.2), so the
    // hinge's frameA is in its scaled local space.
    void MakeDoor(Fenrir::EntityList& entities)
    {
        m_frame = entities.CreateEntity();
        m_frame.GetComponent<Fenrir::Name>().Set("Door frame");
        m_frame.GetComponent<Fenrir::Transform>().pos = Fenrir::Math::Vec3(-3.5f, -3.75f, -12.0f);

        const Fenrir::Math::Vec3 wood(0.55f, 0.35f, 0.2f);
        Fenrir::Entity left = Box(entities, "Post (hinge)", Fenrir::Math::Vec3(-0.6f, 1.1f, 0.0f),
                                  Fenrir::Math::Vec3(0.2f, 2.2f, 0.2f), wood);
        left.AddComponent<Fenrir::Collider>();
        m_frame.AddChild(left);
        Fenrir::Entity right =
            Box(entities, "Post", Fenrir::Math::Vec3(0.6f, 1.1f, 0.0f), Fenrir::Math::Vec3(0.2f, 2.2f, 0.2f), wood);
        right.AddComponent<Fenrir::Collider>();
        m_frame.AddChild(right);
        Fenrir::Entity lintel =
            Box(entities, "Lintel", Fenrir::Math::Vec3(0.0f, 2.3f, 0.0f), Fenrir::Math::Vec3(1.4f, 0.2f, 0.2f), wood);
        lintel.AddComponent<Fenrir::Collider>();
        m_frame.AddChild(lintel);

        m_door = Box(entities, "Door", Fenrir::Math::Vec3(-0.01f, 1.05f, 0.0f), Fenrir::Math::Vec3(0.96f, 2.0f, 0.08f),
                     Fenrir::Math::Vec3(0.2f, 0.75f, 0.25f));
        m_door.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 10.0f});
        m_door.AddComponent<Fenrir::Collider>();
        m_frame.AddChild(m_door);

        // The hinge axis (frame X) turned onto world up, at the left post's inner face.
        const Fenrir::Math::Quat axisUp =
            Fenrir::Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
        const Fenrir::Math::Vec3 anchor =
            Fenrir::GetWorldTransform(m_frame).pos + Fenrir::Math::Vec3(-0.5f, 1.05f, 0.0f);
        const Fenrir::JointFrames frames = Fenrir::FrameFromWorld(left, m_door, Fenrir::JointFrame{anchor, axisUp});
        Fenrir::HingeJoint hinge;
        hinge.limits = Fenrir::JointLimits{.enabled = true, .min = -1.75f, .max = 1.75f, .spring = {}};
        hinge.friction = 2.0f;
        m_door.AddComponent<Fenrir::Joint>(Fenrir::Joint{
            .bodyA = left, .bodyB = m_door, .frameA = frames.frameA, .frameB = frames.frameB, .kind = hinge});
    }

    // Eight links laid out level from a hook fixed to the world, so they fall and swing at once. Each joint is on its
    // own entity beneath "Chain joints"; the top one joins its link to the world (bodyB empty).
    void MakeChain(Fenrir::EntityList& entities)
    {
        Box(entities, "Hook (world point, no body)", hook, Fenrir::Math::Vec3(0.12f), Fenrir::Math::Vec3(0.9f));
        Fenrir::Entity links = entities.CreateEntity();
        links.GetComponent<Fenrir::Name>().Set("Chain");
        Fenrir::Entity jointsRoot = entities.CreateEntity();
        jointsRoot.GetComponent<Fenrir::Name>().Set("Chain joints");

        static constexpr std::array<Fenrir::Math::Vec3, 2> colours = {Fenrir::Math::Vec3(0.85f, 0.25f, 0.2f),
                                                                      Fenrir::Math::Vec3(0.95f, 0.8f, 0.2f)};
        for (int i = 0; i < linkCount; ++i)
        {
            const float start = hook.x + linkLength * static_cast<float>(i);
            Fenrir::Entity link =
                Box(entities, ("Link " + std::to_string(i)).c_str(),
                    Fenrir::Math::Vec3(start + linkLength / 2.0f, hook.y, hook.z),
                    Fenrir::Math::Vec3(linkLength, 0.15f, 0.15f), colours[static_cast<std::size_t>(i % 2)]);
            link.AddComponent<Fenrir::RigidBody>();
            link.AddComponent<Fenrir::Collider>();
            links.AddChild(link);

            const Fenrir::Entity above = i == 0 ? Fenrir::Entity() : m_links.back();
            Fenrir::Entity joint = entities.CreateEntity();
            joint.GetComponent<Fenrir::Name>().Set(("Chain joint " + std::to_string(i)).c_str());
            const Fenrir::JointFrames frames =
                Fenrir::FrameFromWorld(link, above, Fenrir::JointFrame{Fenrir::Math::Vec3(start, hook.y, hook.z)});
            joint.AddComponent<Fenrir::Joint>(Fenrir::Joint{.bodyA = link,
                                                            .bodyB = above,
                                                            .frameA = frames.frameA,
                                                            .frameB = frames.frameB,
                                                            .kind = Fenrir::BallJoint{}});
            jointsRoot.AddChild(joint);
            m_links.push_back(link);
            m_joints.push_back(joint);
        }
    }

    void OnKey(Fenrir::App& app, Fenrir::PhysicsWorld& world, int key)
    {
        auto& logger = *app.Logger();
        Fenrir::Joint& hinge = m_door.GetComponent<Fenrir::Joint>();
        Fenrir::HingeJoint& door = hinge.kind.As<Fenrir::HingeJoint>();
        switch (key)
        {
        case '1':
            world.AddAngularImpulse(m_door, Fenrir::Math::Vec3(0.0f, 15.0f, 0.0f));
            logger.Info("Door pushed");
            break;
        case '2': {
            m_motorStep = (m_motorStep + 1) % 4;
            static constexpr std::array<const char*, 4> names = {"off", "velocity +1.5 rad/s", "velocity -1.5 rad/s",
                                                                 "position 0 (close)"};
            Fenrir::JointMotor motor{.maxForce = 200.0f};
            motor.mode = m_motorStep == 0   ? Fenrir::JointMotorMode::Off
                         : m_motorStep == 3 ? Fenrir::JointMotorMode::Position
                                            : Fenrir::JointMotorMode::Velocity;
            motor.targetVelocity = m_motorStep == 1 ? 1.5f : -1.5f;
            door.motor = motor;
            logger.Info("Door motor: {} (door was {})", names[static_cast<std::size_t>(m_motorStep)],
                        world.IsSleeping(m_door) ? "asleep" : "awake");
            break;
        }
        case '3':
            if (m_links[3].IsValid())
            {
                m_entities->DestroyEntity(m_links[3]);
                logger.Info("Link 3 destroyed");
            }
            break;
        case '4':
            m_door.GetComponent<Fenrir::Transform>().pos.x += 0.7f;
            logger.Info("Door teleported 0.7 along x, alone");
            break;
        case '5':
            m_frame.GetComponent<Fenrir::Transform>().pos.x += 1.0f;
            logger.Info("Door frame (and the door beneath it) teleported 1 along x");
            break;
        case '6':
            if (m_links[5].IsValid())
            {
                Fenrir::Math::Vec3& scale = m_links[5].GetComponent<Fenrir::Transform>().scale;
                const bool grown = scale.y > 0.2f;
                scale = grown ? Fenrir::Math::Vec3(linkLength, 0.15f, 0.15f) : Fenrir::Math::Vec3(0.75f, 0.3f, 0.3f);
                logger.Info("Link 5 scaled to {:.2f}, {:.2f}, {:.2f}", scale.x, scale.y, scale.z);
            }
            break;
        case '7':
            ToggleLump(logger);
            break;
        case '8':
            if (m_links[6].IsValid())
            {
                Fenrir::BoxShape& box = m_links[6].GetComponent<Fenrir::Collider>().shape.As<Fenrir::BoxShape>();
                box.halfExtents.y = box.halfExtents.y > 0.6f ? 0.5f : 1.5f;
                logger.Info("Link 6 reshaped: box half extents y {:.1f} (the model is not)", box.halfExtents.y);
            }
            break;
        case '9':
            m_collide = !m_collide;
            for (Fenrir::Entity& joint : m_joints)
            {
                joint.GetComponent<Fenrir::Joint>().collideConnected = m_collide;
            }
            logger.Info("Chain collideConnected: {}", m_collide);
            break;
        case '0':
            LogState(logger, world);
            break;
        case 'B': {
            float& breakForce = m_joints[1].GetComponent<Fenrir::Joint>().breakForce;
            breakForce = std::isinf(breakForce) ? 15.0f : std::numeric_limits<float>::infinity();
            logger.Info("Chain joint 1 breakForce: {} N (it carries every link below link 0)", breakForce);
            break;
        }
        case 'R': {
            const Fenrir::ComponentType joints[] = {Fenrir::ComponentType::Native<Fenrir::Joint>()};
            m_entities->ForEachWith(joints, [this, &logger](std::uint32_t id) {
                Fenrir::Joint& joint = m_entities->GetEntity(id).GetComponent<Fenrir::Joint>();
                if (!joint.enabled)
                {
                    joint.enabled = true;
                    logger.Info("Joint {} re-enabled", id);
                }
            });
            break;
        }
        case 'P':
            if (m_spare.IsValid())
            {
                m_entities->DestroyEntity(m_spare);
                logger.Info("Spare joint removed: the chain should carry on exactly as before");
            }
            break;
        case 'L':
            door.limits.enabled = !door.limits.enabled;
            logger.Info("Door limits: {}", door.limits.enabled ? "on" : "off");
            break;
        default:
            break;
        }
    }

    // A compound child on link 6: its centre of mass moves, and the anchors must stay where they are on the link.
    void ToggleLump(Fenrir::ILogger& logger)
    {
        if (!m_links[6].IsValid())
        {
            return;
        }
        if (m_lump.IsValid())
        {
            m_entities->DestroyEntity(m_lump);
            logger.Info("Lump removed from link 6");
            return;
        }
        // Local to the link, whose scale is (0.5, 0.15, 0.15): a 0.3 cube standing 0.3 off its side.
        m_lump = Box(*m_entities, "Lump", Fenrir::Math::Vec3(0.0f, 2.0f, 0.0f), Fenrir::Math::Vec3(0.6f, 2.0f, 2.0f),
                     Fenrir::Math::Vec3(0.9f, 0.4f, 0.9f));
        m_lump.AddComponent<Fenrir::Collider>();
        m_links[6].AddChild(m_lump);
        logger.Info("Lump added to link 6: its centre of mass moves off its axis");
    }

    void LogState(Fenrir::ILogger& logger, Fenrir::PhysicsWorld& world)
    {
        const auto log = [&logger, &world](const char* name, const Fenrir::Entity& joint) {
            const Fenrir::JointState state = world.JointState(joint);
            const Fenrir::Math::Vec3& p = state.relative.position;
            logger.Info("{}: position {:.3f}, force {:.1f} N, torque {:.1f} N·m, frames apart {:.4f} m ({:.4f}, "
                        "{:.4f}, {:.4f})",
                        name, state.position, Fenrir::Math::Magnitude(state.force),
                        Fenrir::Math::Magnitude(state.torque), Fenrir::Math::Magnitude(p), p.x, p.y, p.z);
        };
        log("Door hinge", m_door);
        logger.Info("Door is {}", world.IsSleeping(m_door) ? "asleep" : "awake");
        for (std::size_t i = 0; i < m_joints.size(); ++i)
        {
            log(("Chain joint " + std::to_string(i)).c_str(), m_joints[i]);
        }
    }

    static Fenrir::JointFrame InWorld(const Fenrir::Entity& body, const Fenrir::JointFrame& frame)
    {
        const Fenrir::Transform world = Fenrir::GetWorldTransform(body);
        return Fenrir::JointFrame{world.pos + world.rot * (frame.position * world.scale), world.rot * frame.rotation};
    }

    void Axes(const Fenrir::JointFrame& frame, float length, const Fenrir::Math::Vec3& x, const Fenrir::Math::Vec3& y,
              const Fenrir::Math::Vec3& z)
    {
        const std::array<std::pair<Fenrir::Math::Vec3, Fenrir::Math::Vec3>, 3> axes = {
            std::pair{Fenrir::Math::Vec3(1.0f, 0.0f, 0.0f), x}, std::pair{Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f), y},
            std::pair{Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f), z}};
        for (const auto& [axis, colour] : axes)
        {
            m_lines.push_back(LineVertex{frame.position, colour});
            m_lines.push_back(LineVertex{frame.position + frame.rotation * axis * length, colour});
        }
    }

    GLRenderer* m_renderer;
    const Model* m_cube;
    const Shader* m_flatShader;
    std::string m_firstScene;
    Fenrir::EntityList* m_entities = nullptr;
    Fenrir::Entity m_frame;
    Fenrir::Entity m_door;
    Fenrir::Entity m_spare;
    Fenrir::Entity m_lump;
    std::vector<Fenrir::Entity> m_links;
    std::vector<Fenrir::Entity> m_joints;
    int m_motorStep = 0;
    bool m_autoKeys = false;
    int m_ticks = 0;
    bool m_collide = false;
    Fenrir::EventReader<KeyboardKeyEvent> m_switchKeys;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
    Fenrir::EventReader<Fenrir::JointBroken> m_broken;
    std::vector<LineVertex> m_lines;
};

#endif // FENRIR_PHYSICS
