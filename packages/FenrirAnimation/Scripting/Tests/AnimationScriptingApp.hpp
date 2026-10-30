#pragma once

// The animation scripting tests' part of FenrirScripting's ExtensionTestApp, shared by AnimationScriptingTests.cpp and
// the generated defaults test (Generated/FenrirAnimationDefaultsTests.cpp): the fixture CTest names in
// FENRIR_TEST_ANIMATION_FIXTURE, assets, skeletons and animation installed, and their three extensions.

#include "ExtensionTestApp.hpp"

#include "FenrirAnimation/Animation.hpp"
#include "FenrirAnimationScripting/AnimationScripting.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "FenrirSkeletonScripting/SkeletonScripting.hpp"

namespace Fenrir::Testing
{
    /** @brief Assets, skeletons and animation, installed in that order and ending in reverse. */
    struct AnimationPackages
    {
        explicit AnimationPackages(App& app)
            : assets(Assets::Install(app)), skeletons(Skeletons::Install(app)),
              animation(Animation::Install(app, skeletons))
        {
        }

        Assets assets;
        Skeletons skeletons;
        Animation animation;
    };

    /** @brief An App with the animation stack installed and scripting running the fixture with its extensions. */
    struct AnimationScriptingApp : ExtensionTestApp<AnimationPackages>
    {
        AnimationScriptingApp() : ExtensionTestApp(Config())
        {
        }

        static ScriptingConfig Config()
        {
            return FixtureConfig(
                FixtureFromEnvironment("FENRIR_TEST_ANIMATION_FIXTURE"),
                {Assets::ScriptingExtension(), Skeletons::ScriptingExtension(), Animation::ScriptingExtension()});
        }
    };
} // namespace Fenrir::Testing
