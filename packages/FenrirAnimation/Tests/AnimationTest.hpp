#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAnimation/Animation.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ClipData.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace AnimationTest
{
    // Keeps what was logged as warnings and errors, so a test can check a problem was reported, and how often. A second
    // ConsoleLogger in one process throws, so the tests never use one.
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        RecordingLogger(std::shared_ptr<std::vector<std::string>> errors,
                        std::shared_ptr<std::vector<std::string>> warnings)
            : m_errors(std::move(errors)), m_warnings(std::move(warnings))
        {
        }
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view message) override
        {
            m_warnings->emplace_back(message);
        }
        void ErrorImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_errors;
        std::shared_ptr<std::vector<std::string>> m_warnings;
    };

    // Frame deltas the tests step by: exact in binary, so cursor arithmetic in the expectations is exact too.
    inline constexpr double Eighth = 0.125;

    // An App with assets, skeletons and animation installed and one scene, "Level", active. Members end in reverse
    // order, so the owners are destroyed before the App and Animation before Skeletons, as they must be.
    struct AnimationApp
    {
        explicit AnimationApp(Fenrir::AnimationConfig config = {})
            : animation(Fenrir::Animation::Install(app, skeletons, config))
        {
            app.ChangeActiveScene("Level");
        }

        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        std::shared_ptr<std::vector<std::string>> warnings = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<RecordingLogger>(errors, warnings)};
        Fenrir::Scene& level = app.CreateScene("Level");
        Fenrir::Assets assets = Fenrir::Assets::Install(app);
        Fenrir::AssetStore& store = assets.Store();
        Fenrir::Skeletons skeletons = Fenrir::Skeletons::Install(app);
        Fenrir::Animation animation;

        void RunFrame(double deltaTime = Eighth)
        {
            app.RunFrame(deltaTime);
        }

        // How many messages logged so far, errors or warnings, contain `text`.
        [[nodiscard]] static std::size_t Count(const std::vector<std::string>& messages, std::string_view text)
        {
            std::size_t found = 0;
            for (const std::string& message : messages)
            {
                if (message.find(text) != std::string::npos)
                {
                    ++found;
                }
            }
            return found;
        }

        [[nodiscard]] std::size_t Warnings(std::string_view text) const
        {
            return Count(*warnings, text);
        }

        [[nodiscard]] std::size_t Errors(std::string_view text) const
        {
            return Count(*errors, text);
        }
    };

    inline Fenrir::SkeletonBone Bone(std::string name, std::int32_t parent, Fenrir::Math::Vec3 pos)
    {
        Fenrir::SkeletonBone bone;
        bone.sourceName = std::move(name);
        bone.parent = parent;
        bone.rest.pos = pos;
        return bone;
    }

    // The three-bone chain: root at (0, 1, 0), then mid and tip each one unit up from their parent.
    inline Fenrir::SkeletonData ChainData()
    {
        Fenrir::SkeletonData skeleton;
        skeleton.bones = {Bone("root", -1, {0.0f, 1.0f, 0.0f}), Bone("mid", 0, {0.0f, 1.0f, 0.0f}),
                          Bone("tip", 1, {0.0f, 1.0f, 0.0f})};
        return skeleton;
    }

    inline Fenrir::AssetHandle<Fenrir::Skeleton> AddChain(AnimationApp& test)
    {
        return test.store.Add<Fenrir::Skeleton>(ChainData(), test.level);
    }

    // A skeleton entity in `scene` posing `skeleton`, with an Animator playing `clip`.
    inline Fenrir::Entity Animated(Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Skeleton> skeleton,
                                   Fenrir::AssetHandle<Fenrir::Clip> clip,
                                   Fenrir::ClipLoop loop = Fenrir::ClipLoop::Loop, float speed = 1.0f)
    {
        Fenrir::Entity entity = scene.GetEntityList().CreateEntity();
        entity.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{skeleton});
        Fenrir::Animator animator;
        animator.clip = clip;
        animator.loop = loop;
        animator.speed = speed;
        entity.AddComponent<Fenrir::Animator>(animator);
        return entity;
    }

    // A channel on the bone named `target`, keyed at `times` with `values`.
    inline Fenrir::ClipChannel Channel(std::string target, Fenrir::ClipPath path,
                                       Fenrir::ClipInterpolation interpolation, std::vector<float> times,
                                       std::vector<Fenrir::Math::Vec4> values)
    {
        Fenrir::ClipChannel channel;
        channel.targetName = std::move(target);
        channel.path = path;
        channel.interpolation = interpolation;
        channel.times = std::move(times);
        channel.values = std::move(values);
        return channel;
    }

    // A clip of duration 1 moving the tip's x linearly from 0 to 1, with events named after their times.
    inline Fenrir::ClipData SlideData(const std::vector<float>& eventTimes = {})
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel("tip", Fenrir::ClipPath::Translation, Fenrir::ClipInterpolation::Linear, {0.0f, 1.0f},
                                 {{0.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f}})};
        for (const float time : eventTimes)
        {
            clip.events.push_back({time, std::to_string(time)});
        }
        return clip;
    }

    inline Fenrir::AssetHandle<Fenrir::Clip> AddSlide(AnimationApp& test, const std::vector<float>& eventTimes = {})
    {
        return test.store.Add<Fenrir::Clip>(SlideData(eventTimes), test.level);
    }

    // The tip's base-pose x: the slide clip's sampled value.
    inline float TipX(AnimationApp& test, Fenrir::Entity skeleton)
    {
        const auto local = Fenrir::Skeletons::GetBaseLocal(test.level, skeleton, 2);
        return local ? local->pos.x : NAN;
    }

    // Each event read since the reader last read, by authored time.
    inline std::vector<float> ReadTimes(AnimationApp& test, Fenrir::EventReader<Fenrir::AnimationEvent>& reader)
    {
        std::vector<float> times;
        for (const Fenrir::AnimationEvent& event : reader.Read(test.app))
        {
            times.push_back(event.time);
        }
        return times;
    }

    inline Fenrir::Math::Quat AboutZ(float degrees)
    {
        return Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(degrees), Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
    }

    // A rotation key as a clip stores it: (x, y, z, w).
    inline Fenrir::Math::Vec4 Key(const Fenrir::Math::Quat& q)
    {
        return {q.x, q.y, q.z, q.w};
    }

    inline bool Near(float a, float b, float tolerance = 1e-4f)
    {
        return std::abs(a - b) < tolerance;
    }

    inline bool Near(double a, double b, double tolerance = 1e-9)
    {
        return std::abs(a - b) < tolerance;
    }

    inline bool Near(const Fenrir::Math::Vec3& a, const Fenrir::Math::Vec3& b, float tolerance = 1e-4f)
    {
        return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
    }

    // Rotations that turn vectors the same way: q and -q are one rotation.
    inline bool Near(const Fenrir::Math::Quat& a, const Fenrir::Math::Quat& b, float tolerance = 1e-4f)
    {
        const float dot = std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
        return Near(dot, 1.0f, tolerance);
    }
} // namespace AnimationTest
