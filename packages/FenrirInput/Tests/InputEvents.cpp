// Every fed input is also an engine event, read once per EventReader in the order it was fed (ADR-0017, ADR-0012).

#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"

using Fenrir::EventReader;
using Fenrir::Key;
using Fenrir::KeyEvent;
using Fenrir::KeyModifiers;
using Fenrir::Scancode;
using Fenrir::Math::Vec2;
using namespace InputTests;

namespace
{
    template <typename TEvent>
    std::vector<TEvent> ReadAll(EventReader<TEvent>& reader, const Fenrir::App& app)
    {
        std::vector<TEvent> events;
        for (const TEvent& event : reader.Read(app))
        {
            events.push_back(event);
        }
        return events;
    }
} // namespace

TEST_CASE("Every fed key arrives once per reader, in order, and a second reader sees it too", "[input][events]")
{
    InputTestApp test;
    EventReader<KeyEvent> first;
    EventReader<KeyEvent> second;

    test.Press(Key::A, Scancode::A);
    test.Press(Key::B, Scancode::B);
    test.Release(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);

    const std::vector<KeyEvent> read = ReadAll(first, test.app);
    REQUIRE(read.size() == 3);
    INFO("the reader sees the keys in the order they were fed: A down, B down, A up");
    REQUIRE((read[0].key == Key::A && read[0].pressed));
    REQUIRE((read[1].key == Key::B && read[1].pressed));
    REQUIRE((read[2].key == Key::A && !read[2].pressed));

    const std::vector<KeyEvent> again = ReadAll(first, test.app);
    INFO("reading again yields nothing: each event reaches a reader once");
    REQUIRE(again.empty());

    const std::vector<KeyEvent> other = ReadAll(second, test.app);
    INFO("a second reader sees all three too, in the same order");
    REQUIRE(other.size() == 3);
    REQUIRE(other[0].key == Key::A);
    REQUIRE(other[1].key == Key::B);
    REQUIRE(other[2].key == Key::A);

    test.app.RunFrame(oneTick);
    INFO("the next frame, with nothing fed, brings no key events");
    REQUIRE(ReadAll(first, test.app).empty());
}

TEST_CASE("A key event carries its key code, scancode, modifiers, repeat and window", "[input][events]")
{
    InputTestApp test;
    EventReader<KeyEvent> reader;

    test.Feed().KeyDown(otherWindow, Key::Q, Scancode::A, KeyModifiers::Alt, true);
    test.app.RunFrame(oneTick);

    const std::vector<KeyEvent> read = ReadAll(reader, test.app);
    REQUIRE(read.size() == 1);
    const KeyEvent& event = read[0];
    INFO("the event names the window the key went down in");
    REQUIRE(event.window == otherWindow);
    INFO("its key code and scancode are the ones fed, separately");
    REQUIRE(event.key == Key::Q);
    REQUIRE(event.scancode == Scancode::A);
    INFO("its modifiers are reported");
    REQUIRE(event.modifiers == KeyModifiers::Alt);
    INFO("it is a repeat of a press");
    REQUIRE(event.pressed);
    REQUIRE(event.repeat);
}

TEST_CASE("Text arrives as one event per codepoint, naming the window", "[input][events]")
{
    InputTestApp test;
    EventReader<Fenrir::TextEvent> reader;

    test.Feed().Text(otherWindow, U"hé\U0001F600");
    test.app.RunFrame(oneTick);

    const std::vector<Fenrir::TextEvent> read = ReadAll(reader, test.app);
    REQUIRE(read.size() == 3);
    INFO("the codepoints arrive in the order typed, including ones outside the Basic Multilingual Plane");
    REQUIRE(read[0].codepoint == U'h');
    REQUIRE(read[1].codepoint == U'é');
    REQUIRE(read[2].codepoint == U'\U0001F600');
    INFO("each names the window the text was typed in");
    REQUIRE(read[0].window == otherWindow);
    REQUIRE(read[2].window == otherWindow);
}

