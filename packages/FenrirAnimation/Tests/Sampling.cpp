// Sampling (ADR-0034): Step, Linear and CubicSpline keys against glTF's formulas, holding outside the keys, tangent
// scaling and hemisphere flips. Each clip keys the chain's tip; the test seeks a paused animator and reads the tip's
// base pose, so the sampled value is read straight off it.
#include <cmath>
#include <filesystem>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AnimationTest.hpp"
#include "FenrirAssets/Model.hpp"

using AnimationTest::AboutZ;
using AnimationTest::AnimationApp;
using AnimationTest::Channel;
using AnimationTest::Key;
using AnimationTest::Near;
using Fenrir::Animation;
using Fenrir::ClipInterpolation;
using Fenrir::ClipPath;
using Fenrir::Entity;
using Fenrir::Math::Vec4;

namespace
{
    // A paused animator playing `data` on a new chain; `At` seeks it and gives the tip's base local transform.
    struct Sampled
    {
        explicit Sampled(Fenrir::ClipData data)
        {
            skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test),
                                               test.store.Add<Fenrir::Clip>(std::move(data), test.level));
            skeleton.GetComponent<Fenrir::Animator>().playing = false;
        }

        Fenrir::Transform At(double seconds)
        {
            Animation::Seek(test.level, skeleton, seconds);
            test.RunFrame();
            return *Fenrir::Skeletons::GetBaseLocal(test.level, skeleton, 2);
        }

        AnimationApp test;
        Entity skeleton;
    };

    Fenrir::ClipData OneChannel(Fenrir::ClipChannel channel)
    {
        Fenrir::ClipData clip;
        clip.channels = {std::move(channel)};
        return clip;
    }
} // namespace

TEST_CASE("Step keys hold each value until the next key", "[animation][sampling]")
{
    Sampled sampled(OneChannel(Channel("tip", ClipPath::Translation, ClipInterpolation::Step, {0.0f, 1.0f, 2.0f},
                                       {{1, 0, 0, 0}, {2, 0, 0, 0}, {3, 0, 0, 0}})));

    INFO("just before a key the previous value holds; at the key its own value");
    REQUIRE(Near(sampled.At(0.5).pos.x, 1.0f));
    REQUIRE(Near(sampled.At(0.999).pos.x, 1.0f));
    REQUIRE(Near(sampled.At(1.0).pos.x, 2.0f));
    REQUIRE(Near(sampled.At(1.5).pos.x, 2.0f));
}

TEST_CASE("Linear keys lerp translation and scale", "[animation][sampling]")
{
    Fenrir::ClipData clip;
    clip.channels = {
        Channel("tip", ClipPath::Translation, ClipInterpolation::Linear, {0.0f, 2.0f}, {{0, 1, 0, 0}, {4, 1, 0, 0}}),
        Channel("tip", ClipPath::Scale, ClipInterpolation::Linear, {0.0f, 2.0f}, {{1, 1, 1, 0}, {3, 5, 1, 0}})};
    Sampled sampled(clip);

    const Fenrir::Transform quarter = sampled.At(0.5);
    INFO("a quarter of the way: x is 1, and the scale (1.5, 2, 1)");
    REQUIRE(Near(quarter.pos, {1.0f, 1.0f, 0.0f}));
    REQUIRE(Near(quarter.scale, {1.5f, 2.0f, 1.0f}));
}

TEST_CASE("Linear rotation keys slerp along the shortest path, whichever hemisphere a key is written in",
          "[animation][sampling]")
{
    // The far key is 90 degrees about +Z written as -q: the same rotation, in the opposite hemisphere.
    const Fenrir::Math::Quat farQuarter = -AboutZ(90.0f);
    Sampled sampled(OneChannel(Channel("tip", ClipPath::Rotation, ClipInterpolation::Linear, {0.0f, 1.0f},
                                       {Key(AboutZ(0.0f)), Key(farQuarter)})));

    INFO("halfway is 45 degrees about +Z, not the long way round (-135 degrees)");
    REQUIRE(Near(sampled.At(0.5).rot, AboutZ(45.0f)));
    REQUIRE(Near(sampled.At(0.25).rot, AboutZ(22.5f)));
}

TEST_CASE("CubicSpline keys follow glTF's Hermite form with tangents scaled by the key interval",
          "[animation][sampling]")
{
    // Keys at 0 and 2 (an interval of 2), values 0 and 1, out tangent 1 at the first key, in tangent 0 at the last.
    Fenrir::ClipChannel channel = Channel("tip", ClipPath::Translation, ClipInterpolation::CubicSpline, {0.0f, 2.0f},
                                          {{0, 1, 0, 0}, {1, 1, 0, 0}});
    channel.inTangents = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    channel.outTangents = {{1, 0, 0, 0}, {0, 0, 0, 0}};
    Sampled sampled(OneChannel(channel));

    // At u = 0.5: h10 = 0.125 and h01 = 0.5, so x = 0.125 * 2 * 1 + 0.5 * 1 = 0.75 (0.625 if the tangent were not
    // scaled by the interval).
    INFO("halfway, x is 0.75");
    REQUIRE(Near(sampled.At(1.0).pos.x, 0.75f));
}

