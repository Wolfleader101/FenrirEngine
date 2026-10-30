// Assets under playback (ADR-0034): clips and skeletons that stop resolving and recover with no catch-up, revision
// changes rebinding and placing the cursor, binding diagnostics, the binding cache's sharing and cleanup, and binding
// by node identity within a clip's own file.
#include <filesystem>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AnimationTest.hpp"
#include "FenrirAssets/Model.hpp"

using AnimationTest::AnimationApp;
using AnimationTest::Channel;
using AnimationTest::Near;
using AnimationTest::ReadTimes;
using AnimationTest::TipX;
using Fenrir::Animation;
using Fenrir::AnimationEvent;
using Fenrir::Animator;
using Fenrir::ClipInterpolation;
using Fenrir::ClipLoop;
using Fenrir::ClipPath;
using Fenrir::Entity;
using Fenrir::EventReader;

namespace
{
    const std::vector<float> Quarters{0.0f, 0.25f, 0.5f, 0.75f, 1.0f};

    // A clip of duration 0.5 moving mid's x from 0 to 1, with events at the given times.
    Fenrir::ClipData ShortMidData(const std::vector<float>& eventTimes)
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel("mid", ClipPath::Translation, ClipInterpolation::Linear, {0.0f, 0.5f},
                                 {{0, 1, 0, 0}, {1, 1, 0, 0}})};
        for (const float time : eventTimes)
        {
            clip.events.push_back({time, "short"});
        }
        return clip;
    }

    std::filesystem::path Fixture(const char* name)
    {
        return std::filesystem::path(FENRIR_ANIMATION_FIXTURES) / name;
    }
} // namespace

TEST_CASE("a clip that stops resolving freezes the cursor and pose, reads Unavailable and is logged once",
          "[animation][assets]")
{
    AnimationApp test;
    const auto slide = AnimationTest::AddSlide(test, Quarters);
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), slide);
    EventReader<AnimationEvent> reader;
    test.RunFrame(0.5);
    (void)ReadTimes(test, reader);

    REQUIRE(test.store.Unload(slide));
    for (int frame = 0; frame < 3; ++frame)
    {
        test.RunFrame(0.25);
        INFO("frame " << frame << ": nothing crossed, the cursor and pose frozen at 0.5");
        REQUIRE(ReadTimes(test, reader).empty());
        REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.5));
        REQUIRE(Near(TipX(test, skeleton), 0.5f));
        REQUIRE(Animation::Playback(test.level, skeleton).state == Fenrir::PoseState::Unavailable);
    }
    INFO("the problem was logged once");
    REQUIRE(test.Warnings("does not resolve") == 1);
}

TEST_CASE("a skeleton that is not Ready is neither sampled nor advanced, and recovers with no catch-up events",
          "[animation][assets]")
{
    AnimationApp test;
    const auto chain = AnimationTest::AddChain(test);
    Entity skeleton = AnimationTest::Animated(test.level, chain, AnimationTest::AddSlide(test, Quarters));
    EventReader<AnimationEvent> reader;
    test.RunFrame(0.125);
    (void)ReadTimes(test, reader);

    REQUIRE(test.store.Unload(chain));
    for (int frame = 0; frame < 4; ++frame)
    {
        test.RunFrame(0.25);
        INFO("frame " << frame << ": Unavailable, with no events and the cursor frozen");
        REQUIRE(ReadTimes(test, reader).empty());
        REQUIRE(Animation::Playback(test.level, skeleton).state == Fenrir::PoseState::Unavailable);
        REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.125));
    }

    skeleton.GetComponent<Fenrir::SkeletonPose>().skeleton = AnimationTest::AddChain(test);
    test.RunFrame(0.25);

    INFO("Ready again: playback resumes from where it froze, crossing only this frame's step (0.125, 0.375]");
    REQUIRE(Animation::Playback(test.level, skeleton).state == Fenrir::PoseState::Ready);
    REQUIRE(ReadTimes(test, reader) == std::vector<float>{0.25f});
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.375));
    REQUIRE(Near(TipX(test, skeleton), 0.375f));
}

