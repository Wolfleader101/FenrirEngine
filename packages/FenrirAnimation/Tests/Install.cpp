// Animation::Install: its failure without FenrirSkeleton, a second install, and its owner's teardown.
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "AnimationTest.hpp"

using AnimationTest::AnimationApp;
using AnimationTest::RecordingLogger;
using Fenrir::Animation;
using Fenrir::Entity;

TEST_CASE("Animation::Install fails with one diagnostic without a valid Skeletons owner", "[animation][install]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    auto warnings = std::make_shared<std::vector<std::string>>();
    Fenrir::App app(std::make_unique<RecordingLogger>(errors, warnings));
    // With no FenrirAssets, FenrirSkeleton's install fails, leaving an owner that is not valid.
    Fenrir::Skeletons skeletons = Fenrir::Skeletons::Install(app);
    errors->clear();

    Animation animation = Animation::Install(app, skeletons);

    INFO("the owner is not valid, and one error names FenrirSkeleton");
    REQUIRE(!animation.IsValid());
    REQUIRE(errors->size() == 1);
    REQUIRE((*errors)[0].find("FenrirSkeleton") != std::string::npos);
    INFO("the app runs on");
    app.RunFrame(AnimationTest::Eighth);
}

TEST_CASE("Animation::Install with a valid Skeletons owner plays the Animator's clip", "[animation][install]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));

    test.RunFrame();

    INFO("the owner is valid and the first frame samples the clip at its first step");
    REQUIRE(test.animation.IsValid());
    REQUIRE(AnimationTest::Near(AnimationTest::TipX(test, skeleton), 0.125f));
}

TEST_CASE("a second Animation::Install is logged and adds nothing", "[animation][install]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));

    Animation second = Animation::Install(test.app, test.skeletons);
    test.RunFrame();

    INFO("the second owner is not valid, with one error, and the clock advances once a frame, not twice");
    REQUIRE(!second.IsValid());
    REQUIRE(test.Errors("already installed") == 1);
    REQUIRE(AnimationTest::Near(Animation::Playback(test.level, skeleton).time, 0.125));
}

TEST_CASE("once the Animation owner is gone, nothing is sampled and playback reads as Missing", "[animation][install]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));
    test.RunFrame();

    {
        const Animation ended = std::move(test.animation);
        INFO("moving the owner keeps the install");
        REQUIRE(ended.IsValid());
        REQUIRE(!test.animation.IsValid());
    }
    test.RunFrame();

    INFO("the base pose keeps its last sampled value and playback state is gone");
    REQUIRE(AnimationTest::Near(AnimationTest::TipX(test, skeleton), 0.125f));
    REQUIRE(Animation::Playback(test.level, skeleton).state == Fenrir::PoseState::Missing);

    INFO("a new install plays again, from a fresh start");
    test.animation = Animation::Install(test.app, test.skeletons);
    REQUIRE(test.animation.IsValid());
    test.RunFrame();
    REQUIRE(AnimationTest::Near(AnimationTest::TipX(test, skeleton), 0.125f));
}
