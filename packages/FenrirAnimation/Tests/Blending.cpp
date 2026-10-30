// Blending (ADR-0036), seam 9: layer composition and masks, the additive maths and its reference poses, crossfades
// with their snapshots and coverage, weight against the clock, events across layers and the shared budget, and the
// layer lifecycle. Every expectation is worked by hand from the chain skeleton (root, mid and tip, each at (0, 1, 0)
// from its parent at rest) and clips of two keys at 0 and 1.
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AnimationTest.hpp"
#include "FenrirAnimation/AnimationLayer.hpp"
#include "FenrirAssets/BoneMaskData.hpp"

using AnimationTest::AboutZ;
using AnimationTest::AnimationApp;
using AnimationTest::Channel;
using AnimationTest::Key;
using AnimationTest::Near;
using AnimationTest::TipX;
using Fenrir::AdditiveReference;
using Fenrir::Animation;
using Fenrir::AnimationEvent;
using Fenrir::AnimationLayer;
using Fenrir::Animator;
using Fenrir::ClipData;
using Fenrir::ClipInterpolation;
using Fenrir::ClipLoop;
using Fenrir::ClipPath;
using Fenrir::Entity;
using Fenrir::EventReader;
using Fenrir::FadeCurve;
using Fenrir::LayerMode;
using Fenrir::MaskScope;
using Fenrir::Skeletons;
using Fenrir::Math::Quat;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;

namespace
{
    constexpr std::int32_t Root = 0;
    constexpr std::int32_t Mid = 1;
    constexpr std::int32_t Tip = 2;

    Vec4 Point(Vec3 v)
    {
        return {v.x, v.y, v.z, 0.0f};
    }

    // A clip of duration 1 moving one channel of `bone` linearly from `from` to `to`.
    Fenrir::ClipChannel Move(const std::string& bone, ClipPath path, Vec4 from, Vec4 to)
    {
        return Channel(bone, path, ClipInterpolation::Linear, {0.0f, 1.0f}, {from, to});
    }

    Fenrir::AssetHandle<Fenrir::Clip> AddClip(AnimationApp& test, std::vector<Fenrir::ClipChannel> channels,
                                              const std::vector<float>& eventTimes = {})
    {
        ClipData clip;
        clip.channels = std::move(channels);
        for (const float time : eventTimes)
        {
            clip.events.push_back({time, std::to_string(time)});
        }
        return test.store.Add<Fenrir::Clip>(std::move(clip), test.level);
    }

    // A clip holding the tip's x at `x` for one second.
    Fenrir::AssetHandle<Fenrir::Clip> HoldTip(AnimationApp& test, float x, const std::vector<float>& events = {})
    {
        return AddClip(test, {Move("tip", ClipPath::Translation, {x, 1.0f, 0.0f, 0.0f}, {x, 1.0f, 0.0f, 0.0f})},
                       events);
    }

    Entity Skeleton(AnimationApp& test)
    {
        Entity entity = test.level.GetEntityList().CreateEntity();
        entity.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{AnimationTest::AddChain(test)});
        return entity;
    }

    Entity Layer(AnimationApp& test, Entity skeleton, AnimationLayer layer)
    {
        layer.skeleton = skeleton;
        Entity entity = test.level.GetEntityList().CreateEntity();
        entity.AddComponent<AnimationLayer>(layer);
        return entity;
    }

    AnimationLayer Playing(Fenrir::AssetHandle<Fenrir::Clip> clip, float weight = 1.0f,
                           LayerMode mode = LayerMode::Override)
    {
        AnimationLayer layer;
        layer.clip = clip;
        layer.weight = weight;
        layer.mode = mode;
        return layer;
    }

    Fenrir::Transform Local(AnimationApp& test, Entity skeleton, std::int32_t bone)
    {
        return Skeletons::GetBaseLocal(test.level, skeleton, bone).value_or(Fenrir::Transform{});
    }

    // The base Animator, set to change to `clip` with a fade.
    void FadeTo(Entity skeleton, Fenrir::AssetHandle<Fenrir::Clip> clip, float seconds,
                FadeCurve curve = FadeCurve::Linear)
    {
        Animator& animator = skeleton.GetComponent<Animator>();
        animator.clip = clip;
        animator.fadeSeconds = seconds;
        animator.fadeCurve = curve;
    }

    struct Crossing
    {
        float time;
        Entity layer;

        friend bool operator==(const Crossing&, const Crossing&) = default;
    };

    std::vector<Crossing> Read(AnimationApp& test, EventReader<AnimationEvent>& reader)
    {
        std::vector<Crossing> crossings;
        for (const AnimationEvent& event : reader.Read(test.app))
        {
            crossings.push_back({event.time, event.layer});
        }
        return crossings;
    }
} // namespace