TEST_CASE("a revision change rebinds the clip and clamps a Once cursor into its new duration", "[animation][assets]")
{
    AnimationApp test;
    const auto slide = AnimationTest::AddSlide(test);
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), slide, ClipLoop::Once);
    skeleton.GetComponent<Animator>().playing = false;
    Animation::Seek(test.level, skeleton, 0.75);
    test.RunFrame();

    REQUIRE(test.store.Replace(slide, ShortMidData({})));
    test.RunFrame();

    INFO("the cursor is clamped to the new duration, 0.5, and the new channels play: mid moves, the tip is at rest");
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.5));
    REQUIRE(Near(Fenrir::Skeletons::GetBaseLocal(test.level, skeleton, 1)->pos.x, 1.0f));
    REQUIRE(Near(TipX(test, skeleton), 0.0f));
}

TEST_CASE("a revision change wraps a Loop cursor into its new duration, crossing nothing for the adjustment",
          "[animation][assets]")
{
    AnimationApp test;
    const auto slide = AnimationTest::AddSlide(test, Quarters);
    const Entity skeleton = AnimationTest::Animated(test.level, AnimationTest::AddChain(test), slide);
    EventReader<AnimationEvent> reader;
    test.RunFrame(0.75);
    (void)ReadTimes(test, reader);

    REQUIRE(test.store.Replace(slide, ShortMidData({0.125f, 0.25f, 0.3125f})));
    test.RunFrame(0.125);

    std::vector<float> times;
    std::vector<std::uint32_t> revisions;
    for (const AnimationEvent& event : reader.Read(test.app))
    {
        times.push_back(event.time);
        revisions.push_back(event.clipRevision);
    }
    INFO("0.75 wraps to 0.25 in the 0.5 s clip; the step crosses (0.25, 0.375] only, in the new revision");
    REQUIRE(Near(Animation::Playback(test.level, skeleton).time, 0.375));
    REQUIRE(times == std::vector<float>{0.3125f});
    REQUIRE(revisions == std::vector<std::uint32_t>{test.store.Revision(slide)});
}

TEST_CASE("each channel that binds to no bone is logged once, naming the clip, target, path and reason",
          "[animation][assets][bindings]")
{
    AnimationApp test;
    Fenrir::SkeletonData twins;
    twins.bones = {AnimationTest::Bone("root", -1, {0, 1, 0}), AnimationTest::Bone("arm", 0, {0, 1, 0}),
                   AnimationTest::Bone("arm", 1, {0, 1, 0})};
    const auto skeletonHandle = test.store.Add<Fenrir::Skeleton>(twins, test.level);
    Fenrir::ClipData clip;
    clip.channels = {
        Channel("nobody", ClipPath::Translation, ClipInterpolation::Linear, {0.0f, 1.0f}, {{0, 0, 0, 0}, {1, 0, 0, 0}}),
        Channel("arm", ClipPath::Rotation, ClipInterpolation::Linear, {0.0f, 1.0f},
                {{0, 0, 0, 1}, AnimationTest::Key(AnimationTest::AboutZ(90.0f))}),
        Channel("Prop", ClipPath::Scale, ClipInterpolation::Linear, {0.0f, 1.0f}, {{1, 1, 1, 0}, {2, 2, 2, 0}}),
        Channel("root", ClipPath::Translation, ClipInterpolation::Linear, {0.0f, 1.0f}, {{0, 1, 0, 0}, {4, 1, 0, 0}})};
    clip.channels[2].playable = false;
    const Entity skeleton =
        AnimationTest::Animated(test.level, skeletonHandle, test.store.Add<Fenrir::Clip>(clip, test.level));

    for (int frame = 0; frame < 3; ++frame)
    {
        test.RunFrame(0.25);
    }

    INFO("no match, an ambiguous match and an ordinary node, each once");
    REQUIRE(test.Warnings("'nobody' translation") == 1);
    REQUIRE(test.Warnings("no bone has that engine name") == 1);
    REQUIRE(test.Warnings("'arm' rotation") == 1);
    REQUIRE(test.Warnings("ambiguous") == 1);
    REQUIRE(test.Warnings("'Prop' scale") == 1);
    REQUIRE(test.Warnings("ordinary node") == 1);
    INFO("the bound channel plays: root's x at 0.75 s is 3; the ambiguous bones stay at rest");
    REQUIRE(Near(Fenrir::Skeletons::GetBaseLocal(test.level, skeleton, 0)->pos.x, 3.0f));
    REQUIRE(Near(Fenrir::Skeletons::GetBaseLocal(test.level, skeleton, 1)->rot, Fenrir::Math::Quat(1, 0, 0, 0)));
}

