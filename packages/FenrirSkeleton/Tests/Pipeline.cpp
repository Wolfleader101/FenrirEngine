// The pose pipeline (ADR-0033): its stages' order, the order of work within a stage, removing work, what each stage
// may write, the Animate context, inactive scenes pausing, and work hosted through the App (ADR-0039).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "SkeletonTest.hpp"

using Fenrir::Entity;
using Fenrir::Skeletons;
using SkeletonTest::Near;
using SkeletonTest::SkeletonApp;
using SkeletonTest::Translation;

TEST_CASE("the pose stages run Animate, Modify, Late, whatever order their work was added in", "[skeleton][pipeline]")
{
    SkeletonApp test;
    std::vector<std::string> ran;
    test.skeletons.AddLate("late", 0, [&](Fenrir::LateContext&) { ran.emplace_back("Late"); });
    test.skeletons.AddModifier("modify", 0, [&](Fenrir::ModifierContext&) { ran.emplace_back("Modify"); });
    test.skeletons.AddAnimate("animate", 0, [&](Fenrir::AnimateContext&) { ran.emplace_back("Animate"); });

    test.RunFrame();

    INFO("one pass a frame, in the pipeline's order");
    REQUIRE(ran == std::vector<std::string>{"Animate", "Modify", "Late"});
}

TEST_CASE("work in a stage runs by its order, equal orders in the order added", "[skeleton][pipeline]")
{
    SkeletonApp test;
    std::vector<std::string> ran;
    auto record = [&](std::string name) { return [&ran, name](Fenrir::ModifierContext&) { ran.push_back(name); }; };
    test.skeletons.AddModifier("a", 5, record("a"));
    test.skeletons.AddModifier("b", -1, record("b"));
    test.skeletons.AddModifier("c", 5, record("c"));
    test.skeletons.AddModifier("d", 0, record("d"));
    test.skeletons.AddModifier("override", Fenrir::ModifierOrder::Override, record("override"));

    test.RunFrame();

    INFO("-1, 0, then the two 5s as added, then the override's 1000");
    REQUIRE(ran == std::vector<std::string>{"b", "d", "a", "c", "override"});
}

TEST_CASE("a removed handle's work no longer runs", "[skeleton][pipeline]")
{
    SkeletonApp test;
    int runs = 0;
    const Fenrir::PoseStageHandle handle =
        test.skeletons.AddAnimate("counted", 0, [&](Fenrir::AnimateContext&) { ++runs; });
    test.RunFrame();
    REQUIRE(runs == 1);

    INFO("Remove succeeds once, outside the pipeline");
    REQUIRE(test.skeletons.Remove(handle));
    REQUIRE(!test.skeletons.Remove(handle));
    test.RunFrame();
    REQUIRE(runs == 1);
}

TEST_CASE("removing work while the pose pipeline runs is refused", "[skeleton][pipeline]")
{
    SkeletonApp test;
    int runs = 0;
    const Fenrir::PoseStageHandle counted =
        test.skeletons.AddModifier("counted", 0, [&](Fenrir::ModifierContext&) { ++runs; });
    bool removed = true;
    test.skeletons.AddLate("remover", 0, [&](Fenrir::LateContext&) { removed = test.skeletons.Remove(counted); });

    test.RunFrame();
    test.RunFrame();

    INFO("Remove from Late returns false, with an error, and the work stays");
    REQUIRE(!removed);
    REQUIRE(test.Errors("Remove was refused") == 2);
    REQUIRE(runs == 2);
}

TEST_CASE("EditBasePose in Animate is seen by this frame's final pose", "[skeleton][pipeline]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.skeletons.AddAnimate("raise", 0, [&](Fenrir::AnimateContext& context) {
        for (const Entity& instance : context.Instances())
        {
            Skeletons::EditBasePose(context.GetScene(), instance)[0].pos = {0.0f, 5.0f, 0.0f};
        }
    });

    test.RunFrame();

    INFO("the first frame reconciles, animates, prepares and finalises: the tip at 5 + 1 + 1");
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {0.0f, 7.0f, 0.0f}));
}

