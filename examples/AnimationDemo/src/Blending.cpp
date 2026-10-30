#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "AnimationScenes.hpp"
#include "FenrirAnimation/Animation.hpp"
#include "FenrirAnimation/AnimationEvents.hpp"
#include "FenrirAnimation/AnimationLayer.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/BoneMaskData.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "Rigs.hpp"

namespace AnimationDemo
{
    namespace
    {
        using Fenrir::Colour;
        using Fenrir::Math::Vec3;

        // The crossfading human's clips, in turn.
        constexpr std::array<QuaterniusClip, 4> Cycle{QuaterniusClip::Idle, QuaterniusClip::Walk, QuaterniusClip::Run,
                                                      QuaterniusClip::Sprint};
        constexpr std::array<std::string_view, 4> CycleNames{"Idle", "Walk", "Run", "Sprint"};
        constexpr float FadeSeconds = 0.8f;
        /** @brief How long after an interrupting change its second change comes: mid-fade. */
        constexpr double InterruptAfter = 0.3;
        /** @brief The lean's weight ramp: down to 0, held there past a Sway event, then back to 1. */
        constexpr double RampDown = 1.0;
        constexpr double RampHold = 2.5;
        constexpr double RampSeconds = RampDown + RampHold + RampDown;
        /** @brief The lean: the upper spine swaying between these angles about the body's forward axis, and back. */
        constexpr float LeanFrom = 10.0f;
        constexpr float LeanTo = 30.0f;
        constexpr float LeanSeconds = 2.0f;
        constexpr std::string_view LeanBone = "spine_02";
        constexpr std::string_view UpperBody = "spine_01";

        /** @brief Scene 4's state, a resource of its FenrirScene. */
        struct BlendingState
        {
            /** @brief Left: the base Animator crossfading through Cycle. */
            Rig fader;
            std::size_t clip = 0;
            Fenrir::FadeCurve curve = Fenrir::FadeCurve::Smooth;
            /** @brief Seconds until an interrupting second change, or below 0 when none is due. */
            double interruptIn = -1.0;
            /** @brief Middle: walking, the call played on the upper body by an override layer. */
            Rig walker;
            Fenrir::Entity upperBody;
            /** @brief Right: idling, the lean added by an additive layer whose weight ramps. */
            Rig leaner;
            Fenrir::Entity lean;
            /** @brief Seconds into the weight ramp, or below 0 when none runs. */
            double ramp = -1.0;
            std::array<Fenrir::AssetHandle<Fenrir::Clip>, QuaterniusClips.size()> clips{};
            Fenrir::EventReader<Fenrir::AnimationEvent> events;
            std::size_t leanEvents = 0;
        };

        /**
         * @brief The lean clip, made in code: `LeanBone` turned about its parent's Z from LeanFrom to LeanTo and back,
         * on top of its rest rotation, with a "Sway" event at the far end. Measured from its start it sways from
         * upright; measured from rest it leans LeanFrom further throughout.
         */
        Fenrir::AssetHandle<Fenrir::Clip> MakeLean(Fenrir::App& app, Fenrir::Scene& scene, const Rig& rig)
        {
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();
            const Fenrir::SkeletonData* skeleton =
                store.TryGet(rig.skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton);
            const std::int32_t bone = skeleton != nullptr ? skeleton->FindBone(LeanBone) : Fenrir::NoBone;
            if (bone == Fenrir::NoBone)
            {
                app.Logger()->Error("AnimationDemo: the Quaternius human has no {} to lean", LeanBone);
                return {};
            }
            const Fenrir::Math::Quat rest = skeleton->bones[static_cast<std::size_t>(bone)].rest.rot;
            const auto key = [&](float degrees) {
                const Fenrir::Math::Quat q =
                    Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(degrees), Vec3(0.0f, 0.0f, 1.0f)) * rest;
                return Fenrir::Math::Vec4(q.x, q.y, q.z, q.w);
            };
            Fenrir::ClipChannel channel;
            channel.targetName = std::string(LeanBone);
            channel.path = Fenrir::ClipPath::Rotation;
            channel.interpolation = Fenrir::ClipInterpolation::Linear;
            channel.times = {0.0f, LeanSeconds * 0.5f, LeanSeconds};
            channel.values = {key(LeanFrom), key(LeanTo), key(LeanFrom)};
            Fenrir::ClipData clip;
            clip.channels = {channel};
            clip.events = {{LeanSeconds * 0.5f, "Sway"}};
            return store.Add<Fenrir::Clip>(std::move(clip), scene);
        }

