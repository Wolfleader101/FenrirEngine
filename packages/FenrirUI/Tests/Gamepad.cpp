// Gamepad navigation and the virtual cursor (ADR-0065; spec: "FenrirUI" input and focus, seam 1): the D-pad and the
// left stick move the focus to the nearest focusable element in their direction, A activates, B cancels, a scroll
// view scrolls the focus into view, the gamepad claim; and the per-root virtual cursor the stick drives.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirInput/Devices.hpp"
#include "FenrirInput/InputRouter.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "UITestApp.hpp"

using Fenrir::ElementId;
using Fenrir::GamepadAxis;
using Fenrir::GamepadButton;
using Fenrir::GamepadId;
using Fenrir::UIEvent;
using Fenrir::UIEventKind;
using Fenrir::UIRoot;
using Fenrir::Math::Vec2;
using namespace UITest;

namespace
{
    constexpr GamepadId testPad{7};
    // The navigation timings and the cursor's default speed, as UIRoot's documentation gives them.
    constexpr double repeatDelay = 0.4;
    constexpr double repeatInterval = 0.12;
    constexpr float cursorSpeed = 900.0f;

    // A headless app with one pad connected, and the target of the root a test makes.
    struct GamepadTest
    {
        UITestApp test;
        std::shared_ptr<CaptureTarget> target;

        GamepadTest()
        {
            test.Feed().GamepadConnected(testPad);
        }

        // A root on a 400 by 300 capture target, one UI unit a logical unit; without the default stylesheet the
        // elements are exactly the size they are given.
        UIRoot& MakeRoot(bool virtualCursor = false, bool defaultStylesheet = false)
        {
            target = std::make_shared<CaptureTarget>(Fenrir::UITargetSize{{400.0f, 300.0f}, 1.0f});
            Fenrir::UIRootConfig config;
            config.scaleRule = Fenrir::ScaleRule::ContentScale;
            config.target = target;
            config.defaultStylesheet = defaultStylesheet;
            config.virtualCursor = virtualCursor;
            return test.Roots().CreateRoot(config);
        }

        // Presses and releases `button` on the pad and runs the frame that routes it.
        void Tap(GamepadButton button)
        {
            test.Feed().GamepadButtonDown(testPad, button);
            test.Feed().GamepadButtonUp(testPad, button);
            test.Frame();
        }

        void Stick(GamepadAxis axis, float value)
        {
            test.Feed().GamepadAxisMoved(testPad, axis, value);
        }

        [[nodiscard]] bool Claimed()
        {
            return test.app.GetResource<Fenrir::InputClaims>().Gamepad();
        }

        [[nodiscard]] const Fenrir::Gamepads& Pads()
        {
            return test.app.GetResource<Fenrir::Gamepads>();
        }
    };

    // An element of `tag` placed absolutely at `x`, `y`, sized `width` by `height`.
    ElementId Place(UIRoot& root, ElementId parent, std::string_view tag, float x, float y, float width, float height)
    {
        const ElementId element = root.Create(parent, tag);
        REQUIRE(root.SetStyleProperty(element, "position", "absolute"));
        REQUIRE(root.SetStyleProperty(element, "left", std::to_string(x) + "px"));
        REQUIRE(root.SetStyleProperty(element, "top", std::to_string(y) + "px"));
        REQUIRE(root.SetStyleProperty(element, "width", std::to_string(width) + "px"));
        REQUIRE(root.SetStyleProperty(element, "height", std::to_string(height) + "px"));
        return element;
    }

    // Three rows of 50 by 30 buttons: two rows of three, 60 apart across and 50 down, and under them a row of two
    // offset 20 to the right, so that each of its buttons overlaps two above it by different amounts.
    //
    //   [0][1][2]
    //   [3][4][5]
    //     [6][7]
    std::vector<ElementId> MakeGrid(UIRoot& root)
    {
        const Vec2 places[] = {{0.0f, 0.0f},   {60.0f, 0.0f},   {120.0f, 0.0f},  {0.0f, 50.0f},
                               {60.0f, 50.0f}, {120.0f, 50.0f}, {20.0f, 100.0f}, {80.0f, 100.0f}};
        std::vector<ElementId> buttons;
        for (const Vec2 place : places)
        {
            buttons.push_back(Place(root, root.GetRootElement(), "button", place.x, place.y, 50.0f, 30.0f));
        }
        return buttons;
    }

    // A row of `count` 50 by 30 buttons, 60 apart.
    std::vector<ElementId> MakeRow(UIRoot& root, int count)
    {
        std::vector<ElementId> row;
        for (int i = 0; i < count; ++i)
        {
            row.push_back(
                Place(root, root.GetRootElement(), "button", 60.0f * static_cast<float>(i), 0.0f, 50.0f, 30.0f));
        }
        return row;
    }

