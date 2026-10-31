// Ragdolls (spike): the physics map's ticket 55, a throwaway scene checking the ragdoll model of ADR-0053 and ADR-0054
// by hand, through FenrirRagdoll's proposed interface.
//
// A Quaternius human (AnimationDemo's, copied into this app's assets) walks a lane along x, tick-posed, playing its
// walk clip; a capsule CharacterBody carries it (game code moves the model root with the character each tick, as a game
// would), and both share one collision group, so the character and its own ragdoll never touch. Its ragdoll is derived
// by DescribeHumanoid from a hand-filled bone map and built by BuildRagdoll: fifteen kinematic capsules following the
// simulation pose as hit boxes (F2 cycles to the collider lines to see them), joined by hinges at the elbows and knees
// and swing-twist joints elsewhere.
//
// Space drops it: the ragdoll goes Simulating, every bone's override weight goes to 1, the chest takes an AddImpulse
// shove from behind, and the character is disabled. A makes the left arm limp (a partial ragdoll: the arm's three
// bodies Simulating under the following shoulder) and back, blending its weights down. K powers the ragdoll towards the
// walk at the current strength (1 and 2 lower and raise it). G gets up: Following again, the weights ramped down
// over 1.5 s by this demo's code, the character teleported under where the skeleton entity followed the hips to and
// enabled. 3 drops the second human, standing to one side in a group of its own, towards the first; B drops a crate on
// the walker's head; crates and a low wall stand in the lane to fall onto. L logs the state; P runs the sequence by
// itself.
//
// Readouts are in entity names, shown as screen text.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAnimation/Animator.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirPhysics/Characters.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/Ragdoll.hpp"
#include "FenrirRagdoll/RagdollDescription.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/SpawnModel.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "PhysicsScenes.hpp"
#include "SceneLooks.hpp"

namespace PhysicsDemo
{
    namespace
    {
        using Fenrir::Entity;
        using Fenrir::Math::Quat;
        using Fenrir::Math::Vec3;

        constexpr const char* SceneName = "Ragdolls (spike)";
        constexpr std::string_view HumanFile = "quaternius/rig-human.glb";
        constexpr std::string_view WalkFile = "quaternius/walk_loop.glb";
        constexpr std::string_view IdleFile = "quaternius/idle_loop.glb";

        /** @brief The character capsule under the walker: 1.6 m tall, so its centre is 0.8 m up. */
        constexpr float CapsuleHalfHeight = 0.5f;
        constexpr float CapsuleRadius = 0.3f;
        constexpr float FootToCentre = CapsuleHalfHeight + CapsuleRadius;
        constexpr float WalkSpeed = 1.0f;
        constexpr float LaneHalfLength = 3.0f;
        /** @brief The shove Space gives the chest, in newton seconds, and 3 the second human. */
        constexpr float Shove = 120.0f;
        constexpr float GetUpSeconds = 1.5f;
        constexpr float ArmBlendSeconds = 1.0f;

        const Quat identity(1.0f, 0.0f, 0.0f, 0.0f);

        /** @brief The Quaternius human's bones in the humanoid slots. */
        Fenrir::HumanoidBones QuaterniusBones()
        {
            Fenrir::HumanoidBones bones;
            bones.hips = "pelvis";
            bones.spine = "spine_01";
            bones.chest = "spine_03";
            bones.head = "Head";
            bones.leftUpperArm = "upperarm_l";
            bones.leftLowerArm = "lowerarm_l";
            bones.leftHand = "hand_l";
            bones.rightUpperArm = "upperarm_r";
            bones.rightLowerArm = "lowerarm_r";
            bones.rightHand = "hand_r";
            bones.leftUpperLeg = "thigh_l";
            bones.leftLowerLeg = "calf_l";
            bones.leftFoot = "foot_l";
            bones.rightUpperLeg = "thigh_r";
            bones.rightLowerLeg = "calf_r";
            bones.rightFoot = "foot_r";
            return bones;
        }

        /** @brief A skinned human: its model root, its skeleton entity and its ragdoll. */
        struct Human
        {
            Entity root;
            Entity skeleton;
            /** @brief The skeleton entity's Transform under the root at spawn, to put back on getting up. */
            Fenrir::Transform skeletonLocal;
            Fenrir::SpawnedRagdoll ragdoll;
            std::int32_t hipsBone = Fenrir::NoBone;
            std::int32_t leftShoulderBone = Fenrir::NoBone;

