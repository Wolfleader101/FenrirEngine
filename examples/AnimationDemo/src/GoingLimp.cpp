#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include "AnimationScenes.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirInput/Input.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "Rigs.hpp"

namespace AnimationDemo
{
    namespace
    {
        using Fenrir::Colour;
        using Fenrir::Skeletons;
        using Fenrir::Math::Quat;
        using Fenrir::Math::Vec3;

        /** @brief The left arm's chain, shoulder to hand, and the bone each one points at. */
        constexpr std::array<std::string_view, 3> ArmBones{"upperarm_l", "lowerarm_l", "hand_l"};
        constexpr std::array<std::string_view, 3> ArmEnds{"lowerarm_l", "hand_l", "middle_01_l"};
        /** @brief How long B takes to blend the arm back into the walk. */
        constexpr double BlendBackSeconds = 1.5;
        /** @brief The limp arm's sway: how far from straight down, and how fast. */
        constexpr float SwayAmount = 0.35f;
        constexpr float SwaySpeed = 2.4f;

        /** @brief Scene 6's state, a resource of its FenrirScene. */
        struct LimpState
        {
            Rig walker;
            std::array<std::int32_t, ArmBones.size()> bones{};
            std::array<std::int32_t, ArmEnds.size()> ends{};
            /** @brief The override weight on the arm's subtree, which the keys step and ramp. */
            float weight = 0.0f;
            /** @brief Seconds into a blend back, or below 0 when none runs. */
            double blendBack = -1.0;
            bool sway = true;
            double time = 0.0;
        };

        /** @brief The rotation turning direction `from` onto `to`, both unit length. */
        Quat Between(const Vec3& from, const Vec3& to)
        {
            const float dot = glm::dot(from, to);
            if (dot < -0.9999f)
            {
                const Vec3 axis = std::abs(from.x) < 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 0.0f, 1.0f);
                return glm::angleAxis(glm::pi<float>(), glm::normalize(glm::cross(from, axis)));
            }
            const Vec3 axis = glm::cross(from, to);
            return glm::normalize(Quat(1.0f + dot, axis.x, axis.y, axis.z));
        }

        /** @brief A bone's base-pose world position and rotation (a frame late, as base reads are). */
        std::optional<std::pair<Vec3, Quat>> BaseWorld(Fenrir::Scene& scene, const Fenrir::Entity& skeleton,
                                                       std::int32_t bone)
        {
            const std::optional<Fenrir::Math::Mat4> model = Skeletons::GetBaseModel(scene, skeleton, bone);
            if (!model)
            {
                return std::nullopt;
            }
            const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(skeleton) * *model;
            const Fenrir::Math::Mat3 axes(glm::normalize(Vec3(world[0])), glm::normalize(Vec3(world[1])),
                                          glm::normalize(Vec3(world[2])));
            return std::pair{Vec3(world[3]), glm::normalize(glm::quat_cast(axes))};
        }

        /**
         * @brief The demo's stand-in for ragdoll glue: world targets for the arm hanging from the shoulder, swaying,
         * each bone turned from its animated direction to its sagging one and placed at the end of its parent. Written
         * each frame in Update, as glue writes bodies' poses.
         */
        void WriteSag(Fenrir::Scene& scene, const LimpState& state)
        {
            const Fenrir::Entity& skeleton = state.walker.skeleton;
            const auto t = static_cast<float>(state.time) * SwaySpeed;
            const float sway = state.sway ? SwayAmount : 0.0f;
            const Vec3 hang = glm::normalize(Vec3(std::sin(t) * sway, -1.0f, std::cos(t * 0.7f) * sway * 0.6f));
            std::optional<Vec3> placed;
            for (std::size_t i = 0; i < ArmBones.size(); ++i)
            {
                const auto from = BaseWorld(scene, skeleton, state.bones.at(i));
                const auto to = BaseWorld(scene, skeleton, state.ends.at(i));
                if (!from || !to)
                {
                    return;
                }
                const Vec3 animated = to->first - from->first;
                const float length = glm::length(animated);
                if (length <= 1e-5f)
                {
                    return;
                }
                // Each joint a little more bent toward the front than the last, as a dead arm hangs.
                const Vec3 direction = glm::normalize(hang + Vec3(0.0f, 0.0f, 0.12f * static_cast<float>(i)));
                Fenrir::Target target;
                target.space = Fenrir::PoseSpace::World;
                target.channels = Fenrir::Channels::Translation | Fenrir::Channels::Rotation;
                target.trs.pos = placed.value_or(from->first);
                target.trs.rot = glm::normalize(Between(animated / length, direction) * from->second);
                Skeletons::SetOverride(scene, skeleton, state.bones.at(i), target);
                placed = target.trs.pos + direction * length;
            }
        }

