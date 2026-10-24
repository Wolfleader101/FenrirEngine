// Joints (C++), built in C++ through FenrirPhysics's joints (ADR-0013): a door hinged to a static post, and a chain of
// eight links hanging from a point fixed to the world.
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
// Each joint entity's name shows its joint state every frame, shown as screen text: the door's angle and load, each
// chain joint's load. 0 logs every joint's full state.
//
// B gives chain joint 1 a 15 N break force, and takes it away again. It holds the seven links below it, about 69 N, so
// it breaks on the next step: physics disables it and sends a JointBroken, which is logged, and its axes draw dimmed in
// the joints debug lines. R repairs every disabled joint; one still over its break force breaks again.
//
// 3 destroys chain link 3. The two joints naming it go inactive, one warning each, and draw grey on the links left
// with the Joints debug lines on (F2); the links below fall.
//
// 4 teleports the door alone, 0.7 m out of its frame and back: its hinge pulls it back without a jolt. 5 teleports the
// door and its frame together, a metre and back: they line up at once. 6 scales link 5 up by half and back: its
// joints' anchors stay on its ends. 7 adds a lump to the side of link 6, or removes it, rebuilding its body; 8 moves
// the lump to link 6's other side, reshaping it. Its centre of mass moves each time, and its anchors stay put on it.
//
// Joints (C#), its twin in the scene list, is built and run by scripts/Joints.cs; C++ gives it only its entry here.

#include <array>
#include <cstddef>
#include <format>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirScene/Scene.hpp"
#include "PhysicsScenes.hpp"
#include "SceneLooks.hpp"

namespace PhysicsDemo
{
    namespace
    {
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Joints (C++)";

        class JointsScene
        {
          public:
            void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
            {
                // A new entry starts the scene afresh; the readers keep their place, so nothing older is read again.
                const auto enters = m_enters;
                const auto breaks = m_breaks;
                *this = JointsScene{};
                m_enters = enters;
                m_breaks = breaks;
                m_entities = &scene.GetEntityList();
                m_looks.emplace(app, scene);

                // Static, its top face at y = -3.75.
                Fenrir::Entity ground = Box("Ground", Vec3(0.0f, -4.0f, -12.0f), Vec3(16.0f, 0.5f, 16.0f), Vec3(0.45f));
                ground.AddComponent<Fenrir::Collider>();

                MakeDoor();
                MakeChain();
            }

