#pragma once

// The skeleton scripting tests' part of FenrirScripting's ExtensionTestApp, shared by SkeletonScriptingTests.cpp and
// the generated defaults test (Generated/FenrirSkeletonDefaultsTests.cpp): the fixture CTest names in
// FENRIR_TEST_SKELETON_FIXTURE, assets and skeletons installed (before scripting, so the fixture's stage systems find
// the pose pipeline), and the assets and skeleton extensions.

#include "ExtensionTestApp.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "FenrirSkeletonScripting/SkeletonScripting.hpp"

namespace Fenrir::Testing
{
    /** @brief Assets and skeletons, installed in that order and ending in reverse. */
    struct SkeletonPackages
    {
        explicit SkeletonPackages(App& app) : assets(Assets::Install(app)), skeletons(Skeletons::Install(app))
        {
        }

        Assets assets;
        Skeletons skeletons;
    };

    /** @brief One bone of a test chain, `parent`'s child one unit up. */
    inline SkeletonBone ChainBone(std::string name, std::int32_t parent)
    {
        SkeletonBone bone;
        bone.sourceName = std::move(name);
        bone.parent = parent;
        bone.rest.pos = Math::Vec3(0.0f, 1.0f, 0.0f);
        return bone;
    }

    /** @brief An App with assets and skeletons installed and scripting running the fixture with their extensions. */
    struct SkeletonScriptingApp : ExtensionTestApp<SkeletonPackages>
    {
        SkeletonScriptingApp() : ExtensionTestApp(Config())
        {
        }

        static ScriptingConfig Config()
        {
            return FixtureConfig(FixtureFromEnvironment("FENRIR_TEST_SKELETON_FIXTURE"),
                                 {Assets::ScriptingExtension(), Skeletons::ScriptingExtension()});
        }

        /** @brief The three-bone chain root, mid, tip, each one unit above its parent. */
        AssetHandle<Skeleton> AddChain()
        {
            SkeletonData data;
            data.bones = {ChainBone("root", NoBone), ChainBone("mid", 0), ChainBone("tip", 1)};
            for (std::size_t i = 0; i < data.bones.size(); ++i)
            {
                data.bones[i].inverseBind =
                    Math::Translate(Math::Mat4(1.0f), Math::Vec3(0.0f, -static_cast<float>(i + 1), 0.0f));
            }
            return assets.Store().Add<Skeleton>(std::move(data), Get().GetActiveScene());
        }

        /** @brief A Ready skeleton entity posing the chain at `position`, of `timing`. */
        Entity SpawnChain(const Math::Vec3& position, PoseTiming timing = PoseTiming::Frame)
        {
            Entity entity = Entities().CreateEntity();
            entity.GetComponent<Transform>().pos = position;
            entity.AddComponent<SkeletonPose>(SkeletonPose{AddChain(), timing});
            (void)skeletons.Ensure(Get().GetActiveScene(), entity);
            return entity;
        }
    };
} // namespace Fenrir::Testing
