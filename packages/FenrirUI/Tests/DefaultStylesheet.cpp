// The default stylesheet (spec: "FenrirUI" default stylesheet, seam 1): embedded in the package, under every root's
// own sheets, it gives every control a look with no sheet of the game's, and a game's sheet overrides it.

#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "FenrirUI/Stylesheet.hpp"
#include "FenrirUI/UI.hpp"
#include "UITestApp.hpp"

using Fenrir::ElementId;
using Fenrir::UIRoot;
using namespace UITest;

namespace
{
    // The tags the default stylesheet styles with a background and the focusable ones among them.
    constexpr std::string_view backgrounded[] = {"button", "checkbox", "slider", "progress", "scroll"};
    constexpr std::string_view focusable[] = {"button", "checkbox", "slider", "scroll"};

    bool Same(const Fenrir::Colour& a, const Fenrir::Colour& b)
    {
        return Near(a.r, b.r) && Near(a.g, b.g) && Near(a.b, b.b) && Near(a.a, b.a);
    }

    bool IsBlackText(const Fenrir::Colour& colour)
    {
        return colour.r == 0.0f && colour.g == 0.0f && colour.b == 0.0f && colour.a == 1.0f;
    }
} // namespace

TEST_CASE("The default stylesheet is embedded and every declaration in it is one FenrirUI reads", "[ui][default]")
{
    const std::string_view text = Fenrir::DefaultStylesheet();
    REQUIRE_FALSE(text.empty());
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse(text, "default.css");
    INFO("the parser refused nothing");
    REQUIRE(sheet.diagnostics.empty());
    REQUIRE_FALSE(sheet.rules.empty());

    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId probe = root.Create(root.GetRootElement(), "div");
    for (const Fenrir::StyleRule& rule : sheet.rules)
    {
        for (const Fenrir::StyleDeclaration& declaration : rule.declarations)
        {
            INFO(declaration.name << ": " << declaration.value << " (line " << declaration.line << ")");
            REQUIRE(root.SetStyleProperty(probe, declaration.name, declaration.value));
        }
    }
}

TEST_CASE("Every control resolves a background, a text colour and a focus ring from the default stylesheet",
          "[ui][default]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    for (const std::string_view tag : backgrounded)
    {
        const ElementId control = root.Create(root.GetRootElement(), tag);
        test.Frame();
        const Fenrir::ResolvedStyle& style = *root.GetResolvedStyle(control);
        INFO(tag);
        REQUIRE(style.backgroundColor.a > 0.0f);
        INFO("the text colour is the sheet's, not the style's black default");
        REQUIRE_FALSE(IsBlackText(style.color));
        REQUIRE(style.fontFamily == "sans-serif");
        REQUIRE(style.pointerEvents == (tag == "progress" ? Fenrir::PointerEvents::None : Fenrir::PointerEvents::All));
    }
    for (const std::string_view tag : focusable)
    {
        const ElementId control = root.Create(root.GetRootElement(), tag);
        test.Frame();
        INFO(tag);
        REQUIRE(root.GetResolvedStyle(control)->boxShadow.empty());
        REQUIRE(root.Focus(control));
        test.Frame();
        INFO("focused, it has a ring");
        REQUIRE_FALSE(root.GetResolvedStyle(control)->boxShadow.empty());
    }
}

TEST_CASE("The default stylesheet's states change a control's look", "[ui][default]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId button = root.Create(root.GetRootElement(), "button");
    const ElementId checkbox = root.Create(root.GetRootElement(), "checkbox");
    test.Frame();
    const Fenrir::Colour plain = root.GetResolvedStyle(button)->backgroundColor;
    const Fenrir::Colour unchecked = root.GetResolvedStyle(checkbox)->backgroundColor;

    for (const Fenrir::PseudoClass state :
         {Fenrir::PseudoClass::Hover, Fenrir::PseudoClass::Active, Fenrir::PseudoClass::Disabled})
    {
        root.SetPseudoClass(button, state, true);
        test.Frame();
        INFO(static_cast<int>(state));
        REQUIRE_FALSE(Same(root.GetResolvedStyle(button)->backgroundColor, plain));
        root.SetPseudoClass(button, state, false);
    }
    root.SetPseudoClass(checkbox, Fenrir::PseudoClass::Checked, true);
    test.Frame();
    REQUIRE_FALSE(Same(root.GetResolvedStyle(checkbox)->backgroundColor, unchecked));
}

TEST_CASE("A game's sheet overrides the default stylesheet by specificity, and by order at equal specificity",
          "[ui][default]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {400.0f, 300.0f});
    const ElementId button = root.Create(root.GetRootElement(), "button");
    root.AddClass(button, "danger");
    root.AddStylesheet("button { background-color: #00ff00; } .danger { color: #ff0000; }", "game.css");
    test.Frame();
    INFO("the game's button rule comes after the default's of equal specificity");
    REQUIRE(Same(root.GetResolvedStyle(button)->backgroundColor, Fenrir::Colour::Green));
    INFO("the game's class rule is more specific than the default's inherited colour");
    REQUIRE(Same(root.GetResolvedStyle(button)->color, Fenrir::Colour::Red));
    INFO("what the game's sheet leaves alone stays the default's");
    REQUIRE_FALSE(root.GetResolvedStyle(button)->borderTopWidth == 0.0f);

    root.SetPseudoClass(button, Fenrir::PseudoClass::Hover, true);
    test.Frame();
    INFO("hovered, the default's more specific button:hover beats the game's plain rule");
    REQUIRE_FALSE(Same(root.GetResolvedStyle(button)->backgroundColor, Fenrir::Colour::Green));

    root.AddStylesheet("button:hover { background-color: #0000ff; }", "hover.css");
    test.Frame();
    INFO("and the game's own button:hover beats the default's by coming later");
    REQUIRE(Same(root.GetResolvedStyle(button)->backgroundColor, Fenrir::Colour::Blue));
}

TEST_CASE("A root made without the default stylesheet gives its controls no look", "[ui][default]")
{
    UITestApp test;
    auto target = std::make_shared<CaptureTarget>(Fenrir::UITargetSize{{400.0f, 300.0f}, 1.0f});
    Fenrir::UIRootConfig config;
    config.scaleRule = Fenrir::ScaleRule::ContentScale;
    config.target = target;
    config.defaultStylesheet = false;
    UIRoot& root = test.Roots().CreateRoot(config);
    const ElementId button = root.Create(root.GetRootElement(), "button");
    test.Frame();
    REQUIRE(root.GetResolvedStyle(button)->backgroundColor.a == 0.0f);
    INFO("it still behaves as a button");
    REQUIRE(root.IsFocusable(button));
}