TEST_CASE("a binding is shared by the instances of its pair, rebuilt on a revision change and dropped when unused",
          "[animation][assets][bindings]")
{
    AnimationApp test;
    const auto chain = AnimationTest::AddChain(test);
    Fenrir::ClipData data = AnimationTest::SlideData();
    data.channels.push_back(Channel("nobody", ClipPath::Translation, ClipInterpolation::Step, {0.0f}, {{0, 0, 0, 0}}));
    const auto clip = test.store.Add<Fenrir::Clip>(data, test.level);
    const Entity first = AnimationTest::Animated(test.level, chain, clip);
    const Entity second = AnimationTest::Animated(test.level, chain, clip);
    test.RunFrame();
    test.RunFrame();

    INFO("two instances, one binding: logged once");
    REQUIRE(test.Warnings("'nobody'") == 1);

    REQUIRE(test.store.AddClipEvent(clip, 0.5f, "edit").has_value());
    test.RunFrame();
    INFO("the clip's revision moved: the binding is rebuilt, and logged again");
    REQUIRE(test.Warnings("'nobody'") == 2);

    first.GetComponent<Animator>().clip = {};
    second.GetComponent<Animator>().clip = {};
    test.RunFrame();
    first.GetComponent<Animator>().clip = clip;
    test.RunFrame();
    INFO("no instance used it for a frame, so it was dropped: binding again logs again");
    REQUIRE(test.Warnings("'nobody'") == 3);
}

TEST_CASE("a clip binds to a skeleton from its own file by node identity, and to another only by exact name",
          "[animation][assets][bindings]")
{
    AnimationApp test;
    const auto model = test.store.Load<Fenrir::Model>(Fixture("twins.gltf").string(), test.level);
    REQUIRE(model.Ok());
    const Fenrir::Model& data = *test.store.TryGet(model.handle);
    const Fenrir::SpawnedNodes spawned = test.skeletons.SpawnModel(test.level, model.handle);
    Entity owner = spawned.nodes[static_cast<std::size_t>(data.skeletonOwners[0])];
    Animator animator;
    animator.clip = data.clips[0];
    owner.AddComponent<Animator>(animator);

    // The same names, built in code: no file, so no identity to bind by.
    Fenrir::SkeletonData twins;
    twins.bones = {AnimationTest::Bone("Twin", -1, {0, 1, 0}), AnimationTest::Bone("Twin", 0, {0, 1, 0})};
    const Entity coded =
        AnimationTest::Animated(test.level, test.store.Add<Fenrir::Skeleton>(twins, test.level), data.clips[0]);
    test.RunFrame(0.5);

    INFO("in its own file the lower Twin is bound by identity despite the repeated name: halfway, y is 2");
    const std::int32_t lower = Fenrir::Skeletons::FindBone(test.level, owner, "Twin#2");
    REQUIRE(lower != Fenrir::NoBone);
    REQUIRE(Near(Fenrir::Skeletons::GetBaseLocal(test.level, owner, lower)->pos.y, 2.0f));
    INFO("its channel on the ordinary node Prop is not played, and says so once per binding: two skeletons, twice");
    REQUIRE(test.Warnings("'Prop' translation") == 2);
    INFO("the coded skeleton's match by name is ambiguous, so it is not played and stays at rest");
    REQUIRE(test.Warnings("ambiguous") == 1);
    REQUIRE(Near(Fenrir::Skeletons::GetBaseLocal(test.level, coded, 1)->pos.y, 1.0f));
}