        void ClearSag(Fenrir::Scene& scene, const LimpState& state)
        {
            for (const std::int32_t bone : state.bones)
            {
                Skeletons::ClearOverride(scene, state.walker.skeleton, bone);
            }
        }

        void Spawn(Fenrir::App& app, Fenrir::Scene& scene)
        {
            AddStage(app, scene);
            LimpState& state = scene.AddResource<LimpState>();
            // Turned so its left arm faces the camera.
            state.walker = SpawnRig(app, scene, QuaterniusFile, Vec3(0.0f), -0.6f);
            if (!state.walker.IsValid())
            {
                return;
            }
            Play(state.walker.skeleton,
                 LoadClip(app, scene, QuaterniusClips.at(static_cast<std::size_t>(QuaterniusClip::Walk))));
            for (std::size_t i = 0; i < ArmBones.size(); ++i)
            {
                state.bones.at(i) = Skeletons::FindBone(scene, state.walker.skeleton, ArmBones.at(i));
                state.ends.at(i) = Skeletons::FindBone(scene, state.walker.skeleton, ArmEnds.at(i));
                if (state.bones.at(i) == Fenrir::NoBone || state.ends.at(i) == Fenrir::NoBone)
                {
                    app.Logger()->Error("AnimationDemo: the Quaternius human has no {} or {}", ArmBones.at(i),
                                        ArmEnds.at(i));
                    state.walker = {};
                    return;
                }
            }
        }

        void Update(Fenrir::App& app, Fenrir::Scene& scene)
        {
            LimpState* state = scene.GetResource<LimpState>();
            Fenrir::DebugDraw* draw = scene.GetResource<Fenrir::DebugDraw>();
            if (state == nullptr || draw == nullptr || !state->walker.IsValid())
            {
                return;
            }
            const double delta = app.GetTime().deltaTime;
            state->time += delta;
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            if (keyboard.JustPressed(Fenrir::Key::L))
            {
                // Going limp is a step: the arm is all override at once.
                state->weight = 1.0f;
                state->blendBack = -1.0;
                app.Logger()->Info("Going limp: L, the arm goes limp");
            }
            if (keyboard.JustPressed(Fenrir::Key::B) && state->weight > 0.0f)
            {
                state->blendBack = 0.0;
                app.Logger()->Info("Going limp: B, the arm blends back into the walk");
            }
            if (keyboard.JustPressed(Fenrir::Key::S))
            {
                state->sway = !state->sway;
                app.Logger()->Info("Going limp: S, sway {}", state->sway ? "on" : "off");
            }
            if (state->blendBack >= 0.0)
            {
                // Game code ramps the weight: FenrirSkeleton has no fades.
                state->blendBack += delta;
                state->weight = static_cast<float>(std::max(0.0, 1.0 - state->blendBack / BlendBackSeconds));
                if (state->weight <= 0.0f)
                {
                    state->blendBack = -1.0;
                }
            }

            // The glue: while the arm has any weight, refresh its targets; at 0, clear them (ADR-0038).
            const std::int32_t shoulder = state->bones.front();
            Skeletons::SetOverrideWeight(scene, state->walker.skeleton, shoulder, state->weight,
                                         Fenrir::OverrideScope::BoneSubtree);
            if (state->weight > 0.0f)
            {
                WriteSag(scene, *state);
            }
            else
            {
                ClearSag(scene, *state);
            }

            const bool entries = Skeletons::GetOverride(scene, state->walker.skeleton, shoulder).has_value();
            draw->WorldText(state->walker.root.GetComponent<Fenrir::Transform>().pos + Vec3(-0.4f, 2.3f, 0.0f),
                            "Walking, left arm overridden", Colour::White, {.size = 16.0f});
            draw->ScreenText({16.0f, 120.0f},
                             std::format("Left arm ({} subtree): override weight {:.2f}, {}, {}, sway {}",
                                         ArmBones.front(), state->weight, entries ? "targets written" : "no targets",
                                         state->blendBack >= 0.0 ? "blending back"
                                         : state->weight > 0.0f  ? "limp"
                                                                 : "animated",
                                         state->sway ? "on" : "off"),
                             Colour::White);
        }
    } // namespace

    FenrirDemo::DemoScene GoingLimp()
    {
        FenrirDemo::DemoScene scene;
        scene.name = "Going limp";
        scene.keyHelp = "L: the left arm goes limp. B: blend it back into the walk. S: its sway on or off.";
        scene.keys = {Fenrir::Key::L, Fenrir::Key::S, Fenrir::Key::S, Fenrir::Key::B, Fenrir::Key::L, Fenrir::Key::B};
        scene.camera = {{0.0f, 1.4f, 3.4f}, 0.0f, -0.12f};
        scene.spawn = Spawn;
        scene.update = Update;
        return scene;
    }
} // namespace AnimationDemo
