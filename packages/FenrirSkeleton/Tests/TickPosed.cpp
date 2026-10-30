// Tick-posed skeletons (ADR-0040), headless with no animation or physics: an Animate item stands in for a clip, moving
// the chain's root along +X one unit per tick of its own clock, so a pose names the clock that made it. Frame deltas
// are whole ticks or leave a quarter or a half over, so no tick count hangs on the accumulator's rounding.
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirScheduler/Scheduler.hpp"
#include "SkeletonTest.hpp"

using Fenrir::Entity;
using Fenrir::PoseKind;
using Fenrir::PoseTiming;
using Fenrir::Skeletons;
using Fenrir::Math::Vec3;
using SkeletonTest::Near;
using SkeletonTest::SkeletonApp;
using SkeletonTest::Translation;

namespace
{
    constexpr std::int32_t Root = 0;
    constexpr std::int32_t Tip = 2;
    constexpr double Tick = 1.0 / 60.0;
    // The root moves this far per second of clock, so one tick moves it one unit.
    constexpr double Speed = 60.0;

    // What one Animate call was given.
    struct AnimateCall
    {
        PoseTiming timing = PoseTiming::Frame;
        double step = 0.0;
        std::vector<Entity> instances;
    };

    // The chain app with a clip stand-in: each listed instance's clock advances by the pass's step, and its root is
    // put at x = clock × Speed, turning about Z by as many quarter turns. Clocks are per entity, kept across passes,
    // so switching timing resumes them.
    struct Clocked
    {
        SkeletonApp test;
        std::unordered_map<std::uint32_t, double> clocks;
        std::vector<AnimateCall> calls;

        Clocked()
        {
            test.skeletons.AddAnimate("clock", 0, [this](Fenrir::AnimateContext& context) {
                calls.push_back({context.Timing(), context.Step(),
                                 std::vector<Entity>(context.Instances().begin(), context.Instances().end())});
                for (const Entity& instance : context.Instances())
                {
                    double& clock = clocks[instance.GetId()];
                    clock += context.Step();
                    Fenrir::Transform& root = Skeletons::EditBasePose(context.GetScene(), instance)[Root];
                    root.pos = Vec3(static_cast<float>(clock * Speed), 1.0f, 0.0f);
                    root.rot = Fenrir::Math::AngleAxis(
                        static_cast<float>(clock * Speed) * Fenrir::Math::DegToRad(90.0f), Vec3(0.0f, 0.0f, 1.0f));
                }
            });
        }

        Entity Make(PoseTiming timing)
        {
            Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
            skeleton.GetComponent<Fenrir::SkeletonPose>().timing = timing;
            return skeleton;
        }

        [[nodiscard]] std::size_t Calls(PoseTiming timing) const
        {
            std::size_t count = 0;
            for (const AnimateCall& call : calls)
            {
                count += call.timing == timing ? 1 : 0;
            }
            return count;
        }
    };

    float RootX(Fenrir::Scene& scene, Entity skeleton, PoseKind kind)
    {
        return Skeletons::GetBaseLocal(scene, skeleton, Root, kind)->pos.x;
    }
} // namespace

TEST_CASE("a tick-posed skeleton evaluates once per tick, by tickRate, and not in a frame with no tick",
          "[skeleton][tick]")
{
    Clocked run;
    const Entity skeleton = run.Make(PoseTiming::Tick);

    run.test.RunFrame(0.5 * Tick);
    INFO("half a tick: no tick, so nothing is evaluated");
    REQUIRE(run.Calls(PoseTiming::Tick) == 0);
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Simulation), 0.0f));

    run.test.RunFrame(0.5 * Tick);
    INFO("the second half completes one tick: one evaluation, stepping by tickRate");
    REQUIRE(run.Calls(PoseTiming::Tick) == 1);
    REQUIRE(run.calls[1].timing == PoseTiming::Tick);
    REQUIRE(run.calls[1].step == run.test.app.GetTime().tickRate);
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Simulation), 1.0f));

    run.test.RunFrame(3.25 * Tick);
    INFO("three ticks in one frame: three evaluations");
    REQUIRE(run.Calls(PoseTiming::Tick) == 4);
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Simulation), 4.0f));

    run.test.RunFrame(10.0 * Tick);
    INFO("ten ticks owed, five run (maxTicksPerFrame): the clock keeps step with the ticks that ran");
    REQUIRE(run.Calls(PoseTiming::Tick) == 9);
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Simulation), 9.0f));

    INFO("the frame pass never lists it");
    for (const AnimateCall& call : run.calls)
    {
        REQUIRE((call.timing == PoseTiming::Tick || call.instances.empty()));
    }
}

