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
    constexpr GamepadId pad{7};

    // A root on a 400 by 300 capture target, one UI unit a logical unit; without the default stylesheet the elements
    // are exactly the size they are given.
    UIRoot& MakePlainRoot(UITestApp& test, std::shared_ptr<CaptureTarget>& target, bool virtualCursor = false,
                          bool defaultStylesheet = false)
    {
        target = std::make_shared<CaptureTarget>(Fenrir::UITargetSize{{400.0f, 300.0f}, 1.0f});
        Fenrir::UIRootConfig config;
        config.scaleRule = Fenrir::ScaleRule::ContentScale;
        config.target = target;
        config.defaultStylesheet = defaultStylesheet;
        config.virtualCursor = virtualCursor;
        return test.Roots().CreateRoot(config);
    }

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

    // Presses and releases `button` on the pad, fed for the next frame.
    void Tap(UITestApp& test, GamepadButton button)
    {
        test.Feed().GamepadButtonDown(pad, button);
        test.Feed().GamepadButtonUp(pad, button);
    }

    // Taps `button` and runs the frame that routes it.
    void TapFrame(UITestApp& test, GamepadButton button)
    {
        Tap(test, button);
        test.Frame();
    }

    // The UIEvents of `kind` at `target` that `reader` has not read yet.
    std::vector<UIEvent> Of(Fenrir::EventReader<UIEvent>& reader, Fenrir::App& app, UIEventKind kind, ElementId target)
    {
        std::vector<UIEvent> found;
        for (const UIEvent& event : reader.Read(app))
        {
            if (event.kind == kind && event.target == target)
            {
                found.push_back(event);
            }
        }
        return found;
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

    struct Pad
    {
        UITestApp test;
        std::shared_ptr<CaptureTarget> target;

        Pad()
        {
            test.Feed().GamepadConnected(pad);
        }
    };
} // namespace

TEST_CASE("The D-pad moves the focus to the nearest button in its direction across a grid", "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target);
    const std::vector<ElementId> grid = MakeGrid(root);
    test.Frame();
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
        TapFrame(test, step.direction);
        INFO(step.what);
        REQUIRE(root.GetFocused() == grid[static_cast<std::size_t>(step.expected)]);
    }
}

TEST_CASE("A direction with no focusable element that way keeps the focus", "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target);
    const std::vector<ElementId> grid = MakeGrid(root);
    test.Frame();
    REQUIRE(root.Focus(grid[2]));
    Fenrir::EventReader<UIEvent> events;

    TapFrame(test, GamepadButton::DPadRight);
    TapFrame(test, GamepadButton::DPadUp);
    INFO("nothing lies right of or above 2: it keeps the focus, and no focus or blur is sent");
    REQUIRE(root.GetFocused() == grid[2]);
    REQUIRE(Of(events, test.app, UIEventKind::Blur, grid[2]).empty());
}

TEST_CASE("The left stick past its dead zone moves the focus, and repeats while held", "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target);
    std::vector<ElementId> row;
    for (int i = 0; i < 5; ++i)
    {
        row.push_back(Place(root, root.GetRootElement(), "button", 60.0f * static_cast<float>(i), 0.0f, 50.0f, 30.0f));
    }
    test.Frame();
    REQUIRE(root.Focus(row[0]));

    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftX, UIRoot::NavigationDeadZone * 0.5f);
    test.Frame();
    INFO("inside the dead zone the stick moves nothing");
    REQUIRE(root.GetFocused() == row[0]);

    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftX, 0.9f);
    test.Frame();
    INFO("past it, the focus moves once at once");
    REQUIRE(root.GetFocused() == row[1]);

    constexpr double frame = 0.01;
    const auto framesFor = [&](double seconds) { return static_cast<int>(std::lround(seconds / frame)); };
    test.Frames(framesFor(UIRoot::NavigationRepeatDelay) - 2, frame);
    INFO("held, it waits out the repeat delay");
    REQUIRE(root.GetFocused() == row[1]);

    test.Frames(4, frame);
    INFO("then moves again");
    REQUIRE(root.GetFocused() == row[2]);

    test.Frames(framesFor(UIRoot::NavigationRepeatInterval) - 3, frame);
    INFO("not before the repeat interval");
    REQUIRE(root.GetFocused() == row[2]);
    test.Frames(4, frame);
    INFO("and again once it has passed");
    REQUIRE(root.GetFocused() == row[3]);

    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftX, 0.0f);
    test.Frames(framesFor(1.0), frame);
    INFO("let go, it stops");
    REQUIRE(root.GetFocused() == row[3]);
}

TEST_CASE("A on the gamepad activates the focused control as Enter does", "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target);
    const ElementId button = Place(root, root.GetRootElement(), "button", 0.0f, 0.0f, 50.0f, 30.0f);
    const ElementId box = Place(root, root.GetRootElement(), "checkbox", 0.0f, 50.0f, 20.0f, 20.0f);
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    REQUIRE(root.Focus(button));
    TapFrame(test, GamepadButton::South);
    INFO("A on a focused button is one click at it");
    REQUIRE(Of(events, test.app, UIEventKind::Click, button).size() == 1);

    REQUIRE(root.Focus(box));
    TapFrame(test, GamepadButton::South);
    INFO("A on a focused checkbox toggles it");
    REQUIRE(As<Fenrir::Checkbox>(root, box).IsChecked());
}