            // Update, every frame: 1, 2 and L work the door, 9 toggles collideConnected on every chain joint between
            // two links, 0 logs every joint's state, B and R break and repair chain joint 1, 4 to 8 teleport, scale and
            // reshape, 3 destroys chain link 3, each TriggerEnter of the door's trigger and each JointBroken is logged,
            // and each joint entity's name shows its readings. Events sent while another scene is active are read and
            // dropped.
            void Update(Fenrir::App& app)
            {
                const bool active = app.GetActiveScene().GetName() == SceneName && m_entities != nullptr;
                if (active)
                {
                    OnKeys(app, app.GetResource<Fenrir::Keyboard>());
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
                for (const Fenrir::JointBroken& broken : m_breaks.Read(app))
                {
                    if (active)
                    {
                        app.Logger()->Info(LabelOf(broken.joint) + " broke at " +
                                           std::format("{:.1f} N, {:.1f} N m",
                                                       static_cast<double>(Fenrir::Math::Magnitude(broken.force)),
                                                       static_cast<double>(Fenrir::Math::Magnitude(broken.torque))));
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

            // The door's motor settings, cycled through by 2.
            enum class DoorMotor
            {
                Off,
                Opening,
                Closing,
                Shut
            };

            static constexpr int linkCount = 8;
            // The link 3 destroys.
            static constexpr std::size_t destroyedLink = 3;
            // Joint to joint. Each link is a little longer, so neighbours overlap at their joints.
            static constexpr float linkSpacing = 0.5f;
            static constexpr float linkLength = 0.6f;
            // The link 6 scales, and the one 7 and 8 reshape.
            static constexpr std::size_t scaledLink = 5;
            static constexpr std::size_t lumpedLink = 6;
            // Where 7's lump sits against link 6's side, in its scaled space: the link's half thickness and the lump's.
            static constexpr float lumpSide = (0.075f + 0.125f) / 0.15f;
            inline static const Vec3 linkSize = Vec3(linkLength, 0.15f, 0.15f);
            inline static const Vec3 lumpSize = Vec3(0.25f);
            inline static const Vec3 hook = Vec3(1.5f, 2.5f, -12.0f);
            inline static const Fenrir::Math::Quat identity = Fenrir::Math::Quat(1.0f, 0.0f, 0.0f, 0.0f);

            static std::string Describe(const Vec3& v)
            {
                return std::format("({:.2f}, {:.2f}, {:.2f})", v.x, v.y, v.z);
            }

            std::string LabelOf(const Fenrir::Entity& joint) const
            {
                for (const Labelled& labelled : m_joints)
                {
                    if (labelled.joint == joint)
                    {
                        return labelled.label;
                    }
                }
                return "A joint";
            }

            void OnKeys(Fenrir::App& app, const Fenrir::Keyboard& keyboard)
            {
                if (keyboard.JustPressed(Fenrir::Key::Num1))
                {
                    PushDoor(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::Num2))
                {
                    CycleDoorMotor(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::L))
                {
                    Fenrir::JointLimits& limits = DoorHinge().limits;
                    limits.enabled = !limits.enabled;
                    app.Logger()->Info(limits.enabled ? "Door limits on"
                                                      : "Door limits off: it swings all the way round");
                }
                if (keyboard.JustPressed(Fenrir::Key::Num9))
                {
                    m_linksCollide = !m_linksCollide;
                    for (const Fenrir::Entity& joint : m_linkJoints)
                    {
                        joint.GetComponent<Fenrir::Joint>().collideConnected = m_linksCollide;
                    }
                    app.Logger()->Info(m_linksCollide ? "Chain links collide with their neighbours"
                                                      : "Chain links pass through their neighbours");
                }
                if (keyboard.JustPressed(Fenrir::Key::Num3))
                {
                    DestroyLink(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::Num0))
                {
                    LogJointStates(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::B))
                {
                    ToggleBreakable(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::R))
                {
                    RepairJoints(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::Num4))
                {
                    TeleportDoor(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::Num5))
                {
                    TeleportDoorFrame(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::Num6))
                {
                    ScaleLink(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::Num7))
                {
                    ToggleLump(app);
                }
                if (keyboard.JustPressed(Fenrir::Key::Num8))
                {
                    MoveLump(app);
                }
            }

            // B: a 15 N break force on chain joint 1, or none.
            void ToggleBreakable(Fenrir::App& app)
            {
                m_breakable = !m_breakable;
                m_linkJoints.front().GetComponent<Fenrir::Joint>().breakForce =
                    m_breakable ? 15.0f : std::numeric_limits<float>::infinity();
                app.Logger()->Info(m_breakable ? "Chain joint 1 breaks over 15 N" : "Chain joint 1 never breaks");
            }

            // R: enables every disabled joint again.
            void RepairJoints(Fenrir::App& app)
            {
                int repaired = 0;
                for (const Labelled& labelled : m_joints)
                {
                    Fenrir::Joint& joint = labelled.joint.GetComponent<Fenrir::Joint>();
                    if (!joint.enabled)
                    {
                        joint.enabled = true;
                        ++repaired;
                    }
                }
                app.Logger()->Info("Repaired " + std::to_string(repaired) + (repaired == 1 ? " joint" : " joints"));
            }

            // 4: the door alone, out of its frame and back, by writing its Transform.
            void TeleportDoor(Fenrir::App& app)
            {
                m_doorOut = !m_doorOut;
                m_door.GetComponent<Fenrir::Transform>().pos.z += m_doorOut ? 0.7f : -0.7f;
                app.Logger()->Info(m_doorOut ? "Door teleported 0.7 m out of its frame: its hinge pulls it back"
                                             : "Door teleported 0.7 m back: its hinge pulls it back");
            }

            // 5: the door frame, and with it its posts and the door beneath it, a metre and back.
            void TeleportDoorFrame(Fenrir::App& app)
            {
                m_frameMoved = !m_frameMoved;
                m_doorFrame.GetComponent<Fenrir::Transform>().pos.z += m_frameMoved ? 1.0f : -1.0f;
                app.Logger()->Info(m_frameMoved ? "Door and frame teleported a metre together: they line up at once"
                                                : "Door and frame teleported a metre back together");
            }

            // 6: link 5 half as large again, and back. Its joints' frames follow its scale, so they stay on its ends.
            void ScaleLink(Fenrir::App& app)
            {
                m_linkScaled = !m_linkScaled;
                m_links.at(scaledLink).GetComponent<Fenrir::Transform>().scale =
                    linkSize * (m_linkScaled ? 1.5f : 1.0f);
                app.Logger()->Info(m_linkScaled ? "Link 5 scaled up by half: its anchors stay on its ends"
                                                : "Link 5 back to its size");
            }

            // 7: a lump on the side of link 6, a child collider making it a compound body with its centre of mass off
            // its middle, or none. Either way its body is rebuilt, and its joints with it.
            void ToggleLump(Fenrir::App& app)
            {
                if (m_lump.IsValid())
                {
                    m_entities->DestroyEntity(m_lump);
                    m_lump = Fenrir::Entity();
                    app.Logger()->Info("Link 6's lump removed: its anchors stay on its ends");
                    return;
                }
                // In link 6's scaled space: a 0.25 m cube against its side.
                m_lump = Box("Lump", Vec3(0.0f, 0.0f, lumpSide), lumpSize / linkSize, Vec3(0.3f, 0.3f, 0.85f));
                m_lump.AddComponent<Fenrir::Collider>();
                m_links.at(lumpedLink).AddChild(m_lump);
                app.Logger()->Info("Link 6 has a lump on its side: its anchors stay on its ends");
            }

            // 8: link 6's lump to its other side, reshaping its compound body, which keeps its backend body.
            void MoveLump(Fenrir::App& app) const
            {
                if (!m_lump.IsValid())
                {
                    app.Logger()->Info("Link 6 has no lump to move: 7 adds one");
                    return;
                }
                float& side = m_lump.GetComponent<Fenrir::Transform>().pos.z;
                side = -side;
                app.Logger()->Info("Link 6's lump moved to its other side: its anchors stay on its ends");
            }

            // The joints' readings in their names: the door's angle, and how hard each joint pulls; "off" once
            // disabled. Shown as screen text, where the legacy editor's hierarchy showed them.
            void ShowReadings(Fenrir::App& app) const
            {
                Fenrir::Scene& scene = app.GetActiveScene();
                const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(scene);
                if (world == nullptr)
                {
                    return;
                }
                std::vector<Fenrir::Entity> shown;
                for (const Labelled& labelled : m_joints)
                {
                    const Fenrir::JointState state = world->GetJointState(labelled.joint);
                    const double force = static_cast<double>(Fenrir::Math::Magnitude(state.force));
                    const Fenrir::Joint& joint = labelled.joint.GetComponent<Fenrir::Joint>();
                    const std::string reading = !joint.enabled ? labelled.label + ": off"
                                                : joint.kind.Is<Fenrir::HingeJoint>()
                                                    ? std::format("{}: {:.2f} rad, {:.0f} N", labelled.label,
                                                                  static_cast<double>(state.position), force)
                                                    : std::format("{}: {:.1f} N", labelled.label, force);
                    labelled.joint.GetComponent<Fenrir::Name>().Set(reading.c_str());
                    shown.push_back(labelled.joint);
                }
                ShowReadouts(scene, shown);
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
                                       std::format(" turned (w {:.3f}, x {:.3f}, y {:.3f}, z {:.3f}), position {:.3f}",
                                                   static_cast<double>(turn.w), static_cast<double>(turn.x),
                                                   static_cast<double>(turn.y), static_cast<double>(turn.z),
                                                   static_cast<double>(state.position)));
                }
            }

            // A unit cube scaled to `size`, in a flat colour. Its collider, if any, is the default unit box, scaled
            // too.
            Fenrir::Entity Box(const char* boxName, const Vec3& position, const Vec3& size, const Vec3& colour)
            {
                Fenrir::Entity box = m_entities->CreateEntity();
                box.GetComponent<Fenrir::Name>().Set(boxName);
                box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, size);
                m_looks->Cube(box, colour);
                return box;
            }

            // A static frame (two posts and a lintel under a "Door frame" entity) and a dynamic door beneath it,
            // hinged on its left edge to the left post. The post is scaled (0.2, 2.2, 0.2), so the hinge's frame on it
            // is in that scaled space: FrameFromWorld works it out. The door starts swinging open, and comes to rest
            // against the frame. A small trigger beneath the door, at its hinge edge, overlaps the post.
            void MakeDoor()
            {
                Fenrir::Entity frame = m_entities->CreateEntity();
                frame.GetComponent<Fenrir::Name>().Set("Door frame");
                frame.GetComponent<Fenrir::Transform>().pos = Vec3(-3.5f, -3.75f, -12.0f);
                m_doorFrame = frame;

                const Vec3 wood(0.55f, 0.35f, 0.2f);
                Fenrir::Entity post = Box("Post (hinge)", Vec3(-0.6f, 1.1f, 0.0f), Vec3(0.2f, 2.2f, 0.2f), wood);
                post.AddComponent<Fenrir::Collider>();
                frame.AddChild(post);
                Fenrir::Entity right = Box("Post", Vec3(0.6f, 1.1f, 0.0f), Vec3(0.2f, 2.2f, 0.2f), wood);
                right.AddComponent<Fenrir::Collider>();
                frame.AddChild(right);
                Fenrir::Entity lintel = Box("Lintel", Vec3(0.0f, 2.3f, 0.0f), Vec3(1.4f, 0.2f, 0.2f), wood);
                lintel.AddComponent<Fenrir::Collider>();
                frame.AddChild(lintel);

                Fenrir::Entity door =
                    Box("Door", Vec3(-0.01f, 1.05f, 0.0f), Vec3(0.96f, 2.0f, 0.08f), Vec3(0.2f, 0.75f, 0.25f));
                Fenrir::RigidBody doorBody;
                doorBody.mass = 10.0f;
                doorBody.angularVelocity = Vec3(0.0f, 1.5f, 0.0f);
                door.AddComponent<Fenrir::RigidBody>(doorBody);
                door.AddComponent<Fenrir::Collider>();
                frame.AddChild(door);

                // On the door's hinge edge, its scale undoing the door's so the sphere stays round, reaching into the
                // post.
                m_doorTrigger = m_entities->CreateEntity();
                m_doorTrigger.GetComponent<Fenrir::Name>().Set("Door trigger");
                door.AddChild(m_doorTrigger);
                m_doorTrigger.GetComponent<Fenrir::Transform>() =
                    Fenrir::Transform(Vec3(-0.5f, 0.0f, 0.0f), identity, Vec3(1.0f / 0.96f, 1.0f / 2.0f, 1.0f / 0.08f));
                m_doorTrigger.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::SphereShape{0.12f}});
                m_doorTrigger.AddComponent<Fenrir::Trigger>();