TEST_CASE("a tick pass runs only while the active scene has a tick-posed skeleton", "[skeleton][tick]")
{
    Clocked run;
    Entity skeleton = run.Make(PoseTiming::Frame);

    run.test.RunFrame(2.0 * Tick);
    INFO("only frame-posed skeletons: two ticks, no tick pass");
    REQUIRE(run.Calls(PoseTiming::Tick) == 0);
    REQUIRE(run.Calls(PoseTiming::Frame) == 1);

    skeleton.GetComponent<Fenrir::SkeletonPose>().timing = PoseTiming::Tick;
    run.test.RunFrame(2.0 * Tick);
    INFO("once it is tick-posed, a pass per tick");
    REQUIRE(run.Calls(PoseTiming::Tick) == 2);
}

TEST_CASE("the render base pose is the last two ticks blended at accumulator / tickRate", "[skeleton][tick]")
{
    Clocked run;
    const Entity skeleton = run.Make(PoseTiming::Tick);

    run.test.RunFrame(1.5 * Tick);
    INFO("one tick, half a tick left over: halfway from the seeded rest (0) to the tick (1)");
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Simulation), 1.0f));
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Render), 0.5f));
    INFO("rotation slerps: half a quarter turn");
    const Fenrir::Math::Quat eighth = Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(45.0f), Vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(Near(Skeletons::GetBaseLocal(run.test.level, skeleton, Root)->rot, eighth));
    INFO("the final pose, and so the palette and attachments, come from the render pose");
    REQUIRE(Near(Translation(*Skeletons::GetModel(run.test.level, skeleton, Root)), {0.5f, 1.0f, 0.0f}));
    const std::uint64_t generation = Skeletons::GetPoseGeneration(run.test.level, skeleton);

    run.test.RunFrame(Tick);
    INFO("another tick, still half a tick over: halfway from 1 to 2");
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Render), 1.5f));
    REQUIRE(Skeletons::GetPoseGeneration(run.test.level, skeleton) > generation);

    run.test.RunFrame(0.25 * Tick);
    INFO("a frame with no tick blends the same two ticks at its new fraction, three quarters");
    REQUIRE(run.Calls(PoseTiming::Tick) == 2);
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Render), 1.75f));
}

TEST_CASE("Simulation reads in Tick give the tick-time pose, and equal Render reads on a frame-posed skeleton",
          "[skeleton][tick]")
{
    Clocked run;
    Entity ticked = run.Make(PoseTiming::Tick);
    ticked.GetComponent<Fenrir::Transform>().pos = {10.0f, 0.0f, 0.0f};
    const Entity framed = run.Make(PoseTiming::Frame);

    // What a Tick reader saw, and what sampling at that tick's time gives: the root at x = tick count.
    std::vector<std::pair<Vec3, float>> seen;
    run.test.app.AddSequentialSystem(Fenrir::SchedulePriority::Tick, [&](Fenrir::App& app) {
        const std::optional<Fenrir::Math::Mat4> tip =
            Skeletons::GetBoneWorldMatrix(run.test.level, ticked, Tip, PoseKind::Simulation);
        seen.emplace_back(Translation(*tip), static_cast<float>(app.GetTime().tickCount));
    });

    run.test.RunFrame(1.5 * Tick);
    run.test.RunFrame(2.75 * Tick);

    INFO("each tick reads its own pose, under the skeleton entity's world matrix, whatever the frame shows");
    REQUIRE(seen.size() == 4);
    for (const auto& [tip, tick] : seen)
    {
        // The root turns a quarter per tick about Z, so the tip, two units above it, swings with it.
        const Fenrir::Math::Quat turn =
            Fenrir::Math::AngleAxis(tick * Fenrir::Math::DegToRad(90.0f), Vec3(0.0f, 0.0f, 1.0f));
        const Vec3 expected = Vec3(10.0f + tick, 1.0f, 0.0f) + turn * Vec3(0.0f, 2.0f, 0.0f);
        REQUIRE(Near(tip, expected));
    }

    INFO("on a frame-posed skeleton the two kinds read the same pose");
    for (std::int32_t bone = 0; bone < 3; ++bone)
    {
        REQUIRE(*Skeletons::GetBoneWorldMatrix(run.test.level, framed, bone, PoseKind::Simulation) ==
                *Skeletons::GetBoneWorldMatrix(run.test.level, framed, bone, PoseKind::Render));
        REQUIRE(*Skeletons::GetBaseModel(run.test.level, framed, bone, PoseKind::Simulation) ==
                *Skeletons::GetBaseModel(run.test.level, framed, bone, PoseKind::Render));
    }
    REQUIRE(Skeletons::GetBaseLocals(run.test.level, framed, PoseKind::Simulation).data() ==
            Skeletons::GetBaseLocals(run.test.level, framed, PoseKind::Render).data());
}