TEST_CASE("B on the gamepad sends cancel at the focused element", "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target);
    const ElementId panel = Place(root, root.GetRootElement(), "div", 0.0f, 0.0f, 200.0f, 100.0f);
    const ElementId button = Place(root, panel, "button", 0.0f, 0.0f, 50.0f, 30.0f);
    test.Frame();
    REQUIRE(root.Focus(button));
    int bubbled = 0;
    root.AddListener(panel, UIEventKind::Cancel, [&](Fenrir::UIListenerContext&) { ++bubbled; });
    Fenrir::EventReader<UIEvent> events;

    TapFrame(test, GamepadButton::East);
    INFO("one cancel at the focused button, which bubbles to the panel's listener; the focus stays");
    REQUIRE(Of(events, test.app, UIEventKind::Cancel, button).size() == 1);
    REQUIRE(bubbled == 1);
    REQUIRE(root.GetFocused() == button);
}

TEST_CASE("A slider takes the D-pad along its axis, and the focus leaves it across", "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target);
    const ElementId element = Place(root, root.GetRootElement(), "slider", 0.0f, 0.0f, 200.0f, 20.0f);
    const ElementId below = Place(root, root.GetRootElement(), "button", 0.0f, 50.0f, 50.0f, 30.0f);
    auto& slider = As<Fenrir::Slider>(root, element);
    slider.SetRange(0.0f, 10.0f);
    slider.SetStep(1.0f);
    slider.SetValue(5.0f);
    test.Frame();
    REQUIRE(root.Focus(element));

    TapFrame(test, GamepadButton::DPadRight);
    INFO("right on a horizontal slider steps its value up and keeps the focus");
    REQUIRE(slider.GetValue() == 6.0f);
    REQUIRE(root.GetFocused() == element);

    TapFrame(test, GamepadButton::DPadDown);
    INFO("down moves the focus to the button under it");
    REQUIRE(root.GetFocused() == below);
    REQUIRE(slider.GetValue() == 6.0f);
}

TEST_CASE("A focused element inside a scroll view is scrolled into view", "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target, false, true);
    const ElementId view = Place(root, root.GetRootElement(), "scroll", 0.0f, 0.0f, 200.0f, 100.0f);
    std::vector<ElementId> items;
    for (int i = 0; i < 6; ++i)
    {
        items.push_back(Place(root, view, "button", 0.0f, 50.0f * static_cast<float>(i), 100.0f, 40.0f));
    }
    test.Frame();
    REQUIRE(root.Focus(items[0]));

    for (int i = 0; i < 3; ++i)
    {
        TapFrame(test, GamepadButton::DPadDown);
    }
    test.Frame();
    INFO("down three times reaches item 3, past the view's bottom: the view scrolls just far enough to show it");
    REQUIRE(root.GetFocused() == items[3]);
    const Fenrir::Rect2D inside = root.GetBoxes(view).padding;
    const Fenrir::Rect2D item = root.GetBoxes(items[3]).border;
    REQUIRE(root.GetScrollOffset(view).y > 0.0f);
    REQUIRE(Near(item.y + item.height, inside.y + inside.height));

    for (int i = 0; i < 3; ++i)
    {
        TapFrame(test, GamepadButton::DPadUp);
    }
    test.Frame();
    INFO("back up to item 0 scrolls the view back to the top");
    REQUIRE(root.GetFocused() == items[0]);
    REQUIRE(root.GetScrollOffset(view).y == 0.0f);
}

TEST_CASE("A root without the focus takes it on the first direction", "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target);
    const std::vector<ElementId> grid = MakeGrid(root);
    test.Frame();
    Fenrir::EventReader<Fenrir::GamepadButtonEvent> game;

    TapFrame(test, GamepadButton::DPadDown);
    INFO("the first focusable element in tree order takes the focus, and the game does not see the press");
    REQUIRE(root.GetFocused() == grid[0]);
    REQUIRE(ReadAll(game, test.app).empty());

    TapFrame(test, GamepadButton::DPadDown);
    INFO("the next direction moves it from there");
    REQUIRE(root.GetFocused() == grid[3]);
}

