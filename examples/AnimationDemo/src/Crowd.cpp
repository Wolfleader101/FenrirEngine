#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <vector>

#include "AnimationScenes.hpp"
#include "FenrirAnimation/Animation.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "SkinnedModels.hpp"

namespace AnimationDemo
{
    namespace
    {
        using Fenrir::Colour;
        using Fenrir::Math::Vec3;

        constexpr std::size_t StartCount = 64;
        constexpr std::size_t MinCount = 1;
        constexpr std::size_t MaxCount = 1024;
        /** @brief The distance between neighbours in the grid, in world units. */
        constexpr float Spacing = 1.0f;
        /** @brief How much of each new frame time the shown average takes. */
        constexpr double FrameTimeWeight = 0.1;

        /** @brief Scene 7's state, a resource of its FenrirScene. */
        struct CrowdState
        {
            /** @brief Kenney's character, one per member, in spawn order: halving removes the last half. */
            std::vector<SkinnedModel> members;
            /** @brief Its idle and run, which the members take in turn. */
            std::array<Fenrir::AssetHandle<Fenrir::Clip>, KenneyClips.size()> clips{};
            std::size_t bonesEach = 0;
            bool playing = true;
            double frameTime = 0.0;
        };

        /** @brief Every bit of `bits` at an even place, packed together: one coordinate of a Morton (Z-order) index. */
        std::uint32_t EvenBits(std::uint32_t bits)
        {
            std::uint32_t packed = 0;
            for (std::uint32_t bit = 0; bit < 16; ++bit)
            {
                packed |= ((bits >> (2 * bit)) & 1u) << bit;
            }
            return packed;
        }

        /**
         * @brief Member `index`'s place among `count`: along a Z-order curve, so the first quarter of a square fills
         * its own square and doubling adds the next block beside the old ones, the grid centred on the origin.
         */
        Vec3 Place(std::size_t index, std::size_t count)
        {
            const auto at = static_cast<std::uint32_t>(index);
            const auto last = static_cast<std::uint32_t>(count - 1);
            const float width = static_cast<float>(EvenBits(last));
            const float depth = static_cast<float>(EvenBits(last >> 1u));
            return Vec3((static_cast<float>(EvenBits(at)) - width * 0.5f) * Spacing, 0.0f,
                        (static_cast<float>(EvenBits(at >> 1u)) - depth * 0.5f) * Spacing);
        }

        /** @brief Destroys `entity` and everything under it. */
        void DestroyTree(Fenrir::EntityList& entities, const Fenrir::Entity& entity)
        {
            std::vector<Fenrir::Entity> tree{entity};
            for (std::size_t i = 0; i < tree.size(); ++i)
            {
                tree[i].ForEachChild([&](const Fenrir::Entity& child) { tree.push_back(child); });
            }
            for (auto it = tree.rbegin(); it != tree.rend(); ++it)
            {
                entities.DestroyEntity(*it);
            }
        }

        /**
         * @brief Spawns members up to `count`, each taking the clips in turn and starting at its own point of its clip
         * through Seek, or destroys those past it; then places them all for the new count.
         */
        void Resize(Fenrir::App& app, Fenrir::Scene& scene, CrowdState& state, std::size_t count)
        {
            const Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            while (state.members.size() > count)
            {
                DestroyTree(scene.GetEntityList(), state.members.back().root);
                state.members.pop_back();
            }
            while (state.members.size() < count)
            {
                const std::size_t index = state.members.size();
                SkinnedModel spawned = SpawnSkinnedModel(app, scene, KenneyFile, Vec3(0.0f), 0.0f, KenneyScale);
                if (!spawned.IsValid())
                {
                    break;
                }
                Paint(app, scene, spawned, KenneySkin);
                const auto clip = state.clips.at(index % state.clips.size());
                Play(spawned.skeleton, clip);
                spawned.skeleton.GetComponent<Fenrir::Animator>().playing = state.playing;
                // Spread over the clip by the golden ratio, so no two neighbours start together.
                const Fenrir::ClipData* data = store.TryGet(clip);
                const double fraction = std::fmod(static_cast<double>(index) * 0.6180339887, 1.0);
                Fenrir::Animation::Seek(scene, spawned.skeleton, fraction * (data != nullptr ? data->duration : 0.0));
                if (state.bonesEach == 0)
                {
                    const Fenrir::SkeletonData* skeleton =
                        store.TryGet(spawned.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton);
                    state.bonesEach = skeleton != nullptr ? skeleton->BoneCount() : 0;
                }
                state.members.push_back(spawned);
            }
            for (std::size_t i = 0; i < state.members.size(); ++i)
            {
                state.members[i].root.GetComponent<Fenrir::Transform>().pos = Place(i, state.members.size());
            }
            if (!state.members.empty())
            {
                app.Logger()->Info("Crowd: {} instance{}", state.members.size(), state.members.size() == 1 ? "" : "s");
            }
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AddStage(app, scene, 40.0f, 40.0f);
            CrowdState& state = scene.AddResource<CrowdState>();
            for (std::size_t i = 0; i < KenneyClips.size(); ++i)
            {
                state.clips.at(i) = LoadClip(app, scene, KenneyClips.at(i), KenneyClip);
            }
            Resize(app, scene, state, StartCount);
        }