        Fenrir::Entity AddLayer(Fenrir::Scene& scene, const Rig& rig, Fenrir::AnimationLayer layer)
        {
            layer.skeleton = rig.skeleton;
            Fenrir::Entity entity = scene.GetEntityList().CreateEntity();
            entity.AddComponent<Fenrir::AnimationLayer>(layer);
            return entity;
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AddStage(app, scene);
            BlendingState& state = scene.AddResource<BlendingState>();
            for (std::size_t i = 0; i < QuaterniusClips.size(); ++i)
            {
                state.clips.at(i) = LoadClip(app, scene, QuaterniusClips.at(i));
            }
            state.fader = SpawnRig(app, scene, QuaterniusFile, Vec3(-2.2f, 0.0f, 0.0f), 0.3f);
            state.walker = SpawnRig(app, scene, QuaterniusFile, Vec3(0.0f, 0.0f, 0.0f));
            state.leaner = SpawnRig(app, scene, QuaterniusFile, Vec3(2.2f, 0.0f, 0.0f), -0.3f);
            if (!state.fader.IsValid() || !state.walker.IsValid() || !state.leaner.IsValid())
            {
                return;
            }
            Fenrir::AssetStore& store = app.GetResource<Fenrir::AssetStore>();

            Play(state.fader.skeleton, state.clips.at(static_cast<std::size_t>(Cycle.at(state.clip))));
            auto& animator = state.fader.skeleton.GetComponent<Fenrir::Animator>();
            animator.fadeSeconds = FadeSeconds;
            animator.fadeCurve = state.curve;

            Play(state.walker.skeleton, state.clips.at(static_cast<std::size_t>(QuaterniusClip::Walk)));
            const auto upper = store.CreateBoneMask(scene);
            store.IncludeInBoneMask(upper, UpperBody, 1.0f, Fenrir::MaskScope::Subtree);
            Fenrir::AnimationLayer call;
            call.clip = state.clips.at(static_cast<std::size_t>(QuaterniusClip::Call));
            call.mask = upper;
            call.fadeSeconds = 0.3f;
            state.upperBody = AddLayer(scene, state.walker, call);

            Play(state.leaner.skeleton, state.clips.at(static_cast<std::size_t>(QuaterniusClip::Idle)));
            Fenrir::AnimationLayer lean;
            lean.mode = Fenrir::LayerMode::Additive;
            lean.clip = MakeLean(app, scene, state.leaner);
            state.lean = AddLayer(scene, state.leaner, lean);
            (void)state.events.Read(app);
        }

        void CrossfadeToNext(Fenrir::App& app, BlendingState& state)
        {
            state.clip = (state.clip + 1) % Cycle.size();
            auto& animator = state.fader.skeleton.GetComponent<Fenrir::Animator>();
            animator.clip = state.clips.at(static_cast<std::size_t>(Cycle.at(state.clip)));
            animator.fadeCurve = state.curve;
            app.Logger()->Info("Blending: crossfade to {}", CycleNames.at(state.clip));
        }