TEST_CASE("EditBasePose in Modify or Late writes nothing and is logged once per call site", "[skeleton][pipeline]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    std::size_t modifyWrites = 1;
    std::size_t lateWrites = 1;
    test.skeletons.AddModifier("modify", 0, [&](Fenrir::ModifierContext& context) {
        modifyWrites = Skeletons::EditBasePose(context.GetScene(), skeleton).size();
    });
    test.skeletons.AddLate("late", 0, [&](Fenrir::LateContext& context) {
        lateWrites = Skeletons::EditBasePose(context.GetScene(), skeleton).size();
    });

    for (int frame = 0; frame < 3; ++frame)
    {
        test.RunFrame();
    }

    INFO("both get an empty span");
    REQUIRE(modifyWrites == 0);
    REQUIRE(lateWrites == 0);
    INFO("each call site is warned about once, naming the stage, over three frames");
    REQUIRE(test.Warnings("refused in Modify") == 1);
    REQUIRE(test.Warnings("refused in Late") == 1);
    INFO("the pose is unchanged and the instance stays clean");
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {0.0f, 3.0f, 0.0f}));
}

TEST_CASE("EditBasePose after the pose pipeline is seen by the next frame's pose", "[skeleton][pipeline]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();

    // Written between frames, as a Render-phase or frame-end caller would.
    Skeletons::EditBasePose(test.level, skeleton)[0].pos = {0.0f, 2.0f, 0.0f};
    INFO("this frame's final pose is unchanged");
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {0.0f, 3.0f, 0.0f}));

    test.RunFrame();
    INFO("the next frame's has it");
    REQUIRE(Near(Translation(*Skeletons::GetModel(test.level, skeleton, 2)), {0.0f, 4.0f, 0.0f}));
}

TEST_CASE("an Animate item gets the frame pass's timing, its step and its frame-posed Ready instances",
          "[skeleton][pipeline]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Entity framed = SkeletonTest::SkeletonEntity(test.level, chain);
    Entity ticked = SkeletonTest::SkeletonEntity(test.level, chain);
    ticked.GetComponent<Fenrir::SkeletonPose>().timing = Fenrir::PoseTiming::Tick;
    const Entity unavailable = SkeletonTest::SkeletonEntity(test.level, {});

    Fenrir::PoseTiming timing = Fenrir::PoseTiming::Tick;
    double step = 0.0;
    std::vector<Entity> instances;
    test.skeletons.AddAnimate("observer", 0, [&](Fenrir::AnimateContext& context) {
        timing = context.Timing();
        step = context.Step();
        instances.assign(context.Instances().begin(), context.Instances().end());
    });

    test.RunFrame(0.05);

    INFO("the frame pass, stepping by the frame's delta");
    REQUIRE(timing == Fenrir::PoseTiming::Frame);
    REQUIRE(Near(static_cast<float>(step), 0.05f));
    INFO("only the frame-posed Ready instance is listed");
    REQUIRE(instances == std::vector<Entity>{framed});
    (void)unavailable;
}

TEST_CASE("EditBasePose on a tick-posed skeleton in the frame pass of Animate is refused, and works outside it",
          "[skeleton][pipeline]")
{
    SkeletonApp test;
    Entity ticked = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    ticked.GetComponent<Fenrir::SkeletonPose>().timing = Fenrir::PoseTiming::Tick;
    std::size_t written = 1;
    test.skeletons.AddAnimate("wrong timing", 0, [&](Fenrir::AnimateContext& context) {
        written = Skeletons::EditBasePose(context.GetScene(), ticked).size();
    });

    test.RunFrame();
    test.RunFrame();

    INFO("refused in Animate, logged once for the call site");
    REQUIRE(written == 0);
    REQUIRE(test.Warnings("tick-posed") == 1);

    INFO("outside the pipeline it writes either timing");
    REQUIRE(Skeletons::EditBasePose(test.level, ticked).size() == 3);
}

