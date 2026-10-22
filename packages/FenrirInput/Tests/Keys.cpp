// Key codes and scancodes, and modifiers (ADR-0017; GLOSSARY: key code, scancode).

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"

using Fenrir::Key;
using Fenrir::KeyModifiers;
using Fenrir::Scancode;
using namespace InputTests;

TEST_CASE("Key codes and scancodes are tracked separately", "[input][keys]")
{
    InputTestApp test;
    // On an AZERTY keyboard the key at QWERTY's W position types Z.
    test.Press(Key::Z, Scancode::W);
    test.app.RunFrame(oneTick);

    const Fenrir::Keyboard& keyboard = test.Keyboard();
    INFO("the key is held by the key code the layout gave it");
    REQUIRE(keyboard.IsDown(Key::Z));
    REQUIRE(keyboard.JustPressed(Key::Z));
    INFO("and by the scancode of where it sits");
    REQUIRE(keyboard.IsDown(Scancode::W));
    REQUIRE(keyboard.JustPressed(Scancode::W));
    INFO("the key code of the same name as the scancode is not held");
    REQUIRE_FALSE(keyboard.IsDown(Key::W));
    REQUIRE_FALSE(keyboard.JustPressed(Key::W));
    INFO("nor the scancode of the same name as the key code");
    REQUIRE_FALSE(keyboard.IsDown(Scancode::Z));

    test.Release(Key::Z, Scancode::W);
    test.app.RunFrame(oneTick);
    INFO("releasing it releases both");
    REQUIRE_FALSE(keyboard.IsDown(Key::Z));
    REQUIRE_FALSE(keyboard.IsDown(Scancode::W));
    REQUIRE(keyboard.JustReleased(Key::Z));
    REQUIRE(keyboard.JustReleased(Scancode::W));
}

TEST_CASE("The keyboard reports the modifiers of the latest key", "[input][keys]")
{
    InputTestApp test;
    test.Feed().KeyDown(mainWindow, Key::S, Scancode::S, KeyModifiers::Ctrl | KeyModifiers::Shift);
    test.app.RunFrame(oneTick);

    const KeyModifiers modifiers = test.Keyboard().Modifiers();
    INFO("Ctrl and Shift were held when S went down");
    REQUIRE(Fenrir::HasModifiers(modifiers, KeyModifiers::Ctrl | KeyModifiers::Shift));
    INFO("Alt was not");
    REQUIRE_FALSE(Fenrir::HasModifiers(modifiers, KeyModifiers::Alt));

    test.Feed().KeyUp(mainWindow, Key::S, Scancode::S, KeyModifiers::None);
    test.app.RunFrame(oneTick);
    INFO("none were held when it went up");
    REQUIRE(test.Keyboard().Modifiers() == KeyModifiers::None);
}

TEST_CASE("The keyboard reports which window has the input focus", "[input][keys]")
{
    InputTestApp test;
    INFO("no window has the focus before one gains it");
    REQUIRE_FALSE(test.Keyboard().FocusedWindow().has_value());

    test.Feed().FocusGained(otherWindow);
    test.app.RunFrame(oneTick);
    INFO("the window that gained the focus has it");
    REQUIRE(test.Keyboard().FocusedWindow() == otherWindow);

    test.Feed().FocusLost(otherWindow);
    test.app.RunFrame(oneTick);
    INFO("after it loses the focus, no window has it");
    REQUIRE_FALSE(test.Keyboard().FocusedWindow().has_value());
}

TEST_CASE("Every letter from A to Z has a key code and a scancode of its own", "[input][keys]")
{
    // The letters' enumerators are in alphabetical order after Unknown, so X sits between W and Y.
    INFO("Key::X and Scancode::X are named, and follow W");
    REQUIRE(static_cast<int>(Key::X) == static_cast<int>(Key::W) + 1);
    REQUIRE(static_cast<int>(Scancode::X) == static_cast<int>(Scancode::W) + 1);
    INFO("Z is the 26th key after Unknown");
    REQUIRE(static_cast<int>(Key::Z) == 26);
    REQUIRE(static_cast<int>(Scancode::Z) == 26);
}
