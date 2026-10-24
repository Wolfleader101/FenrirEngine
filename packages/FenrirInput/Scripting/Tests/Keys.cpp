// The keyboard from C#: fed keys read through Keyboard by the reading phase's edges, as C++ reads them, and key events
// read once through a C# EventReader (spec seam 7).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "InputScriptingApp.hpp"

using namespace Fenrir;
using namespace InputScriptingTests;

namespace
{
    // A reading of the space bar as the fixture writes it: "<phase> <just pressed><held><just released>".
    std::string Reading(const char* phase, const Keyboard& keyboard)
    {
        const auto bit = [](bool value) { return value ? '1' : '0'; };
        std::string reading = std::string(phase) + " ";
        reading += bit(keyboard.JustPressed(Key::Space));
        reading += bit(keyboard.IsDown(Key::Space));
        reading += bit(keyboard.JustReleased(Key::Space));
        return reading;
    }
} // namespace

TEST_CASE("A fed key reads as just pressed in C# by the phase's edges, as in C++, and arrives once as an event",
          "[input][scripting]")
{
    InputScriptingApp test;
    // C++ systems in the same phases as the fixture's, reading the same key.
    auto cppReadings = std::make_shared<std::vector<std::string>>();
    test.Get().AddSequentialSystem(SchedulePriority::Tick, [cppReadings](App& app) {
        cppReadings->push_back(Reading("Tick", app.GetResource<Keyboard>()));
    });
    test.Get().AddSequentialSystem(SchedulePriority::Update, [cppReadings](App& app) {
        cppReadings->push_back(Reading("Update", app.GetResource<Keyboard>()));
    });
    test.Get().RunFrame(oneTick);
    test.Call<void>(KeysType, "Clear");
    cppReadings->clear();

    // Pressed before a frame that runs no tick, which reads it in Update; the tick at the next frame reads it there.
    test.Feed().KeyDown(mainWindow, Key::Space, Scancode::Space, KeyModifiers::Shift);
    test.Get().RunFrame(noTick);
    test.Get().RunFrame(oneTick);
    // Released before a frame of two ticks: the first tick reads the release, the second does not.
    test.Feed().KeyUp(mainWindow, Key::Space, Scancode::Space);
    test.Get().RunFrame(twoTicks);

    const std::string expected = "Update 110\n"
                                 "Tick 110\n"
                                 "Update 010\n"
                                 "Tick 001\n"
                                 "Tick 000\n"
                                 "Update 001";
    const auto csharp = test.Call<std::optional<std::string>>(KeysType, "Log").value_or("");
    {
        INFO("C# read the space bar per phase as:\n" << csharp);
        REQUIRE(csharp == expected);
    }
    std::string cpp;
    for (const std::string& reading : *cppReadings)
    {
        cpp += (cpp.empty() ? "" : "\n") + reading;
    }
    {
        INFO("C++ read it, in the same phases, as:\n" << cpp);
        REQUIRE(cpp == csharp);
    }

    {
        INFO("The C# EventReader did not read the press and the release once each");
        REQUIRE(test.Call<int>(KeysType, "EventCount") == 2);
    }
    INFO("The first KeyEvent C# read is not the space bar's press in the main window with Shift");
    REQUIRE(test.Call<std::uint32_t>(KeysType, "EventWindow", 0) == mainWindow.value);
    REQUIRE(test.Call<int>(KeysType, "EventKey", 0) == static_cast<int>(Key::Space));
    REQUIRE(test.Call<int>(KeysType, "EventScancode", 0) == static_cast<int>(Scancode::Space));
    REQUIRE(test.Call<int>(KeysType, "EventModifiers", 0) == static_cast<int>(KeyModifiers::Shift));
    REQUIRE(test.Call<bool>(KeysType, "EventPressed", 0));
    REQUIRE_FALSE(test.Call<bool>(KeysType, "EventRepeat", 0));
    INFO("The second is not its release");
    REQUIRE_FALSE(test.Call<bool>(KeysType, "EventPressed", 1));
}

TEST_CASE("C# reads keys by scancode apart from key codes, and the latest modifiers, as C++ does", "[input][scripting]")
{
    InputScriptingApp test;
    // On an AZERTY keyboard the key at QWERTY's W position types Z.
    test.Feed().KeyDown(mainWindow, Key::Z, Scancode::W, KeyModifiers::Ctrl | KeyModifiers::Alt);
    test.Get().RunFrame(oneTick);

    const Keyboard& keyboard = test.Get().GetResource<Keyboard>();
    const int w = static_cast<int>(Scancode::W);
    {
        INFO("C# reads the scancode W held and just pressed, as C++ does");
        REQUIRE(keyboard.JustPressed(Scancode::W));
        REQUIRE(test.Call<bool>(KeysType, "ScancodeIsDown", w));
        REQUIRE(test.Call<bool>(KeysType, "ScancodeJustPressed", w));
    }
    {
        INFO("C# reads the key code Z held and the key code W not, as C++ does");
        REQUIRE(test.Call<bool>(KeysType, "KeyIsDown", static_cast<int>(Key::Z)));
        REQUIRE_FALSE(keyboard.IsDown(Key::W));
        REQUIRE_FALSE(test.Call<bool>(KeysType, "KeyIsDown", static_cast<int>(Key::W)));
    }
    {
        INFO("C# reads Ctrl and Alt as the modifiers, as C++ does");
        REQUIRE(test.Call<int>(KeysType, "Modifiers") == static_cast<int>(keyboard.Modifiers()));
        REQUIRE(test.Call<int>(KeysType, "Modifiers") == static_cast<int>(KeyModifiers::Ctrl | KeyModifiers::Alt));
    }

    test.Feed().KeyUp(mainWindow, Key::Z, Scancode::W);
    test.Get().RunFrame(oneTick);
    INFO("C# reads the scancode's release the next frame, as C++ does");
    REQUIRE(keyboard.JustReleased(Scancode::W));
    REQUIRE(test.Call<bool>(KeysType, "ScancodeJustReleased", w));
    REQUIRE_FALSE(test.Call<bool>(KeysType, "ScancodeIsDown", w));
}