                // The hinge axis (frame X) turned onto world up, at the left post's inner face.
                const Fenrir::Math::Quat axisUp =
                    Fenrir::Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Vec3(0.0f, 0.0f, 1.0f));
                const Vec3 anchor = Fenrir::GetWorldTransform(frame).pos + Vec3(-0.5f, 1.05f, 0.0f);
                const Fenrir::JointFrames frames =
                    Fenrir::FrameFromWorld(post, door, Fenrir::JointFrame{anchor, axisUp});
                // Shut to 100 degrees open, and stiff enough to stay where it is left once it slows.
                door.AddComponent<Fenrir::Joint>(
                    Fenrir::Joint{.bodyA = post,
                                  .bodyB = door,
                                  .frameA = frames.frameA,
                                  .frameB = frames.frameB,
                                  .kind = Fenrir::HingeJoint{.limits = {.enabled = true, .min = 0.0f, .max = 1.75f},
                                                             .friction = 3.0f}});
                m_door = door;
                m_joints.push_back(Labelled{door, "Door hinge"});
            }

            [[nodiscard]] Fenrir::HingeJoint& DoorHinge() const
            {
                return m_door.GetComponent<Fenrir::Joint>().kind.As<Fenrir::HingeJoint>();
            }

            // 1: an angular impulse about the hinge, opening and closing in turn: about 2 rad/s.
            void PushDoor(Fenrir::App& app)
            {
                m_pushOpen = !m_pushOpen;
                if (Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene()))
                {
                    world->AddAngularImpulse(m_door, Vec3(0.0f, m_pushOpen ? 6.0f : -6.0f, 0.0f));
                }
                app.Logger()->Info(m_pushOpen ? "Door pushed open" : "Door pushed shut");
            }

            // 2: the next motor setting, with no limit on its torque. Logged with whether the door was asleep: the edit
            // wakes it.
            void CycleDoorMotor(Fenrir::App& app)
            {
                const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
                const bool asleep = world != nullptr && world->IsSleeping(m_door);
                Fenrir::JointMotor& motor = DoorHinge().motor;
                const char* what = "";
                if (m_doorMotor == DoorMotor::Off)
                {
                    m_doorMotor = DoorMotor::Opening;
                    motor = {.mode = Fenrir::JointMotorMode::Velocity, .targetVelocity = 1.0f};
                    what = "opening at 1 rad/s";
                }
                else if (m_doorMotor == DoorMotor::Opening)
                {
                    m_doorMotor = DoorMotor::Closing;
                    motor = {.mode = Fenrir::JointMotorMode::Velocity, .targetVelocity = -1.0f};
                    what = "closing at 1 rad/s";
                }
                else if (m_doorMotor == DoorMotor::Closing)
                {
                    m_doorMotor = DoorMotor::Shut;
                    motor = {.mode = Fenrir::JointMotorMode::Position, .targetPosition = 0.0f};
                    what = "driving it shut";
                }
                else
                {
                    m_doorMotor = DoorMotor::Off;
                    motor = {};
                    what = "off";
                }
                app.Logger()->Info(std::string("Door motor ") + what +
                                   (asleep ? " (the door was asleep)" : " (the door was awake)"));
            }

            // 3: chain link 3 destroyed, once. Its joints keep their Joint components and go inactive.
            void DestroyLink(Fenrir::App& app) const
            {
                const Fenrir::Entity& link = m_links.at(destroyedLink);
                if (!link.IsValid())
                {
                    app.Logger()->Info("Chain link " + std::to_string(destroyedLink) + " is already destroyed");
                    return;
                }
                m_entities->DestroyEntity(link);
                app.Logger()->Info("Chain link " + std::to_string(destroyedLink) +
                                   " destroyed: the joints above and below it are inactive, and the links below fall");
            }

            // Eight links laid out level from a hook fixed to the world, so they fall and swing at once. Each joint is
            // on its own entity beneath "Chain joints"; the top one joins its link to the world.
            void MakeChain()
            {
                Box("Hook (a world point, no body)", hook, Vec3(0.12f), Vec3(0.9f));
                Fenrir::Entity links = m_entities->CreateEntity();
                links.GetComponent<Fenrir::Name>().Set("Chain");
                Fenrir::Entity jointsRoot = m_entities->CreateEntity();
                jointsRoot.GetComponent<Fenrir::Name>().Set("Chain joints");

                const std::array<Vec3, 2> colours = {Vec3(0.85f, 0.25f, 0.2f), Vec3(0.95f, 0.8f, 0.2f)};
                Fenrir::Entity above; // the world, for the top link
                for (int i = 0; i < linkCount; ++i)
                {
                    const float start = hook.x + linkSpacing * static_cast<float>(i);
                    Fenrir::Entity link =
                        Box(("Link " + std::to_string(i)).c_str(), Vec3(start + linkSpacing / 2.0f, hook.y, hook.z),
                            linkSize, colours.at(static_cast<std::size_t>(i % 2)));
                    link.AddComponent<Fenrir::RigidBody>();
                    link.AddComponent<Fenrir::Collider>();
                    links.AddChild(link);
                    m_links.push_back(link);

                    Fenrir::Entity joint = m_entities->CreateEntity();
                    joint.GetComponent<Fenrir::Name>().Set(("Chain joint " + std::to_string(i)).c_str());
                    const Fenrir::JointFrames frames =
                        Fenrir::FrameFromWorld(link, above, Fenrir::JointFrame{Vec3(start, hook.y, hook.z)});
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

            Fenrir::EntityList* m_entities = nullptr;
            std::optional<SceneLooks> m_looks;
            Fenrir::Entity m_door;
            Fenrir::Entity m_doorTrigger;
            Fenrir::Entity m_doorFrame;
            bool m_doorOut = false;
            bool m_frameMoved = false;
            bool m_linkScaled = false;
            Fenrir::Entity m_lump;
            DoorMotor m_doorMotor = DoorMotor::Off;
            bool m_pushOpen = false;
            std::vector<Fenrir::Entity> m_links;
            std::vector<Fenrir::Entity> m_linkJoints;
            std::vector<Labelled> m_joints;
            bool m_linksCollide = false;
            bool m_breakable = false;
            Fenrir::EventReader<Fenrir::TriggerEnter> m_enters;
            Fenrir::EventReader<Fenrir::JointBroken> m_breaks;
        };
    } // namespace

    FenrirDemo::DemoScene Joints(Fenrir::App& app)
    {
        auto joints = std::make_shared<JointsScene>();
        app.AddSequentialSystem(Fenrir::SchedulePriority::Update,
                                [joints](Fenrir::App& running) { joints->Update(running); });

        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp =
            "The door swings on its hinge, the chain hangs from its hook. 1 pushes the door, 2 cycles its "
            "motor (off, opening, closing, to shut), L turns its limits off and on. 9 lets the links "
            "collide with their neighbours, and back. 0 logs every joint's state. B gives chain joint 1 a "
            "15 N break force, and back; R repairs broken joints. 4 teleports the door alone, 5 the door and "
            "its frame. 6 scales link 5, 7 adds or removes a lump on link 6, 8 moves it. 3 destroys chain "
            "link 3.";
        using Fenrir::Key;
        scene.keys = {Key::Num9, Key::Num1, Key::Num2, Key::Num2, Key::L,    Key::Num2, Key::Num2, Key::L,    Key::Num0,
                      Key::B,    Key::R,    Key::B,    Key::R,    Key::Num4, Key::Num4, Key::Num5, Key::Num5, Key::Num6,
                      Key::Num6, Key::Num8, Key::Num7, Key::Num8, Key::Num8, Key::Num7, Key::Num3, Key::Num0};
        scene.camera.position = {0.0f, 0.5f, 0.0f};
        scene.camera.pitch = -0.2f;
        scene.spawn = [joints](Fenrir::App& running, Fenrir::Scene& spawned) { joints->Spawn(running, spawned); };
        return scene;
    }

    FenrirDemo::DemoScene JointsCSharp()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Joints (C#)";
        scene.keyHelp = "A bridge, a lift, a spring rope and a limb, all built by C#. B drops a weight on the bridge, "
                        "whose middle hinge breaks over 250 N; R repairs broken joints. K swaps the middle joint "
                        "between hinge and fixed. 1 cycles the lift's motor (off, up, down, half way), 2 raises the "
                        "limb and lowers it. 0 logs every joint's state.";
        using Fenrir::Key;
        scene.keys = {Key::Num0, Key::K,    Key::K,    Key::B,    Key::Num0, Key::R,   Key::Num1,
                      Key::Num1, Key::Num1, Key::Num1, Key::Num2, Key::Num2, Key::Num0};
        scene.camera.position = {0.0f, 0.5f, 0.0f};
        scene.camera.pitch = -0.2f;
        scene.csharpScene = JointsCSharpScene;
        return scene;
    }
} // namespace PhysicsDemo
