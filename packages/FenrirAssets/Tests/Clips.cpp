#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/ClipData.hpp"

using AssetsTest::AssetsApp;

namespace
{
    Fenrir::ClipChannel Channel(Fenrir::ClipPath path, std::vector<float> times)
    {
        Fenrir::ClipChannel channel;
        channel.targetName = "arm";
        channel.path = path;
        channel.values.assign(times.size(), Fenrir::Math::Vec4(0.0f, 0.0f, 0.0f, 1.0f));
        channel.times = std::move(times);
        return channel;
    }

    // Two channels, the longer ending at 1.5 seconds, with events out of order.
    Fenrir::ClipData Walk()
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Translation, {0.0f, 0.5f, 1.0f}),
                         Channel(Fenrir::ClipPath::Rotation, {0.25f, 1.5f})};
        clip.events = {{1.0f, "step"}, {0.5f, "step"}, {0.5f, "breath"}, {0.0f, "start"}};
        return clip;
    }
} // namespace

TEST_CASE("A clip's duration is its last key's time over every channel", "[assets][clips]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Clip>(Walk(), Fenrir::AssetOwner::App());
    REQUIRE(test.store.IsValid(handle));
    REQUIRE(test.store.TryGet(handle)->duration == 1.5f);

    INFO("a clip with no channels lasts no time");
    const auto empty = test.store.Add<Fenrir::Clip>(Fenrir::ClipData{}, Fenrir::AssetOwner::App());
    REQUIRE(test.store.TryGet(empty)->duration == 0.0f);
}

TEST_CASE("A clip's events are sorted by time, equal times in the order given", "[assets][clips]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Clip>(Walk(), Fenrir::AssetOwner::App());
    const std::vector<Fenrir::ClipEvent>& events = test.store.TryGet(handle)->events;

    REQUIRE(events.size() == 4);
    REQUIRE(events[0].name == "start");
    REQUIRE(events[1].name == "step");
    REQUIRE(events[1].time == 0.5f);
    REQUIRE(events[2].name == "breath");
    REQUIRE(events[3].time == 1.0f);

    INFO("events are found by name, in time order");
    REQUIRE(test.store.TryGet(handle)->FindEvents("step") == std::vector<std::uint32_t>{1, 3});
    REQUIRE(test.store.TryGet(handle)->FindEvents("jump").empty());
}

TEST_CASE("Clip event edits keep time order and bump the revision", "[assets][clips]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Clip>(Walk(), Fenrir::AssetOwner::App());

    INFO("a new event goes after events already at its time");
    REQUIRE(test.store.AddClipEvent(handle, 0.5f, "blink") == std::optional<std::uint32_t>{3});
    REQUIRE(test.store.Revision(handle) == 2);
    REQUIRE(test.store.TryGet(handle)->events[3].name == "blink");
    REQUIRE(test.store.AddClipEvent(handle, 1.5f, "end") == std::optional<std::uint32_t>{5});
    REQUIRE(test.store.Revision(handle) == 3);

    INFO("removing one moves the later ones down");
    REQUIRE(test.store.RemoveClipEvent(handle, 0));
    REQUIRE(test.store.Revision(handle) == 4);
    REQUIRE(test.store.TryGet(handle)->events[0].name == "step");
    REQUIRE(test.store.TryGet(handle)->events.size() == 5);
}

TEST_CASE("Clip event edits refuse bad input and change nothing", "[assets][clips]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Clip>(Walk(), Fenrir::AssetOwner::App());

    INFO("a time outside the clip, or not finite");
    REQUIRE_FALSE(test.store.AddClipEvent(handle, 2.0f, "late").has_value());
    REQUIRE_FALSE(test.store.AddClipEvent(handle, -0.1f, "early").has_value());
    REQUIRE_FALSE(test.store.AddClipEvent(handle, std::numeric_limits<float>::quiet_NaN(), "nan").has_value());
    INFO("an index past the events");
    REQUIRE_FALSE(test.store.RemoveClipEvent(handle, 4));
    REQUIRE(test.store.Revision(handle) == 1);
    REQUIRE(test.errors->size() == 4);

    INFO("a stale handle");
    REQUIRE(test.store.Unload(handle));
    REQUIRE_FALSE(test.store.AddClipEvent(handle, 0.5f, "gone").has_value());
    REQUIRE_FALSE(test.store.RemoveClipEvent(handle, 0));
    REQUIRE(test.errors->size() == 6);
}