// ---- Composition and masks ----

TEST_CASE("an override layer blends toward its clip at its weight and passes through what it does not animate",
          "[animation][blending]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));
    const auto midToTwo =
        AddClip(test, {Move("mid", ClipPath::Translation, {2.0f, 1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f, 0.0f})});
    Layer(test, skeleton, Playing(midToTwo, 0.5f));

    test.RunFrame(0.25);

    INFO("the layer's mid is halfway from rest (0, 1, 0) to (2, 1, 0)");
    REQUIRE(Near(Local(test, skeleton, Mid).pos, Vec3(1.0f, 1.0f, 0.0f)));
    INFO("the tip, which the layer does not animate, keeps the base clip's value");
    REQUIRE(Near(TipX(test, skeleton), 0.25f));
}

TEST_CASE("layers apply in sequence by order, with nothing normalised", "[animation][blending]")
{
    AnimationApp test;
    const Entity skeleton = Skeleton(test);
    AnimationLayer first = Playing(HoldTip(test, 4.0f), 0.5f);
    first.order = 1;
    AnimationLayer second = Playing(HoldTip(test, 8.0f), 0.5f);
    second.order = 0;
    Layer(test, skeleton, first);
    Layer(test, skeleton, second);

    test.RunFrame();

    INFO("order 0 first: 0 -> 8 halfway is 4, then order 1: 4 -> 4 stays 4; the other way round gives 5");
    REQUIRE(Near(TipX(test, skeleton), 4.0f));
}

TEST_CASE("a mask reaches a subtree, a later entry overrides an earlier one, and an unmatched name selects nothing",
          "[animation][blending][mask]")
{
    AnimationApp test;
    const Entity skeleton = Skeleton(test);
    const auto all =
        AddClip(test, {Move("root", ClipPath::Translation, {1.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f}),
                       Move("mid", ClipPath::Translation, {1.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f}),
                       Move("tip", ClipPath::Translation, {1.0f, 1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f, 0.0f})});
    const auto mask = test.store.CreateBoneMask(test.level);
    test.store.IncludeInBoneMask(mask, "mid", 1.0f, MaskScope::Subtree);
    test.store.IncludeInBoneMask(mask, "tip", 0.5f, MaskScope::Bone);
    test.store.IncludeInBoneMask(mask, "nobody", 1.0f, MaskScope::Subtree);
    AnimationLayer layer = Playing(all);
    layer.mask = mask;
    Layer(test, skeleton, layer);

    test.RunFrame();
    test.RunFrame();

    INFO("the root is outside the mask, the mid inside its subtree, the tip overridden to half");
    REQUIRE(Near(Local(test, skeleton, Root).pos.x, 0.0f));
    REQUIRE(Near(Local(test, skeleton, Mid).pos.x, 1.0f));
    REQUIRE(Near(Local(test, skeleton, Tip).pos.x, 0.5f));
    INFO("the unmatched name is logged once for the binding");
    REQUIRE(test.Warnings("'nobody', which selects nothing") == 1);

    // An edit bumps the mask's revision, so it binds again.
    test.store.IncludeInBoneMask(mask, "root", 1.0f, MaskScope::Bone);
    test.RunFrame();
    REQUIRE(Near(Local(test, skeleton, Root).pos.x, 1.0f));
}

