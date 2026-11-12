// Focus (ADR-0065; spec: "FenrirUI" input and focus, seam 1): by click, by Tab and Shift+Tab, from code; `:focus` and
// `:focus-within`; keys and text to the focused element, consumed; and the keyboard-focus claim.

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirInput/Devices.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "UITestApp.hpp"

using Fenrir::ElementId;
using Fenrir::Key;
using Fenrir::KeyModifiers;
using Fenrir::PseudoClass;
using Fenrir::Scancode;
using Fenrir::UIEvent;
using Fenrir::UIEventKind;
using Fenrir::UIRoot;
using namespace UITest;

namespace
{
    // A row of `count` focusable buttons, 50 units wide and 10 apart, under a panel; the panel takes the pointer.
    struct Menu
    {
        ElementId panel;
        std::vector<ElementId> buttons;
    };

    Menu MakeMenu(UIRoot& root, int count)
    {
        Menu menu;
        menu.panel = root.Create(root.GetRootElement(), "div");
        root.SetStyleProperty(menu.panel, "flex-direction", "row");
        root.SetStyleProperty(menu.panel, "width", "400px");
        root.SetStyleProperty(menu.panel, "height", "100px");
        root.SetStyleProperty(menu.panel, "pointer-events", "all");
        for (int i = 0; i < count; ++i)
        {
            const ElementId button = root.Create(menu.panel, "button");
            root.SetStyleProperty(button, "width", "50px");
            root.SetStyleProperty(button, "height", "50px");
            root.SetStyleProperty(button, "margin-right", "10px");
            root.SetFocusable(button, true);
            menu.buttons.push_back(button);
        }
        return menu;
    }

    // The centre of button `i` of a MakeMenu row, in the window.
    Fenrir::Math::Vec2 ButtonCentre(int i)
    {
        return {25.0f + 60.0f * static_cast<float>(i), 25.0f};
    }

    std::vector<std::pair<UIEventKind, ElementId>> FocusSends(Fenrir::EventReader<UIEvent>& reader, Fenrir::App& app)
    {
        std::vector<std::pair<UIEventKind, ElementId>> sent;
        for (const UIEvent& event : reader.Read(app))
        {
            if (event.kind == UIEventKind::Focus || event.kind == UIEventKind::Blur)
            {
                sent.emplace_back(event.kind, event.target);
            }
        }
        return sent;
    }

    using Sends = std::vector<std::pair<UIEventKind, ElementId>>;
} // namespace

TEST_CASE("A click focuses the nearest focusable ancestor of what it hit", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 2);
    // The label inside the second button takes the pointer itself; the button is the focusable one above it.
    const ElementId label = root.Create(menu.buttons[1], "label");
    root.SetStyleProperty(label, "width", "20px");
    root.SetStyleProperty(label, "height", "20px");
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Click({ButtonCentre(1).x - 20.0f, 10.0f});
    test.Frame();

    INFO("the label was hit (through the panel's inherited pointer-events); its button took the focus");
    REQUIRE(root.GetFocused() == menu.buttons[1]);
    REQUIRE(root.HasPseudoClass(menu.buttons[1], PseudoClass::Focus));
    REQUIRE(FocusSends(events, test.app) == Sends{{UIEventKind::Focus, menu.buttons[1]}});
}

TEST_CASE("A click outside the focused element on nothing focusable blurs it", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 2);
    test.Frame();
    test.Click(ButtonCentre(0));
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    INFO("a click on the other button moves the focus: blur, then focus");
    test.Click(ButtonCentre(1));
    test.Frame();
    REQUIRE(FocusSends(events, test.app) ==
            Sends{{UIEventKind::Blur, menu.buttons[0]}, {UIEventKind::Focus, menu.buttons[1]}});

    INFO("a click on the panel between them, which is not focusable, blurs");
    test.Click({300.0f, 50.0f});
    test.Frame();
    REQUIRE(FocusSends(events, test.app) == Sends{{UIEventKind::Blur, menu.buttons[1]}});
    REQUIRE_FALSE(root.GetFocused().IsValid());
    REQUIRE_FALSE(root.HasPseudoClass(menu.buttons[1], PseudoClass::Focus));

    root.Focus(menu.buttons[0]);
    INFO("a click outside the UI altogether blurs as well");
    test.Click({300.0f, 250.0f});
    test.Frame();
    REQUIRE_FALSE(root.GetFocused().IsValid());
}

