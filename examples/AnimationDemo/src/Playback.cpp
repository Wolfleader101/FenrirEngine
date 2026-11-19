#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "AnimationScenes.hpp"
#include "FenrirAnimation/Animation.hpp"
#include "FenrirAnimation/AnimationEvents.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirScene/Scene.hpp"
#include "SkinnedModels.hpp"

namespace AnimationDemo
{
    namespace
    {
        using Fenrir::Colour;
        using Fenrir::Math::Vec3;

        // The Quaternius human's clips the keys pick, in QuaterniusClip's order; its library has no wave, so 5 calls
        // out.
        constexpr std::array<std::string_view, 5> ClipNames{"Idle", "Walk", "Run", "Sprint", "Call"};
        constexpr std::array<Fenrir::Key, 5> ClipKeys{Fenrir::Key::Num1, Fenrir::Key::Num2, Fenrir::Key::Num3,
                                                      Fenrir::Key::Num4, Fenrir::Key::Num5};
        /** @brief Flutter's events: far more than the 64 a skeleton may be sent in one frame at its speed. */
        constexpr int FlutterEvents = 600;
        constexpr float FlutterSpeed = 2.0f;
        constexpr float SpeedStep = 0.25f;
        constexpr double SeekStep = 0.25;

        /** @brief Scene 1's state, a resource of its FenrirScene. */
        struct PlaybackState
        {
            /** @brief Quaternius's human, playing the clips in `clips`. */
            SkinnedModel walker;
            std::array<Fenrir::AssetHandle<Fenrir::Clip>, QuaterniusClips.size()> clips{};
            /** @brief The procedural mannequin, playing its Flutter clip. */
            SkinnedModel flutterer;
            std::size_t clip = static_cast<std::size_t>(QuaterniusClip::Walk);
            float speed = 1.0f;
            Fenrir::ClipLoop loop = Fenrir::ClipLoop::Loop;
            bool playing = true;
            Fenrir::EventReader<Fenrir::AnimationEvent> events;
            Fenrir::EventReader<Fenrir::ClipFinished> finished;
            Fenrir::EventReader<Fenrir::AnimationEventsDropped> dropped;
            std::uint64_t droppedTotal = 0;
            std::uint64_t droppedLastFrame = 0;
            std::uint64_t flutterDelivered = 0;
        };

        /** @brief Names the moments of each clip as events, so their crossings show as world text. */
        void AddEvents(Fenrir::App& app, const PlaybackState& state)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            // At fractions of each clip's duration, so a cycle's two halves each get one.
            const auto add = [&](QuaterniusClip clip, float fraction, std::string_view name) {
                const auto handle = state.clips.at(static_cast<std::size_t>(clip));
                if (const Fenrir::ClipData* data = store.TryGet(handle))
                {
                    store.AddClipEvent(handle, data->duration * fraction, name);
                }
            };
            add(QuaterniusClip::Idle, 0.5f, "Breathe");
            add(QuaterniusClip::Walk, 0.0f, "Step.A");
            add(QuaterniusClip::Walk, 0.5f, "Step.B");
            add(QuaterniusClip::Run, 0.0f, "Stride.A");
            add(QuaterniusClip::Run, 0.5f, "Stride.B");
            add(QuaterniusClip::Sprint, 0.0f, "Dash");
            add(QuaterniusClip::Call, 0.3f, "Hey!");
        }

        void Apply(PlaybackState& state)
        {
            auto& animator = state.walker.skeleton.GetComponent<Fenrir::Animator>();
            animator.speed = state.speed;
            animator.loop = state.loop;
            animator.playing = state.playing;
        }