TEST_CASE("a layer with no base Animator layers over a hand-authored base pose without compounding",
          "[animation][blending]")
{
    AnimationApp test;
    const Entity skeleton = Skeleton(test);
    test.RunFrame();
    Skeletons::EditBasePose(test.level, skeleton)[Root].pos = {5.0f, 1.0f, 0.0f};
    AnimationLayer layer = Playing(HoldTip(test, 0.0f), 1.0f, LayerMode::Additive);
    layer.clip =
        AddClip(test, {Move("tip", ClipPath::Translation, {0.0f, 2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f, 0.0f})});
    layer.reference = AdditiveReference::RestPose;
    Layer(test, skeleton, layer);

    for (int frame = 0; frame < 3; ++frame)
    {
        test.RunFrame();
        INFO("frame " << frame << ": the tip is rest plus the clip's one unit up, once");
        REQUIRE(Near(Local(test, skeleton, Tip).pos, Vec3(0.0f, 2.0f, 0.0f)));
        REQUIRE(Near(Local(test, skeleton, Root).pos, Vec3(5.0f, 1.0f, 0.0f)));
    }
}

// ---- Additive ----

TEST_CASE("an additive rotation applies before the accumulated rotation: slerp(identity, delta, w) x current",
          "[animation][blending][additive]")
{
    AnimationApp test;
    const Quat aboutX = Fenrir::Math::AngleAxis(Fenrir::Math::DegToRad(90.0f), Vec3(1.0f, 0.0f, 0.0f));
    const auto base = AddClip(test, {Move("tip", ClipPath::Rotation, Key(aboutX), Key(aboutX))});
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), base);
    AnimationLayer layer =
        Playing(AddClip(test, {Move("tip", ClipPath::Rotation, Key(AboutZ(40.0f)), Key(AboutZ(40.0f)))}), 0.5f,
                LayerMode::Additive);
    layer.reference = AdditiveReference::RestPose;
    Layer(test, skeleton, layer);

    test.RunFrame();

    const Quat rot = Local(test, skeleton, Tip).rot;
    INFO("half the 40 degrees about Z, before the 90 about X");
    REQUIRE(Near(rot, AboutZ(20.0f) * aboutX));
    REQUIRE_FALSE(Near(rot, aboutX * AboutZ(20.0f)));
}

TEST_CASE("an additive layer measures from the clip's start or from rest", "[animation][blending][additive]")
{
    for (const auto [reference, expected] :
         {std::pair{AdditiveReference::ClipStart, 0.5f}, std::pair{AdditiveReference::RestPose, 0.75f}})
    {
        AnimationApp test;
        const Entity skeleton = Skeleton(test);
        AnimationLayer layer = Playing(
            AddClip(test, {Move("tip", ClipPath::Translation, {0.25f, 1.0f, 0.0f, 0.0f}, {1.25f, 1.0f, 0.0f, 0.0f})}),
            1.0f, LayerMode::Additive);
        layer.reference = reference;
        Layer(test, skeleton, layer);

        test.RunFrame(0.5);

        INFO("at 0.5 the clip says x 0.75: less its start 0.25, or less rest 0");
        REQUIRE(Near(TipX(test, skeleton), expected));
    }
}