TEST_CASE("Tab and Shift+Tab walk the focusable elements in tree order and wrap", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 3);
    test.Frame();
    REQUIRE(root.Focus(menu.buttons[0]));

    std::vector<ElementId> walked;
    for (int i = 0; i < 3; ++i)
    {
        test.Type(Key::Tab, Scancode::Tab);
        test.Frame();
        walked.push_back(root.GetFocused());
    }
    INFO("Tab goes forward and wraps from the last to the first");
    REQUIRE(walked == std::vector{menu.buttons[1], menu.buttons[2], menu.buttons[0]});

    walked.clear();
    for (int i = 0; i < 2; ++i)
    {
        test.Type(Key::Tab, Scancode::Tab, KeyModifiers::Shift);
        test.Frame();
        walked.push_back(root.GetFocused());
    }
    INFO("Shift+Tab goes back and wraps from the first to the last");
    REQUIRE(walked == std::vector{menu.buttons[2], menu.buttons[1]});
}

TEST_CASE("Tab skips elements that are not focusable or not displayed", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 4);
    root.SetFocusable(menu.buttons[1], false);
    root.SetStyleProperty(menu.buttons[2], "display", "none");
    test.Frame();
    REQUIRE(root.Focus(menu.buttons[0]));

    test.Type(Key::Tab, Scancode::Tab);
    test.Frame();
    INFO("the unfocusable second and the hidden third are passed over");
    REQUIRE(root.GetFocused() == menu.buttons[3]);
}

TEST_CASE("The focused element's ancestors carry :focus-within until it blurs", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 2);
    root.AddStylesheet("div:focus-within { opacity: 0.25; }", "within.css");
    test.Frame();

    REQUIRE(root.Focus(menu.buttons[1]));
    test.Frame();
    INFO("the panel and the root element hold :focus-within; the button holds :focus, not :focus-within");
    REQUIRE(root.HasPseudoClass(menu.panel, PseudoClass::FocusWithin));
    REQUIRE(root.HasPseudoClass(root.GetRootElement(), PseudoClass::FocusWithin));
    REQUIRE(root.HasPseudoClass(menu.buttons[1], PseudoClass::Focus));
    REQUIRE_FALSE(root.HasPseudoClass(menu.buttons[1], PseudoClass::FocusWithin));
    REQUIRE(Near(root.GetResolvedStyle(menu.panel)->opacity, 0.25f));

    root.Blur();
    test.Frame();
    INFO("blurred, none of them do");
    REQUIRE_FALSE(root.HasPseudoClass(menu.panel, PseudoClass::FocusWithin));
    REQUIRE_FALSE(root.HasPseudoClass(root.GetRootElement(), PseudoClass::FocusWithin));
    REQUIRE(Near(root.GetResolvedStyle(menu.panel)->opacity, 1.0f));
}

TEST_CASE("Removing the focused element takes the focus and :focus-within away", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 2);
    test.Frame();
    REQUIRE(root.Focus(menu.buttons[0]));

    root.Remove(menu.buttons[0]);
    INFO("nothing has the focus and the panel no longer holds :focus-within");
    REQUIRE_FALSE(root.GetFocused().IsValid());
    REQUIRE_FALSE(root.HasPseudoClass(menu.panel, PseudoClass::FocusWithin));
}

TEST_CASE("Focus refuses an element that is not focusable", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 1);

    INFO("the panel is not focusable, so it does not take the focus");
    REQUIRE_FALSE(root.Focus(menu.panel));
    REQUIRE_FALSE(root.GetFocused().IsValid());
}

TEST_CASE("Keys and text go to the focused element and are consumed", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 1);
    test.Frame();
    REQUIRE(root.Focus(menu.buttons[0]));
    Fenrir::EventReader<UIEvent> events;
    Fenrir::EventReader<Fenrir::KeyEvent> keys;
    Fenrir::EventReader<Fenrir::TextEvent> text;
    // A new reader starts at the oldest event still held: past the Focus just sent.
    (void)ReadAll(events, test.app);

    test.Feed().KeyDown(testWindow, Key::A, Scancode::A, KeyModifiers::Ctrl);
    test.Feed().Text(testWindow, U"a");
    test.Feed().KeyUp(testWindow, Key::A, Scancode::A);
    test.Frame();

    const std::vector<UIEvent> read = ReadAll(events, test.app);
    INFO("the button got the key press (with Ctrl), the text and the release");
    REQUIRE(read.size() == 3);
    REQUIRE(read[0].kind == UIEventKind::KeyPress);
    REQUIRE(read[0].target == menu.buttons[0]);
    REQUIRE(read[0].key == Key::A);
    REQUIRE(read[0].integer == static_cast<std::int32_t>(KeyModifiers::Ctrl));
    REQUIRE(read[1].kind == UIEventKind::TextInput);
    REQUIRE(read[1].integer == static_cast<std::int32_t>(U'a'));
    REQUIRE(read[2].kind == UIEventKind::KeyRelease);
    INFO("the game saw none of it");
    REQUIRE_FALSE(test.app.GetResource<Fenrir::Keyboard>().JustPressed(Key::A));
    REQUIRE(keys.Read(test.app).empty());
    REQUIRE(text.Read(test.app).empty());
}

