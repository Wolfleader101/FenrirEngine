// Pointer input through a root's input router (ADR-0065, ADR-0027; spec: "FenrirUI" input and focus, seam 1): hit
// testing, consumption, hover and press, and the UIEvents they send. Each test lays the tree out on one frame and feeds
// input for the next, since a router hit-tests against the last frame's layout.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirInput/Devices.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "UITestApp.hpp"

using Fenrir::ElementId;
using Fenrir::MouseButton;
using Fenrir::UIEvent;
using Fenrir::UIEventKind;
using Fenrir::UIRoot;
using Fenrir::Math::Vec2;
using namespace UITest;

namespace
{
    // An absolutely placed box under `parent`, styled by `extra` (CSS declarations as name and value pairs).
    ElementId Box(UIRoot& root, ElementId parent, float x, float y, float width, float height,
                  std::vector<std::pair<std::string, std::string>> extra = {})
    {
        const ElementId box = root.Create(parent, "div");
        root.SetStyleProperty(box, "position", "absolute");
        root.SetStyleProperty(box, "left", std::to_string(x) + "px");
        root.SetStyleProperty(box, "top", std::to_string(y) + "px");
        root.SetStyleProperty(box, "width", std::to_string(width) + "px");
        root.SetStyleProperty(box, "height", std::to_string(height) + "px");
        for (const auto& [name, value] : extra)
        {
            root.SetStyleProperty(box, name, value);
        }
        return box;
    }

    // The kinds of the UIEvents `reader` has not read yet, with their targets.
    std::vector<std::pair<UIEventKind, ElementId>> Sent(Fenrir::EventReader<UIEvent>& reader, Fenrir::App& app)
    {
        std::vector<std::pair<UIEventKind, ElementId>> sent;
        for (const UIEvent& event : reader.Read(app))
        {
            sent.emplace_back(event.kind, event.target);
        }
        return sent;
    }

    using Sends = std::vector<std::pair<UIEventKind, ElementId>>;
} // namespace

TEST_CASE("A click on an element with pointer-events none reaches the game's Mouse and sends no UI event",
          "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    (void)Box(root, root.GetRootElement(), 10.0f, 10.0f, 100.0f, 100.0f);
    test.Frame();
    Fenrir::EventReader<UIEvent> events;
    Fenrir::EventReader<Fenrir::MouseButtonEvent> buttons;

    test.Feed().MouseMove(testWindow, {50.0f, 50.0f}, {0.0f, 0.0f});
    test.Feed().MouseButtonDown(testWindow, MouseButton::Left);
    test.Frame();

    INFO("the HUD is transparent to input by default, so the game holds the button and reads its press");
    REQUIRE(test.app.GetResource<Fenrir::Mouse>().IsDown(MouseButton::Left));
    REQUIRE(buttons.Read(test.app).size() == 1);
    INFO("nothing took the click, so no UI event was sent");
    REQUIRE(Sent(events, test.app).empty());
}

