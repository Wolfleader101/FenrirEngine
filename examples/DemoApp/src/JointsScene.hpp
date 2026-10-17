#pragma once

// The Joints (C++) scene, built in C++ through FenrirPhysics's joints (ADR-0013): a door hinged to a static post, and a
// chain of eight links hanging from a point fixed to the world. F4 steps to it from Default and back.
//
// The door holds its own hinge, its post as bodyA, so its opening angle reads positive. Each chain joint is on its own
// entity, the top one joined to the world (an empty bodyB). Every frame is made with FrameFromWorld.
//
// Jointed bodies do not collide: neighbouring links overlap at their joints, and the door's hinge edge sits against
// its post. A trigger beneath the door, overlapping the post too, still reports it: triggers are never excluded. 9 lets
// the links collide with their neighbours (collideConnected), which pushes them apart, and back.
//
// The door's hinge has hard limits (shut to a little past a right angle open), friction, and a motor that starts off.
// 1 pushes the door, 2 cycles its motor (off, opening, closing, to shut), logging whether the door was asleep, and L
// turns its limits off and on. With limits on, the opening motor holds the door at its limit, however strong.
// Each joint entity's name shows its joint state every frame: the door's angle and load, each chain joint's load. 0
// logs every joint's full state.

#ifdef FENRIR_PHYSICS

#include <array>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>

#include "DemoScenes.hpp"
#include "Events.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "Material.hpp"
#include "ModelLibrary.hpp"

class JointsScene
{
  public:
    static constexpr const char* name = "Joints (C++)";

    /** @brief The scene as the switcher steps to it: F4 from Default and back. */
    static DemoScene Scene()
    {
        return DemoScene{.name = name,
                         .familyKey = DemoFamilyKey::joints,
                         .keyHelp =
                             "The door swings on its hinge, the chain hangs from its hook. 1 pushes the door, 2 "
                             "cycles its motor (off, opening, closing, to shut), L turns its limits off and on. 9 lets "
                             "the links collide with their neighbours, and back. 0 logs every joint's state. F2 cycles "
                             "the physics debug lines, F4 goes on.",
                         .smokeKeys = {GLFW_KEY_9, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_2, GLFW_KEY_L, GLFW_KEY_2,
                                       GLFW_KEY_2, GLFW_KEY_L, GLFW_KEY_0}};
    }

    JointsScene(const Model& cube, const Shader& flatShader) : m_cube(&cube), m_flatShader(&flatShader)
    {
    }

    // PostInit: the cube and the flat shader are loaded in Init. The scene stays inactive until the switcher steps to
    // it.
    void Init(Fenrir::App& app)
    {
        m_scene = name;
        Fenrir::EntityList& entities = app.CreateScene(name).GetEntityList();

        // Static, its top face at y = -3.75.
        Fenrir::Entity ground = Box(entities, "Ground", Fenrir::Math::Vec3(0.0f, -4.0f, -12.0f),
                                    Fenrir::Math::Vec3(16.0f, 0.5f, 16.0f), Fenrir::Math::Vec3(0.45f));
        ground.AddComponent<Fenrir::Collider>();

        MakeDoor(entities);
        MakeChain(entities);
    }

    // Update, while the scene is active: 1, 2 and L work the door, 9 toggles collideConnected on every chain joint
    // between two links, 0 logs every joint's state, each TriggerEnter of the door's trigger is logged, and each joint
    // entity's name shows its readings. Events sent while another scene is active are read and dropped.
    void Update(Fenrir::App& app)
    {
        const bool active = app.GetActiveScene().GetName() == m_scene;
        for (const KeyboardKeyEvent& event : m_keys.Read(app))
        {
            if (!active || event.state != InputState::Pressed)
            {
                continue;
            }
            if (event.key == GLFW_KEY_1)
            {
                PushDoor(app);
            }
            else if (event.key == GLFW_KEY_2)
            {
                CycleDoorMotor(app);
            }
            else if (event.key == GLFW_KEY_L)
            {
                Fenrir::JointLimits& limits = DoorHinge().limits;
                limits.enabled = !limits.enabled;
                app.Logger()->Info(limits.enabled ? "Door limits on" : "Door limits off: it swings all the way round");
            }
            else if (event.key == GLFW_KEY_9)
            {
                m_linksCollide = !m_linksCollide;
                for (const Fenrir::Entity& joint : m_linkJoints)
                {
                    joint.GetComponent<Fenrir::Joint>().collideConnected = m_linksCollide;
                }
                app.Logger()->Info(m_linksCollide ? "Chain links collide with their neighbours"
                                                  : "Chain links pass through their neighbours");
            }
            else if (event.key == GLFW_KEY_0)
            {
                LogJointStates(app);
            }
        }
        if (active)
        {
            ShowReadings(app);
        }
        for (const Fenrir::TriggerEnter& enter : m_enters.Read(app))
        {
            if (active && enter.trigger == m_doorTrigger)
            {
                app.Logger()->Info(std::string("Door trigger entered by ") +
                                   enter.otherBody.GetComponent<Fenrir::Name>().Get());
            }
        }
    }

