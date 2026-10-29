// Skeletons::Install (ADR-0041): its failure without FenrirAssets, a second install, and the skinning source it
// registers and its owner removes.
#include <memory>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "SkeletonTest.hpp"

using Fenrir::Entity;
using Fenrir::Skeletons;
using SkeletonTest::Near;
using SkeletonTest::RecordingLogger;
using SkeletonTest::SkeletonApp;

TEST_CASE("Skeletons::Install fails with one diagnostic without FenrirAssets", "[skeleton][install]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    auto warnings = std::make_shared<std::vector<std::string>>();
    Fenrir::App app(std::make_unique<RecordingLogger>(errors, warnings));

    Skeletons skeletons = Skeletons::Install(app);

    INFO("the owner is not valid, one error says why, and no skinning source is registered");
    REQUIRE(!skeletons.IsValid());
    REQUIRE(errors->size() == 1);
    REQUIRE((*errors)[0].find("FenrirAssets") != std::string::npos);
    REQUIRE(app.TryGetResource<Fenrir::SkinningSourceRegistration>() == nullptr);

    INFO("its calls do nothing, and the app runs on");
    REQUIRE(skeletons.AddModifier("unused", 0, [](Fenrir::ModifierContext&) {}).id == 0);
    app.RunFrame(SkeletonTest::Frame);
}

TEST_CASE("Skeletons::Install registers the skinning source, and its owner's teardown removes it",
          "[skeleton][install]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();

    const Fenrir::SkinningSourceRegistration* registration =
        test.app.TryGetResource<Fenrir::SkinningSourceRegistration>();
    INFO("installed, the app has the skinning source");
    REQUIRE(registration != nullptr);
    REQUIRE(registration->source != nullptr);

    {
        const Skeletons ended = std::move(test.skeletons);
        INFO("moving the owner keeps the install");
        REQUIRE(ended.IsValid());
        REQUIRE(!test.skeletons.IsValid());
        REQUIRE(test.app.TryGetResource<Fenrir::SkinningSourceRegistration>() != nullptr);
    }

    INFO("once the owner is gone, so are the source and every instance");
    REQUIRE(test.app.TryGetResource<Fenrir::SkinningSourceRegistration>() == nullptr);
    REQUIRE(Skeletons::GetPoseState(test.level, skeleton) == Fenrir::PoseState::Missing);
    test.RunFrame();
    REQUIRE(Skeletons::GetPoseState(test.level, skeleton) == Fenrir::PoseState::Missing);
}

TEST_CASE("a second Skeletons::Install is logged and ignored", "[skeleton][install]")
{
    SkeletonApp test;
    Fenrir::SkinningSource* first = test.app.GetResource<Fenrir::SkinningSourceRegistration>().source;

    Skeletons second = Skeletons::Install(test.app);

    INFO("the second owner is not valid, with one error, and the first source stays registered");
    REQUIRE(!second.IsValid());
    REQUIRE(test.Errors("already registered") == 1);
    REQUIRE(test.app.GetResource<Fenrir::SkinningSourceRegistration>().source == first);

    INFO("the pipeline still runs once a frame");
    int runs = 0;
    test.skeletons.AddAnimate("counted", 0, [&](Fenrir::AnimateContext&) { ++runs; });
    test.RunFrame();
    REQUIRE(runs == 1);
}

TEST_CASE("the skinning source answers as the reads do", "[skeleton][install]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, chain);
    const Entity missing = test.level.GetEntityList().CreateEntity();
    test.RunFrame();
    Skeletons::EditBasePose(test.level, skeleton)[1].rot = SkeletonTest::QuarterTurnZ();
    test.RunFrame();

    Fenrir::SkinningSource& source = *test.app.GetResource<Fenrir::SkinningSourceRegistration>().source;
    INFO("state, handle and generation");
    REQUIRE(source.GetPoseState(test.level, skeleton) == Fenrir::PoseState::Ready);
    REQUIRE(source.GetSkeleton(test.level, skeleton) == chain);
    REQUIRE(source.GetPoseGeneration(test.level, skeleton) == Skeletons::GetPoseGeneration(test.level, skeleton));

    INFO("the palette and model matrices, one per bone");
    REQUIRE(source.GetModelMatrices(test.level, skeleton).size() == 3);
    REQUIRE(Near(SkeletonTest::Translation(source.GetModelMatrices(test.level, skeleton)[2]), {-1.0f, 2.0f, 0.0f}));
    REQUIRE(Near(SkeletonTest::Translation(source.GetPalette(test.level, skeleton)[2]), {2.0f, 2.0f, 0.0f}));

    INFO("an entity with no SkeletonPose is Missing, with empty results");
    REQUIRE(source.GetPoseState(test.level, missing) == Fenrir::PoseState::Missing);
    REQUIRE(source.GetSkeleton(test.level, missing).IsEmpty());
    REQUIRE(source.GetPoseGeneration(test.level, missing) == 0);
    REQUIRE(source.GetPalette(test.level, missing).empty());
}