TEST_CASE("additive scale multiplies by lerp(1, ratio, w), and a degenerate component adds nothing, logged once",
          "[animation][blending][additive]")
{
    AnimationApp test;
    const Entity skeleton = Skeleton(test);
    AnimationLayer doubling =
        Playing(AddClip(test, {Move("mid", ClipPath::Scale, {2.0f, 2.0f, 2.0f, 0.0f}, {2.0f, 2.0f, 2.0f, 0.0f})}), 0.5f,
                LayerMode::Additive);
    doubling.reference = AdditiveReference::RestPose;
    Layer(test, skeleton, doubling);
    // Measured from its start, whose x scale is 0: x cannot be divided by it.
    Layer(test, skeleton,
          Playing(AddClip(test, {Move("tip", ClipPath::Scale, {0.0f, 1.0f, 1.0f, 0.0f}, {2.0f, 2.0f, 2.0f, 0.0f})}),
                  1.0f, LayerMode::Additive));

    test.RunFrame(0.5);
    test.RunFrame(0.0);

    REQUIRE(Near(Local(test, skeleton, Mid).scale, Vec3(1.5f)));
    INFO("at 0.5 the tip's sample is (1, 1.5, 1.5): x is degenerate, y and z are ratios of 1.5");
    REQUIRE(Near(Local(test, skeleton, Tip).scale, Vec3(1.0f, 1.5f, 1.5f)));
    REQUIRE(test.Warnings("cannot divide by") == 1);
}

// ---- Crossfades ----

TEST_CASE("a clip change fades over fadeSeconds, the outgoing clip still advancing, by either curve",
          "[animation][blending][fade]")
{
    for (const auto [curve, halfway] : {std::pair{FadeCurve::Linear, 0.5f}, std::pair{FadeCurve::Smooth, 0.5f}})
    {
        AnimationApp test;
        const Entity skeleton =
            AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));
        test.RunFrame(0.25);
        FadeTo(skeleton, HoldTip(test, 10.0f), 1.0f, curve);

        test.RunFrame(0.25);
        const float c = curve == FadeCurve::Linear ? 0.25f : 0.25f * 0.25f * (3.0f - 2.0f * 0.25f);
        INFO("the outgoing slide is at 0.5, the incoming hold at 10, progress 0.25");
        REQUIRE(Near(TipX(test, skeleton), 0.5f + (10.0f - 0.5f) * c));
        const Fenrir::PlaybackInfo info = Animation::Playback(test.level, skeleton);
        REQUIRE(info.fading);
        REQUIRE(Near(info.fadeProgress, 0.25f));
        INFO("time is the incoming clip's");
        REQUIRE(Near(info.time, 0.25));

        test.RunFrame(0.25);
        REQUIRE(Near(Animation::Playback(test.level, skeleton).fadeProgress, halfway));
        test.RunFrame(0.5);
        INFO("done: only the new clip shows, and the fade is over");
        REQUIRE(Near(TipX(test, skeleton), 10.0f));
        REQUIRE_FALSE(Animation::Playback(test.level, skeleton).fading);
    }
}

TEST_CASE("a fade's length ignores speed and its progress freezes while paused", "[animation][blending][fade]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));
    FadeTo(skeleton, HoldTip(test, 10.0f), 1.0f);
    skeleton.GetComponent<Animator>().speed = 4.0f;
    test.RunFrame(0.25);
    REQUIRE(Near(Animation::Playback(test.level, skeleton).fadeProgress, 0.25f));

    skeleton.GetComponent<Animator>().playing = false;
    test.RunFrame(0.25);
    REQUIRE(Near(Animation::Playback(test.level, skeleton).fadeProgress, 0.25f));
}

TEST_CASE("a change during a fade fades on from a snapshot of the blend, with no jump", "[animation][blending][fade]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));
    test.RunFrame(0.25);
    FadeTo(skeleton, HoldTip(test, 10.0f), 1.0f);
    test.RunFrame(0.5);
    const float before = TipX(test, skeleton);
    REQUIRE(Near(before, 0.75f + (10.0f - 0.75f) * 0.5f));

    FadeTo(skeleton, HoldTip(test, -10.0f), 1.0f);
    test.RunFrame(0.0);
    INFO("at the interruption the pose is the snapshot: unchanged");
    REQUIRE(Near(TipX(test, skeleton), before));
    test.RunFrame(0.5);
    INFO("then halfway from the frozen snapshot to the newest clip");
    REQUIRE(Near(TipX(test, skeleton), before + (-10.0f - before) * 0.5f));
}

