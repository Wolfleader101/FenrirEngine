#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "AnimationScenes.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "Rigs.hpp"

namespace AnimationDemo
{
    namespace
    {
        using Fenrir::AttachmentState;
        using Fenrir::BoneAttachment;
        using Fenrir::Colour;
        using Fenrir::Entity;
        using Fenrir::Skeletons;
        using Fenrir::Math::Quat;
        using Fenrir::Math::Vec3;

        /** @brief The clips C steps through, so the prop is seen following the hand through each. */
        constexpr std::array<QuaterniusClip, 5> Clips{QuaterniusClip::Idle, QuaterniusClip::Walk, QuaterniusClip::Run,
                                                      QuaterniusClip::Sprint, QuaterniusClip::Call};
        constexpr std::array<std::string_view, 5> ClipNames{"idle", "walk", "jog", "sprint", "call"};

        /** @brief Where the failure cases stand: a row to the human's right, one slot each. */
        const Vec3 FirstSlot{1.0f, 1.0f, 0.0f};
        constexpr float SlotSpacing = 0.95f;

        /** @brief One labelled attachment that cannot be placed, at a slot it keeps because it is never placed. */
        struct FailureCase
        {
            Entity entity;
            std::string_view label;
            Vec3 slot;
        };

        /** @brief Scene 5's state, a resource of its FenrirScene. */
        struct AttachState
        {
            Rig human;
            std::size_t clip = 0;
            /** @brief The rifle: a skeleton entity of its own (stock and rail), attached to the human's right hand. */
            Entity rifle;
            BoneAttachment rifleAttachment;
            /** @brief The scope, attached to the rifle's rail: an attachment on an attachment. */
            Entity scope;
            /** @brief The hat, attached to the head. */
            Entity hat;
            BoneAttachment hatAttachment;
            std::array<FailureCase, 5> failures{};
        };

        std::string_view StateName(AttachmentState state)
        {
            switch (state)
            {
            case AttachmentState::Attached:
                return "Attached";
            case AttachmentState::SkeletonMissing:
                return "SkeletonMissing";
            case AttachmentState::SkeletonUnavailable:
                return "SkeletonUnavailable";
            case AttachmentState::BoneNotFound:
                return "BoneNotFound";
            case AttachmentState::TransformMissing:
                return "TransformMissing";
            case AttachmentState::Unplaceable:
                return "Unplaceable";
            default:
                return "NotAttached";
            }
        }

        BoneAttachment MakeAttachment(Fenrir::App& app, Entity skeleton, std::string_view bone, Vec3 offset,
                                      Quat rotation = Quat(1.0f, 0.0f, 0.0f, 0.0f))
        {
            BoneAttachment attachment;
            attachment.skeleton = skeleton;
            attachment.bone.Set(bone, *app.Logger());
            attachment.offset.pos = offset;
            attachment.offset.rot = rotation;
            return attachment;
        }

        /** @brief The rifle's skeleton: a stock at its origin and a rail 0.25 along +X, where the scope sits. */
        Fenrir::AssetHandle<Fenrir::Skeleton> RifleSkeleton(Fenrir::AssetStore& store, Fenrir::Scene& scene)
        {
            Fenrir::SkeletonData data;
            Fenrir::SkeletonBone stock;
            stock.sourceName = "stock";
            Fenrir::SkeletonBone rail;
            rail.sourceName = "rail";
            rail.parent = 0;
            rail.rest.pos = {0.25f, 0.0f, 0.0f};
            data.bones = {stock, rail};
            return store.Add<Fenrir::Skeleton>(std::move(data), scene);
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AddStage(app, scene);
            AttachState& state = scene.AddResource<AttachState>();
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const auto cube = store.Load<Fenrir::Mesh>("cube.obj#Mesh0", scene);
            const auto sphere = store.Load<Fenrir::Mesh>("sphere.obj#Mesh0", scene);
            const auto lit = store.Load<Fenrir::Shader>("builtin:lit", scene);
            if (!cube.Ok() || !sphere.Ok() || !lit.Ok())
            {
                app.Logger()->Error("Bone attachments: the cube, sphere or builtin:lit did not load");
                return;
            }
            const auto paint = [&](std::uint32_t srgb) {
                const auto made = store.CreateMaterial(lit.handle, scene);
                store.SetColour(made, "baseColour", Colour::FromSrgb(srgb));
                return made;
            };
            Fenrir::EntityList& entities = scene.GetEntityList();
            // A mesh as a plain child of `under`, so it is carried with whatever places `under`.
            const auto shape = [&](Entity under, Fenrir::AssetHandle<Fenrir::Mesh> mesh, std::uint32_t srgb, Vec3 pos,
                                   Vec3 scale) {
                Entity made = entities.CreateEntity();
                auto& transform = made.GetComponent<Fenrir::Transform>();
                transform.pos = pos;
                transform.scale = scale;
                made.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, paint(srgb), true});
                under.AddChild(made);
                return made;
            };
            const auto named = [&](std::string_view name) {
                Entity made = entities.CreateEntity();
                made.GetComponent<Fenrir::Name>().Set(std::string(name).c_str());
                return made;
            };