    // How many UIEvents of `kind` at `target` `reader` has not read yet.
    std::size_t CountAt(Fenrir::EventReader<UIEvent>& reader, Fenrir::App& app, UIEventKind kind, ElementId target)
    {
        std::size_t count = 0;
        for (const UIEvent& event : reader.Read(app))
        {
            count += event.kind == kind && event.target == target ? 1 : 0;
        }
        return count;
    }

    template <typename TControl>
    TControl& As(UIRoot& root, ElementId element)
    {
        auto* control = dynamic_cast<TControl*>(root.GetElement(element));
        REQUIRE(control != nullptr);
        return *control;
    }

    Vec2 Centre(const Fenrir::Rect2D& rect)
    {
        return {rect.x + rect.width / 2.0f, rect.y + rect.height / 2.0f};
    }

    int FramesFor(double seconds, double frame)
    {
        return static_cast<int>(std::lround(seconds / frame));
    }
} // namespace

TEST_CASE("The D-pad moves the focus to the nearest button in its direction across a grid", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const std::vector<ElementId> grid = MakeGrid(root);
    pad.test.Frame();
    REQUIRE(root.Focus(grid[0]));

    struct Step
    {
        GamepadButton direction;
        int expected;
        const char* what;
    };
    const Step steps[] = {
        {GamepadButton::DPadRight, 1, "right from 0 is its neighbour in the row, not the row below"},
        {GamepadButton::DPadDown, 4, "down from 1 is the button under it"},
        {GamepadButton::DPadLeft, 3, "left from 4 is 3"},
        {GamepadButton::DPadDown, 6, "down from 3 is 6, which overlaps it by 30, not 7 beside it"},
        {GamepadButton::DPadUp, 3, "up from 6 is 3, which it overlaps by 30, not 4 by 10"},
        {GamepadButton::DPadRight, 4, "right from 3 is 4"},
        {GamepadButton::DPadRight, 5, "right from 4 is 5"},
        {GamepadButton::DPadDown, 7, "down from 5 is 7, which overlaps it by 10, not 6 which does not at all"},
        {GamepadButton::DPadUp, 4, "up from 7 is 4, which it overlaps by 30, not 5 by 10"},
        {GamepadButton::DPadUp, 1, "up from 4 is 1"},
    };
    for (const Step& step : steps)
    {
        pad.Tap(step.direction);
        INFO(step.what);
        REQUIRE(root.GetFocused() == grid[static_cast<std::size_t>(step.expected)]);
    }
}

TEST_CASE("A direction with no focusable element that way keeps the focus", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const std::vector<ElementId> grid = MakeGrid(root);
    pad.test.Frame();
    REQUIRE(root.Focus(grid[2]));
    Fenrir::EventReader<UIEvent> events;

    pad.Tap(GamepadButton::DPadRight);
    pad.Tap(GamepadButton::DPadUp);
    INFO("nothing lies right of or above 2: it keeps the focus");
    REQUIRE(root.GetFocused() == grid[2]);
    INFO("and no blur is sent");
    REQUIRE(CountAt(events, pad.test.app, UIEventKind::Blur, grid[2]) == 0);
}

TEST_CASE("The left stick moves the focus only past its dead zone", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const std::vector<ElementId> row = MakeRow(root, 2);
    pad.test.Frame();
    REQUIRE(root.Focus(row[0]));

    pad.Stick(GamepadAxis::LeftX, 0.4f);
    pad.test.Frame();
    INFO("leaning 0.4, inside the 0.5 dead zone, the stick moves nothing");
    REQUIRE(root.GetFocused() == row[0]);

    pad.Stick(GamepadAxis::LeftX, 0.9f);
    pad.test.Frame();
    INFO("leaning 0.9 right, the focus moves right at once");
    REQUIRE(root.GetFocused() == row[1]);
}