        void HandleKeys(Fenrir::App& app, BlendingState& state)
        {
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::C))
            {
                CrossfadeToNext(app, state);
            }
            if (keyboard.JustPressed(Fenrir::Key::V))
            {
                state.curve =
                    state.curve == Fenrir::FadeCurve::Smooth ? Fenrir::FadeCurve::Linear : Fenrir::FadeCurve::Smooth;
                app.Logger()->Info("Blending: V, {} fades",
                                   state.curve == Fenrir::FadeCurve::Smooth ? "smooth" : "linear");
            }
            if (keyboard.JustPressed(Fenrir::Key::I))
            {
                CrossfadeToNext(app, state);
                state.interruptIn = InterruptAfter;
            }
            if (keyboard.JustPressed(Fenrir::Key::U))
            {
                auto& layer = state.upperBody.GetComponent<Fenrir::AnimationLayer>();
                layer.weight = layer.weight > 0.0f ? 0.0f : 1.0f;
                app.Logger()->Info("Blending: U, upper-body call {}", layer.weight > 0.0f ? "on" : "off");
            }
            if (keyboard.JustPressed(Fenrir::Key::A))
            {
                auto& layer = state.lean.GetComponent<Fenrir::AnimationLayer>();
                layer.reference = layer.reference == Fenrir::AdditiveReference::ClipStart
                                      ? Fenrir::AdditiveReference::RestPose
                                      : Fenrir::AdditiveReference::ClipStart;
                app.Logger()->Info("Blending: A, the lean measured from {}",
                                   layer.reference == Fenrir::AdditiveReference::ClipStart ? "its start" : "rest");
            }
            if (keyboard.JustPressed(Fenrir::Key::W))
            {
                state.ramp = 0.0;
                app.Logger()->Info("Blending: W, the lean's weight ramps to 0 and back");
            }
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            BlendingState* state = scene.GetResource<BlendingState>();
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (state == nullptr || draw == nullptr || !state->fader.IsValid() || !state->walker.IsValid() ||
                !state->leaner.IsValid())
            {
                return;
            }
            HandleKeys(app, *state);
            const double delta = app.GetTime().deltaTime;
            if (state->interruptIn >= 0.0)
            {
                state->interruptIn -= delta;
                if (state->interruptIn < 0.0)
                {
                    // Mid-fade: the blend so far is frozen as a snapshot and fades on into the newest clip.
                    CrossfadeToNext(app, *state);
                }
            }
            auto& lean = state->lean.GetComponent<Fenrir::AnimationLayer>();
            if (state->ramp >= 0.0)
            {
                state->ramp += delta;
                const double t = state->ramp;
                const double up = t - RampDown - RampHold;
                lean.weight = t >= RampSeconds ? 1.0f
                              : t < RampDown   ? static_cast<float>(1.0 - t / RampDown)
                              : up < 0.0       ? 0.0f
                                               : static_cast<float>(up / RampDown);
                if (state->ramp >= RampSeconds)
                {
                    state->ramp = -1.0;
                }
            }
            for (const Fenrir::AnimationEvent& event : state->events.Read(app))
            {
                if (event.layer == state->lean)
                {
                    ++state->leanEvents;
                    draw->WorldText(Vec3(0.0f, 2.1f, 0.0f), "Sway", Colour::FromSrgb(0x80FF80FF),
                                    {.duration = 0.8f, .relativeTo = state->leaner.skeleton, .size = 20.0f});
                }
            }

            const Fenrir::TextOptions label{.size = 16.0f};
            const auto labelAt = [&](const Rig& rig, std::string_view text) {
                draw->WorldText(rig.root.GetComponent<Fenrir::Transform>().pos + Vec3(-0.5f, 2.35f, 0.0f), text,
                                Colour::White, label);
            };
            labelAt(state->fader, "Crossfades");
            labelAt(state->walker, "Walk + upper-body call");
            labelAt(state->leaner, "Idle + additive lean");

            const Fenrir::PlaybackInfo fade = Fenrir::Animation::Playback(scene, state->fader.skeleton);
            draw->ScreenText({16.0f, 120.0f},
                             std::format("Crossfades: {}, {} curve, {}", CycleNames.at(state->clip),
                                         state->curve == Fenrir::FadeCurve::Smooth ? "smooth" : "linear",
                                         fade.fading ? std::format("fading {:.0f}%", fade.fadeProgress * 100.0f)
                                                     : std::string("settled")),
                             Colour::White);
            const auto& call = state->upperBody.GetComponent<Fenrir::AnimationLayer>();
            draw->ScreenText({16.0f, 144.0f},
                             std::format("Upper body: call at weight {:.0f}, mask {} subtree", call.weight, UpperBody),
                             Colour::White);
            const Fenrir::PlaybackInfo leaning = Fenrir::Animation::Playback(scene, state->lean);
            draw->ScreenText({16.0f, 168.0f},
                             std::format("Lean: additive from {}, weight {:.2f}, clock {:.2f} s (lap {}), {} Sway "
                                         "events",
                                         lean.reference == Fenrir::AdditiveReference::ClipStart ? "its start" : "rest",
                                         lean.weight, leaning.time, leaning.completedLoops, state->leanEvents),
                             Colour::FromSrgb(0x80FF80FF));
        }
    } // namespace

    FenrirDemo::DemoScene Blending()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Blending";
        scene.keyHelp = "C: crossfade to the next clip. V: smooth or linear. I: crossfade, then interrupt it mid-fade. "
                        "U: upper-body call on or off. A: lean from its start or from rest. W: ramp the lean's weight "
                        "to 0 and back.";
        scene.keys = {Fenrir::Key::C, Fenrir::Key::V, Fenrir::Key::C, Fenrir::Key::I, Fenrir::Key::U,
                      Fenrir::Key::A, Fenrir::Key::W, Fenrir::Key::U, Fenrir::Key::A};
        scene.camera = {{0.0f, 1.4f, 6.0f}, 0.0f, -0.1f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace AnimationDemo