            [[nodiscard]] bool IsValid() const
            {
                return root.IsValid() && skeleton.IsValid() && !ragdoll.bodies.empty();
            }

            [[nodiscard]] Entity Body(std::string_view bone) const
            {
                for (const Entity& body : ragdoll.bodies)
                {
                    if (body.GetComponent<Fenrir::RagdollBody>().bone.Get() == bone)
                    {
                        return body;
                    }
                }
                return {};
            }
        };

        void Collect(const Entity& entity, Entity& skeleton)
        {
            if (entity.HasComponent<Fenrir::SkeletonPose>() && !skeleton.IsValid())
            {
                skeleton = entity;
            }
            entity.ForEachChild([&](const Entity& child) { Collect(child, skeleton); });
        }

        class RagdollScene
        {
          public:
            void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
            {
                *this = RagdollScene();
                m_entities = &scene.GetEntityList();
                m_looks.emplace(app, scene);
                BuildLevel();
                m_walker = SpawnHuman(app, scene, "Walker", Vec3(-LaneHalfLength, 0.0f, 0.0f),
                                      0.5f * std::numbers::pi_v<float>, WalkFile);
                m_other = SpawnHuman(app, scene, "Bystander", Vec3(0.0f, 0.0f, -2.5f), 0.0f, IdleFile);
                if (m_walker.IsValid())
                {
                    MakeCharacter();
                }
                m_readout = m_entities->CreateEntity();
                m_readout.GetComponent<Fenrir::Name>().Set(
                    "Ragdolls (spike): Space drops, A limp arm, K powered, G up");
            }

            void Update(Fenrir::App& app)
            {
                if (!IsActive(app) || !m_walker.IsValid())
                {
                    return;
                }
                const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
                using Fenrir::Key;
                for (const Key key :
                     {Key::Space, Key::A, Key::K, Key::G, Key::Num1, Key::Num2, Key::Num3, Key::B, Key::L, Key::P})
                {
                    if (keyboard.JustPressed(key))
                    {
                        OnKey(app, key);
                    }
                }
                const float dt = static_cast<float>(app.GetTime().deltaTime);
                Fenrir::Scene& scene = app.GetActiveScene();
                RampWeights(scene, dt);
                if (m_autopilot)
                {
                    Autopilot(app, dt);
                }
                ShowReadings(scene);
            }

            void Tick(Fenrir::App& app)
            {
                if (!IsActive(app) || !m_walker.IsValid() || !m_character.IsValid())
                {
                    return;
                }
                const Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(app.GetActiveScene());
                if (world == nullptr)
                {
                    return;
                }
                const float dt = static_cast<float>(app.GetTime().tickRate);
                Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
                if (!body.enabled)
                {
                    return; // the ragdoll has the body: game code leaves the character and the model root alone
                }
                // Walks the lane, turning at each end, as a game would drive it; the model root rides the character.
                Fenrir::Transform& character = m_character.GetComponent<Fenrir::Transform>();
                if (character.pos.x > LaneHalfLength)
                {
                    m_heading = -1.0f;
                }
                else if (character.pos.x < -LaneHalfLength)
                {
                    m_heading = 1.0f;
                }
                body.velocity.x = m_heading * WalkSpeed;
                body.velocity.z = 0.0f;
                body.velocity.y += world->Gravity().y * dt;
                // The human's model faces +Z: turned to face along its heading.
                const Quat facing = Fenrir::Math::AngleAxis(m_heading > 0.0f ? 0.5f * std::numbers::pi_v<float>
                                                                             : -0.5f * std::numbers::pi_v<float>,
                                                            Vec3(0.0f, 1.0f, 0.0f));
                Fenrir::Transform& root = m_walker.root.GetComponent<Fenrir::Transform>();
                root.pos = character.pos - Vec3(0.0f, FootToCentre, 0.0f);
                root.rot = facing;
            }

          private:
            [[nodiscard]] bool IsActive(Fenrir::App& app) const
            {
                return m_entities != nullptr && app.GetActiveScene().GetName() == SceneName;
            }

            Entity Box(const char* boxName, const Vec3& position, const Vec3& size, const Vec3& colour)
            {
                Entity box = m_entities->CreateEntity();
                box.GetComponent<Fenrir::Name>().Set(boxName);
                box.GetComponent<Fenrir::Transform>() = Fenrir::Transform(position, identity, size);
                m_looks->Cube(box, colour);
                return box;
            }