TEST_CASE("A direction held on the stick repeats after a delay, at an interval, until let go", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const std::vector<ElementId> row = MakeRow(root, 5);
    pad.test.Frame();
    REQUIRE(root.Focus(row[0]));
    pad.Stick(GamepadAxis::LeftX, 0.9f);
    pad.test.Frame();
    REQUIRE(root.GetFocused() == row[1]);

    constexpr double frame = 0.01;
    pad.test.Frames(FramesFor(repeatDelay, frame) - 2, frame);
    INFO("held, it waits out the 0.4 s repeat delay");
    REQUIRE(root.GetFocused() == row[1]);

    pad.test.Frames(4, frame);
    INFO("then moves again");
    REQUIRE(root.GetFocused() == row[2]);

    pad.test.Frames(FramesFor(repeatInterval, frame) - 3, frame);
    INFO("not again before the 0.12 s interval");
    REQUIRE(root.GetFocused() == row[2]);
    pad.test.Frames(4, frame);
    INFO("and again once it has passed");
    REQUIRE(root.GetFocused() == row[3]);

    pad.Stick(GamepadAxis::LeftX, 0.0f);
    pad.test.Frames(FramesFor(1.0, frame), frame);
    INFO("let go, it stops");
    REQUIRE(root.GetFocused() == row[3]);
}

TEST_CASE("A on the gamepad clicks the focused button, as Enter does", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const ElementId button = Place(root, root.GetRootElement(), "button", 0.0f, 0.0f, 50.0f, 30.0f);
    pad.test.Frame();
    REQUIRE(root.Focus(button));
    Fenrir::EventReader<UIEvent> events;

    pad.Tap(GamepadButton::South);
    INFO("A on a focused button is one click at it");
    REQUIRE(CountAt(events, pad.test.app, UIEventKind::Click, button) == 1);
}

TEST_CASE("A on the gamepad toggles the focused checkbox", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const ElementId box = Place(root, root.GetRootElement(), "checkbox", 0.0f, 0.0f, 20.0f, 20.0f);
    pad.test.Frame();
    REQUIRE(root.Focus(box));

    pad.Tap(GamepadButton::South);
    INFO("A, an Enter press at it, checks it");
    REQUIRE(As<Fenrir::Checkbox>(root, box).IsChecked());
}

TEST_CASE("B on the gamepad sends cancel at the focused element, which bubbles", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const ElementId panel = Place(root, root.GetRootElement(), "div", 0.0f, 0.0f, 200.0f, 100.0f);
    const ElementId button = Place(root, panel, "button", 0.0f, 0.0f, 50.0f, 30.0f);
    pad.test.Frame();
    REQUIRE(root.Focus(button));
    int bubbled = 0;
    root.AddListener(panel, UIEventKind::Cancel, [&](Fenrir::UIListenerContext&) { ++bubbled; });
    Fenrir::EventReader<UIEvent> events;

    pad.Tap(GamepadButton::East);
    INFO("one cancel at the focused button");
    REQUIRE(CountAt(events, pad.test.app, UIEventKind::Cancel, button) == 1);
    INFO("seen by the panel's listener above it");
    REQUIRE(bubbled == 1);
    INFO("and the focus stays where it was");
    REQUIRE(root.GetFocused() == button);
}

TEST_CASE("A slider takes the D-pad along its axis, and the focus leaves it across", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const ElementId element = Place(root, root.GetRootElement(), "slider", 0.0f, 0.0f, 200.0f, 20.0f);
    const ElementId below = Place(root, root.GetRootElement(), "button", 0.0f, 50.0f, 50.0f, 30.0f);
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetRange(0.0f, 10.0f);
    slider.SetStep(1.0f);
    slider.SetValue(5.0f);
    pad.test.Frame();
    REQUIRE(root.Focus(element));

    pad.Tap(GamepadButton::DPadRight);
    INFO("right on a horizontal slider steps its value from 5 to 6");
    REQUIRE(slider.GetValue() == 6.0f);
    INFO("and keeps the focus");
    REQUIRE(root.GetFocused() == element);

    pad.Tap(GamepadButton::DPadDown);
    INFO("down moves the focus to the button under it");
    REQUIRE(root.GetFocused() == below);
    INFO("and leaves the value alone");
    REQUIRE(slider.GetValue() == 6.0f);
}

TEST_CASE("A slider at the end of its range lets the focus leave that way", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const ElementId element = Place(root, root.GetRootElement(), "slider", 0.0f, 0.0f, 100.0f, 20.0f);
    const ElementId beside = Place(root, root.GetRootElement(), "button", 150.0f, 0.0f, 50.0f, 20.0f);
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetRange(0.0f, 10.0f);
    slider.SetStep(1.0f);
    slider.SetValue(10.0f);
    pad.test.Frame();
    REQUIRE(root.Focus(element));

    pad.Tap(GamepadButton::DPadRight);
    INFO("at its maximum, right moves the focus to the button beside it");
    REQUIRE(root.GetFocused() == beside);
}

