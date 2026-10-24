// Input claims and cancellation: InputClaims shows the union per window; raising keyboardFocus or pointerCapture
// cancels the game's held controls; focus loss cancels every owned control and drops capture; a removed router's
// controls end silently (ADR-0027; spec: "FenrirInput", seam 3).

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"
#include "TestRouter.hpp"

using Fenrir::Input;
using Fenrir::InputClaims;
using Fenrir::Key;
using Fenrir::MouseButton;
using Fenrir::Scancode;
using Fenrir::WindowInputClaims;
using namespace InputTests;

namespace
{
    WindowInputClaims KeyboardFocus()
    {
        WindowInputClaims claims;
        claims.keyboardFocus = true;
        return claims;
    }

    WindowInputClaims PointerCapture()
    {
        WindowInputClaims claims;
        claims.pointerCapture = true;
        return claims;
    }
} // namespace

TEST_CASE("Raising keyboard focus cancels the game's held keys, and their physical release reaches no one",
          "[input][claims][cancellation]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    const Fenrir::InputRouterId ui = Input::AddInputRouter(test.app, 0, std::make_shared<TestRouter>("ui", journal));
    Fenrir::EventReader<Fenrir::KeyEvent> keys;
    test.Press(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);
    (void)ReadDescribed(keys, test.app);
    journal->clear();

    Input::SetInputClaims(test.app, ui, mainWindow, KeyboardFocus());

    INFO("the game gets a cancelled release at once, and the key is no longer held");
    REQUIRE(ReadDescribed(keys, test.app) == Lines{"key A cancelled"});
    REQUIRE_FALSE(test.Keyboard().IsDown(Key::A));
    REQUIRE_FALSE(test.Keyboard().IsDown(Scancode::A));

    test.Feed().KeyDown(mainWindow, Key::A, Scancode::A, Fenrir::KeyModifiers::None, true);
    test.Release(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);
    INFO("the later repeat and physical release reach neither the router nor the game");
    REQUIRE(Routed(journal).empty());
    REQUIRE(keys.Read(test.app).empty());
}

TEST_CASE("Raising pointer capture cancels the game's held mouse buttons in that window",
          "[input][claims][cancellation]")
{
    InputTestApp test;
    const Fenrir::InputRouterId ui =
        Input::AddInputRouter(test.app, 0, std::make_shared<TestRouter>("ui", NewJournal()));
    Fenrir::EventReader<Fenrir::MouseButtonEvent> buttons;
    test.Feed().MouseButtonDown(mainWindow, MouseButton::Left);
    test.Press(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);
    (void)buttons.Read(test.app);

    Input::SetInputClaims(test.app, ui, mainWindow, PointerCapture());

    INFO("the button gets a cancelled release; the key, another device, stays held");
    REQUIRE(ReadDescribed(buttons, test.app) == Lines{"button Left cancelled"});
    REQUIRE_FALSE(test.Mouse().IsDown(MouseButton::Left));
    REQUIRE(test.Keyboard().IsDown(Key::A));

    test.Feed().MouseButtonUp(mainWindow, MouseButton::Left);
    test.app.RunFrame(oneTick);
    INFO("the physical release reaches no one");
    REQUIRE(buttons.Read(test.app).empty());
}

TEST_CASE("Focus loss cancels every owned control in the window and drops its pointer capture",
          "[input][claims][cancellation]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    // Takes the left button only, so the key stays the game's.
    ui->decide = [](const Fenrir::RoutedInput& input) {
        const auto* button = std::get_if<Fenrir::MouseButtonEvent>(&input);
        return button && button->pressed ? Fenrir::RouteResult::Consumed : Fenrir::RouteResult::Passed;
    };
    const Fenrir::InputRouterId uiId = Input::AddInputRouter(test.app, 0, ui);
    Fenrir::EventReader<Fenrir::KeyEvent> keys;
    test.Feed().FocusGained(mainWindow);
    test.Feed().MouseButtonDown(mainWindow, MouseButton::Left);
    test.Press(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);
    Input::SetInputClaims(test.app, uiId, mainWindow, PointerCapture());
    (void)keys.Read(test.app);
    journal->clear();

    test.Feed().FocusLost(mainWindow);
    test.Feed().MouseButtonUp(mainWindow, MouseButton::Left);
    test.Release(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);

    INFO("the router gets a cancelled release of its button, then the focus loss; the physical releases go to no one");
    REQUIRE(Routed(journal) == Lines{"ui: button Left cancelled", "ui: focus lost"});
    INFO("the game gets a cancelled release of its key, and not the physical one");
    REQUIRE(ReadDescribed(keys, test.app) == Lines{"key A cancelled"});
    REQUIRE_FALSE(test.Keyboard().IsDown(Key::A));
    INFO("the window's pointer capture is dropped");
    REQUIRE_FALSE(test.app.GetResource<InputClaims>().For(mainWindow).pointerCapture);
}