  private:
    // Each joint entity, with the name it is shown under before its readings.
    struct Labelled
    {
        Fenrir::Entity joint;
        std::string label;
    };

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

    // The joints' readings in their names: the door's angle, and how hard each joint pulls.
    void ShowReadings(Fenrir::App& app) const
    {
        const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        if (world == nullptr)
        {
            return;
        }
        for (const Labelled& labelled : m_joints)
        {
            const Fenrir::JointState state = world->GetJointState(labelled.joint);
            const double force = static_cast<double>(Fenrir::Math::Magnitude(state.force));
            const std::string reading =
                labelled.joint.GetComponent<Fenrir::Joint>().kind.Is<Fenrir::HingeJoint>()
                    ? Format("%s: %.2f rad, %.0f N", labelled.label.c_str(), static_cast<double>(state.position), force)
                    : Format("%s: %.1f N", labelled.label.c_str(), force);
            labelled.joint.GetComponent<Fenrir::Name>().Set(reading.c_str());
        }
    }

    void LogJointStates(Fenrir::App& app) const
    {
        const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
        if (world == nullptr)
        {
            return;
        }
        for (const Labelled& labelled : m_joints)
        {
            const Fenrir::JointState state = world->GetJointState(labelled.joint);
            const Fenrir::Math::Quat& turn = state.relative.rotation;
            app.Logger()->Info(labelled.label + ": force " + Describe(state.force) + " N, torque " +
                               Describe(state.torque) + " N m, frameB at " + Describe(state.relative.position) +
                               Format(" turned (w %.3f, x %.3f, y %.3f, z %.3f), position %.3f",
                                      static_cast<double>(turn.w), static_cast<double>(turn.x),
                                      static_cast<double>(turn.y), static_cast<double>(turn.z),
                                      static_cast<double>(state.position)));
        }
    }

    // The door's motor settings, cycled through by 2.
    enum class DoorMotor
    {
        Off,
        Opening,
        Closing,
        Shut
    };

    static constexpr int linkCount = 8;
    // Joint to joint. Each link is a little longer, so neighbours overlap at their joints.
    static constexpr float linkSpacing = 0.5f;
    static constexpr float linkLength = 0.6f;
    inline static const Fenrir::Math::Vec3 hook = Fenrir::Math::Vec3(1.5f, 2.5f, -12.0f);
    inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

