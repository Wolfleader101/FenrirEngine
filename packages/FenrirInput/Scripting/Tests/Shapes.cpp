// The C# mirrors' shapes: each Fenrir.Input enum names C++'s values (its Count among them: the enums are generated from
// C++'s) with C++'s underlying size, and WindowId and
// GamepadId are laid out as C++'s (spec seam 7: each extension's layouts). The events and InputClaims are checked field
// by field, offsets included, whenever the fixture loads, so every test here also checks them.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "InputScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    // "Name=value" lines, as the fixture writes an enum's members, with each value from the C++ enumerator.
    template <typename Enum>
    std::string Members(std::initializer_list<std::pair<const char*, Enum>> members)
    {
        std::string text;
        for (const auto& [name, value] : members)
        {
            text +=
                (text.empty() ? "" : "\n") + std::string(name) + "=" + std::to_string(static_cast<long long>(value));
        }
        return text;
    }

// Every key FenrirInput names, in the order of the list in InputTypes.hpp, which is the order of their values.
// clang-format off
#define FENRIR_TEST_KEYS(KEY) \
    KEY(A) KEY(B) KEY(C) KEY(D) KEY(E) KEY(F) KEY(G) KEY(H) KEY(I) KEY(J) KEY(K) KEY(L) KEY(M) KEY(N) KEY(O) KEY(P) \
    KEY(Q) KEY(R) KEY(S) KEY(T) KEY(U) KEY(V) KEY(W) KEY(X) KEY(Y) KEY(Z) KEY(Num0) KEY(Num1) KEY(Num2) KEY(Num3) \
    KEY(Num4) KEY(Num5) KEY(Num6) KEY(Num7) KEY(Num8) KEY(Num9) KEY(Escape) KEY(Enter) KEY(Tab) KEY(Backspace) \
    KEY(Space) KEY(Minus) KEY(Equals) KEY(LeftBracket) KEY(RightBracket) KEY(Backslash) KEY(Semicolon) KEY(Apostrophe) \
    KEY(Grave) KEY(Comma) KEY(Period) KEY(Slash) KEY(CapsLock) KEY(F1) KEY(F2) KEY(F3) KEY(F4) KEY(F5) KEY(F6) KEY(F7) \
    KEY(F8) KEY(F9) KEY(F10) KEY(F11) KEY(F12) KEY(F13) KEY(F14) KEY(F15) KEY(F16) KEY(F17) KEY(F18) KEY(F19) KEY(F20) \
    KEY(F21) KEY(F22) KEY(F23) KEY(F24) KEY(PrintScreen) KEY(ScrollLock) KEY(Pause) KEY(Insert) KEY(Delete) KEY(Home) \
    KEY(End) KEY(PageUp) KEY(PageDown) KEY(Left) KEY(Right) KEY(Up) KEY(Down) KEY(NumLock) KEY(KeypadDivide) \
    KEY(KeypadMultiply) KEY(KeypadMinus) KEY(KeypadPlus) KEY(KeypadEnter) KEY(KeypadPeriod) KEY(Keypad0) KEY(Keypad1) \
    KEY(Keypad2) KEY(Keypad3) KEY(Keypad4) KEY(Keypad5) KEY(Keypad6) KEY(Keypad7) KEY(Keypad8) KEY(Keypad9) KEY(Menu) \
    KEY(LeftCtrl) KEY(LeftShift) KEY(LeftAlt) KEY(LeftSuper) KEY(RightCtrl) KEY(RightShift) KEY(RightAlt) \
    KEY(RightSuper)
    // clang-format on
#define FENRIR_TEST_KEY_MEMBER(name) {#name, Enum::name},

    // Key's or Scancode's members, which are the same keys.
    template <typename Enum>
    std::string KeyMembers()
    {
        return Members<Enum>({{"Unknown", Enum::Unknown}, FENRIR_TEST_KEYS(FENRIR_TEST_KEY_MEMBER)});
    }

    // `members` and then the enum's Count, which the C# enum names too.
    template <typename Enum>
    std::string WithCount(const std::string& members)
    {
        return members + "\nCount=" + std::to_string(static_cast<long long>(Enum::Count));
    }

#undef FENRIR_TEST_KEY_MEMBER
#undef FENRIR_TEST_KEYS

    std::string EnumMembers(InputScriptingApp& test, const char* name)
    {
        return test.Call<std::optional<std::string>>(ShapesType, "EnumMembers", std::string_view(name)).value_or("");
    }

    int EnumSize(InputScriptingApp& test, const char* name)
    {
        return test.Call<int>(ShapesType, "EnumSize", std::string_view(name));
    }
} // namespace

