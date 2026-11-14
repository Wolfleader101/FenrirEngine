#pragma once

// The ragdoll scripting tests' part of FenrirScripting's ExtensionTestApp, shared by RagdollScriptingTests.cpp and the
// generated defaults test (Generated/FenrirRagdollDefaultsTests.cpp): the fixture CTest names in
// FENRIR_TEST_RAGDOLL_FIXTURE, assets, skeletons, physics and the ragdoll glue installed (no renderer), and the assets,
// skeleton, physics and ragdoll extensions. The rig is FenrirRagdoll's own test humanoid, built in code (ADR-0052).

#include "ExtensionTestApp.hpp"

#include <cstdint>

#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/SkeletonData.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysicsScripting/PhysicsScripting.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "FenrirRagdollScripting/RagdollScripting.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "FenrirSkeletonScripting/SkeletonScripting.hpp"
#include "RagdollTest.hpp"

namespace Fenrir::Testing
{
    /** @brief Assets, skeletons, physics (step poses on the App's thread) and the glue, ending in reverse. */
    struct RagdollPackages
    {
        explicit RagdollPackages(App& app)
            : assets(Assets::Install(app)), skeletons(Skeletons::Install(app)),
              physics(Physics::Install(app, RagdollTest::StepPosesOnly())), ragdolls(Ragdolls::Install(app))
        {
        }

        Assets assets;
        Skeletons skeletons;
        Physics physics;
        Ragdolls ragdolls;
    };

    /** @brief An App with ragdolls installed and scripting running the fixture with their extensions. */
    struct RagdollScriptingApp : ExtensionTestApp<RagdollPackages>
    {
        RagdollScriptingApp() : ExtensionTestApp(Config())
        {
        }

        static ScriptingConfig Config()
        {
            return FixtureConfig(FixtureFromEnvironment("FENRIR_TEST_RAGDOLL_FIXTURE"),
                                 {Assets::ScriptingExtension(), Skeletons::ScriptingExtension(),
                                  Physics::ScriptingExtension(), Ragdolls::ScriptingExtension()});
        }

        /** @brief A Ready skeleton entity posing the test humanoid, standing at the origin, tick-posed. */
        Entity SpawnHumanoid()
        {
            const AssetHandle<Skeleton> handle =
                assets.Store().Add<Skeleton>(RagdollTest::Humanoid(), Get().GetActiveScene());
            Entity entity = Entities().CreateEntity();
            entity.GetComponent<Name>().Set("Walker");
            entity.AddComponent<SkeletonPose>(SkeletonPose{handle, PoseTiming::Tick});
            (void)skeletons.Ensure(Get().GetActiveScene(), entity);
            return entity;
        }
    };
} // namespace Fenrir::Testing