TEST_CASE("a fade is coverage-aware: a channel only one side animates fades from or to the underlying value",
          "[animation][blending][fade]")
{
    AnimationApp test;
    const auto moves =
        AddClip(test, {Move("tip", ClipPath::Translation, {2.0f, 1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f, 0.0f})});
    const auto turns = AddClip(test, {Move("tip", ClipPath::Rotation, Key(AboutZ(90.0f)), Key(AboutZ(90.0f)))});
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), moves);
    test.RunFrame();
    FadeTo(skeleton, turns, 1.0f);

    test.RunFrame(0.5);

    const Fenrir::Transform tip = Local(test, skeleton, Tip);
    INFO("translation leaves toward rest x 0; rotation arrives from rest");
    REQUIRE(Near(tip.pos.x, 1.0f));
    REQUIRE(Near(tip.rot, AboutZ(45.0f)));
}

TEST_CASE("on a layer, no clip -> clip fades in and clip -> no clip fades out, then clears",
          "[animation][blending][fade]")
{
    AnimationApp test;
    const Entity skeleton = Skeleton(test);
    AnimationLayer layer = Playing(HoldTip(test, 2.0f));
    layer.fadeSeconds = 1.0f;
    layer.fadeCurve = FadeCurve::Linear;
    const Entity entity = Layer(test, skeleton, layer);

    test.RunFrame(0.5);
    REQUIRE(Near(TipX(test, skeleton), 1.0f));
    test.RunFrame(0.5);
    REQUIRE(Near(TipX(test, skeleton), 2.0f));

    entity.GetComponent<AnimationLayer>().clip = {};
    test.RunFrame(0.5);
    REQUIRE(Near(TipX(test, skeleton), 1.0f));
    test.RunFrame(0.5);
    INFO("faded out: the contribution is gone, the layer entity kept");
    REQUIRE(Near(TipX(test, skeleton), 0.0f));
    REQUIRE_FALSE(Animation::Playback(test.level, entity).fading);
    REQUIRE(entity.HasComponent<AnimationLayer>());
}

TEST_CASE("on the base, clip -> no clip keeps the last pose and no clip -> clip fades from it",
          "[animation][blending][fade]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test));
    test.RunFrame(0.5);
    FadeTo(skeleton, {}, 1.0f);
    test.RunFrame(0.5);
    INFO("no fade to nothing: the pose stays as last sampled");
    REQUIRE(Near(TipX(test, skeleton), 0.5f));
    REQUIRE_FALSE(Animation::Playback(test.level, skeleton).fading);

    FadeTo(skeleton, HoldTip(test, 10.0f), 1.0f);
    test.RunFrame(0.5);
    REQUIRE(Near(TipX(test, skeleton), 0.5f + (10.0f - 0.5f) * 0.5f));
}

TEST_CASE("a seek during a fade moves only the incoming clip, and only it reports finished",
          "[animation][blending][fade]")
{
    AnimationApp test;
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test),
                                                    AnimationTest::AddSlide(test), ClipLoop::Once);
    EventReader<Fenrir::ClipFinished> finished;
    test.RunFrame(0.75);
    const auto later =
        AddClip(test, {Move("tip", ClipPath::Translation, {10.0f, 1.0f, 0.0f, 0.0f}, {11.0f, 1.0f, 0.0f, 0.0f})});
    FadeTo(skeleton, later, 1.0f);
    Animation::Seek(test.level, skeleton, 0.5);

    test.RunFrame(0.5);

    INFO("the incoming clip went from the seek's 0.5 to 1 (x 11); the outgoing slide from 0.75 to its end, 1");
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 1.0));
    REQUIRE(Near(TipX(test, skeleton), 1.0f + (11.0f - 1.0f) * 0.5f));
    INFO("the incoming Once clip finished; the outgoing one reaching its end sent nothing");
    std::vector<Fenrir::AssetHandle<Fenrir::Clip>> clips;
    for (const Fenrir::ClipFinished& event : finished.Read(test.app))
    {
        clips.push_back(event.clip);
    }
    REQUIRE(clips == std::vector<Fenrir::AssetHandle<Fenrir::Clip>>{later});
}