    // A unit cube model scaled to `size`, in a flat colour. Its collider, if any, is the default unit box, scaled too.
    Fenrir::Entity Box(Fenrir::EntityList& entities, const char* boxName, const Fenrir::Math::Vec3& position,
                       const Fenrir::Math::Vec3& size, const Fenrir::Math::Vec3& colour) const
    {
        Fenrir::Entity box = entities.CreateEntity();
        box.GetComponent<Fenrir::Name>().Set(boxName);
        box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, size);
        Material material{*m_flatShader, {}};
        material.properties["color"] = colour;
        box.AddComponent<Material>(material);
        box.AddComponent<Model>(*m_cube);
        return box;
    }

    // A static frame (two posts and a lintel under a "Door frame" entity) and a dynamic door beneath it, hinged on its
    // left edge to the left post. The post is scaled (0.2, 2.2, 0.2), so the hinge's frame on it is in that scaled
    // space: FrameFromWorld works it out. The door starts swinging open, and comes to rest against the frame. A
    // small trigger beneath the door, at its hinge edge, overlaps the post.
    void MakeDoor(Fenrir::EntityList& entities)
    {
        Fenrir::Entity frame = entities.CreateEntity();
        frame.GetComponent<Fenrir::Name>().Set("Door frame");
        frame.GetComponent<Fenrir::Transform>().pos = Fenrir::Math::Vec3(-3.5f, -3.75f, -12.0f);

        const Fenrir::Math::Vec3 wood(0.55f, 0.35f, 0.2f);
        Fenrir::Entity post = Box(entities, "Post (hinge)", Fenrir::Math::Vec3(-0.6f, 1.1f, 0.0f),
                                  Fenrir::Math::Vec3(0.2f, 2.2f, 0.2f), wood);
        post.AddComponent<Fenrir::Collider>();
        frame.AddChild(post);
        Fenrir::Entity right =
            Box(entities, "Post", Fenrir::Math::Vec3(0.6f, 1.1f, 0.0f), Fenrir::Math::Vec3(0.2f, 2.2f, 0.2f), wood);
        right.AddComponent<Fenrir::Collider>();
        frame.AddChild(right);
        Fenrir::Entity lintel =
            Box(entities, "Lintel", Fenrir::Math::Vec3(0.0f, 2.3f, 0.0f), Fenrir::Math::Vec3(1.4f, 0.2f, 0.2f), wood);
        lintel.AddComponent<Fenrir::Collider>();
        frame.AddChild(lintel);

        Fenrir::Entity door = Box(entities, "Door", Fenrir::Math::Vec3(-0.01f, 1.05f, 0.0f),
                                  Fenrir::Math::Vec3(0.96f, 2.0f, 0.08f), Fenrir::Math::Vec3(0.2f, 0.75f, 0.25f));
        Fenrir::RigidBody doorBody;
        doorBody.mass = 10.0f;
        doorBody.angularVelocity = Fenrir::Math::Vec3(0.0f, 1.5f, 0.0f);
        door.AddComponent<Fenrir::RigidBody>(doorBody);
        door.AddComponent<Fenrir::Collider>();
        frame.AddChild(door);

        // On the door's hinge edge, its scale undoing the door's so the sphere stays round, reaching into the post.
        m_doorTrigger = entities.CreateEntity();
        m_doorTrigger.GetComponent<Fenrir::Name>().Set("Door trigger");
        door.AddChild(m_doorTrigger);
        m_doorTrigger.GetComponent<Fenrir::Transform>() =
            Fenrir::Transform(Fenrir::Math::Vec3(-0.5f, 0.0f, 0.0f), identity,
                              Fenrir::Math::Vec3(1.0f / 0.96f, 1.0f / 2.0f, 1.0f / 0.08f));
        m_doorTrigger.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::SphereShape{0.12f}});
        m_doorTrigger.AddComponent<Fenrir::Trigger>();

        // The hinge axis (frame X) turned onto world up, at the left post's inner face.
        const Fenrir::Math::Quat axisUp =
            Fenrir::Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
        const Fenrir::Math::Vec3 anchor = Fenrir::GetWorldTransform(frame).pos + Fenrir::Math::Vec3(-0.5f, 1.05f, 0.0f);
        const Fenrir::JointFrames frames = Fenrir::FrameFromWorld(post, door, Fenrir::JointFrame{anchor, axisUp});
        // Shut to 100 degrees open, and stiff enough to stay where it is left once it slows.
        door.AddComponent<Fenrir::Joint>(Fenrir::Joint{
            .bodyA = post,
            .bodyB = door,
            .frameA = frames.frameA,
            .frameB = frames.frameB,
            .kind = Fenrir::HingeJoint{.limits = {.enabled = true, .min = 0.0f, .max = 1.75f}, .friction = 3.0f}});
        m_door = door;
        m_joints.push_back(Labelled{door, "Door hinge"});
    }

    Fenrir::HingeJoint& DoorHinge() const
    {
        return m_door.GetComponent<Fenrir::Joint>().kind.As<Fenrir::HingeJoint>();
    }

    // 1: an angular impulse about the hinge, opening and closing in turn: about 2 rad/s.
    void PushDoor(Fenrir::App& app)
    {
        m_pushOpen = !m_pushOpen;
        Fenrir::PhysicsWorld::Of(app.GetActiveScene())
            ->AddAngularImpulse(m_door, Fenrir::Math::Vec3(0.0f, m_pushOpen ? 6.0f : -6.0f, 0.0f));
        app.Logger()->Info(m_pushOpen ? "Door pushed open" : "Door pushed shut");
    }

    // 2: the next motor setting, with no limit on its torque. Logged with whether the door was asleep: the edit wakes
    // it.
    void CycleDoorMotor(Fenrir::App& app)
    {
        const bool asleep = Fenrir::PhysicsWorld::Of(app.GetActiveScene())->IsSleeping(m_door);
        Fenrir::JointMotor& motor = DoorHinge().motor;
        const char* what = "";
        switch (m_doorMotor)
        {
        case DoorMotor::Off:
            m_doorMotor = DoorMotor::Opening;
            motor = {.mode = Fenrir::JointMotorMode::Velocity, .targetVelocity = 1.0f};
            what = "opening at 1 rad/s";
            break;
        case DoorMotor::Opening:
            m_doorMotor = DoorMotor::Closing;
            motor = {.mode = Fenrir::JointMotorMode::Velocity, .targetVelocity = -1.0f};
            what = "closing at 1 rad/s";
            break;
        case DoorMotor::Closing:
            m_doorMotor = DoorMotor::Shut;
            motor = {.mode = Fenrir::JointMotorMode::Position, .targetPosition = 0.0f};
            what = "driving it shut";
            break;
        case DoorMotor::Shut:
            m_doorMotor = DoorMotor::Off;
            motor = {};
            what = "off";
            break;
        }
        app.Logger()->Info(std::string("Door motor ") + what +
                           (asleep ? " (the door was asleep)" : " (the door was awake)"));
    }

    // Eight links laid out level from a hook fixed to the world, so they fall and swing at once. Each joint is on its
    // own entity beneath "Chain joints"; the top one joins its link to the world.
    void MakeChain(Fenrir::EntityList& entities)
    {
        Box(entities, "Hook (a world point, no body)", hook, Fenrir::Math::Vec3(0.12f), Fenrir::Math::Vec3(0.9f));
        Fenrir::Entity links = entities.CreateEntity();
        links.GetComponent<Fenrir::Name>().Set("Chain");
        Fenrir::Entity jointsRoot = entities.CreateEntity();
        jointsRoot.GetComponent<Fenrir::Name>().Set("Chain joints");

        // Not constexpr: MSVC does not take glm's vec3 constructor as one.
        static const std::array<Fenrir::Math::Vec3, 2> colours = {Fenrir::Math::Vec3(0.85f, 0.25f, 0.2f),
                                                                  Fenrir::Math::Vec3(0.95f, 0.8f, 0.2f)};
        Fenrir::Entity above; // the world, for the top link
        for (int i = 0; i < linkCount; ++i)
        {
            const float start = hook.x + linkSpacing * static_cast<float>(i);
            Fenrir::Entity link =
                Box(entities, ("Link " + std::to_string(i)).c_str(),
                    Fenrir::Math::Vec3(start + linkSpacing / 2.0f, hook.y, hook.z),
                    Fenrir::Math::Vec3(linkLength, 0.15f, 0.15f), colours[static_cast<std::size_t>(i % 2)]);
            link.AddComponent<Fenrir::RigidBody>();
            link.AddComponent<Fenrir::Collider>();
            links.AddChild(link);

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
            m_joints.push_back(Labelled{joint, "Chain joint " + std::to_string(i)});
            if (i > 0)
            {
                m_linkJoints.push_back(joint);
            }
            above = link;
        }
    }

    const Model* m_cube;
    const Shader* m_flatShader;
    std::string m_scene;
    Fenrir::Entity m_door;
    Fenrir::Entity m_doorTrigger;
    DoorMotor m_doorMotor = DoorMotor::Off;
    bool m_pushOpen = false;
    std::vector<Fenrir::Entity> m_linkJoints;
    std::vector<Labelled> m_joints;
    bool m_linksCollide = false;
    Fenrir::EventReader<KeyboardKeyEvent> m_keys;
    Fenrir::EventReader<Fenrir::TriggerEnter> m_enters;
};

#endif // FENRIR_PHYSICS