TEST_CASE("A focused element inside a scroll view is scrolled into view", "[ui][gamepad]")
{
    GamepadTest pad;
    // The default stylesheet makes `scroll` scroll.
    UIRoot& root = pad.MakeRoot(false, true);
    const ElementId view = Place(root, root.GetRootElement(), "scroll", 0.0f, 0.0f, 200.0f, 100.0f);
    std::vector<ElementId> items;
    for (int i = 0; i < 6; ++i)
    {
        items.push_back(Place(root, view, "button", 0.0f, 50.0f * static_cast<float>(i), 100.0f, 40.0f));
    }
    pad.test.Frame();
    REQUIRE(root.Focus(items[0]));
    const Fenrir::Rect2D inside = root.GetBoxes(view).padding;
    const Fenrir::Rect2D first = root.GetBoxes(items[0]).border;
    // Item 3 is laid out 150 below item 0 and 40 tall: shown at the view's bottom, the view is scrolled this far.
    const float expected = first.y + 150.0f + 40.0f - (inside.y + inside.height);

    for (int i = 0; i < 3; ++i)
    {
        pad.Tap(GamepadButton::DPadDown);
    }
    pad.test.Frame();
    INFO("down three times reaches item 3, past the view's bottom");
    REQUIRE(root.GetFocused() == items[3]);
    INFO("and the view scrolls just far enough to show it");
    REQUIRE(Near(root.GetScrollOffset(view).y, expected));

    for (int i = 0; i < 3; ++i)
    {
        pad.Tap(GamepadButton::DPadUp);
    }
    pad.test.Frame();
    INFO("back up to item 0 scrolls the view back to the top");
    REQUIRE(root.GetScrollOffset(view).y == 0.0f);
}

TEST_CASE("A root without the focus takes it on the first direction", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const std::vector<ElementId> grid = MakeGrid(root);
    pad.test.Frame();
    Fenrir::EventReader<Fenrir::GamepadButtonEvent> game;

    pad.Tap(GamepadButton::DPadDown);
    INFO("the first focusable element in tree order takes the focus");
    REQUIRE(root.GetFocused() == grid[0]);
    INFO("and the game does not see the press");
    REQUIRE(ReadAll(game, pad.test.app).empty());

    pad.Tap(GamepadButton::DPadDown);
    INFO("the next direction moves it from there");
    REQUIRE(root.GetFocused() == grid[3]);
}

TEST_CASE("The gamepad claim is up while the UI has the focus, and down otherwise", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const ElementId button = Place(root, root.GetRootElement(), "button", 0.0f, 0.0f, 50.0f, 30.0f);
    pad.test.Frame();
    INFO("nothing has the focus: no claim");
    REQUIRE_FALSE(pad.Claimed());

    REQUIRE(root.Focus(button));
    pad.test.Frame();
    INFO("focused: the root claims the gamepad");
    REQUIRE(pad.Claimed());

    root.Blur();
    pad.test.Frame();
    INFO("blurred: the claim is down again");
    REQUIRE_FALSE(pad.Claimed());
}

TEST_CASE("Gamepad input reaches the game while the UI has no focus to move", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    (void)Place(root, root.GetRootElement(), "div", 0.0f, 0.0f, 50.0f, 30.0f);
    pad.test.Frame();

    pad.test.Feed().GamepadButtonDown(testPad, GamepadButton::DPadDown);
    pad.test.Feed().GamepadButtonDown(testPad, GamepadButton::South);
    pad.Stick(GamepadAxis::LeftX, 1.0f);
    pad.test.Frame();
    INFO("with nothing focusable, the D-pad reaches the game");
    REQUIRE(pad.Pads().IsDown(testPad, GamepadButton::DPadDown));
    INFO("so does A");
    REQUIRE(pad.Pads().IsDown(testPad, GamepadButton::South));
    INFO("and the stick");
    REQUIRE(pad.Pads().Axis(testPad, GamepadAxis::LeftX) == 1.0f);
}

TEST_CASE("Gamepad input the UI acts on is kept from the game while it has the focus", "[ui][gamepad]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot();
    const ElementId button = Place(root, root.GetRootElement(), "button", 0.0f, 0.0f, 50.0f, 30.0f);
    pad.test.Frame();
    REQUIRE(root.Focus(button));

    pad.test.Feed().GamepadButtonDown(testPad, GamepadButton::South);
    pad.Stick(GamepadAxis::LeftX, 1.0f);
    pad.test.Frame();
    INFO("A is the UI's");
    REQUIRE_FALSE(pad.Pads().IsDown(testPad, GamepadButton::South));
    INFO("and so is the stick");
    REQUIRE(pad.Pads().Axis(testPad, GamepadAxis::LeftX) == 0.0f);
}