TEST_CASE("Mouse, focus and gamepad input arrive as events with what was fed", "[input][events]")
{
    InputTestApp test;
    EventReader<Fenrir::MouseButtonEvent> buttons;
    EventReader<Fenrir::MouseMotionEvent> motions;
    EventReader<Fenrir::MouseScrollEvent> scrolls;
    EventReader<Fenrir::WindowFocusEvent> focus;
    EventReader<Fenrir::GamepadConnectionEvent> connections;
    EventReader<Fenrir::GamepadButtonEvent> padButtons;
    EventReader<Fenrir::GamepadAxisEvent> axes;

    constexpr Fenrir::GamepadId pad{3};
    test.Feed().FocusGained(mainWindow);
    test.Feed().MouseMove(mainWindow, Vec2(4.0f, 6.0f), Vec2(1.0f, -2.0f));
    test.Feed().MouseButtonDown(mainWindow, Fenrir::MouseButton::Middle);
    test.Feed().Scroll(mainWindow, Vec2(0.0f, -1.0f));
    test.Feed().GamepadConnected(pad);
    test.Feed().GamepadButtonDown(pad, Fenrir::GamepadButton::North);
    test.Feed().GamepadAxisMoved(pad, Fenrir::GamepadAxis::LeftTrigger, 0.25f);
    test.Feed().FocusLost(mainWindow);
    test.app.RunFrame(oneTick);

    const auto button = ReadAll(buttons, test.app);
    INFO("one mouse button event: Middle down in the main window");
    REQUIRE(button.size() == 1);
    REQUIRE((button[0].button == Fenrir::MouseButton::Middle && button[0].pressed && button[0].window == mainWindow));

    const auto motion = ReadAll(motions, test.app);
    INFO("one motion event with the position and motion fed");
    REQUIRE(motion.size() == 1);
    REQUIRE(motion[0].position == Vec2(4.0f, 6.0f));
    REQUIRE(motion[0].motion == Vec2(1.0f, -2.0f));

    const auto scroll = ReadAll(scrolls, test.app);
    INFO("one scroll event of one notch towards the user");
    REQUIRE(scroll.size() == 1);
    REQUIRE(scroll[0].notches == Vec2(0.0f, -1.0f));

    const auto focusChanges = ReadAll(focus, test.app);
    INFO("the focus was gained, then lost, in that order");
    REQUIRE(focusChanges.size() == 2);
    REQUIRE((focusChanges[0].focused && focusChanges[0].window == mainWindow));
    REQUIRE_FALSE(focusChanges[1].focused);

    const auto connection = ReadAll(connections, test.app);
    INFO("the pad connected");
    REQUIRE(connection.size() == 1);
    REQUIRE((connection[0].gamepad == pad && connection[0].connected));

    const auto padButton = ReadAll(padButtons, test.app);
    INFO("its North button went down");
    REQUIRE(padButton.size() == 1);
    REQUIRE(
        (padButton[0].gamepad == pad && padButton[0].button == Fenrir::GamepadButton::North && padButton[0].pressed));

    const auto axis = ReadAll(axes, test.app);
    INFO("its left trigger moved to a quarter");
    REQUIRE(axis.size() == 1);
    REQUIRE((axis[0].axis == Fenrir::GamepadAxis::LeftTrigger && axis[0].value == 0.25f));
}

TEST_CASE("A Tick reader sees a key event once across frames with no tick and with two", "[input][events]")
{
    InputTestApp test;
    auto seen = std::make_shared<std::vector<Key>>();
    test.app.AddSystem(Fenrir::SchedulePriority::Tick,
                       [seen, reader = EventReader<KeyEvent>()](Fenrir::App& app) mutable {
                           for (const KeyEvent& event : reader.Read(app))
                           {
                               seen->push_back(event.key);
                           }
                       });

    test.Press(Key::E, Scancode::E);
    test.app.RunFrame(noTick);
    test.app.RunFrame(twoTicks);

    INFO("the key event reached the Tick reader exactly once");
    REQUIRE(*seen == std::vector<Key>{Key::E});
}