            state.human = SpawnRig(app, scene, QuaterniusFile, Vec3(-0.6f, 0.0f, 0.0f), 0.3f);
            if (!state.human.IsValid())
            {
                return;
            }
            Play(state.human.skeleton, LoadClip(app, scene, QuaterniusClips.at(static_cast<std::size_t>(Clips.at(0)))));

            // The rifle, its own skeleton entity, in the right hand along the fingers; its body a long box from the
            // stock past the rail.
            state.rifle = named("Rifle");
            state.rifle.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{RifleSkeleton(store, scene)});
            state.rifleAttachment = MakeAttachment(app, state.human.skeleton, "hand_r", {0.08f, 0.03f, 0.0f});
            state.rifle.AddComponent<BoneAttachment>(state.rifleAttachment);
            shape(state.rifle, cube.handle, 0x3A3A3AFF, {0.2f, 0.0f, 0.0f}, {0.6f, 0.05f, 0.04f});

            // The scope on the rifle's rail, on top as the hand holds it (the hand's -Y is up): placed after the rifle
            // each frame, whatever their entity order.
            state.scope = named("Scope");
            state.scope.AddComponent<BoneAttachment>(MakeAttachment(app, state.rifle, "rail", {0.0f, -0.05f, 0.0f}));
            shape(state.scope, cube.handle, 0x2060C0FF, {0.0f, 0.0f, 0.0f}, {0.16f, 0.04f, 0.04f});

            // The hat on the head.
            state.hat = named("Hat");
            state.hatAttachment = MakeAttachment(app, state.human.skeleton, "Head", {0.0f, 0.2f, 0.0f});
            state.hat.AddComponent<BoneAttachment>(state.hatAttachment);
            shape(state.hat, cube.handle, 0xC03030FF, {0.0f, 0.0f, 0.0f}, {0.24f, 0.08f, 0.24f});
            shape(state.hat, cube.handle, 0xC03030FF, {0.0f, -0.035f, 0.0f}, {0.36f, 0.01f, 0.36f});

            // The failure cases: each a ball at its slot, which it keeps since it is never placed (except where its
            // Transform is the failure), labelled with its state.
            for (std::size_t i = 0; i < state.failures.size(); ++i)
            {
                FailureCase& failure = state.failures.at(i);
                failure.slot = FirstSlot + Vec3(SlotSpacing * static_cast<float>(i), 0.0f, 0.0f);
                failure.entity = named("Failure");
                failure.entity.GetComponent<Fenrir::Transform>().pos = failure.slot;
                shape(failure.entity, sphere.handle, 0xE0B040FF, Vec3(0.0f), Vec3(0.2f));
            }
            // A dead skeleton entity.
            Entity gone = entities.CreateEntity();
            entities.DestroyEntity(gone);
            state.failures.at(0).label = "Destroyed skeleton";
            state.failures.at(0).entity.AddComponent<BoneAttachment>(MakeAttachment(app, gone, "hand_r", Vec3(0.0f)));
            // A skeleton entity naming no skeleton.
            Entity empty = named("Empty skeleton");
            empty.AddComponent<Fenrir::SkeletonPose>();
            state.failures.at(1).label = "Empty SkeletonPose";
            state.failures.at(1).entity.AddComponent<BoneAttachment>(MakeAttachment(app, empty, "hand_r", Vec3(0.0f)));
            // A bone the human does not have.
            state.failures.at(2).label = "Bone 'tail'";
            state.failures.at(2).entity.AddComponent<BoneAttachment>(
                MakeAttachment(app, state.human.skeleton, "tail", Vec3(0.0f)));
            // No Transform to place: nothing is drawn, only the label.
            state.failures.at(3).label = "No Transform";
            state.failures.at(3).entity.AddComponent<BoneAttachment>(
                MakeAttachment(app, state.human.skeleton, "Head", Vec3(0.0f)));
            state.failures.at(3).entity.ForEachChild(
                [](const Entity& child) { child.RemoveComponent<Fenrir::MeshRenderer>(); });
            state.failures.at(3).entity.RemoveComponent<Fenrir::Transform>();
            // A cycle: the ball attached to a bone of a skeleton parented under the ball.
            Entity knot = named("Knot");
            knot.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{RifleSkeleton(store, scene)});
            state.failures.at(4).entity.AddChild(knot);
            state.failures.at(4).label = "Skeleton under itself";
            state.failures.at(4).entity.AddComponent<BoneAttachment>(MakeAttachment(app, knot, "rail", Vec3(0.0f)));
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AttachState* state = scene.GetResource<AttachState>();
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (state == nullptr || draw == nullptr || !state->human.IsValid())
            {
                return;
            }
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::C))
            {
                state->clip = (state->clip + 1) % Clips.size();
                Play(state->human.skeleton,
                     LoadClip(app, scene, QuaterniusClips.at(static_cast<std::size_t>(Clips.at(state->clip)))));
                app.Logger()->Info("Bone attachments: C, the human plays its {}", ClipNames.at(state->clip));
            }
            if (keyboard.JustPressed(Fenrir::Key::W) && state->rifle.HasComponent<BoneAttachment>())
            {
                // Dropped where it is: to the top of the scene, keeping its world transform; the scope stays on it.
                const Fenrir::DetachResult result =
                    Skeletons::Detach(scene, state->rifle, Fenrir::DetachMode::KeepWorld);
                app.Logger()->Info("Bone attachments: W, the rifle detached keeping its world transform ({})",
                                   result == Fenrir::DetachResult::Detached ? "done" : "refused");
            }
            if (keyboard.JustPressed(Fenrir::Key::K) && state->hat.HasComponent<BoneAttachment>())
            {
                // The component removed only: the hat keeps its local Transform, at the top of the scene, so it hangs
                // in the air where the head last was.
                Skeletons::Detach(scene, state->hat, Fenrir::DetachMode::KeepLocal);
                app.Logger()->Info("Bone attachments: K, the hat detached keeping its local transform");
            }
            if (keyboard.JustPressed(Fenrir::Key::R))
            {
                state->rifle.AddComponent<BoneAttachment>(state->rifleAttachment);
                state->hat.AddComponent<BoneAttachment>(state->hatAttachment);
                app.Logger()->Info("Bone attachments: R, the rifle and hat attached again");
            }

            const Colour green = Colour::FromSrgb(0x60E060FF);
            const Colour amber = Colour::FromSrgb(0xF0B040FF);
            const auto label = [&](const Vec3& at, std::string_view what, AttachmentState attachment) {
                draw->WorldText(at, std::format("{}: {}", what, StateName(attachment)),
                                attachment == AttachmentState::Attached ? green : amber, {.size = 13.0f});
            };
            const auto above = [](const Entity& entity, float height) {
                return Fenrir::GetWorldTransform(entity).pos + Vec3(0.0f, height, 0.0f);
            };
            label(above(state->rifle, 0.12f), "Rifle", Skeletons::GetAttachmentState(scene, state->rifle));
            label(above(state->scope, 0.22f), "Scope on rifle", Skeletons::GetAttachmentState(scene, state->scope));
            label(above(state->hat, 0.2f), "Hat", Skeletons::GetAttachmentState(scene, state->hat));
            for (std::size_t i = 0; i < state->failures.size(); ++i)
            {
                const FailureCase& failure = state->failures.at(i);
                // Alternate heights, so neighbouring labels do not overlap.
                const float height = i % 2 == 0 ? 0.35f : 0.6f;
                label(failure.slot + Vec3(-0.35f, height, 0.0f), failure.label,
                      Skeletons::GetAttachmentState(scene, failure.entity));
                draw->Circle(Vec3(failure.slot.x, 0.01f, failure.slot.z), Vec3(0.0f, 1.0f, 0.0f), 0.2f, amber);
            }
            draw->ScreenText({16.0f, 120.0f},
                             std::format("Clip: {}. Rifle {}, hat {}.", ClipNames.at(state->clip),
                                         state->rifle.HasComponent<BoneAttachment>() ? "in the hand" : "dropped",
                                         state->hat.HasComponent<BoneAttachment>() ? "on the head" : "left behind"),
                             Colour::White);
        }
    } // namespace

    FenrirDemo::DemoScene BoneAttachments()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Bone attachments";
        scene.keyHelp = "C: the next clip. W: drop the rifle (KeepWorld). K: leave the hat (KeepLocal). R: attach both "
                        "again.";
        scene.keys = {Fenrir::Key::C, Fenrir::Key::C, Fenrir::Key::W, Fenrir::Key::K,
                      Fenrir::Key::C, Fenrir::Key::R, Fenrir::Key::W, Fenrir::Key::R};
        scene.camera = {{1.0f, 1.4f, 4.4f}, 0.0f, -0.12f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace AnimationDemo