TEST_CASE("A clip event id is the 32-bit FNV-1a hash of its name", "[assets][clips]")
{
    INFO("the published FNV-1a 32 values");
    STATIC_REQUIRE(Fenrir::ClipEventId::Of("").value == 2166136261u);
    STATIC_REQUIRE(Fenrir::ClipEventId::Of("a").value == 0xE40C292Cu);
    STATIC_REQUIRE(Fenrir::ClipEventId::Of("foobar").value == 0xBF9CF968u);
    REQUIRE(Fenrir::ClipEventId::Of("step") == Fenrir::ClipEventId::Of(std::string("st") + "ep"));
    REQUIRE_FALSE(Fenrir::ClipEventId::Of("step") == Fenrir::ClipEventId::Of("Step"));
}

TEST_CASE("Adding a clip refuses keys the sampler could not play", "[assets][clips]")
{
    const auto refused = [](Fenrir::ClipData clip) {
        AssetsApp test;
        const auto handle = test.store.Add<Fenrir::Clip>(std::move(clip), Fenrir::AssetOwner::App());
        return handle.IsEmpty() && !test.errors->empty();
    };
    SECTION("a channel with no keys")
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Scale, {})};
        REQUIRE(refused(clip));
    }
    SECTION("key times out of order")
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Scale, {0.0f, 1.0f, 1.0f})};
        REQUIRE(refused(clip));
    }
    SECTION("a negative key time")
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Scale, {-1.0f, 1.0f})};
        REQUIRE(refused(clip));
    }
    SECTION("a value per key missing")
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Scale, {0.0f, 1.0f})};
        clip.channels[0].values.pop_back();
        REQUIRE(refused(clip));
    }
    SECTION("a value that is not finite")
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Translation, {0.0f, 1.0f})};
        clip.channels[0].values[1].x = std::numeric_limits<float>::infinity();
        REQUIRE(refused(clip));
    }
    SECTION("a zero rotation key")
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Rotation, {0.0f, 1.0f})};
        clip.channels[0].values[0] = Fenrir::Math::Vec4(0.0f);
        REQUIRE(refused(clip));
    }
    SECTION("cubic keys without their tangents")
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Translation, {0.0f, 1.0f})};
        clip.channels[0].interpolation = Fenrir::ClipInterpolation::CubicSpline;
        clip.channels[0].inTangents.assign(2, Fenrir::Math::Vec4(0.0f));
        REQUIRE(refused(clip));
    }
    SECTION("tangents on linear keys")
    {
        Fenrir::ClipData clip;
        clip.channels = {Channel(Fenrir::ClipPath::Translation, {0.0f, 1.0f})};
        clip.channels[0].inTangents.assign(2, Fenrir::Math::Vec4(0.0f));
        clip.channels[0].outTangents.assign(2, Fenrir::Math::Vec4(0.0f));
        REQUIRE(refused(clip));
    }
    SECTION("an event past the end")
    {
        Fenrir::ClipData clip = Walk();
        clip.events.push_back({1.75f, "late"});
        REQUIRE(refused(clip));
    }
}

TEST_CASE("Cubic keys with a tangent each are taken", "[assets][clips]")
{
    AssetsApp test;
    Fenrir::ClipData clip;
    clip.channels = {Channel(Fenrir::ClipPath::Translation, {0.0f, 1.0f})};
    clip.channels[0].interpolation = Fenrir::ClipInterpolation::CubicSpline;
    clip.channels[0].inTangents.assign(2, Fenrir::Math::Vec4(1.0f));
    clip.channels[0].outTangents.assign(2, Fenrir::Math::Vec4(-1.0f));
    REQUIRE(test.store.IsValid(test.store.Add<Fenrir::Clip>(std::move(clip), Fenrir::AssetOwner::App())));
}