TEST_CASE("CubicSpline rotations are normalised after interpolating", "[animation][sampling]")
{
    Fenrir::ClipChannel channel = Channel("tip", ClipPath::Rotation, ClipInterpolation::CubicSpline, {0.0f, 1.0f},
                                          {Key(AboutZ(0.0f)), Key(AboutZ(90.0f))});
    channel.inTangents = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    channel.outTangents = {{0, 0, 0, 0}, {0, 0, 0, 0}};
    Sampled sampled(OneChannel(channel));

    // With zero tangents the midpoint is half the sum of the keys, whose length is cos(22.5 degrees): normalised, it
    // is 45 degrees about +Z.
    const Fenrir::Math::Quat half = sampled.At(0.5).rot;
    INFO("the result is a unit quaternion, 45 degrees about +Z");
    REQUIRE(Near(half.x * half.x + half.y * half.y + half.z * half.z + half.w * half.w, 1.0f));
    REQUIRE(Near(half, AboutZ(45.0f)));
}

TEST_CASE("values hold before the first key and after the last", "[animation][sampling]")
{
    Fenrir::ClipData clip;
    // The clip lasts 2 s, but the tip's translation is keyed only from 0.5 to 1.
    clip.channels = {
        Channel("tip", ClipPath::Translation, ClipInterpolation::Linear, {0.5f, 1.0f}, {{1, 1, 0, 0}, {2, 1, 0, 0}}),
        Channel("mid", ClipPath::Translation, ClipInterpolation::Linear, {0.0f, 2.0f}, {{0, 1, 0, 0}, {0, 1, 0, 0}})};
    Sampled sampled(clip);
    // Once, so a seek to the duration stays there rather than wrapping to 0.
    sampled.skeleton.GetComponent<Fenrir::Animator>().loop = Fenrir::ClipLoop::Once;

    INFO("before 0.5 the first key's value holds, after 1 the last key's");
    REQUIRE(Near(sampled.At(0.0).pos.x, 1.0f));
    REQUIRE(Near(sampled.At(0.25).pos.x, 1.0f));
    REQUIRE(Near(sampled.At(1.5).pos.x, 2.0f));
    REQUIRE(Near(sampled.At(2.0).pos.x, 2.0f));
}

TEST_CASE("sampling agrees whether playback runs forward, in reverse or across wraps", "[animation][sampling]")
{
    // Many keys, so a stale key cursor would give a visibly wrong value after a seek, a reversal or a wrap.
    std::vector<float> times;
    std::vector<Vec4> values;
    for (int k = 0; k <= 8; ++k)
    {
        times.push_back(static_cast<float>(k) * 0.125f);
        values.push_back({static_cast<float>(k * k), 1, 0, 0});
    }
    AnimationApp test;
    const Entity skeleton = AnimationTest::Animated(
        test.level, AnimationTest::AddChain(test),
        test.store.Add<Fenrir::Clip>(
            OneChannel(Channel("tip", ClipPath::Translation, ClipInterpolation::Linear, times, values)), test.level));
    // A key's value is k squared at k / 8 s; between keys it lerps. Forward 0.1875 s a frame for four frames, then
    // back for three, crossing the wrap both ways.
    const auto expected = [](double t) {
        const double k = std::floor(t * 8.0);
        const double u = t * 8.0 - k;
        return static_cast<float>(k * k + u * (2.0 * k + 1.0));
    };
    auto& animator = skeleton.GetComponent<Fenrir::Animator>();
    double cursor = 0.0;
    for (int frame = 0; frame < 10; ++frame)
    {
        const double step = frame < 7 ? 0.1875 : -0.3125;
        animator.speed = step > 0.0 ? 1.0f : -1.0f;
        test.RunFrame(std::abs(step));
        cursor += step;
        cursor -= std::floor(cursor);
        INFO("frame " << frame << " at " << cursor);
        REQUIRE(Near(AnimationTest::TipX(test, skeleton), expected(cursor), 1e-3f));
    }
}

TEST_CASE("an imported CubicSpline rotation, its far-hemisphere key flipped with its tangents, samples as glTF says",
          "[animation][sampling]")
{
    AnimationApp test;
    const auto model = test.store.Load<Fenrir::Model>(
        (std::filesystem::path(FENRIR_ANIMATION_FIXTURES) / "rig.gltf").string(), test.level);
    REQUIRE(model.Ok());
    const Fenrir::Model& data = *test.store.TryGet(model.handle);
    const Fenrir::SpawnedNodes spawned = test.skeletons.SpawnModel(test.level, model.handle);
    Entity owner = spawned.nodes[static_cast<std::size_t>(data.skeletonOwners[0])];
    REQUIRE(owner.HasComponent<Fenrir::SkeletonPose>());
    Fenrir::Animator animator;
    animator.clip = data.clips[1];
    animator.playing = false;
    owner.AddComponent<Fenrir::Animator>(animator);
    const std::int32_t shoulder = Fenrir::Skeletons::FindBone(test.level, owner, "Shoulder");

    // Worked from glTF's formula with the importer's flip (key 1 and both its tangents negated), interval 1:
    // at 0.5, 0.5 * key0 + 0.125 * out0 + 0.5 * key1 - 0.125 * in1 normalised is (0, 0, 0.204647, 0.978836); at 1.5,
    // between key 1 and key 2, (0, 0, 0.578064, 0.815992). Unflipped, 0.5 would give (0, 0, -0.997903, 0.064730).
    Animation::Seek(test.level, owner, 0.5);
    test.RunFrame();
    INFO("at 0.5");
    REQUIRE(Near(Fenrir::Skeletons::GetBaseLocal(test.level, owner, shoulder)->rot,
                 Fenrir::Math::Quat(0.978836f, 0.0f, 0.0f, 0.204647f)));
    Animation::Seek(test.level, owner, 1.5);
    test.RunFrame();
    INFO("at 1.5");
    REQUIRE(Near(Fenrir::Skeletons::GetBaseLocal(test.level, owner, shoulder)->rot,
                 Fenrir::Math::Quat(0.815992f, 0.0f, 0.0f, 0.578064f)));
}