            // A ground, a low wall across the far end of the lane to fall over, and crates to knock about.
            void BuildLevel()
            {
                Box("Ground", Vec3(0.0f, -0.5f, 0.0f), Vec3(40.0f, 1.0f, 40.0f), Vec3(0.45f))
                    .AddComponent<Fenrir::Collider>();
                Box("Lane", Vec3(0.0f, 0.005f, 0.0f), Vec3(2.0f * LaneHalfLength + 1.0f, 0.01f, 1.2f), Vec3(0.6f));
                Box("Low wall", Vec3(LaneHalfLength + 1.2f, 0.25f, 0.0f), Vec3(0.4f, 0.5f, 3.0f),
                    Vec3(0.55f, 0.4f, 0.3f))
                    .AddComponent<Fenrir::Collider>();
                for (int i = 0; i < 3; ++i)
                {
                    Entity crate = Box("Crate", Vec3(1.0f, 0.2f + 0.4f * static_cast<float>(i), 1.0f), Vec3(0.4f),
                                       Vec3(0.8f, 0.6f, 0.3f));
                    crate.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 5.0f});
                    crate.AddComponent<Fenrir::Collider>();
                }
            }

            Human SpawnHuman(Fenrir::App& app, Fenrir::Scene& scene, const char* humanName, const Vec3& position,
                             float yaw, std::string_view clipFile)
            {
                Human human;
                Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
                const auto model = store.Load<Fenrir::Model>(HumanFile, scene);
                if (!model.Ok())
                {
                    app.Logger()->Error("Ragdolls (spike): {} did not load from the asset root", HumanFile);
                    return {};
                }
                human.root = Fenrir::SpawnModel(app, scene, model.handle);
                if (!human.root.IsValid())
                {
                    app.Logger()->Error("Ragdolls (spike): {} did not spawn", HumanFile);
                    return {};
                }
                human.root.GetComponent<Fenrir::Name>().Set(humanName);
                Fenrir::Transform& transform = human.root.GetComponent<Fenrir::Transform>();
                transform.pos = position;
                transform.rot = Fenrir::Math::AngleAxis(yaw, Vec3(0.0f, 1.0f, 0.0f));
                Collect(human.root, human.skeleton);
                if (!human.skeleton.IsValid())
                {
                    app.Logger()->Error("Ragdolls (spike): {} spawned no skeleton entity", HumanFile);
                    return {};
                }
                human.skeletonLocal = human.skeleton.GetComponent<Fenrir::Transform>();
                // Tick-posed, as a ragdoll requires, playing its clip.
                human.skeleton.GetComponent<Fenrir::SkeletonPose>().timing = Fenrir::PoseTiming::Tick;
                const auto clipModel = store.Load<Fenrir::Model>(clipFile, scene);
                const Fenrir::Model* clipData = clipModel.Ok() ? store.TryGet(clipModel.handle) : nullptr;
                if (clipData == nullptr || clipData->clips.empty())
                {
                    app.Logger()->Error("Ragdolls (spike): {} has no clip", clipFile);
                }
                else
                {
                    Fenrir::Animator animator;
                    animator.clip = clipData->clips.front();
                    human.skeleton.AddComponent<Fenrir::Animator>(animator);
                }

                const Fenrir::SkeletonData* data =
                    store.TryGet(human.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton);
                if (data == nullptr)
                {
                    app.Logger()->Error("Ragdolls (spike): the human's skeleton asset did not resolve");
                    return {};
                }
                const Fenrir::RagdollDescription description = Fenrir::DescribeHumanoid(*data, QuaterniusBones());
                if (!description.error.empty())
                {
                    app.Logger()->Error("Ragdolls (spike): {}", description.error);
                    return {};
                }
                human.ragdoll = Fenrir::BuildRagdoll(app, scene, human.skeleton, description);
                if (!human.ragdoll.error.empty())
                {
                    app.Logger()->Error("Ragdolls (spike): {}", human.ragdoll.error);
                    return {};
                }
                human.hipsBone = Fenrir::Skeletons::FindBone(scene, human.skeleton, "pelvis");
                human.leftShoulderBone = Fenrir::Skeletons::FindBone(scene, human.skeleton, "upperarm_l");
                app.Logger()->Info("Ragdolls (spike): {} built with {} bodies and {} joints in collision group {}",
                                   humanName, human.ragdoll.bodies.size(), human.ragdoll.joints.size(),
                                   human.skeleton.GetComponent<Fenrir::Ragdoll>().group);
                return human;
            }