TEST_CASE("an inactive scene's pose pipeline pauses, and resumes without catching up", "[skeleton][pipeline]")
{
    SkeletonApp test;
    Fenrir::Scene& other = test.app.CreateScene("Other");
    const Entity away = SkeletonTest::SkeletonEntity(other, SkeletonTest::AddChain(test));
    REQUIRE(test.skeletons.Ensure(other, away) == Fenrir::PoseState::Ready);
    const std::uint64_t generation = Skeletons::GetPoseGeneration(other, away);

    std::vector<double> steps;
    std::vector<Fenrir::Scene*> scenes;
    test.skeletons.AddAnimate("observer", 0, [&](Fenrir::AnimateContext& context) {
        scenes.push_back(&context.GetScene());
        steps.push_back(context.Step());
    });

    Skeletons::EditBasePose(other, away)[0].pos = {0.0f, 2.0f, 0.0f};
    test.RunFrame(0.1);
    test.RunFrame(0.1);

    INFO("while Level is active, Other's pose is neither prepared nor finalised");
    REQUIRE(Skeletons::GetPoseGeneration(other, away) == generation);
    REQUIRE(Near(Translation(*Skeletons::GetModel(other, away, 2)), {0.0f, 3.0f, 0.0f}));
    REQUIRE(scenes == std::vector<Fenrir::Scene*>{&test.level, &test.level});

    test.app.ChangeActiveScene("Other");
    test.RunFrame(0.02);

    INFO("once active, its pending pose is finalised, and Animate steps by this frame's delta alone");
    REQUIRE(Near(Translation(*Skeletons::GetModel(other, away, 2)), {0.0f, 4.0f, 0.0f}));
    REQUIRE(scenes.back() == &other);
    REQUIRE(Near(static_cast<float>(steps.back()), 0.02f));
}

TEST_CASE("hosted work runs after the owner's at an equal order, and pass-end work after the pass, outside it",
          "[skeleton][pipeline][hosted]")
{
    SkeletonApp test;
    std::vector<std::string> ran;
    auto record = [&](std::string name) { return [&ran, name](Fenrir::ModifierContext&) { ran.push_back(name); }; };
    const auto hosted = Skeletons::HostModifier(test.app, "hosted", 5, record("hosted 5"));
    (void)Skeletons::HostModifier(test.app, "early", 1, record("hosted 1"));
    // Added after the hosted work at the same order, and still before it.
    test.skeletons.AddModifier("owner", 5, record("owner 5"));
    const auto end = Skeletons::HostPassEnd(test.app, [&](Fenrir::PoseTiming timing) {
        ran.push_back(timing == Fenrir::PoseTiming::Frame ? "end of frame pass" : "end of tick pass");
    });
    REQUIRE(hosted.id != 0);
    REQUIRE(end.id != 0);

    test.RunFrame();
    // Each fixed tick's pass ends before the frame pass runs, whether or not it posed anything.
    const auto ticks = std::erase(ran, std::string("end of tick pass"));
    CHECK(ticks == static_cast<std::size_t>(test.app.GetTime().tickCount));
    CHECK(ran == std::vector<std::string>{"hosted 1", "owner 5", "hosted 5", "end of frame pass"});

    ran.clear();
    CHECK(Skeletons::RemoveHosted(test.app, hosted));
    CHECK(Skeletons::RemoveHosted(test.app, end));
    CHECK_FALSE(Skeletons::RemoveHosted(test.app, end));
    test.RunFrame();
    CHECK(ran == std::vector<std::string>{"hosted 1", "owner 5"});
}

TEST_CASE("with FenrirSkeleton not installed, hosting adds nothing", "[skeleton][pipeline][hosted]")
{
    Fenrir::App app{std::make_unique<SkeletonTest::RecordingLogger>(std::make_shared<std::vector<std::string>>(),
                                                                    std::make_shared<std::vector<std::string>>())};
    CHECK(Skeletons::HostLate(app, "late", 0, [](Fenrir::LateContext&) {}).id == 0);
    CHECK(Skeletons::HostPassEnd(app, [](Fenrir::PoseTiming) {}).id == 0);
    CHECK_FALSE(Skeletons::RemoveHosted(app, Fenrir::PoseStageHandle{1}));
}
