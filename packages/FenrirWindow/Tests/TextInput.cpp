// SDL text input follows the input routers' textInput claims per window, with the claimant's caret, and otherwise
// WindowConfig::textInputByDefault (ADR-0027; spec: "FenrirWindow").

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "FenrirInput/Input.hpp"
#include "WindowProbe.hpp"
#include "WindowTestApp.hpp"

using Fenrir::Input;
using Fenrir::Window;
using namespace WindowTests;

namespace
{
    // A router that consumes nothing: it is there to publish claims.
    class ClaimingRouter : public Fenrir::InputRouter
    {
      public:
        Fenrir::RouteResult Route(Fenrir::App&, const Fenrir::RoutedInput&) override
        {
            return Fenrir::RouteResult::Passed;
        }
    };
} // namespace

TEST_CASE("Text input turns on for a textInput claim, with its caret, and off when the claim clears",
          "[window][input][display]")
{
    Fenrir::WindowConfig config = HiddenWindow();
    config.textInputByDefault = false;
    WindowTestApp test(config);
    test.RequireDisplay();
    const Fenrir::InputRouterId ui = Input::AddInputRouter(test.app, 0, std::make_shared<ClaimingRouter>());
    test.app.RunFrame(oneTick);
    INFO("with textInputByDefault off and no claim, the main window's text input is off");
    REQUIRE_FALSE(Fenrir::WindowProbe::IsTextInputOn(test.app, Window::MainWindow()));

    Fenrir::WindowInputClaims claims;
    claims.textInput = true;
    claims.caret = {{40.0f, 30.0f}, {2.0f, 16.0f}};
    Input::SetInputClaims(test.app, ui, Window::MainWindow(), claims);
    test.app.RunFrame(oneTick);
    INFO("a router claiming textInput turns it on in the next frame's pump");
    REQUIRE(Fenrir::WindowProbe::IsTextInputOn(test.app, Window::MainWindow()));
    INFO("with the claim's caret rectangle as SDL's text input area");
    REQUIRE(Fenrir::WindowProbe::TextInputArea(test.app, Window::MainWindow()) ==
            Fenrir::CaretRect{{40.0f, 30.0f}, {2.0f, 16.0f}});

    Input::SetInputClaims(test.app, ui, Window::MainWindow(), Fenrir::WindowInputClaims{});
    test.app.RunFrame(oneTick);
    INFO("when the claim clears, text input goes back off");
    REQUIRE_FALSE(Fenrir::WindowProbe::IsTextInputOn(test.app, Window::MainWindow()));
}

TEST_CASE("With no claim, text input follows textInputByDefault", "[window][input][display]")
{
    WindowTestApp test;
    test.RequireDisplay();
    test.app.RunFrame(oneTick);
    test.app.RunFrame(oneTick);

    INFO("textInputByDefault is true by default, so text input is on with no router at all");
    REQUIRE(Fenrir::WindowProbe::IsTextInputOn(test.app, Window::MainWindow()));
}