            // The capsule the walker's game code moves, in the walker's collision group so its own ragdoll ignores it.
            void MakeCharacter()
            {
                m_character = m_entities->CreateEntity();
                m_character.GetComponent<Fenrir::Name>().Set("Walker character");
                m_character.GetComponent<Fenrir::Transform>().pos =
                    m_walker.root.GetComponent<Fenrir::Transform>().pos + Vec3(0.0f, FootToCentre, 0.0f);
                m_character.AddComponent<Fenrir::Collider>(
                    Fenrir::Collider{Fenrir::CapsuleShape{.halfHeight = CapsuleHalfHeight, .radius = CapsuleRadius}});
                m_character.AddComponent<Fenrir::CharacterBody>();
                m_character.AddComponent<Fenrir::CollisionGroup>(
                    Fenrir::CollisionGroup{m_walker.skeleton.GetComponent<Fenrir::Ragdoll>().group});
            }

            void OnKey(Fenrir::App& app, Fenrir::Key key)
            {
                using Fenrir::Key;
                Fenrir::Scene& scene = app.GetActiveScene();
                if (key == Key::Space)
                {
                    Drop(app, scene);
                }
                else if (key == Key::A)
                {
                    ToggleLimpArm(app, scene);
                }
                else if (key == Key::K)
                {
                    Power(app, scene);
                }
                else if (key == Key::G)
                {
                    GetUp(app, scene);
                }
                else if (key == Key::Num1 || key == Key::Num2)
                {
                    float& strength = m_walker.skeleton.GetComponent<Fenrir::Ragdoll>().strength;
                    strength = std::clamp(strength + (key == Key::Num2 ? 0.1f : -0.1f), 0.0f, 1.0f);
                    app.Logger()->Info("Ragdolls (spike): strength {:.1f}", strength);
                }
                else if (key == Key::Num3)
                {
                    DropOther(app, scene);
                }
                else if (key == Key::B)
                {
                    DropCrate(app);
                }
                else if (key == Key::L)
                {
                    LogState(app, scene);
                }
                else if (key == Key::P)
                {
                    m_autopilot = !m_autopilot;
                    m_autopilotTime = 0.0f;
                    m_autopilotStep = 0;
                    app.Logger()->Info(m_autopilot ? "Ragdolls (spike): autopilot running the sequence"
                                                   : "Ragdolls (spike): autopilot off");
                }
            }

            // Space: simulating, every bone overridden, a shove from behind, the character handed over.
            void Drop(Fenrir::App& app, Fenrir::Scene& scene)
            {
                Fenrir::Ragdoll& ragdoll = m_walker.skeleton.GetComponent<Fenrir::Ragdoll>();
                ragdoll.mode = Fenrir::RagdollMode::Simulating;
                Fenrir::Skeletons::SetOverrideWeight(scene, m_walker.skeleton, m_walker.hipsBone, 1.0f,
                                                     Fenrir::OverrideScope::BoneSubtree);
                m_getUp = -1.0f;
                m_character.GetComponent<Fenrir::CharacterBody>().enabled = false;
                if (Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(scene))
                {
                    world->AddImpulse(m_walker.Body("spine_03"), Vec3(m_heading * Shove, 0.2f * Shove, 0.0f));
                }
                app.Logger()->Info("Ragdolls (spike): Space, dropped with a {:.0f} N s shove from behind", Shove);
            }

            // A: the left arm's three bodies simulate under the following shoulder, and back.
            void ToggleLimpArm(Fenrir::App& app, Fenrir::Scene& scene)
            {
                m_armLimp = !m_armLimp;
                for (const std::string_view bone : {"upperarm_l", "lowerarm_l", "hand_l"})
                {
                    m_walker.Body(bone).GetComponent<Fenrir::RagdollBody>().mode =
                        m_armLimp ? Fenrir::RagdollBodyMode::Simulating : Fenrir::RagdollBodyMode::Inherit;
                }
                if (m_armLimp)
                {
                    Fenrir::Skeletons::SetOverrideWeight(scene, m_walker.skeleton, m_walker.leftShoulderBone, 1.0f,
                                                         Fenrir::OverrideScope::BoneSubtree);
                    m_armBlend = -1.0f;
                    app.Logger()->Info("Ragdolls (spike): A, the left arm is limp (a partial ragdoll)");
                }
                else
                {
                    m_armBlend = 0.0f;
                    app.Logger()->Info("Ragdolls (spike): A, the left arm follows again, blending back");
                }
            }