// ---- Weight and the clock ----

TEST_CASE("weight never touches a layer's clock: brought back, it is in step", "[animation][blending]")
{
    AnimationApp test;
    const Entity skeleton = Skeleton(test);
    const Entity layer = Layer(test, skeleton, Playing(AnimationTest::AddSlide(test), 0.0f));

    test.RunFrame(0.25);
    test.RunFrame(0.25);
    INFO("at weight 0 the tip is untouched, but the cursor ran on");
    REQUIRE(Near(TipX(test, skeleton), 0.0f));
    REQUIRE(Near(Animation::Playback(test.level, layer).time, 0.5));

    layer.GetComponent<AnimationLayer>().weight = 1.0f;
    test.RunFrame(0.25);
    REQUIRE(Near(TipX(test, skeleton), 0.75f));
}

// ---- Events across layers ----

TEST_CASE("events come only from layers above weight 0, merged in time order across layers, ties by layer order",
          "[animation][blending][events]")
{
    AnimationApp test;
    const Entity skeleton =
        AnimationTest::Animated(test.level, AnimationTest::AddChain(test), AnimationTest::AddSlide(test, {0.1f, 0.3f}));
    AnimationLayer fast = Playing(HoldTip(test, 0.0f, {0.2f, 0.6f, 0.9f}));
    fast.speed = 2.0f;
    const Entity layer = Layer(test, skeleton, fast);
    const Entity silent = Layer(test, skeleton, Playing(HoldTip(test, 0.0f, {0.05f}), 0.0f));
    EventReader<AnimationEvent> reader;

    test.RunFrame(0.5);

    INFO("by fraction of the frame, the layer at twice the speed: base 0.1 and the layer's 0.2 both at 0.2 (the base "
         "first), base 0.3 and the layer's 0.6 at 0.6, then the layer's 0.9");
    const std::vector<Crossing> crossings = Read(test, reader);
    REQUIRE(crossings ==
            std::vector<Crossing>{{0.1f, skeleton}, {0.2f, layer}, {0.3f, skeleton}, {0.6f, layer}, {0.9f, layer}});
    INFO("the silent layer's cursor still ran");
    REQUIRE(Near(Animation::Playback(test.level, silent).time, 0.5));

    silent.GetComponent<AnimationLayer>().weight = 1.0f;
    test.RunFrame(0.25);
    INFO("brought back, it does not replay the crossing it made while silent");
    for (const Crossing& crossing : Read(test, reader))
    {
        REQUIRE(crossing.layer != silent);
    }
}

TEST_CASE("an incoming clip fires from its fade's start and an outgoing one until its fade's end, exclusive",
          "[animation][blending][events]")
{
    for (const auto [seconds, expected] :
         {std::pair{0.5f, std::vector<float>{0.0f, 0.5f}}, std::pair{0.25f, std::vector<float>{0.0f}}})
    {
        AnimationApp test;
        const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test),
                                                        AnimationTest::AddSlide(test, {0.5f, 0.9f}));
        EventReader<AnimationEvent> reader;
        test.RunFrame(0.25);
        (void)Read(test, reader);
        FadeTo(skeleton, HoldTip(test, 0.0f, {0.0f}), seconds);

        test.RunFrame(0.5);

        std::vector<float> times;
        for (const Crossing& crossing : Read(test, reader))
        {
            times.push_back(crossing.time);
        }
        INFO("fade " << seconds << " s: the outgoing 0.5 lies halfway through the frame's step");
        REQUIRE(times == expected);
    }
}