TEST_CASE("A click on an element with pointer-events all is consumed and sends press, release and click", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId panel = Box(root, root.GetRootElement(), 10.0f, 10.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    test.Frame();
    Fenrir::EventReader<UIEvent> events;
    Fenrir::EventReader<Fenrir::MouseButtonEvent> buttons;

    test.Click({50.0f, 60.0f});
    test.Frame();

    INFO("the game's Mouse never saw the button and no game reader got it");
    REQUIRE_FALSE(test.app.GetResource<Fenrir::Mouse>().JustPressed(MouseButton::Left));
    REQUIRE_FALSE(test.app.GetResource<Fenrir::Mouse>().JustReleased(MouseButton::Left));
    REQUIRE(buttons.Read(test.app).empty());
    INFO("the panel got enter (the pointer came over it), press, release and click, in that order");
    REQUIRE(Sent(events, test.app) == Sends{{UIEventKind::PointerEnter, panel},
                                            {UIEventKind::PointerPress, panel},
                                            {UIEventKind::PointerRelease, panel},
                                            {UIEventKind::Click, panel}});
}

TEST_CASE("A UI event carries its root, the pointer in UI units and the button", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    // At the 1080 scale rule a 540-high target is half a UI unit per logical unit.
    UIRoot& root = test.MakeRoot(target, {960.0f, 540.0f}, 1.0f, Fenrir::ScaleRule::Height1080);
    const ElementId panel = Box(root, root.GetRootElement(), 0.0f, 0.0f, 400.0f, 400.0f, {{"pointer-events", "all"}});
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Feed().MouseMove(testWindow, {50.0f, 60.0f}, {0.0f, 0.0f});
    test.Feed().MouseButtonDown(testWindow, MouseButton::Right);
    test.Frame();

    const std::vector<UIEvent> read = ReadAll(events, test.app);
    REQUIRE(read.size() == 2);
    const UIEvent& press = read[1];
    INFO("the press names the root, the panel, the right button and the pointer at twice the logical position");
    REQUIRE(press.kind == UIEventKind::PointerPress);
    REQUIRE(press.root == root.GetId());
    REQUIRE(press.target == panel);
    REQUIRE(press.button == MouseButton::Right);
    REQUIRE(Near(press.position.x, 100.0f));
    REQUIRE(Near(press.position.y, 120.0f));
}

TEST_CASE("A click on a rounded corner's cut-off hits what is behind", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId back = Box(root, root.GetRootElement(), 0.0f, 0.0f, 400.0f, 300.0f, {{"pointer-events", "all"}});
    const ElementId round = Box(root, root.GetRootElement(), 100.0f, 100.0f, 100.0f, 100.0f,
                                {{"pointer-events", "all"}, {"border-radius", "40px"}});
    test.Frame();

    INFO("the corner square's outer point is outside the 40px arc, so it is the panel behind");
    REQUIRE(root.HitTest({103.0f, 103.0f}) == back);
    INFO("inside the arc, and in the middle, it is the rounded box");
    REQUIRE(root.HitTest({115.0f, 115.0f}) == round);
    REQUIRE(root.HitTest({150.0f, 150.0f}) == round);

    Fenrir::EventReader<UIEvent> events;
    test.Click({103.0f, 103.0f});
    test.Frame();
    INFO("a click there clicks the panel behind");
    const Sends sent = Sent(events, test.app);
    REQUIRE(sent.back() == std::pair{UIEventKind::Click, back});
}

TEST_CASE("A click outside an overflow-hidden parent's box misses a child that overflows it", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId clip = Box(root, root.GetRootElement(), 50.0f, 50.0f, 100.0f, 100.0f, {{"overflow", "hidden"}});
    const ElementId child = Box(root, clip, 50.0f, 50.0f, 150.0f, 150.0f, {{"pointer-events", "all"}});
    test.Frame();

    INFO("inside the parent, the child is hit");
    REQUIRE(root.HitTest({120.0f, 120.0f}) == child);
    INFO("where the child overflows the hidden parent, nothing is: the click goes to the game");
    REQUIRE_FALSE(root.HitTest({180.0f, 180.0f}).IsValid());
    test.Click({180.0f, 180.0f});
    test.Frame();
    REQUIRE(test.app.GetResource<Fenrir::Mouse>().JustReleased(MouseButton::Left));
}

TEST_CASE("Hover enters and leaves as the pointer moves, and :hover follows it", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId a = Box(root, root.GetRootElement(), 0.0f, 0.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    const ElementId b = Box(root, root.GetRootElement(), 200.0f, 0.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    root.AddStylesheet("div:hover { opacity: 0.5; }", "hover.css");
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Feed().MouseMove(testWindow, {50.0f, 50.0f}, {0.0f, 0.0f});
    test.Frame();
    INFO("over a: a is entered, carries :hover and is styled by it");
    REQUIRE(Sent(events, test.app) == Sends{{UIEventKind::PointerEnter, a}});
    REQUIRE(root.HasPseudoClass(a, Fenrir::PseudoClass::Hover));
    REQUIRE(root.GetHovered() == a);
    REQUIRE(Near(root.GetResolvedStyle(a)->opacity, 0.5f));

    test.Feed().MouseMove(testWindow, {60.0f, 50.0f}, {10.0f, 0.0f});
    test.Frame();
    INFO("moving within a is a move on a");
    REQUIRE(Sent(events, test.app) == Sends{{UIEventKind::PointerMove, a}});

    test.Feed().MouseMove(testWindow, {250.0f, 50.0f}, {190.0f, 0.0f});
    test.Frame();
    INFO("over b: a is left, then b entered, and :hover moved with the pointer");
    REQUIRE(Sent(events, test.app) == Sends{{UIEventKind::PointerLeave, a}, {UIEventKind::PointerEnter, b}});
    REQUIRE_FALSE(root.HasPseudoClass(a, Fenrir::PseudoClass::Hover));
    REQUIRE(root.HasPseudoClass(b, Fenrir::PseudoClass::Hover));

    test.Feed().MouseMove(testWindow, {350.0f, 250.0f}, {100.0f, 200.0f});
    test.Frame();
    INFO("over nothing: b is left and nothing is hovered");
    REQUIRE(Sent(events, test.app) == Sends{{UIEventKind::PointerLeave, b}});
    REQUIRE_FALSE(root.GetHovered().IsValid());
}

TEST_CASE("The topmost of two overlapping elements wins by z-index, then tree order", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId first = Box(root, root.GetRootElement(), 0.0f, 0.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    const ElementId second =
        Box(root, root.GetRootElement(), 50.0f, 50.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    test.Frame();

    INFO("at equal z-index the later sibling is drawn over the earlier, so it is hit");
    REQUIRE(root.HitTest({75.0f, 75.0f}) == second);

    root.SetStyleProperty(first, "z-index", "1");
    test.Frame();
    INFO("with a higher z-index the earlier sibling is on top");
    REQUIRE(root.HitTest({75.0f, 75.0f}) == first);

    Fenrir::EventReader<UIEvent> events;
    test.Click({75.0f, 75.0f});
    test.Frame();
    REQUIRE(Sent(events, test.app).back() == std::pair{UIEventKind::Click, first});
}

TEST_CASE("A transformed element is hit where it is drawn", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId moved = Box(root, root.GetRootElement(), 0.0f, 0.0f, 100.0f, 100.0f,
                                {{"pointer-events", "all"}, {"transform", "translate(200px, 0px)"}});
    test.Frame();

    INFO("its laid-out place is empty; it is hit 200 units to the right");
    REQUIRE_FALSE(root.HitTest({50.0f, 50.0f}).IsValid());
    REQUIRE(root.HitTest({250.0f, 50.0f}) == moved);
}

TEST_CASE("A child with pointer-events all is hit inside a parent that takes none", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId parent = Box(root, root.GetRootElement(), 0.0f, 0.0f, 200.0f, 200.0f);
    const ElementId button = Box(root, parent, 10.0f, 10.0f, 50.0f, 50.0f, {{"pointer-events", "all"}});
    test.Frame();

    INFO("the button takes the pointer; the rest of its parent lets it through");
    REQUIRE(root.HitTest({20.0f, 20.0f}) == button);
    REQUIRE_FALSE(root.HitTest({150.0f, 150.0f}).IsValid());
}

TEST_CASE("Press and release elsewhere sends no click, and the release goes to the pressed element", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId a = Box(root, root.GetRootElement(), 0.0f, 0.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Feed().MouseMove(testWindow, {50.0f, 50.0f}, {0.0f, 0.0f});
    test.Feed().MouseButtonDown(testWindow, MouseButton::Left);
    test.Frame();
    INFO("while pressed, a carries :active");
    REQUIRE(root.HasPseudoClass(a, Fenrir::PseudoClass::Active));
    (void)events.Read(test.app);

    test.Feed().MouseMove(testWindow, {300.0f, 250.0f}, {250.0f, 200.0f});
    test.Feed().MouseButtonUp(testWindow, MouseButton::Left);
    test.Frame();
    INFO("a is left, then released, and never clicked; the game's Mouse never saw the release");
    REQUIRE(Sent(events, test.app) == Sends{{UIEventKind::PointerLeave, a}, {UIEventKind::PointerRelease, a}});
    REQUIRE_FALSE(root.HasPseudoClass(a, Fenrir::PseudoClass::Active));
    REQUIRE_FALSE(test.app.GetResource<Fenrir::Mouse>().JustReleased(MouseButton::Left));
}

TEST_CASE("Two quick clicks on one element send a double click after the second click", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId a = Box(root, root.GetRootElement(), 0.0f, 0.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    test.Frame();
    test.Click({50.0f, 50.0f});
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Click({52.0f, 50.0f});
    test.Frame();

    const std::vector<UIEvent> read = ReadAll(events, test.app);
    INFO("the second click within the double-click time is followed by a double click with a count of two");
    REQUIRE(read.size() >= 2);
    REQUIRE(read[read.size() - 2].kind == UIEventKind::Click);
    REQUIRE(read.back().kind == UIEventKind::DoubleClick);
    REQUIRE(read.back().target == a);
    REQUIRE(read.back().integer == 2);
}

TEST_CASE("A scroll over an element that takes the pointer is sent to it and consumed", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId a = Box(root, root.GetRootElement(), 0.0f, 0.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    test.Frame();
    test.Feed().MouseMove(testWindow, {50.0f, 50.0f}, {0.0f, 0.0f});
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Feed().Scroll(testWindow, {0.0f, 2.0f});
    test.Frame();

    const std::vector<UIEvent> read = ReadAll(events, test.app);
    INFO("one scroll of two notches at a, and the game's Mouse did not scroll");
    REQUIRE(read.size() == 1);
    REQUIRE(read[0].kind == UIEventKind::Scroll);
    REQUIRE(read[0].target == a);
    REQUIRE(Near(read[0].value, 2.0f));
    REQUIRE(test.app.GetResource<Fenrir::Mouse>().Scroll() == Vec2(0.0f, 0.0f));
}

TEST_CASE("The root claims the pointer over its UI and captures it while pressed", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    (void)Box(root, root.GetRootElement(), 0.0f, 0.0f, 100.0f, 100.0f, {{"pointer-events", "all"}});
    test.Frame();
    const auto claims = [&] { return test.app.GetResource<Fenrir::InputClaims>().For(testWindow); };

    test.Feed().MouseMove(testWindow, {50.0f, 50.0f}, {0.0f, 0.0f});
    test.Frame();
    INFO("over the box: the pointer claim, no capture");
    REQUIRE(claims().pointer);
    REQUIRE_FALSE(claims().pointerCapture);

    test.Feed().MouseButtonDown(testWindow, MouseButton::Left);
    test.Frame();
    INFO("pressed on the box: the pointer is captured");
    REQUIRE(claims().pointerCapture);

    test.Feed().MouseButtonUp(testWindow, MouseButton::Left);
    test.Feed().MouseMove(testWindow, {300.0f, 250.0f}, {250.0f, 200.0f});
    test.Frame();
    INFO("released and moved off: no claim");
    REQUIRE_FALSE(claims().pointer);
    REQUIRE_FALSE(claims().pointerCapture);
}

TEST_CASE("A root whose root element takes pointer-events captures every click on its target", "[ui][input]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    root.AddStylesheet(":root { pointer-events: all; }", "menu.css");
    test.Frame();

    test.Click({390.0f, 290.0f});
    test.Frame();
    INFO("one rule on the root makes the whole menu opaque to clicks");
    REQUIRE(root.HitTest({390.0f, 290.0f}) == root.GetRootElement());
    REQUIRE_FALSE(test.app.GetResource<Fenrir::Mouse>().JustReleased(MouseButton::Left));
}
