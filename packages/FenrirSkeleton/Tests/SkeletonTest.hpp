#pragma once

#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

namespace SkeletonTest
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

    inline constexpr double Frame = 1.0 / 60.0;

    // An App with assets and skeletons installed and one scene, "Level", active. Members end in reverse order, so the
    // owners are destroyed before the App, as they must be.
    struct SkeletonApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        std::shared_ptr<std::vector<std::string>> warnings = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<RecordingLogger>(errors, warnings)};
        Fenrir::Scene& level = app.CreateScene("Level");
        Fenrir::Assets assets = Fenrir::Assets::Install(app);
        Fenrir::AssetStore& store = assets.Store();
        Fenrir::Skeletons skeletons = Fenrir::Skeletons::Install(app);

        SkeletonApp()
        {
            app.ChangeActiveScene("Level");
        }

        void RunFrame(double deltaTime = Frame)
        {
            app.RunFrame(deltaTime);
        }

        // How many messages logged so far, errors or warnings, contain `text`.
        [[nodiscard]] std::size_t Count(const std::vector<std::string>& messages, std::string_view text) const
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

    // The three-bone chain: root at (0, 1, 0), then mid and tip each one unit up from their parent. At rest the tip is
    // at (0, 3, 0) in model space. Inverse binds are the inverse of each bone's rest model matrix, so the palette at
    // rest is the identity.
    inline Fenrir::SkeletonData ChainData()
    {
        Fenrir::SkeletonData skeleton;
        skeleton.bones = {Bone("root", -1, {0.0f, 1.0f, 0.0f}), Bone("mid", 0, {0.0f, 1.0f, 0.0f}),
                          Bone("tip", 1, {0.0f, 1.0f, 0.0f})};
        for (std::size_t i = 0; i < skeleton.bones.size(); ++i)
        {
            const float height = static_cast<float>(i + 1);
            skeleton.bones[i].inverseBind =
                Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), Fenrir::Math::Vec3(0.0f, -height, 0.0f));
        }
        return skeleton;
    }

    inline Fenrir::AssetHandle<Fenrir::Skeleton> AddChain(SkeletonApp& test)
    {
        return test.store.Add<Fenrir::Skeleton>(ChainData(), test.level);
    }

    // A skeleton entity in `scene` posing `skeleton`.
    inline Fenrir::Entity SkeletonEntity(Fenrir::Scene& scene, Fenrir::AssetHandle<Fenrir::Skeleton> skeleton)
    {
        Fenrir::Entity entity = scene.GetEntityList().CreateEntity();
        entity.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{skeleton});
        return entity;
    }

    inline Fenrir::Math::Quat QuarterTurnZ()
    {
        return Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(90.0f), Fenrir::Math::Vec3(0.0f, 0.0f, 1.0f));
    }

    inline bool Near(float a, float b, float tolerance = 1e-4f)
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

    inline Fenrir::Math::Vec3 Translation(const Fenrir::Math::Mat4& matrix)
    {
        return Fenrir::Math::Vec3(matrix[3]);
    }
} // namespace SkeletonTest