TEST_CASE("The virtual cursor starts at the root's centre, drawn by its element there", "[ui][gamepad][cursor]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot(true, true);
    pad.test.Frame();
    pad.test.Frame();
    INFO("the pointer is at the centre of the 400 by 300 root");
    REQUIRE(root.GetPointer().has_value());
    REQUIRE(Near(root.GetPointer()->x, 200.0f));
    REQUIRE(Near(root.GetPointer()->y, 150.0f));
    INFO("and the cursor element's box is centred there");
    REQUIRE(root.IsAlive(root.GetVirtualCursor()));
    REQUIRE(Near(Centre(root.GetBoxes(root.GetVirtualCursor()).border).x, 200.0f));
    REQUIRE(Near(Centre(root.GetBoxes(root.GetVirtualCursor()).border).y, 150.0f));
}

TEST_CASE("The virtual cursor follows the stick at its speed, and hover follows the cursor", "[ui][gamepad][cursor]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot(true, true);
    const ElementId button = Place(root, root.GetRootElement(), "button", 250.0f, 125.0f, 100.0f, 50.0f);
    pad.test.Frame();
    INFO("at the centre, the button to the right is not hovered");
    REQUIRE(root.GetHovered() != button);

    pad.Stick(GamepadAxis::LeftX, 1.0f);
    pad.test.Frame();
    const float start = root.GetPointer()->x;
    pad.test.Frames(6, 1.0 / 60.0);
    INFO("full right for six 60th-second frames, it moves 900 / 10 = 90 units right");
    REQUIRE(Near(root.GetPointer()->x, start + cursorSpeed / 10.0f, 0.01f));
    INFO("and not down");
    REQUIRE(Near(root.GetPointer()->y, 150.0f));
    INFO("the button now under it is hovered");
    REQUIRE(root.GetHovered() == button);

    pad.Stick(GamepadAxis::LeftX, 0.0f);
    pad.test.Frame();
    pad.test.Frame();
    // The frame that brings the stick back to 0 moves the cursor once more before it reads it: seven frames in all.
    INFO("the cursor element is laid out where the cursor stopped, 900 * 7 / 60 units right");
    REQUIRE(Near(Centre(root.GetBoxes(root.GetVirtualCursor()).border).x, start + cursorSpeed * 7.0f / 60.0f, 0.01f));
}

TEST_CASE("A with the virtual cursor presses and releases under it, a click", "[ui][gamepad][cursor]")
{
    GamepadTest pad;
    // The default stylesheet gives the button pointer input.
    UIRoot& root = pad.MakeRoot(true, true);
    const ElementId button = Place(root, root.GetRootElement(), "button", 150.0f, 100.0f, 100.0f, 100.0f);
    pad.test.Frame();
    Fenrir::EventReader<UIEvent> events;
    Fenrir::EventReader<Fenrir::GamepadButtonEvent> game;

    pad.Tap(GamepadButton::South);
    const std::vector<UIEvent> sent = ReadAll(events, pad.test.app);
    const auto count = [&](UIEventKind kind) {
        return std::count_if(sent.begin(), sent.end(),
                             [&](const UIEvent& event) { return event.kind == kind && event.target == button; });
    };
    INFO("A over the button, where the cursor starts, presses it");
    REQUIRE(count(UIEventKind::PointerPress) == 1);
    INFO("releases it");
    REQUIRE(count(UIEventKind::PointerRelease) == 1);
    INFO("and so clicks it");
    REQUIRE(count(UIEventKind::Click) == 1);
    INFO("and the game sees no A");
    REQUIRE(ReadAll(game, pad.test.app).empty());
}

TEST_CASE("With the virtual cursor off the stick moves nothing and is the game's", "[ui][gamepad][cursor]")
{
    GamepadTest pad;
    UIRoot& root = pad.MakeRoot(true);
    pad.test.Frame();
    root.SetVirtualCursor(false);
    INFO("switched off, its element goes");
    REQUIRE_FALSE(root.GetVirtualCursor().IsValid());

    pad.Stick(GamepadAxis::LeftX, 1.0f);
    pad.Stick(GamepadAxis::LeftY, 1.0f);
    pad.test.Frames(10, 1.0 / 60.0);
    INFO("the pointer stays at the centre where it started");
    REQUIRE(Near(root.GetPointer()->x, 200.0f));
    REQUIRE(Near(root.GetPointer()->y, 150.0f));
    INFO("the stick reaches the game");
    REQUIRE(pad.Pads().Axis(testPad, GamepadAxis::LeftX) == 1.0f);
    INFO("and the gamepad is not claimed");
    REQUIRE_FALSE(pad.Claimed());
}