            // K: powered at the current strength (dropping first if still following).
            void Power(Fenrir::App& app, Fenrir::Scene& scene)
            {
                Fenrir::Ragdoll& ragdoll = m_walker.skeleton.GetComponent<Fenrir::Ragdoll>();
                if (ragdoll.mode == Fenrir::RagdollMode::Following)
                {
                    Fenrir::Skeletons::SetOverrideWeight(scene, m_walker.skeleton, m_walker.hipsBone, 1.0f,
                                                         Fenrir::OverrideScope::BoneSubtree);
                    m_character.GetComponent<Fenrir::CharacterBody>().enabled = false;
                    m_getUp = -1.0f;
                }
                ragdoll.mode = Fenrir::RagdollMode::Powered;
                app.Logger()->Info("Ragdolls (spike): K, powered towards the walk at strength {:.1f}",
                                   ragdoll.strength);
            }

            // G: following again; the weights ramp down over GetUpSeconds; the character goes under the skeleton
            // entity, which the glue left where the hips lay, upright.
            void GetUp(Fenrir::App& app, Fenrir::Scene&)
            {
                Fenrir::Ragdoll& ragdoll = m_walker.skeleton.GetComponent<Fenrir::Ragdoll>();
                if (ragdoll.mode == Fenrir::RagdollMode::Following)
                {
                    app.Logger()->Info("Ragdolls (spike): G, already following");
                    return;
                }
                ragdoll.mode = Fenrir::RagdollMode::Following;
                m_getUp = 0.0f;
                // The skeleton entity's world pose, as the glue left it, becomes the model root's, with the skeleton
                // back at its spawn place under the root, so nothing moves in the world.
                const Fenrir::Math::Mat4 skeletonWorld = Fenrir::GetWorldMatrix(m_walker.skeleton);
                const Fenrir::Math::Mat4 rootWorld =
                    skeletonWorld * Fenrir::Math::Inverse(Fenrir::TransformToMat4(m_walker.skeletonLocal));
                Fenrir::Transform root;
                Fenrir::Math::Decompose(rootWorld, root.pos, root.rot, root.scale);
                root.pos.y = 0.0f;
                m_walker.root.GetComponent<Fenrir::Transform>() = root;
                m_walker.skeleton.GetComponent<Fenrir::Transform>() = m_walker.skeletonLocal;
                // The walk heads the way the model faces.
                const Vec3 facing = Fenrir::Math::Rotate(root.rot, Vec3(0.0f, 0.0f, 1.0f));
                m_heading = facing.x >= 0.0f ? 1.0f : -1.0f;
                Fenrir::Transform& character = m_character.GetComponent<Fenrir::Transform>();
                character.pos = root.pos + Vec3(0.0f, FootToCentre, 0.0f);
                Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
                body.velocity = Vec3(0.0f);
                body.enabled = true;
                app.Logger()->Info("Ragdolls (spike): G, getting up at ({:.2f}, {:.2f}) heading {}", root.pos.x,
                                   root.pos.z, m_heading > 0.0f ? "+x" : "-x");
            }

            // 3: the bystander drops towards the walker's lane.
            void DropOther(Fenrir::App& app, Fenrir::Scene& scene)
            {
                if (!m_other.IsValid())
                {
                    return;
                }
                m_other.skeleton.GetComponent<Fenrir::Ragdoll>().mode = Fenrir::RagdollMode::Simulating;
                Fenrir::Skeletons::SetOverrideWeight(scene, m_other.skeleton, m_other.hipsBone, 1.0f,
                                                     Fenrir::OverrideScope::BoneSubtree);
                if (Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(scene))
                {
                    world->AddImpulse(m_other.Body("spine_03"), Vec3(0.0f, 0.3f * Shove, 1.5f * Shove));
                }
                app.Logger()->Info("Ragdolls (spike): 3, the bystander drops towards the lane (group {})",
                                   m_other.skeleton.GetComponent<Fenrir::Ragdoll>().group);
            }

