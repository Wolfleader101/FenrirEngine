// Held, just pressed and just released as read from each phase (ADR-0017: input edges follow the reading phase).

#include <cstddef>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"

using Fenrir::Key;
using Fenrir::Scancode;
using Fenrir::SchedulePriority;
using namespace InputTests;

namespace
{
    // What one system saw of the Space key in one run of its phase.
    struct SpaceRead
    {
        bool held = false;
        bool justPressed = false;
        bool justReleased = false;
    };

    // Records what a system in `phase` reads of the Space key, once per run of the phase.
    std::shared_ptr<std::vector<SpaceRead>> RecordSpace(InputTestApp& test, SchedulePriority phase)
    {
        auto reads = std::make_shared<std::vector<SpaceRead>>();
        test.app.AddSystem(phase, [reads](Fenrir::App& app) {
            const Fenrir::Keyboard& keyboard = app.GetResource<Fenrir::Keyboard>();
            reads->push_back(
                {keyboard.IsDown(Key::Space), keyboard.JustPressed(Key::Space), keyboard.JustReleased(Key::Space)});
        });
        return reads;
    }
} // namespace

TEST_CASE("A key pressed before a frame is held and just pressed in that frame's Update only", "[input][edges]")
{
    InputTestApp test;
    const std::vector<SpaceRead>& update = *RecordSpace(test, SchedulePriority::Update);

    test.Press(Key::Space, Scancode::Space);
    test.app.RunFrame(oneTick);
    test.app.RunFrame(oneTick);

    REQUIRE(update.size() == 2);
    INFO("Update reads the key as held in the frame it went down");
    REQUIRE(update[0].held);
    INFO("Update reads it as just pressed in that frame");
    REQUIRE(update[0].justPressed);
    INFO("it is still held the next frame");
    REQUIRE(update[1].held);
    INFO("but no longer just pressed");
    REQUIRE_FALSE(update[1].justPressed);
}

TEST_CASE("A key released before a frame is just released in that frame's Update and no longer held", "[input][edges]")
{
    InputTestApp test;
    const std::vector<SpaceRead>& update = *RecordSpace(test, SchedulePriority::Update);

    test.Press(Key::Space, Scancode::Space);
    test.app.RunFrame(oneTick);
    test.Release(Key::Space, Scancode::Space);
    test.app.RunFrame(oneTick);
    test.app.RunFrame(oneTick);

    REQUIRE(update.size() == 3);
    INFO("the key is not held in the frame it went up");
    REQUIRE_FALSE(update[1].held);
    INFO("Update reads it as just released in that frame");
    REQUIRE(update[1].justReleased);
    INFO("and not just pressed");
    REQUIRE_FALSE(update[1].justPressed);
    INFO("the frame after, it is no longer just released");
    REQUIRE_FALSE(update[2].justReleased);
}

TEST_CASE("Tick, PreTick and PostTick read a press as just pressed in the tick after it", "[input][edges]")
{
    InputTestApp test;
    const std::vector<SpaceRead>& preTick = *RecordSpace(test, SchedulePriority::PreTick);
    const std::vector<SpaceRead>& tick = *RecordSpace(test, SchedulePriority::Tick);
    const std::vector<SpaceRead>& postTick = *RecordSpace(test, SchedulePriority::PostTick);

    test.Press(Key::Space, Scancode::Space);
    test.app.RunFrame(oneTick);
    test.app.RunFrame(oneTick);

    REQUIRE(tick.size() == 2);
    INFO("Tick reads the key as held and just pressed in the first tick after the press");
    REQUIRE(tick[0].held);
    REQUIRE(tick[0].justPressed);
    INFO("PreTick and PostTick of that tick read it as just pressed too");
    REQUIRE(preTick[0].justPressed);
    REQUIRE(postTick[0].justPressed);
    INFO("the next tick reads it as held but not just pressed");
    REQUIRE(tick[1].held);
    REQUIRE_FALSE(tick[1].justPressed);
    REQUIRE_FALSE(preTick[1].justPressed);
}

TEST_CASE("A frame with no tick keeps the tick edges for the next tick", "[input][edges]")
{
    InputTestApp test;
    const std::vector<SpaceRead>& tick = *RecordSpace(test, SchedulePriority::Tick);
    const std::vector<SpaceRead>& update = *RecordSpace(test, SchedulePriority::Update);

    test.Press(Key::Space, Scancode::Space);
    test.app.RunFrame(noTick);

    INFO("no tick ran in the first frame");
    REQUIRE(tick.empty());
    INFO("Update reads the press in the frame it came");
    REQUIRE(update[0].justPressed);

    test.app.RunFrame(oneTick);

    REQUIRE(tick.size() == 1);
    INFO("the next frame's tick still reads the press as just pressed");
    REQUIRE(tick[0].justPressed);
    INFO("while that frame's Update, a frame later, does not");
    REQUIRE_FALSE(update[1].justPressed);
}

TEST_CASE("A frame with two ticks shows a press only in the first", "[input][edges]")
{
    InputTestApp test;
    const std::vector<SpaceRead>& tick = *RecordSpace(test, SchedulePriority::Tick);
    const std::vector<SpaceRead>& update = *RecordSpace(test, SchedulePriority::Update);

    test.Press(Key::Space, Scancode::Space);
    test.app.RunFrame(twoTicks);

    REQUIRE(tick.size() == 2);
    INFO("the first tick reads the press");
    REQUIRE(tick[0].justPressed);
    INFO("the second tick does not read it again");
    REQUIRE_FALSE(tick[1].justPressed);
    INFO("the second tick still reads the key as held");
    REQUIRE(tick[1].held);
    INFO("Update, after both ticks, reads the press once for the frame");
    REQUIRE(update[0].justPressed);
}

TEST_CASE("A tap between two ticks reads in the next tick as just pressed and just released, not held",
          "[input][edges]")
{
    InputTestApp test;
    const std::vector<SpaceRead>& tick = *RecordSpace(test, SchedulePriority::Tick);

    test.Press(Key::Space, Scancode::Space);
    test.app.RunFrame(noTick);
    test.Release(Key::Space, Scancode::Space);
    test.app.RunFrame(noTick);
    test.app.RunFrame(oneTick);

    REQUIRE(tick.size() == 1);
    INFO("the tick after the tap reads it as just pressed");
    REQUIRE(tick[0].justPressed);
    INFO("and as just released");
    REQUIRE(tick[0].justReleased);
    INFO("with the key not held");
    REQUIRE_FALSE(tick[0].held);
}

TEST_CASE("Repeats of a held key are not new presses", "[input][edges]")
{
    InputTestApp test;
    const std::vector<SpaceRead>& tick = *RecordSpace(test, SchedulePriority::Tick);
    const std::vector<SpaceRead>& update = *RecordSpace(test, SchedulePriority::Update);

    test.Press(Key::Space, Scancode::Space);
    test.app.RunFrame(oneTick);
    test.Feed().KeyDown(mainWindow, Key::Space, Scancode::Space, Fenrir::KeyModifiers::None, true);
    test.Feed().KeyDown(mainWindow, Key::Space, Scancode::Space, Fenrir::KeyModifiers::None, true);
    test.app.RunFrame(oneTick);

    REQUIRE(update.size() == 2);
    INFO("Update does not read the repeats as a press");
    REQUIRE_FALSE(update[1].justPressed);
    INFO("the key stays held");
    REQUIRE(update[1].held);
    INFO("Tick does not read the repeats as a press either");
    REQUIRE_FALSE(tick[1].justPressed);
}