TEST_CASE("modifiers change a tick-posed skeleton's render pose only", "[skeleton][tick]")
{
    Clocked run;
    const Entity skeleton = run.Make(PoseTiming::Tick);
    run.test.skeletons.AddModifier("lift", 0, [&](Fenrir::ModifierContext& context) {
        Fenrir::PoseEdit edit = context.Edit(skeleton);
        Fenrir::Target lift;
        lift.space = Fenrir::PoseSpace::Local;
        lift.trs.pos = {0.0f, 5.0f, 0.0f};
        lift.channels = Fenrir::Channels::Translation;
        edit.Write(Tip, lift);
    });

    run.test.RunFrame(Tick);
    run.test.RunFrame(Tick);

    INFO("the render pose has the lifted tip");
    REQUIRE(Near(Skeletons::GetLocal(run.test.level, skeleton, Tip)->pos, {0.0f, 5.0f, 0.0f}));
    INFO("the simulation pose does not: the tip is one unit above mid, as at rest");
    REQUIRE(
        Near(Skeletons::GetBaseLocal(run.test.level, skeleton, Tip, PoseKind::Simulation)->pos, {0.0f, 1.0f, 0.0f}));
    const Fenrir::Math::Mat4 tip = *Skeletons::GetBaseModel(run.test.level, skeleton, Tip, PoseKind::Simulation);
    const Fenrir::Math::Mat4 mid = *Skeletons::GetBaseModel(run.test.level, skeleton, 1, PoseKind::Simulation);
    REQUIRE(Near(Translation(tip),
                 Translation(mid * Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), Vec3(0.0f, 1.0f, 0.0f)))));
    REQUIRE(*Skeletons::GetBoneWorldMatrix(run.test.level, skeleton, Tip, PoseKind::Simulation) == tip);
}

TEST_CASE("switching timing does not jump, and switching back resumes the clock where the ticks left it",
          "[skeleton][tick]")
{
    Clocked run;
    Entity skeleton = run.Make(PoseTiming::Frame);
    for (int frame = 0; frame < 3; ++frame)
    {
        run.test.RunFrame(Tick);
    }
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Render), 3.0f));

    skeleton.GetComponent<Fenrir::SkeletonPose>().timing = PoseTiming::Tick;
    run.test.RunFrame(Tick);
    INFO("seeded from the frame pose: the tick evaluates on from 3, and the frame, at fraction 0, still shows 3");
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Simulation), 4.0f));
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Render), 3.0f));
    run.test.RunFrame(Tick);
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Render), 4.0f));

    skeleton.GetComponent<Fenrir::SkeletonPose>().timing = PoseTiming::Frame;
    const std::size_t ticks = run.Calls(PoseTiming::Tick);
    run.test.RunFrame(Tick);
    INFO("frame-posed again: no tick pass, and the frame clock resumes from the tick clock's 5");
    REQUIRE(run.Calls(PoseTiming::Tick) == ticks);
    REQUIRE(Near(RootX(run.test.level, skeleton, PoseKind::Render), 6.0f));
}

TEST_CASE("Animate items get each pass's timing, step and instances", "[skeleton][tick]")
{
    Clocked run;
    const Entity framed = run.Make(PoseTiming::Frame);
    const Entity ticked = run.Make(PoseTiming::Tick);

    run.test.RunFrame(1.5 * Tick);

    INFO("a tick pass, then the frame pass");
    REQUIRE(run.calls.size() == 2);
    REQUIRE(run.calls[0].timing == PoseTiming::Tick);
    REQUIRE(run.calls[0].step == run.test.app.GetTime().tickRate);
    REQUIRE(run.calls[0].instances == std::vector<Entity>{ticked});
    REQUIRE(run.calls[1].timing == PoseTiming::Frame);
    REQUIRE(run.calls[1].step == 1.5 * Tick);
    REQUIRE(run.calls[1].instances == std::vector<Entity>{framed});
}