TEST_CASE("a skeleton's layers share one event budget, the earliest sent and the rest counted per layer",
          "[animation][blending][events]")
{
    Fenrir::AnimationConfig config;
    config.maxEventsPerInstancePerFrame = 3;
    AnimationApp test(config);
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test),
                                                    AnimationTest::AddSlide(test, {0.1f, 0.2f, 0.3f, 0.4f}));
    const Entity layer = Layer(test, skeleton, Playing(HoldTip(test, 0.0f, {0.15f, 0.25f})));
    EventReader<AnimationEvent> reader;
    EventReader<Fenrir::AnimationEventsDropped> dropped;

    test.RunFrame(0.5);

    REQUIRE(Read(test, reader) == std::vector<Crossing>{{0.1f, skeleton}, {0.15f, layer}, {0.2f, skeleton}});
    std::uint64_t base = 0;
    std::uint64_t layered = 0;
    for (const Fenrir::AnimationEventsDropped& drop : dropped.Read(test.app))
    {
        (drop.layer == skeleton ? base : layered) += drop.dropped;
    }
    REQUIRE(base == 2);
    REQUIRE(layered == 1);
}

TEST_CASE("ClipFinished is sent whatever the layer's weight, naming the layer", "[animation][blending][events]")
{
    AnimationApp test;
    const Entity skeleton = Skeleton(test);
    AnimationLayer layer = Playing(AnimationTest::AddSlide(test), 0.0f);
    layer.loop = ClipLoop::Once;
    const Entity entity = Layer(test, skeleton, layer);
    EventReader<Fenrir::ClipFinished> finished;

    test.RunFrame(0.75);
    test.RunFrame(0.5);

    std::vector<Entity> layers;
    for (const Fenrir::ClipFinished& event : finished.Read(test.app))
    {
        layers.push_back(event.layer);
    }
    REQUIRE(layers == std::vector<Entity>{entity});
}

// ---- Lifecycle ----

TEST_CASE("a layer on a missing, non-skeleton or unready entity contributes nothing and does not advance",
          "[animation][blending][lifecycle]")
{
    AnimationApp test;
    const auto slide = AnimationTest::AddSlide(test);
    const Entity plain = test.level.GetEntityList().CreateEntity();
    Entity unready = test.level.GetEntityList().CreateEntity();
    unready.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{});
    const Entity onNothing = Layer(test, Entity{}, Playing(slide));
    const Entity onPlain = Layer(test, plain, Playing(slide));
    const Entity onUnready = Layer(test, unready, Playing(slide));

    test.RunFrame();

    REQUIRE(Animation::Playback(test.level, onNothing).state == Fenrir::PoseState::Missing);
    REQUIRE(Animation::Playback(test.level, onPlain).state == Fenrir::PoseState::Missing);
    REQUIRE(Animation::Playback(test.level, onUnready).state == Fenrir::PoseState::Unavailable);
    REQUIRE(Near(Animation::Playback(test.level, onUnready).time, 0.0));
}

TEST_CASE("a layer retargeted to another skeleton restarts, and a removed layer is cut at once",
          "[animation][blending][lifecycle]")
{
    AnimationApp test;
    const Entity first = Skeleton(test);
    const Entity second = Skeleton(test);
    const Entity layer = Layer(test, first, Playing(AnimationTest::AddSlide(test)));
    test.RunFrame(0.5);
    REQUIRE(Near(TipX(test, first), 0.5f));

    layer.GetComponent<AnimationLayer>().skeleton = second;
    test.RunFrame(0.25);
    INFO("restarted on the second skeleton, with no fade");
    REQUIRE(Near(Animation::Playback(test.level, layer).time, 0.25));
    REQUIRE(Near(TipX(test, second), 0.25f));

    layer.RemoveComponent<AnimationLayer>();
    test.RunFrame(0.25);
    INFO("its contribution is gone: the second skeleton's tip is back at rest");
    REQUIRE(Near(TipX(test, second), 0.0f));
}
