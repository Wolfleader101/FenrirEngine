// Input::Install: works with no window, can be shared, and ends with its last owner (ADR-0017).

#include <memory>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "InputTestApp.hpp"

using Fenrir::Key;
using Fenrir::Scancode;
using namespace InputTests;

TEST_CASE("Installing input with no window adds the input resources", "[input][install]")
{
    Fenrir::App app(std::make_unique<SilentLogger>(), 1);
    Fenrir::Input input = Fenrir::Input::Install(app);

    INFO("the owner Install returns is valid");
    REQUIRE(input.IsValid());
    INFO("the app has the keyboard, mouse, gamepads, input feed and input claims");
    REQUIRE(app.TryGetResource<Fenrir::Keyboard>() != nullptr);
    REQUIRE(app.TryGetResource<Fenrir::Mouse>() != nullptr);
    REQUIRE(app.TryGetResource<Fenrir::Gamepads>() != nullptr);
    REQUIRE(app.TryGetResource<Fenrir::InputFeed>() != nullptr);
    REQUIRE(app.TryGetResource<Fenrir::InputClaims>() != nullptr);
}

TEST_CASE("A second Install shares the input the app already has", "[input][install]")
{
    Fenrir::App app(std::make_unique<SilentLogger>(), 1);
    std::optional<Fenrir::Input> game = Fenrir::Input::Install(app);
    Fenrir::Input window = Fenrir::Input::Install(app);

    app.GetResource<Fenrir::InputFeed>().KeyDown(mainWindow, Key::Space, Scancode::Space);
    app.RunFrame(oneTick);

    Fenrir::EventReader<Fenrir::KeyEvent> reader;
    INFO("a key fed once is dispatched once, not once per Install");
    REQUIRE(reader.Read(app).size() == 1);

    game.reset();
    INFO("the input stays while the second owner lives");
    REQUIRE(app.TryGetResource<Fenrir::Keyboard>() != nullptr);
    REQUIRE(app.GetResource<Fenrir::Keyboard>().IsDown(Key::Space));
}

TEST_CASE("The input resources go with the last owner", "[input][install]")
{
    Fenrir::App app(std::make_unique<SilentLogger>(), 1);
    std::optional<Fenrir::Input> input = Fenrir::Input::Install(app);
    Fenrir::Input moved = std::move(*input);

    INFO("a moved-from owner is not valid, and the one moved to is");
    REQUIRE_FALSE(input->IsValid());
    REQUIRE(moved.IsValid());

    input.reset();
    INFO("destroying the moved-from owner leaves the input in place");
    REQUIRE(app.TryGetResource<Fenrir::Keyboard>() != nullptr);

    {
        Fenrir::Input last = std::move(moved);
    }
    INFO("once the last owner is gone, the app has no input resources");
    REQUIRE(app.TryGetResource<Fenrir::Keyboard>() == nullptr);
    REQUIRE(app.TryGetResource<Fenrir::InputFeed>() == nullptr);
    REQUIRE(app.TryGetResource<Fenrir::InputClaims>() == nullptr);

    app.RunFrame(oneTick);
    Fenrir::Input again = Fenrir::Input::Install(app);
    app.GetResource<Fenrir::InputFeed>().KeyDown(mainWindow, Key::Space, Scancode::Space);
    app.RunFrame(oneTick);
    Fenrir::EventReader<Fenrir::KeyEvent> reader;
    INFO("input installed again works, and the first dispatch, now ended, does not dispatch a second time");
    REQUIRE(reader.Read(app).size() == 1);
}