TEST_CASE("Each C# input enum names the values the C++ enum does, with its underlying size", "[input][scripting]")
{
    InputScriptingApp test;
    {
        const std::string keys = KeyMembers<Key>();
        INFO("The test's key list does not name every Key but Count");
        REQUIRE(static_cast<std::size_t>(std::count(keys.begin(), keys.end(), '\n')) + 1 == KeyCount);
        REQUIRE(ScancodeCount == KeyCount);
    }
    {
        INFO("C# Key differs from C++:\n" << EnumMembers(test, "Key"));
        REQUIRE(EnumMembers(test, "Key") == WithCount<Key>(KeyMembers<Key>()));
    }
    {
        INFO("C# Scancode differs from C++:\n" << EnumMembers(test, "Scancode"));
        REQUIRE(EnumMembers(test, "Scancode") == WithCount<Scancode>(KeyMembers<Scancode>()));
    }
    {
        INFO("C# KeyModifiers differs from C++:\n" << EnumMembers(test, "KeyModifiers"));
        REQUIRE(EnumMembers(test, "KeyModifiers") == Members<KeyModifiers>({{"None", KeyModifiers::None},
                                                                            {"Shift", KeyModifiers::Shift},
                                                                            {"Ctrl", KeyModifiers::Ctrl},
                                                                            {"Alt", KeyModifiers::Alt},
                                                                            {"Super", KeyModifiers::Super},
                                                                            {"CapsLock", KeyModifiers::CapsLock},
                                                                            {"NumLock", KeyModifiers::NumLock}}));
    }
    {
        INFO("C# MouseButton differs from C++:\n" << EnumMembers(test, "MouseButton"));
        REQUIRE(MouseButtonCount == 5);
        REQUIRE(EnumMembers(test, "MouseButton") ==
                WithCount<MouseButton>(Members<MouseButton>({{"Left", MouseButton::Left},
                                                             {"Right", MouseButton::Right},
                                                             {"Middle", MouseButton::Middle},
                                                             {"X1", MouseButton::X1},
                                                             {"X2", MouseButton::X2}})));
    }
    {
        INFO("C# GamepadButton differs from C++:\n" << EnumMembers(test, "GamepadButton"));
        REQUIRE(GamepadButtonCount == 21);
        REQUIRE(EnumMembers(test, "GamepadButton") ==
                WithCount<GamepadButton>(Members<GamepadButton>({{"South", GamepadButton::South},
                                                                 {"East", GamepadButton::East},
                                                                 {"West", GamepadButton::West},
                                                                 {"North", GamepadButton::North},
                                                                 {"Back", GamepadButton::Back},
                                                                 {"Guide", GamepadButton::Guide},
                                                                 {"Start", GamepadButton::Start},
                                                                 {"LeftStick", GamepadButton::LeftStick},
                                                                 {"RightStick", GamepadButton::RightStick},
                                                                 {"LeftShoulder", GamepadButton::LeftShoulder},
                                                                 {"RightShoulder", GamepadButton::RightShoulder},
                                                                 {"DPadUp", GamepadButton::DPadUp},
                                                                 {"DPadDown", GamepadButton::DPadDown},
                                                                 {"DPadLeft", GamepadButton::DPadLeft},
                                                                 {"DPadRight", GamepadButton::DPadRight},
                                                                 {"Misc1", GamepadButton::Misc1},
                                                                 {"RightPaddle1", GamepadButton::RightPaddle1},
                                                                 {"LeftPaddle1", GamepadButton::LeftPaddle1},
                                                                 {"RightPaddle2", GamepadButton::RightPaddle2},
                                                                 {"LeftPaddle2", GamepadButton::LeftPaddle2},
                                                                 {"Touchpad", GamepadButton::Touchpad}})));
    }
    {
        INFO("C# GamepadAxis differs from C++:\n" << EnumMembers(test, "GamepadAxis"));
        REQUIRE(GamepadAxisCount == 6);
        REQUIRE(EnumMembers(test, "GamepadAxis") ==
                WithCount<GamepadAxis>(Members<GamepadAxis>({{"LeftX", GamepadAxis::LeftX},
                                                             {"LeftY", GamepadAxis::LeftY},
                                                             {"RightX", GamepadAxis::RightX},
                                                             {"RightY", GamepadAxis::RightY},
                                                             {"LeftTrigger", GamepadAxis::LeftTrigger},
                                                             {"RightTrigger", GamepadAxis::RightTrigger}})));
    }
    INFO("A C# enum's underlying type does not have the C++ enum's size");
    REQUIRE(EnumSize(test, "Key") == static_cast<int>(sizeof(Key)));
    REQUIRE(EnumSize(test, "Scancode") == static_cast<int>(sizeof(Scancode)));
    REQUIRE(EnumSize(test, "KeyModifiers") == static_cast<int>(sizeof(KeyModifiers)));
    REQUIRE(EnumSize(test, "MouseButton") == static_cast<int>(sizeof(MouseButton)));
    REQUIRE(EnumSize(test, "GamepadButton") == static_cast<int>(sizeof(GamepadButton)));
    REQUIRE(EnumSize(test, "GamepadAxis") == static_cast<int>(sizeof(GamepadAxis)));
}

TEST_CASE("The C# WindowId and GamepadId have the C++ layout: one 32-bit value", "[input][scripting]")
{
    InputScriptingApp test;
    INFO("C# WindowId or GamepadId is not the size of C++'s, or a WindowId's bytes are not its value");
    REQUIRE(test.Call<int>(ShapesType, "WindowIdSize") == static_cast<int>(sizeof(WindowId)));
    REQUIRE(test.Call<int>(ShapesType, "GamepadIdSize") == static_cast<int>(sizeof(GamepadId)));
    REQUIRE(test.Call<std::uint32_t>(ShapesType, "WindowIdBytes", 0xA1B2C3D4u) == 0xA1B2C3D4u);
}
