#include <array>
#include <cstddef>
#include <format>
#include <string_view>

#include "AnimationScenes.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
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

        /** @brief One model in the row: what it is, where it stands, and the clip it plays. */
        struct Model
        {
            std::string_view file;
            std::string_view label;
            Vec3 position;
            float yaw;
            float scale;
            /** @brief The clip to play, by index in the model's file, or in clipFile when it is given. */
            std::size_t clip;
            /** @brief A file holding the clip to play instead, for a rig whose clips are files of their own. */
            std::string_view clipFile;
            /** @brief How high above its root its label sits, in world units. */
            float labelHeight;
            /** @brief An image to paint its meshes with, for a model whose file names no texture. */
            std::string_view skin = {};
        };

        const std::array<Model, 5> Models{{
            {QuaterniusFile,
             "Quaternius human",
             {-3.5f, 0.0f, 0.0f},
             0.5f,
             1.0f,
             0,
             QuaterniusClips.at(static_cast<std::size_t>(QuaterniusClip::Walk)),
             2.1f},
            {MannequinFile,
             "Mannequin (procedural)",
             {-1.8f, 0.0f, 0.0f},
             0.3f,
             1.0f,
             static_cast<std::size_t>(MannequinClip::Walk),
             {},
             2.3f},
            {CesiumManFile, "CesiumMan (Khronos, untextured)", {0.0f, 0.0f, -0.2f}, 0.0f, 1.0f, 0, {}, 1.8f},
            // The Fox's clips are Survey, Walk and Run.
            {FoxFile, "Fox (Khronos)", {1.7f, 0.0f, 0.2f}, -0.9f, FoxScale, 1, {}, 1.1f},
            // The FBX character (ticket 06), running: its clips are files of their own.
            {KenneyFile,
             "Kenney character (FBX)",
             {3.4f, 0.0f, 0.0f},
             -0.3f,
             KenneyScale,
             KenneyClip,
             KenneyClips.at(1),
             2.1f,
             KenneySkin},
        }};

        /** @brief Scene 2's state, a resource of its FenrirScene. */
        struct SkeletonState
        {
            std::array<Rig, Models.size()> rigs;
            bool meshes = true;
            bool bones = true;
            bool names = false;
        };

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AddStage(app, scene);
            SkeletonState& state = scene.AddResource<SkeletonState>();
            for (std::size_t i = 0; i < Models.size(); ++i)
            {
                const Model& model = Models.at(i);
                state.rigs.at(i) = SpawnRig(app, scene, model.file, model.position, model.yaw, model.scale);
                if (state.rigs.at(i).IsValid())
                {
                    Play(state.rigs.at(i).skeleton, model.clipFile.empty()
                                                        ? state.rigs.at(i).Clip(app, model.clip)
                                                        : LoadClip(app, scene, model.clipFile, model.clip));
                    if (!model.skin.empty())
                    {
                        Paint(app, scene, state.rigs.at(i), model.skin);
                    }
                }
            }
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            SkeletonState* state = scene.GetResource<SkeletonState>();
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (state == nullptr || draw == nullptr)
            {
                return;
            }
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::M))
            {
                state->meshes = !state->meshes;
                for (const Rig& rig : state->rigs)
                {
                    for (const Fenrir::Entity& mesh : rig.meshes)
                    {
                        mesh.GetComponent<Fenrir::MeshRenderer>().visible = state->meshes;
                    }
                }
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
            for (std::size_t i = 0; i < Models.size(); ++i)
            {
                const Rig& rig = state->rigs.at(i);
                if (!rig.IsValid())
                {
                    continue;
                }
                DrawBones(app, scene, *draw, rig.skeleton, state->bones, state->names);
                // In world units: the Fox's root is scaled, so text relative to it would be too.
                const Vec3 at = rig.root.GetComponent<Fenrir::Transform>().pos;
                draw->WorldText(at + Vec3(-0.4f, Models.at(i).labelHeight, 0.0f), Models.at(i).label, Colour::White);
            }
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
        scene.camera = {{0.0f, 1.4f, 6.8f}, 0.0f, -0.12f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace AnimationDemo