TEST_CASE("The gamepad claim is published while the UI has the focus, and gamepad input passes otherwise",
          "[ui][gamepad]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target);
    const ElementId button = Place(root, root.GetRootElement(), "button", 0.0f, 0.0f, 50.0f, 30.0f);
    const ElementId label = Place(root, root.GetRootElement(), "div", 0.0f, 50.0f, 50.0f, 30.0f);
    test.Frame();
    const auto claimed = [&] { return test.app.GetResource<Fenrir::InputClaims>().Gamepad(); };
    const auto& pads = test.app.GetResource<Fenrir::Gamepads>();
    INFO("nothing has the focus: no claim");
    REQUIRE_FALSE(claimed());

    REQUIRE(root.Focus(button));
    test.Feed().GamepadButtonDown(pad, GamepadButton::South);
    test.Frame();
    INFO("focused: the root claims the gamepad and takes A from the game");
    REQUIRE(claimed());
    REQUIRE_FALSE(pads.IsDown(pad, GamepadButton::South));
    test.Feed().GamepadButtonUp(pad, GamepadButton::South);
    test.Frame();

    root.Blur();
    root.SetFocusable(button, false);
    test.Feed().GamepadButtonDown(pad, GamepadButton::DPadDown);
    test.Feed().GamepadButtonDown(pad, GamepadButton::South);
    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftX, 1.0f);
    test.Frame();
    INFO("with no focus and nothing to focus, the claim is down and the D-pad, A and the stick reach the game");
    REQUIRE_FALSE(claimed());
    REQUIRE(pads.IsDown(pad, GamepadButton::DPadDown));
    REQUIRE(pads.IsDown(pad, GamepadButton::South));
    REQUIRE(pads.Axis(pad, GamepadAxis::LeftX) == 1.0f);
    REQUIRE_FALSE(root.GetFocused().IsValid());
    (void)label;
}

TEST_CASE("The virtual cursor follows the stick, and hover follows the cursor", "[ui][gamepad][cursor]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target, true, true);
    const ElementId button = Place(root, root.GetRootElement(), "button", 250.0f, 125.0f, 100.0f, 50.0f);
    test.Frame();
    INFO("switched on, the cursor starts at the root's centre, drawn by its element there");
    REQUIRE(root.GetPointer().has_value());
    REQUIRE(Near(root.GetPointer()->x, 200.0f));
    REQUIRE(Near(root.GetPointer()->y, 150.0f));
    const ElementId cursor = root.GetVirtualCursor();
    REQUIRE(root.IsAlive(cursor));
    test.Frame();
    REQUIRE(Near(Centre(root.GetBoxes(cursor).border).x, 200.0f));
    REQUIRE(root.GetHovered() != button);

    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftX, 1.0f);
    test.Frame();
    const float start = root.GetPointer()->x;
    constexpr int frames = 6;
    test.Frames(frames, 1.0 / 60.0);
    INFO("full right, it moves the configured speed each second");
    const float speed = root.GetConfig().virtualCursorSpeed;
    REQUIRE(Near(root.GetPointer()->x, start + speed * frames / 60.0f, 0.01f));
    REQUIRE(Near(root.GetPointer()->y, 150.0f));
    INFO("and the button under it is hovered");
    REQUIRE(root.GetHovered() == button);

    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftX, 0.0f);
    test.Frame();
    test.Frame();
    INFO("the cursor element follows it");
    REQUIRE(Near(Centre(root.GetBoxes(cursor).border).x, root.GetPointer()->x));
}

TEST_CASE("A with the virtual cursor presses and releases under it, a click", "[ui][gamepad][cursor]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    // The default stylesheet gives the button pointer input.
    UIRoot& root = MakePlainRoot(test, fixture.target, true, true);
    const ElementId button = Place(root, root.GetRootElement(), "button", 150.0f, 100.0f, 100.0f, 100.0f);
    test.Frame();
    Fenrir::EventReader<UIEvent> events;
    Fenrir::EventReader<Fenrir::GamepadButtonEvent> game;

    TapFrame(test, GamepadButton::South);
    INFO("A over the button, where the cursor starts, presses, releases and clicks it, and the game sees no A");
    const std::vector<UIEvent> sent = ReadAll(events, test.app);
    const auto count = [&](UIEventKind kind) {
        return std::count_if(sent.begin(), sent.end(),
                             [&](const UIEvent& event) { return event.kind == kind && event.target == button; });
    };
    REQUIRE(count(UIEventKind::PointerPress) == 1);
    REQUIRE(count(UIEventKind::PointerRelease) == 1);
    REQUIRE(count(UIEventKind::Click) == 1);
    REQUIRE(ReadAll(game, test.app).empty());
}

TEST_CASE("With the virtual cursor off the stick moves nothing", "[ui][gamepad][cursor]")
{
    Pad fixture;
    UITestApp& test = fixture.test;
    UIRoot& root = MakePlainRoot(test, fixture.target, true);
    test.Frame();
    root.SetVirtualCursor(false);
    INFO("switched off, its element goes");
    REQUIRE_FALSE(root.GetVirtualCursor().IsValid());
    const Vec2 before = *root.GetPointer();

    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftX, 1.0f);
    test.Feed().GamepadAxisMoved(pad, GamepadAxis::LeftY, 1.0f);
    test.Frames(10, 1.0 / 60.0);
    INFO("the pointer stays where it was, and the stick reaches the game");
    REQUIRE(Near(root.GetPointer()->x, before.x));
    REQUIRE(Near(root.GetPointer()->y, before.y));
    REQUIRE(test.app.GetResource<Fenrir::Gamepads>().Axis(pad, GamepadAxis::LeftX) == 1.0f);
    REQUIRE_FALSE(test.app.GetResource<Fenrir::InputClaims>().Gamepad());
}