TEST_CASE("Removing a router that owns controls ends them silently and clears its claims",
          "[input][claims][cancellation]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    ui->decide = ConsumePresses;
    const Fenrir::InputRouterId uiId = Input::AddInputRouter(test.app, 0, ui);
    Fenrir::EventReader<Fenrir::KeyEvent> keys;
    test.Press(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);
    Input::SetInputClaims(test.app, uiId, mainWindow, KeyboardFocus());
    Input::SetGamepadClaim(test.app, uiId, true);
    journal->clear();

    Input::RemoveInputRouter(test.app, uiId);
    test.Release(Key::A, Scancode::A);
    test.app.RunFrame(oneTick);

    INFO("the removed router hears nothing more, not even a cancellation");
    REQUIRE(journal->empty());
    INFO("the release of the key it owned reaches the game neither");
    REQUIRE(keys.Read(test.app).empty());
    INFO("its claims are gone");
    REQUIRE(test.app.GetResource<InputClaims>().For(mainWindow) == WindowInputClaims{});
    REQUIRE_FALSE(test.app.GetResource<InputClaims>().Gamepad());
}

TEST_CASE("InputClaims shows the union of the routers' claims per window", "[input][claims]")
{
    InputTestApp test;
    const Fenrir::InputRouterId low =
        Input::AddInputRouter(test.app, 0, std::make_shared<TestRouter>("low", NewJournal()));
    const Fenrir::InputRouterId high =
        Input::AddInputRouter(test.app, 10, std::make_shared<TestRouter>("high", NewJournal()));

    WindowInputClaims lowClaims;
    lowClaims.pointer = true;
    lowClaims.textInput = true;
    lowClaims.caret = {{1.0f, 2.0f}, {3.0f, 4.0f}};
    Input::SetInputClaims(test.app, low, mainWindow, lowClaims);
    WindowInputClaims highClaims;
    highClaims.keyboardFocus = true;
    highClaims.textInput = true;
    highClaims.caret = {{10.0f, 20.0f}, {1.0f, 16.0f}};
    Input::SetInputClaims(test.app, high, mainWindow, highClaims);
    Input::SetGamepadClaim(test.app, low, true);

    const InputClaims& claims = test.app.GetResource<InputClaims>();
    const WindowInputClaims main = claims.For(mainWindow);
    INFO("each claim either router raised is raised");
    REQUIRE(main.pointer);
    REQUIRE(main.keyboardFocus);
    REQUIRE(main.textInput);
    REQUIRE_FALSE(main.pointerCapture);
    INFO("the caret is the higher-priority claimant's");
    REQUIRE(main.caret == Fenrir::CaretRect{{10.0f, 20.0f}, {1.0f, 16.0f}});
    INFO("the gamepad claim is app-wide");
    REQUIRE(claims.Gamepad());

    Input::SetInputClaims(test.app, high, mainWindow, WindowInputClaims{});
    INFO("when the higher router lowers its claims, the lower's caret shows and keyboard focus is gone");
    REQUIRE(claims.For(mainWindow).caret == Fenrir::CaretRect{{1.0f, 2.0f}, {3.0f, 4.0f}});
    REQUIRE_FALSE(claims.For(mainWindow).keyboardFocus);
}

TEST_CASE("Two windows keep their claims and their owners apart", "[input][claims][cancellation]")
{
    InputTestApp test;
    Journal journal = NewJournal();
    auto ui = std::make_shared<TestRouter>("ui", journal);
    // Takes presses in the other window only.
    ui->decide = [](const Fenrir::RoutedInput& input) {
        const auto* key = std::get_if<Fenrir::KeyEvent>(&input);
        return key && key->pressed && key->window == otherWindow ? Fenrir::RouteResult::Consumed
                                                                 : Fenrir::RouteResult::Passed;
    };
    const Fenrir::InputRouterId uiId = Input::AddInputRouter(test.app, 0, ui);
    Fenrir::EventReader<Fenrir::KeyEvent> keys;
    test.Press(Key::A, Scancode::A);
    test.Feed().KeyDown(otherWindow, Key::B, Scancode::B);
    test.app.RunFrame(oneTick);
    (void)keys.Read(test.app);

    Input::SetInputClaims(test.app, uiId, otherWindow, KeyboardFocus());
    INFO("focus claimed in the other window cancels nothing held in the main window");
    REQUIRE(keys.Read(test.app).empty());
    REQUIRE(test.Keyboard().IsDown(Key::A));
    INFO("and shows in the other window's claims only");
    REQUIRE(test.app.GetResource<InputClaims>().For(otherWindow).keyboardFocus);
    REQUIRE_FALSE(test.app.GetResource<InputClaims>().For(mainWindow).keyboardFocus);

    journal->clear();
    test.Release(Key::A, Scancode::A);
    test.Feed().KeyUp(otherWindow, Key::B, Scancode::B);
    test.app.RunFrame(oneTick);
    INFO("each release goes to its own press's owner: A to the game, B to the router");
    REQUIRE(ReadDescribed(keys, test.app) == Lines{"key A up"});
    REQUIRE(Routed(journal) == Lines{"ui: key B up"});
}
