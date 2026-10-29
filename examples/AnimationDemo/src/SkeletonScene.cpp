#include <format>

#include "AnimationScenes.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"
#include "Rigs.hpp"

namespace AnimationDemo
{
    namespace
    {
        using Fenrir::Colour;
        using Fenrir::Math::Vec3;

        /** @brief Scene 2's state, a resource of its FenrirScene. */
        struct SkeletonState
        {
            Rig mannequin;
            Rig critter;
            bool meshes = true;
            bool bones = true;
            bool names = false;
        };

        void ShowMeshes(const Rig& rig, bool visible)
        {
            for (const Fenrir::Entity& mesh : rig.meshes)
            {
                mesh.GetComponent<Fenrir::MeshRenderer>().visible = visible;
            }
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AddStage(app, scene);
            SkeletonState& state = scene.AddResource<SkeletonState>();
            state.mannequin = SpawnRig(app, scene, MannequinFile, Vec3(-0.8f, 0.0f, 0.0f), 0.5f);
            state.critter = SpawnRig(app, scene, CritterFile, Vec3(1.2f, 0.0f, 0.3f), -0.9f);
            if (state.mannequin.IsValid())
            {
                Play(state.mannequin.skeleton,
                     state.mannequin.Clip(app, static_cast<std::size_t>(MannequinClip::Walk)));
            }
            if (state.critter.IsValid())
            {
                Play(state.critter.skeleton, state.critter.Clip(app, 0));
            }
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            SkeletonState* state = scene.GetResource<SkeletonState>();
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (state == nullptr || draw == nullptr || !state->mannequin.IsValid() || !state->critter.IsValid())
            {
                return;
            }
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::M))
            {
                state->meshes = !state->meshes;
                ShowMeshes(state->mannequin, state->meshes);
                ShowMeshes(state->critter, state->meshes);
                app.Logger()->Info("Skeleton: M, meshes {}", state->meshes ? "shown" : "hidden");
            }
            if (keyboard.JustPressed(Fenrir::Key::B))
            {
                state->bones = !state->bones;
                app.Logger()->Info("Skeleton: B, bones {}", state->bones ? "shown" : "hidden");
            }
            if (keyboard.JustPressed(Fenrir::Key::N))
            {
                state->names = !state->names;
                app.Logger()->Info("Skeleton: N, names {}", state->names ? "shown" : "hidden");
            }
            DrawBones(app, scene, *draw, state->mannequin.skeleton, state->bones, state->names);
            DrawBones(app, scene, *draw, state->critter.skeleton, state->bones, state->names);
            draw->WorldText(Vec3(0.0f, 2.3f, 0.0f), "Mannequin (stand-in for the Quaternius humanoid)", Colour::White,
                            {.relativeTo = state->mannequin.root});
            draw->WorldText(Vec3(0.0f, 1.0f, 0.0f), "Critter (stand-in for the Khronos Fox)", Colour::White,
                            {.relativeTo = state->critter.root});
            draw->ScreenText({16.0f, 120.0f},
                             std::format("Meshes {}, bones {}, names {}", state->meshes ? "on" : "off",
                                         state->bones ? "on" : "off", state->names ? "on" : "off"),
                             Colour::White);
        }
    } // namespace

    FenrirDemo::DemoScene SkeletonScene()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Skeleton";
        scene.keyHelp = "M: show or hide the meshes. B: the bones and their axes. N: the bones' names.";
        scene.keys = {Fenrir::Key::N, Fenrir::Key::M, Fenrir::Key::B, Fenrir::Key::M, Fenrir::Key::B, Fenrir::Key::N};
        scene.camera = {{0.2f, 1.3f, 4.0f}, 0.0f, -0.12f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace AnimationDemo