TEST_CASE("Keys pass to the game while nothing has the focus", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    (void)MakeMenu(root, 2);
    test.Frame();
    Fenrir::EventReader<UIEvent> events;

    test.Type(Key::Tab, Scancode::Tab);
    test.Frame();
    INFO("Tab with no focus is the game's, and moves no focus");
    REQUIRE(test.app.GetResource<Fenrir::Keyboard>().JustPressed(Key::Tab));
    REQUIRE_FALSE(root.GetFocused().IsValid());
    REQUIRE(events.Read(test.app).empty());
}

TEST_CASE("The claims resource shows keyboard focus while an element is focused", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 1);
    test.Frame();
    const auto claims = [&] { return test.app.GetResource<Fenrir::InputClaims>().For(testWindow); };

    INFO("unfocused, no keyboard focus is claimed");
    REQUIRE_FALSE(claims().keyboardFocus);
    test.Click(ButtonCentre(0));
    test.Frame();
    INFO("focused by a click, it is");
    REQUIRE(claims().keyboardFocus);
    root.Blur();
    INFO("blurred, it is not, at once");
    REQUIRE_FALSE(claims().keyboardFocus);
}

TEST_CASE("Tab's release goes to the element that had its press, not the one it focused", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const Menu menu = MakeMenu(root, 2);
    test.Frame();
    REQUIRE(root.Focus(menu.buttons[0]));
    Fenrir::EventReader<UIEvent> events;
    (void)ReadAll(events, test.app);

    test.Type(Key::Tab, Scancode::Tab);
    test.Frame();

    std::vector<std::pair<UIEventKind, ElementId>> keys;
    for (const UIEvent& event : ReadAll(events, test.app))
    {
        if (event.kind == UIEventKind::KeyPress || event.kind == UIEventKind::KeyRelease)
        {
            keys.emplace_back(event.kind, event.target);
        }
    }
    INFO("the first button got both the press and the release; the focus moved to the second");
    REQUIRE(keys == Sends{{UIEventKind::KeyPress, menu.buttons[0]}, {UIEventKind::KeyRelease, menu.buttons[0]}});
    REQUIRE(root.GetFocused() == menu.buttons[1]);
}

TEST_CASE("A click on another root on the window blurs this one", "[ui][focus]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> hudTarget;
    UIRoot& hud = test.MakeRoot(hudTarget, {400.0f, 300.0f});
    const Menu menu = MakeMenu(hud, 1);
    hud.SetStyleProperty(menu.panel, "height", "300px");
    // Drawn over the HUD, so its router goes first and the HUD never sees a press it consumes.
    Fenrir::UIRootConfig popupConfig;
    popupConfig.scaleRule = Fenrir::ScaleRule::ContentScale;
    popupConfig.target = std::make_shared<CaptureTarget>(Fenrir::UITargetSize{{400.0f, 300.0f}, 1.0f});
    popupConfig.inputPriority = 10;
    UIRoot& popup = test.Roots().CreateRoot(popupConfig);
    const ElementId panel = popup.Create(popup.GetRootElement(), "div");
    popup.SetStyleProperty(panel, "position", "absolute");
    popup.SetStyleProperty(panel, "top", "200px");
    popup.SetStyleProperty(panel, "width", "100px");
    popup.SetStyleProperty(panel, "height", "50px");
    popup.SetStyleProperty(panel, "pointer-events", "all");
    test.Frame();
    REQUIRE(hud.Focus(menu.buttons[0]));

    test.Click({50.0f, 220.0f});
    test.Frame();
    INFO("the other root took the click, and the first lost its focus and its keyboard claim");
    REQUIRE_FALSE(hud.GetFocused().IsValid());
    REQUIRE_FALSE(test.app.GetResource<Fenrir::InputClaims>().For(testWindow).keyboardFocus);
}