TEST_CASE("EditBasePose on the other timing is refused in either pass, once per call site", "[skeleton][tick]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Entity framed = SkeletonTest::SkeletonEntity(test.level, chain);
    Entity ticked = SkeletonTest::SkeletonEntity(test.level, chain);
    ticked.GetComponent<Fenrir::SkeletonPose>().timing = PoseTiming::Tick;
    std::vector<std::pair<PoseTiming, std::size_t>> written;
    test.skeletons.AddAnimate("other timing", 0, [&](Fenrir::AnimateContext& context) {
        const Entity other = context.Timing() == PoseTiming::Tick ? framed : ticked;
        written.emplace_back(context.Timing(), Skeletons::EditBasePose(context.GetScene(), other).size());
    });

    test.RunFrame(Tick);
    test.RunFrame(Tick);

    INFO("both passes, both frames: nothing written");
    REQUIRE(written.size() == 4);
    for (const auto& [timing, size] : written)
    {
        REQUIRE(size == 0);
    }
    INFO("one warning per pass's refusal, from one call site: the first refusal's");
    REQUIRE(test.Warnings("was refused in Animate's") == 1);
}

TEST_CASE("a tick-posed base-pose write is seen by the next tick", "[skeleton][tick]")
{
    SkeletonApp test;
    Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    skeleton.GetComponent<Fenrir::SkeletonPose>().timing = PoseTiming::Tick;
    std::vector<float> atTick;
    test.skeletons.AddAnimate("observer", 0, [&](Fenrir::AnimateContext& context) {
        if (context.Timing() == PoseTiming::Tick)
        {
            atTick.push_back(RootX(context.GetScene(), skeleton, PoseKind::Simulation));
        }
    });
    bool writeInTick = false;
    test.app.AddSequentialSystem(Fenrir::SchedulePriority::Tick, [&](Fenrir::App&) {
        if (writeInTick)
        {
            Skeletons::EditBasePose(test.level, skeleton)[Root].pos.x = 9.0f;
            writeInTick = false;
        }
    });
    test.RunFrame(Tick);

    Skeletons::EditBasePose(test.level, skeleton)[Root].pos.x = 7.0f;
    INFO("written between frames: the simulation pose has it at once");
    REQUIRE(Near(RootX(test.level, skeleton, PoseKind::Simulation), 7.0f));
    writeInTick = true;
    test.RunFrame(2.0 * Tick);

    INFO("the next frame's first tick sees 7, and the write in its Tick is seen by the second");
    REQUIRE(atTick == std::vector<float>{0.0f, 7.0f, 9.0f});
    INFO("with nothing animating it, the skeleton keeps the written pose, and the frame draws it");
    REQUIRE(Near(RootX(test.level, skeleton, PoseKind::Render), 9.0f));
}

TEST_CASE("a test glue in Tick places a plain entity on the simulation bone each tick", "[skeleton][tick]")
{
    Clocked run;
    Entity skeleton = run.Make(PoseTiming::Tick);
    skeleton.GetComponent<Fenrir::Transform>().pos = {10.0f, 0.0f, 0.0f};
    const Entity body = run.test.level.GetEntityList().CreateEntity();
    const Fenrir::Math::Mat4 offset = Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), Vec3(0.0f, 0.0f, 1.0f));

    // The glue: bone × offset, into the body's parent space (it has none), as TRS.
    run.test.app.AddSequentialSystem(Fenrir::SchedulePriority::Tick, [&](Fenrir::App&) {
        const Fenrir::Math::Mat4 target =
            *Skeletons::GetBoneWorldMatrix(run.test.level, skeleton, Tip, PoseKind::Simulation) * offset;
        Fenrir::Transform& placed = Entity(body).GetComponent<Fenrir::Transform>();
        Fenrir::Math::Decompose(target, placed.pos, placed.rot, placed.scale);
    });
    // Where a physics step would read the body: PostTick, after the glue.
    std::vector<std::pair<Vec3, float>> stepped;
    run.test.app.AddSequentialSystem(Fenrir::SchedulePriority::PostTick, [&](Fenrir::App& app) {
        stepped.emplace_back(Entity(body).GetComponent<Fenrir::Transform>().pos,
                             static_cast<float>(app.GetTime().tickCount));
    });

    run.test.RunFrame(1.5 * Tick);
    run.test.RunFrame(2.75 * Tick);

    INFO("every step finds the body on that tick's tip, not a frame behind");
    REQUIRE(stepped.size() == 4);
    for (const auto& [pos, tick] : stepped)
    {
        const Fenrir::Math::Quat turn =
            Fenrir::Math::AngleAxis(tick * Fenrir::Math::DegToRad(90.0f), Vec3(0.0f, 0.0f, 1.0f));
        const Vec3 tip = Vec3(10.0f + tick, 1.0f, 0.0f) + turn * Vec3(0.0f, 2.0f, 0.0f);
        REQUIRE(Near(pos, tip + Vec3(0.0f, 0.0f, 1.0f)));
    }
}
