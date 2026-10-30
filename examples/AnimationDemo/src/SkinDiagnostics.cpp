#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "AnimationScenes.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirShaders/AnimationDemoFlat.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "SkinnedModels.hpp"

namespace AnimationDemo
{
    namespace
    {
        using Fenrir::Colour;
        using Fenrir::Math::Vec3;

        /** @brief One slot of the row: where it stands, and its label. */
        struct Slot
        {
            float x;
            std::string_view label;
            std::string_view outcome;
        };

        // A working skinned mesh first, then one per skip case (ADR-0035), left to right.
        constexpr std::array<Slot, 5> Slots{{
            {-6.0f, "Skinned", "drawn, playing Idle"},
            {-3.0f, "No SkinnedMesh", "skipped, logged once"},
            {0.0f, "Unavailable skeleton", "skipped, logged once"},
            {3.0f, "Bind signature differs", "skipped, logged once"},
            {6.0f, "Shader without SKINNED", "drawn magenta, logged once"},
        }};

        /** @brief Scene 3's marker resource: the labels are drawn while it exists. */
        struct DiagnosticsState
        {
        };

        Vec3 At(std::size_t slot)
        {
            return {Slots.at(slot).x, 0.0f, 0.0f};
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AddStage(app, scene, 18.0f, 6.0f);
            scene.AddResource<DiagnosticsState>();
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            Fenrir::EntityList& entities = scene.GetEntityList();

            // 0: drawn as it should be.
            const SkinnedModel skinned = SpawnSkinnedModel(app, scene, MannequinFile, At(0));
            if (!skinned.IsValid())
            {
                return;
            }
            Play(skinned.skeleton, skinned.Clip(app, static_cast<std::size_t>(MannequinClip::Idle)));

            // 1: skin data, but no SkinnedMesh naming its skeleton.
            const SkinnedModel unlinked = SpawnSkinnedModel(app, scene, MannequinFile, At(1));
            for (const Fenrir::Entity& mesh : unlinked.meshes)
            {
                mesh.RemoveComponent<Fenrir::SkinnedMesh>();
            }

            // 2: a SkeletonPose naming no skeleton asset.
            const SkinnedModel unavailable = SpawnSkinnedModel(app, scene, MannequinFile, At(2));
            if (unavailable.IsValid())
            {
                unavailable.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = {};
            }

            // 3: the critter's mesh, baked for its own skeleton, skinned by a skeleton entity posing the mannequin's:
            // fewer bones than the mannequin's, but another bind signature.
            const SkinnedModel critter = SpawnSkinnedModel(app, scene, CritterFile, At(3));
            Fenrir::Entity imposter = entities.CreateEntity();
            imposter.GetComponent<Fenrir::Name>().Set("Mannequin skeleton for the critter's mesh");
            imposter.GetComponent<Fenrir::Transform>().pos = At(3);
            imposter.AddComponent<Fenrir::SkeletonPose>(
                Fenrir::SkeletonPose{store.TryGet(skinned.model)->skeletons.at(0)});
            for (const Fenrir::Entity& mesh : critter.meshes)
            {
                mesh.GetComponent<Fenrir::SkinnedMesh>().skeleton = imposter;
            }

            // 4: a user shader built without the SKINNED key, so the fallback material draws it, skinned.
            Fenrir::ShaderData flat;
            const auto bytes = std::as_bytes(std::span(Fenrir::Shaders::AnimationDemoFlat));
            flat.bytes.assign(bytes.begin(), bytes.end());
            const auto material = store.CreateMaterial(store.Add<Fenrir::Shader>(std::move(flat), scene), scene);
            const SkinnedModel unskinnable = SpawnSkinnedModel(app, scene, MannequinFile, At(4));
            for (const Fenrir::Entity& mesh : unskinnable.meshes)
            {
                mesh.GetComponent<Fenrir::MeshRenderer>().material = material;
            }
            if (unskinnable.IsValid())
            {
                Play(unskinnable.skeleton, unskinnable.Clip(app, static_cast<std::size_t>(MannequinClip::Wave)));
            }
        }

        void Update(Fenrir::App&, Fenrir::Scene& scene)
        {
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (draw == nullptr || scene.GetResource<DiagnosticsState>() == nullptr)
            {
                return;
            }
            // Placed by slot, not by what is drawn there, so a skipped draw still has its label.
            for (std::size_t slot = 0; slot < Slots.size(); ++slot)
            {
                draw->WorldText(At(slot) + Vec3(-0.8f, 2.5f, 0.0f), Slots.at(slot).label, Colour::White,
                                {.size = 18.0f});
                draw->WorldText(At(slot) + Vec3(-0.8f, 2.3f, 0.0f), Slots.at(slot).outcome,
                                Colour::FromSrgb(0xC0C0C0FF));
                draw->Circle(At(slot) + Vec3(0.0f, 0.01f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), 0.6f,
                             Colour::FromSrgb(0xFFD040FF));
            }
            draw->ScreenText({16.0f, 120.0f}, "Each skip case logs one warning: see the log.", Colour::White);
        }
    } // namespace

    FenrirDemo::DemoScene SkinDiagnostics()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Skin diagnostics";
        scene.keyHelp = "No keys: one labelled entity per skinning skip case, and a working one first.";
        scene.camera = {{0.0f, 1.8f, 9.5f}, 0.0f, -0.1f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace AnimationDemo
