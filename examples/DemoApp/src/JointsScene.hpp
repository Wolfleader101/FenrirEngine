#pragma once

// The Joints (C++) scene, built in C++ through FenrirPhysics's joints (ADR-0013): a door hinged to a static post, and a
// chain of eight links hanging from a point fixed to the world. F4 steps to it from Default and back.
//
// The door holds its own hinge, its post as bodyA, so its opening angle reads positive. Each chain joint is on its own
// entity, the top one joined to the world (an empty bodyB). Every frame is made with FrameFromWorld.

#ifdef FENRIR_PHYSICS

#include <array>
#include <cstddef>
#include <numbers>
#include <string>

#include "DemoScenes.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Physics.hpp"
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
                             "The door swings on its hinge, the chain hangs from its hook. F2 cycles the physics "
                             "debug lines, F4 goes on.",
                         .smokeKeys = {}};
    }

    JointsScene(const Model& cube, const Shader& flatShader) : m_cube(&cube), m_flatShader(&flatShader)
    {
    }

    // PostInit: the cube and the flat shader are loaded in Init. The scene stays inactive until the switcher steps to
    // it.
    void Init(Fenrir::App& app)
    {
        Fenrir::EntityList& entities = app.CreateScene(name).GetEntityList();

        // Static, its top face at y = -3.75.
        Fenrir::Entity ground = Box(entities, "Ground", Fenrir::Math::Vec3(0.0f, -4.0f, -12.0f),
                                    Fenrir::Math::Vec3(16.0f, 0.5f, 16.0f), Fenrir::Math::Vec3(0.45f));
        ground.AddComponent<Fenrir::Collider>();

        MakeDoor(entities);
        MakeChain(entities);
    }

  private:
    static constexpr int linkCount = 8;
    // Joint to joint. Each link is a little shorter, so neighbours do not touch as the chain bends.
    static constexpr float linkSpacing = 0.5f;
    static constexpr float linkLength = 0.4f;
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
    // space: FrameFromWorld works it out. The door starts swinging open, and comes to rest against the frame.
    void MakeDoor(Fenrir::EntityList& entities) const
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

        // The hinge axis (frame X) turned onto world up, at the left post's inner face.
        const Fenrir::Math::Quat axisUp =
            Fenrir::Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
        const Fenrir::Math::Vec3 anchor = Fenrir::GetWorldTransform(frame).pos + Fenrir::Math::Vec3(-0.5f, 1.05f, 0.0f);
        const Fenrir::JointFrames frames = Fenrir::FrameFromWorld(post, door, Fenrir::JointFrame{anchor, axisUp});
        door.AddComponent<Fenrir::Joint>(Fenrir::Joint{.bodyA = post,
                                                       .bodyB = door,
                                                       .frameA = frames.frameA,
                                                       .frameB = frames.frameB,
                                                       .kind = Fenrir::HingeJoint{}});
    }

    // Eight links laid out level from a hook fixed to the world, so they fall and swing at once. Each joint is on its
    // own entity beneath "Chain joints"; the top one joins its link to the world.
    void MakeChain(Fenrir::EntityList& entities) const
    {
        Box(entities, "Hook (a world point, no body)", hook, Fenrir::Math::Vec3(0.12f), Fenrir::Math::Vec3(0.9f));
        Fenrir::Entity links = entities.CreateEntity();
        links.GetComponent<Fenrir::Name>().Set("Chain");
        Fenrir::Entity jointsRoot = entities.CreateEntity();
        jointsRoot.GetComponent<Fenrir::Name>().Set("Chain joints");

        static constexpr std::array<Fenrir::Math::Vec3, 2> colours = {Fenrir::Math::Vec3(0.85f, 0.25f, 0.2f),
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
            above = link;
        }
    }

    const Model* m_cube;
    const Shader* m_flatShader;
};

#endif // FENRIR_PHYSICS