            // B: a 5 kg crate dropped from 2.5 m onto the walker's head.
            void DropCrate(Fenrir::App& app)
            {
                if (!m_headCrate.IsValid())
                {
                    m_headCrate = Box("Head crate", Vec3(0.0f), Vec3(0.4f), Vec3(0.3f, 0.75f, 0.4f));
                    m_headCrate.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 5.0f});
                    m_headCrate.AddComponent<Fenrir::Collider>();
                }
                Fenrir::Transform& crate = m_headCrate.GetComponent<Fenrir::Transform>();
                crate.pos = Fenrir::GetWorldTransform(m_walker.Body("Head")).pos + Vec3(0.0f, 1.0f, 0.0f);
                crate.rot = identity;
                m_headCrate.GetComponent<Fenrir::RigidBody>().linearVelocity = Vec3(0.0f);
                app.Logger()->Info("Ragdolls (spike): B, crate dropped on the head");
            }

            // Game code's ramps: FenrirSkeleton has no fades, so the weights come down here.
            void RampWeights(Fenrir::Scene& scene, float dt)
            {
                if (m_getUp >= 0.0f)
                {
                    m_getUp += dt;
                    const float weight = std::max(0.0f, 1.0f - m_getUp / GetUpSeconds);
                    Fenrir::Skeletons::SetOverrideWeight(scene, m_walker.skeleton, m_walker.hipsBone, weight,
                                                         Fenrir::OverrideScope::BoneSubtree);
                    if (weight <= 0.0f)
                    {
                        m_getUp = -1.0f;
                    }
                }
                if (m_armBlend >= 0.0f)
                {
                    m_armBlend += dt;
                    const float weight = std::max(0.0f, 1.0f - m_armBlend / ArmBlendSeconds);
                    Fenrir::Skeletons::SetOverrideWeight(scene, m_walker.skeleton, m_walker.leftShoulderBone, weight,
                                                         Fenrir::OverrideScope::BoneSubtree);
                    if (weight <= 0.0f)
                    {
                        m_armBlend = -1.0f;
                    }
                }
            }

            struct Step
            {
                float at;
                Fenrir::Key key;
            };
            static constexpr std::array<Step, 8> Sequence = {
                Step{1.0f, Fenrir::Key::A},  Step{3.0f, Fenrir::Key::A},  Step{5.0f, Fenrir::Key::Space},
                Step{8.0f, Fenrir::Key::K},  Step{11.0f, Fenrir::Key::G}, Step{13.5f, Fenrir::Key::Num3},
                Step{15.0f, Fenrir::Key::B}, Step{17.0f, Fenrir::Key::L},
            };

            void Autopilot(Fenrir::App& app, float dt)
            {
                m_autopilotTime += dt;
                while (m_autopilotStep < Sequence.size() && Sequence.at(m_autopilotStep).at <= m_autopilotTime)
                {
                    OnKey(app, Sequence.at(m_autopilotStep).key);
                    ++m_autopilotStep;
                }
                if (m_autopilotStep >= Sequence.size())
                {
                    m_autopilot = false;
                    app.Logger()->Info("Ragdolls (spike): autopilot done");
                }
            }

            static const char* NameOf(Fenrir::RagdollMode mode)
            {
                switch (mode)
                {
                case Fenrir::RagdollMode::Following:
                    return "Following";
                case Fenrir::RagdollMode::Simulating:
                    return "Simulating";
                case Fenrir::RagdollMode::Powered:
                    return "Powered";
                }
                return "?";
            }

            std::string Describe(Fenrir::Scene& scene, const Human& human, const char* label) const
            {
                const Fenrir::Ragdoll& ragdoll = human.skeleton.GetComponent<Fenrir::Ragdoll>();
                const Vec3 hips = Fenrir::GetWorldTransform(human.Body("pelvis")).pos;
                const Vec3 entity = Fenrir::GetWorldTransform(human.skeleton).pos;
                const float weight = Fenrir::Skeletons::GetOverrideWeight(scene, human.skeleton, human.hipsBone);
                std::array<char, 256> text{};
                std::snprintf(text.data(), text.size(),
                              "%s: %s, strength %.1f, %u bodies, %s%s, hips weight %.2f, hips (%.2f, %.2f, %.2f), "
                              "skeleton entity (%.2f, %.2f, %.2f)",
                              label, NameOf(ragdoll.mode), static_cast<double>(ragdoll.strength), ragdoll.state.bodies,
                              ragdoll.state.anyAwake ? "awake" : "asleep", ragdoll.state.inactive ? ", INACTIVE" : "",
                              static_cast<double>(weight), static_cast<double>(hips.x), static_cast<double>(hips.y),
                              static_cast<double>(hips.z), static_cast<double>(entity.x), static_cast<double>(entity.y),
                              static_cast<double>(entity.z));
                return text.data();
            }

            void ShowReadings(Fenrir::Scene& scene)
            {
                m_walker.skeleton.GetComponent<Fenrir::Name>().Set(Describe(scene, m_walker, "Walker").c_str());
                const Fenrir::CharacterBody& body = m_character.GetComponent<Fenrir::CharacterBody>();
                std::array<char, 160> text{};
                const float armWeight =
                    Fenrir::Skeletons::GetOverrideWeight(scene, m_walker.skeleton, m_walker.leftShoulderBone);
                std::snprintf(text.data(), text.size(), "Walker character: %s, %s, left arm %s (weight %.2f)%s",
                              body.enabled ? "enabled" : "disabled",
                              body.state.ground == Fenrir::GroundState::OnGround ? "on ground" : "not on ground",
                              m_armLimp ? "limp" : "animated", static_cast<double>(armWeight),
                              m_getUp >= 0.0f ? ", getting up" : "");
                m_character.GetComponent<Fenrir::Name>().Set(text.data());
                std::vector<Entity> shown = {m_readout, m_walker.skeleton, m_character};
                if (m_other.IsValid())
                {
                    m_other.skeleton.GetComponent<Fenrir::Name>().Set(Describe(scene, m_other, "Bystander").c_str());
                    shown.push_back(m_other.skeleton);
                }
                ShowReadouts(scene, shown);
            }

            void LogState(Fenrir::App& app, Fenrir::Scene& scene) const
            {
                app.Logger()->Info("Ragdolls (spike): L, {}", Describe(scene, m_walker, "Walker"));
                app.Logger()->Info("Ragdolls (spike): L, {}",
                                   std::string(m_character.GetComponent<Fenrir::Name>().Get()));
                if (m_other.IsValid())
                {
                    app.Logger()->Info("Ragdolls (spike): L, {}", Describe(scene, m_other, "Bystander"));
                }
            }

            Fenrir::EntityList* m_entities = nullptr;
            std::optional<SceneLooks> m_looks;
            Human m_walker;
            Human m_other;
            Entity m_character;
            Entity m_readout;
            Entity m_headCrate;
            float m_heading = 1.0f;
            bool m_armLimp = false;
            /** @brief Seconds into the get-up ramp, or below 0 when none runs; likewise the arm's blend back. */
            float m_getUp = -1.0f;
            float m_armBlend = -1.0f;
            bool m_autopilot = false;
            float m_autopilotTime = 0.0f;
            std::size_t m_autopilotStep = 0;
        };
    } // namespace

    FenrirDemo::DemoScene Ragdolls(Fenrir::App& app)
    {
        auto ragdolls = std::make_shared<RagdollScene>();
        app.AddSequentialSystem(Fenrir::SchedulePriority::Tick,
                                [ragdolls](Fenrir::App& running) { ragdolls->Tick(running); });
        app.AddSequentialSystem(Fenrir::SchedulePriority::Update,
                                [ragdolls](Fenrir::App& running) { ragdolls->Update(running); });

        FenrirDemo::DemoScene scene;
        scene.name = SceneName;
        scene.keyHelp = "Space drops the walker with a shove, A makes its left arm limp and back, K powers it towards "
                        "the walk (1 and 2 lower and raise the strength), G gets it up. 3 drops the bystander towards "
                        "the lane, B drops a crate on the walker's head. F2 to the collider lines shows the hit boxes. "
                        "L logs the state, P runs the sequence by itself.";
        using Fenrir::Key;
        scene.keys = {Key::P};
        scene.keys.insert(scene.keys.end(), 18, Key::L);
        scene.camera = {{0.0f, 2.2f, 7.5f}, 0.0f, -0.18f};
        scene.spawn = [ragdolls](Fenrir::App& running, Fenrir::Scene& spawned) { ragdolls->Spawn(running, spawned); };
        return scene;
    }
} // namespace PhysicsDemo