        void HandleKeys(Fenrir::App& app, Fenrir::Scene& scene, CrowdState& state)
        {
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            const std::size_t count = state.members.size();
            if (keyboard.JustPressed(Fenrir::Key::Up))
            {
                Resize(app, scene, state, std::min(MaxCount, std::max(MinCount, count * 2)));
            }
            if (keyboard.JustPressed(Fenrir::Key::Down))
            {
                Resize(app, scene, state, std::max(MinCount, count / 2));
            }
            if (keyboard.JustPressed(Fenrir::Key::Space))
            {
                state.playing = !state.playing;
                for (const SkinnedModel& spawned : state.members)
                {
                    spawned.skeleton.GetComponent<Fenrir::Animator>().playing = state.playing;
                }
                app.Logger()->Info("Crowd: Space, {}", state.playing ? "playing" : "paused");
            }
            if (keyboard.JustPressed(Fenrir::Key::R))
            {
                // Back to the start: every member respawned, playing from its staggered start.
                state.playing = true;
                Resize(app, scene, state, 0);
                Resize(app, scene, state, StartCount);
                app.Logger()->Info("Crowd: R resets");
            }
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            CrowdState* state = scene.GetResource<CrowdState>();
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (state == nullptr || draw == nullptr)
            {
                return;
            }
            HandleKeys(app, scene, *state);

            const double delta = app.GetTime().deltaTime;
            state->frameTime =
                state->frameTime == 0.0 ? delta : state->frameTime + (delta - state->frameTime) * FrameTimeWeight;
            // The renderer's counts are the last drawn frame's.
            const Fenrir::RendererStats stats = Fenrir::Renderer::Stats(app);
            const std::size_t count = state->members.size();
            draw->ScreenText({16.0f, 120.0f},
                             std::format("Crowd: {} instances, {} bones, {}", count, count * state->bonesEach,
                                         state->playing ? "playing" : "paused"),
                             Colour::White);
            draw->ScreenText({16.0f, 144.0f},
                             std::format("Frame time {:.2f} ms ({:.0f} fps)", state->frameTime * 1000.0,
                                         state->frameTime > 0.0 ? 1.0 / state->frameTime : 0.0),
                             Colour::White);
            draw->ScreenText({16.0f, 168.0f},
                             std::format("Palettes: {} distinct written, {} bytes uploaded; draws {}, {} skinned",
                                         stats.distinctPalettesWritten, stats.paletteBytesUploaded, stats.draws,
                                         stats.skinnedDraws),
                             Colour::FromSrgb(0x80FF80FF));
        }
    } // namespace

    FenrirDemo::DemoScene Crowd()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Crowd";
        scene.keyHelp = "Up/Down: double or halve the crowd (1 to 1024). Space: pause. R: reset to 64.";
        // Up to 1024, paused and played, down to 1, then reset.
        scene.keys = {Fenrir::Key::Up,    Fenrir::Key::Up,   Fenrir::Key::Up,   Fenrir::Key::Up,   Fenrir::Key::Space,
                      Fenrir::Key::Space, Fenrir::Key::Down, Fenrir::Key::Down, Fenrir::Key::Down, Fenrir::Key::Down,
                      Fenrir::Key::Down,  Fenrir::Key::Down, Fenrir::Key::Down, Fenrir::Key::Down, Fenrir::Key::Down,
                      Fenrir::Key::Down,  Fenrir::Key::R};
        scene.camera = {{0.0f, 6.0f, 11.0f}, 0.0f, -0.45f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace AnimationDemo