        /** @brief The walker's playback, in a section of the demo panel, pulled each frame while the scene is there. */
        void AddReadouts(Fenrir::App& app, Fenrir::Scene& scene, const PlaybackState& state)
        {
            const FenrirDemo::DemoSection walker = SceneSection(app, "Walker");
            const auto playback = [&scene, &state] {
                return Fenrir::Animation::Playback(scene, state.walker.skeleton);
            };
            walker.Readout("Clip", [&state] { return std::string(ClipNames.at(state.clip)); });
            walker.Readout("Time", [&app, &state, playback] {
                const Fenrir::ClipData* clip = app.GetResource<Fenrir::AssetStore>().TryGet(state.clips.at(state.clip));
                return std::format("{:.2f} / {:.2f} s", playback().time, clip != nullptr ? clip->duration : 0.0f);
            });
            walker.Readout("Speed", [&state] { return std::format("{:.2f}", state.speed); });
            walker.Readout("Loop",
                           [&state] { return std::string(state.loop == Fenrir::ClipLoop::Loop ? "Loop" : "Once"); });
            walker.Readout("Playing", [&state, playback] {
                return std::format("{}{}", state.playing ? "playing" : "paused",
                                   playback().finished ? ", finished" : "");
            });
            walker.Readout("Laps", [playback] { return std::to_string(playback().completedLoops); });
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AddStage(app, scene);
            PlaybackState& state = scene.AddResource<PlaybackState>();
            state.walker = SpawnSkinnedModel(app, scene, QuaterniusFile, Vec3(-1.5f, 0.0f, 0.0f), 0.4f);
            for (std::size_t i = 0; i < QuaterniusClips.size(); ++i)
            {
                state.clips.at(i) = LoadClip(app, scene, QuaterniusClips.at(i));
            }
            state.flutterer = SpawnSkinnedModel(app, scene, MannequinFile, Vec3(2.0f, 0.0f, -0.5f), -0.4f);
            if (!state.walker.IsValid() || !state.flutterer.IsValid())
            {
                return;
            }
            AddEvents(app, state);
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const auto flutter = state.flutterer.Clip(app, static_cast<std::size_t>(MannequinClip::Flutter));
            const float duration = store.TryGet(flutter)->duration;
            for (int i = 0; i < FlutterEvents; ++i)
            {
                store.AddClipEvent(flutter, duration * static_cast<float>(i) / FlutterEvents, "Tick");
            }
            Play(state.walker.skeleton, state.clips.at(state.clip));
            Play(state.flutterer.skeleton, flutter, Fenrir::ClipLoop::Loop, FlutterSpeed);
            // Readers start at what is sent from now on.
            (void)state.events.Read(app);
            (void)state.finished.Read(app);
            (void)state.dropped.Read(app);
            AddReadouts(app, scene, state);
        }

        void ReadEvents(Fenrir::App& app, PlaybackState& state, Fenrir::DebugDraw& draw)
        {
            const Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            state.flutterDelivered = 0;
            for (const Fenrir::AnimationEvent& event : state.events.Read(app))
            {
                if (event.skeleton == state.flutterer.skeleton)
                {
                    ++state.flutterDelivered;
                    continue;
                }
                const Fenrir::ClipData* clip = store.TryGet(event.clip);
                const std::string name =
                    clip != nullptr && event.index < clip->events.size() ? clip->events[event.index].name : "?";
                // Above the walker's head for a second, following it.
                draw.WorldText(Vec3(0.0f, 2.1f, 0.0f), std::format("{} (lap {})", name, event.loop),
                               Colour::FromSrgb(0x80FF80FF),
                               {.duration = 1.0f, .relativeTo = state.walker.skeleton, .size = 20.0f});
                app.Logger()->Info("Playback: event {} at {:.2f} s, lap {}", name, event.time, event.loop);
            }
            for (const Fenrir::ClipFinished& done : state.finished.Read(app))
            {
                if (done.skeleton == state.walker.skeleton)
                {
                    draw.Message("ClipFinished: the Once clip reached its end and holds there",
                                 Colour::FromSrgb(0xFFC060FF), {.duration = 2.0f});
                    app.Logger()->Info("Playback: ClipFinished");
                }
            }
            state.droppedLastFrame = 0;
            for (const Fenrir::AnimationEventsDropped& dropped : state.dropped.Read(app))
            {
                state.droppedLastFrame += dropped.dropped;
                state.droppedTotal += dropped.dropped;
            }
        }

        void HandleKeys(Fenrir::App& app, Fenrir::Scene& scene, PlaybackState& state)
        {
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            for (std::size_t i = 0; i < ClipKeys.size(); ++i)
            {
                if (keyboard.JustPressed(ClipKeys.at(i)))
                {
                    state.clip = i;
                    state.walker.skeleton.GetComponent<Fenrir::Animator>().clip = state.clips.at(i);
                    app.Logger()->Info("Playback: {} plays {}", i + 1, ClipNames.at(i));
                }
            }
            if (keyboard.JustPressed(Fenrir::Key::Up) || keyboard.JustPressed(Fenrir::Key::Down))
            {
                const float magnitude = std::max(
                    0.0f, std::abs(state.speed) + (keyboard.JustPressed(Fenrir::Key::Up) ? SpeedStep : -SpeedStep));
                state.speed = std::copysign(magnitude, state.speed);
                app.Logger()->Info("Playback: speed {:.2f}", state.speed);
            }
            if (keyboard.JustPressed(Fenrir::Key::R))
            {
                state.speed = -state.speed;
                app.Logger()->Info("Playback: R reverses, speed {:.2f}", state.speed);
            }
            if (keyboard.JustPressed(Fenrir::Key::L))
            {
                state.loop = state.loop == Fenrir::ClipLoop::Loop ? Fenrir::ClipLoop::Once : Fenrir::ClipLoop::Loop;
                app.Logger()->Info("Playback: L, {}", state.loop == Fenrir::ClipLoop::Loop ? "Loop" : "Once");
            }
            if (keyboard.JustPressed(Fenrir::Key::Space))
            {
                state.playing = !state.playing;
                app.Logger()->Info("Playback: Space, {}", state.playing ? "playing" : "paused");
            }
            if (keyboard.JustPressed(Fenrir::Key::Left) || keyboard.JustPressed(Fenrir::Key::Right) ||
                keyboard.JustPressed(Fenrir::Key::Home))
            {
                const double now = Fenrir::Animation::Playback(scene, state.walker.skeleton).time;
                const double to = keyboard.JustPressed(Fenrir::Key::Home)   ? 0.0
                                  : keyboard.JustPressed(Fenrir::Key::Left) ? now - SeekStep
                                                                            : now + SeekStep;
                Fenrir::Animation::Seek(scene, state.walker.skeleton, std::max(0.0, to));
                app.Logger()->Info("Playback: seek to {:.2f} s", std::max(0.0, to));
            }
            Apply(state);
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            PlaybackState* state = scene.GetResource<PlaybackState>();
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (state == nullptr || draw == nullptr || !state->walker.IsValid() || !state->flutterer.IsValid())
            {
                return;
            }
            HandleKeys(app, scene, *state);
            ReadEvents(app, *state, *draw);

            const Fenrir::TextOptions above{.relativeTo = state->flutterer.skeleton, .size = 16.0f};
            draw->WorldText(
                Vec3(0.0f, 2.6f, 0.0f),
                std::format("Flutter: {} events, {} sent this frame", FlutterEvents, state->flutterDelivered),
                Colour::FromSrgb(0xFF9080FF), above);
            draw->WorldText(Vec3(0.0f, 2.4f, 0.0f),
                            std::format("AnimationEventsDropped: {} this frame, {} in all", state->droppedLastFrame,
                                        state->droppedTotal),
                            Colour::FromSrgb(0xFF9080FF), above);
        }
    } // namespace

    FenrirDemo::DemoScene Playback()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Playback";
        scene.keyRows = {{"1-5", "Idle, Walk, Run, Sprint, Call"},
                         {"Up/Down", "speed up / slow down"},
                         {"R", "reverse"},
                         {"L", "Loop or Once"},
                         {"Space", "pause or play"},
                         {"Left/Right", "seek 0.25 s back / on"},
                         {"Home", "seek to 0"}};
        // The app's first scene: the scaffold's keys, then this scene's.
        scene.tutorial = FenrirDemo::DemoTutorial{
            .title = "AnimationDemo",
            .steps = {{.title = "The scenes",
                       .text = "Each scene shows one part of FenrirAnimation and FenrirSkeleton. The Scenes section at "
                               "the top of the demo panel, on the right, enters any of them; PageDown and PageUp step "
                               "through them in turn.",
                       .keys = {{"PageDown/PageUp", "the next or previous scene"}}},
                      {.title = "The demo panel",
                       .text = "The panel shows each scene's readouts, and the help at the bottom left lists its keys. "
                               "F1 hides or shows them both, and this card.",
                       .keys = {{"F1", "hide or show the demo panel and the help"}}},
                      {.title = "The camera",
                       .text = "Hold the right mouse button to look around and move with WASD: Shift is faster, and "
                               "the wheel changes the speed. F3 turns the free-fly camera off and on.",
                       .keys = {{"Right mouse + WASD", "look and move"}, {"F3", "the free-fly camera off or on"}}},
                      {.title = "Playback",
                       .text = "The human on the left plays a clip through its Animator, under these keys; the Walker "
                               "section shows what it plays. The mannequin on the right sends more clip events than a "
                               "frame may carry, and counts the dropped ones above its head.",
                       .keys = {{"1-5", "Idle, Walk, Run, Sprint, Call"},
                                {"Up/Down", "speed up / slow down"},
                                {"R", "reverse"},
                                {"L", "Loop or Once"},
                                {"Space", "pause or play"},
                                {"Left/Right, Home", "seek 0.25 s back / on, seek to 0"}}}}};
        scene.keys = {Fenrir::Key::Num3,  Fenrir::Key::Up,    Fenrir::Key::R,    Fenrir::Key::Space,
                      Fenrir::Key::Right, Fenrir::Key::Space, Fenrir::Key::Num5, Fenrir::Key::L,
                      Fenrir::Key::Left,  Fenrir::Key::Home,  Fenrir::Key::Down, Fenrir::Key::Num2};
        scene.camera = {{0.0f, 1.4f, 5.5f}, 0.0f, -0.1f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace AnimationDemo
