// The stylesheet parser (ADR-0064): selectors, shorthands, nesting, at-rules and what it refuses, each with its line.
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirUI/Stylesheet.hpp"
#include "FenrirUI/UIRoot.hpp"

namespace
{
    using Declarations = std::vector<std::pair<std::string, std::string>>;

    Declarations NamesAndValues(const Fenrir::StyleRule& rule)
    {
        Declarations out;
        for (const Fenrir::StyleDeclaration& declaration : rule.declarations)
        {
            out.emplace_back(declaration.name, declaration.value);
        }
        return out;
    }

    // The declarations one rule `selector { declaration }` parses to.
    Declarations Expand(std::string_view declaration)
    {
        const Fenrir::Stylesheet sheet =
            Fenrir::Stylesheet::Parse("panel { " + std::string(declaration) + " }", "test");
        INFO("the declaration parses with no diagnostic: " << declaration);
        REQUIRE(sheet.diagnostics.empty());
        REQUIRE(sheet.rules.size() == 1);
        return NamesAndValues(sheet.rules[0]);
    }

    std::vector<std::string> SelectorTexts(const Fenrir::Stylesheet& sheet)
    {
        std::vector<std::string> out;
        for (const Fenrir::StyleRule& rule : sheet.rules)
        {
            for (const Fenrir::Selector& selector : rule.selectors)
            {
                out.push_back(selector.ToString());
            }
        }
        return out;
    }

    bool HasDiagnosticOnLine(const Fenrir::Stylesheet& sheet, std::uint32_t line, std::string_view mentions)
    {
        return std::any_of(sheet.diagnostics.begin(), sheet.diagnostics.end(), [&](const Fenrir::StyleDiagnostic& d) {
            return d.line == line && d.message.find(mentions) != std::string::npos;
        });
    }

    constexpr std::uint32_t Specificity(std::uint32_t ids, std::uint32_t classes, std::uint32_t tags)
    {
        return (ids << 20u) | (classes << 10u) | tags;
    }
} // namespace

TEST_CASE("Type, class, id and compound selectors parse with CSS's specificity", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse("panel {} .card {} #title {} button.primary#ok {}", "t");
    REQUIRE(sheet.diagnostics.empty());
    REQUIRE(sheet.rules.size() == 4);

    const Fenrir::CompoundSelector& type = sheet.rules[0].selectors[0].compounds[0];
    INFO("a type selector names the tag");
    REQUIRE(type.tag == "panel");
    REQUIRE(sheet.rules[0].selectors[0].specificity == Specificity(0, 0, 1));

    INFO("a class selector names a class and counts as a class");
    REQUIRE(sheet.rules[1].selectors[0].compounds[0].classes == std::vector<std::string>{"card"});
    REQUIRE(sheet.rules[1].selectors[0].specificity == Specificity(0, 1, 0));

    INFO("an id selector names the id and counts as an id");
    REQUIRE(sheet.rules[2].selectors[0].compounds[0].id == "title");
    REQUIRE(sheet.rules[2].selectors[0].specificity == Specificity(1, 0, 0));

    const Fenrir::CompoundSelector& compound = sheet.rules[3].selectors[0].compounds[0];
    INFO("a compound selector holds all three, and adds their weights");
    REQUIRE(compound.tag == "button");
    REQUIRE(compound.classes == std::vector<std::string>{"primary"});
    REQUIRE(compound.id == "ok");
    REQUIRE(sheet.rules[3].selectors[0].specificity == Specificity(1, 1, 1));
}

TEST_CASE("Descendant and child combinators and selector lists parse", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse(".menu   .item>label, #hud > .bar {}", "t");
    REQUIRE(sheet.diagnostics.empty());
    REQUIRE(sheet.rules.size() == 1);
    REQUIRE(sheet.rules[0].selectors.size() == 2);

    const Fenrir::Selector& first = sheet.rules[0].selectors[0];
    INFO("whitespace is a descendant combinator and > a child one, with or without spaces");
    REQUIRE(first.compounds.size() == 3);
    REQUIRE(first.combinators ==
            std::vector{Fenrir::SelectorCombinator::Descendant, Fenrir::SelectorCombinator::Child});
    REQUIRE(first.compounds[2].tag == "label");
    REQUIRE(first.specificity == Specificity(0, 2, 1));
    INFO("each selector of a list is its own, in canonical form");
    REQUIRE(SelectorTexts(sheet) == std::vector<std::string>{".menu .item > label", "#hud > .bar"});
}

TEST_CASE("Every pseudo-class parses, and counts as a class", "[ui][stylesheet]")
{
    const std::vector<std::pair<std::string, Fenrir::PseudoClass>> flags{
        {"hover", Fenrir::PseudoClass::Hover},       {"active", Fenrir::PseudoClass::Active},
        {"focus", Fenrir::PseudoClass::Focus},       {"focus-within", Fenrir::PseudoClass::FocusWithin},
        {"disabled", Fenrir::PseudoClass::Disabled}, {"checked", Fenrir::PseudoClass::Checked},
        {"intro", Fenrir::PseudoClass::Intro},       {"outro", Fenrir::PseudoClass::Outro},
    };
    for (const auto& [name, flag] : flags)
    {
        const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse("button:" + name + " {}", "t");
        INFO("the pseudo-class :" << name << " parses to its flag");
        REQUIRE(sheet.diagnostics.empty());
        REQUIRE(sheet.rules.size() == 1);
        const Fenrir::Selector& selector = sheet.rules[0].selectors[0];
        REQUIRE(selector.compounds[0].pseudoClasses == static_cast<std::uint16_t>(flag));
        REQUIRE(selector.specificity == Specificity(0, 1, 1));
    }

    const std::vector<std::pair<std::string, Fenrir::StructuralPseudoClass>> structural{
        {"first-child", Fenrir::StructuralPseudoClass::FirstChild},
        {"last-child", Fenrir::StructuralPseudoClass::LastChild},
        {"root", Fenrir::StructuralPseudoClass::Root},
    };
    for (const auto& [name, flag] : structural)
    {
        const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse(":" + name + " {}", "t");
        INFO("the structural pseudo-class :" << name << " parses, with no tag");
        REQUIRE(sheet.diagnostics.empty());
        REQUIRE(sheet.rules.size() == 1);
        const Fenrir::Selector& selector = sheet.rules[0].selectors[0];
        REQUIRE(selector.compounds[0].structural == static_cast<std::uint8_t>(flag));
        REQUIRE(selector.compounds[0].tag.empty());
        REQUIRE(selector.specificity == Specificity(0, 1, 0));
    }

    const Fenrir::Stylesheet combined = Fenrir::Stylesheet::Parse("button:hover:focus {}", "t");
    INFO("pseudo-classes combine, and each counts");
    REQUIRE(combined.rules[0].selectors[0].compounds[0].pseudoClasses ==
            (static_cast<std::uint16_t>(Fenrir::PseudoClass::Hover) |
             static_cast<std::uint16_t>(Fenrir::PseudoClass::Focus)));
    REQUIRE(combined.rules[0].selectors[0].specificity == Specificity(0, 2, 1));
}

TEST_CASE("Margin, padding, gap and the border lists expand from one to four values as CSS does", "[ui][stylesheet]")
{
    INFO("one value is every side");
    REQUIRE(
        Expand("margin: 4px;") ==
        Declarations{{"margin-top", "4px"}, {"margin-right", "4px"}, {"margin-bottom", "4px"}, {"margin-left", "4px"}});
    INFO("two are vertical then horizontal");
    REQUIRE(Expand("padding: 1px 2px;") ==
            Declarations{
                {"padding-top", "1px"}, {"padding-right", "2px"}, {"padding-bottom", "1px"}, {"padding-left", "2px"}});
    INFO("three are top, horizontal, bottom");
    REQUIRE(Expand("margin: 1px auto 3%;") ==
            Declarations{
                {"margin-top", "1px"}, {"margin-right", "auto"}, {"margin-bottom", "3%"}, {"margin-left", "auto"}});
    INFO("four go round from the top");
    REQUIRE(Expand("border-width: 1px 2px 3px 4px;") == Declarations{{"border-top-width", "1px"},
                                                                     {"border-right-width", "2px"},
                                                                     {"border-bottom-width", "3px"},
                                                                     {"border-left-width", "4px"}});
    REQUIRE(Expand("border-color: red blue;") == Declarations{{"border-top-color", "red"},
                                                              {"border-right-color", "blue"},
                                                              {"border-bottom-color", "red"},
                                                              {"border-left-color", "blue"}});
    INFO("gap is the row gap then the column gap");
    REQUIRE(Expand("gap: 5px;") == Declarations{{"row-gap", "5px"}, {"column-gap", "5px"}});
    REQUIRE(Expand("gap: 5px 10%;") == Declarations{{"row-gap", "5px"}, {"column-gap", "10%"}});
}

TEST_CASE("Border-radius expands from the top-left corner round", "[ui][stylesheet]")
{
    INFO("two values are the top-left and bottom-right, then the other two");
    REQUIRE(Expand("border-radius: 4px 8px;") == Declarations{{"border-top-left-radius", "4px"},
                                                              {"border-top-right-radius", "8px"},
                                                              {"border-bottom-right-radius", "4px"},
                                                              {"border-bottom-left-radius", "8px"}});
    INFO("three are top-left, the top-right and bottom-left, bottom-right");
    REQUIRE(Expand("border-radius: 1px 2px 3px;") == Declarations{{"border-top-left-radius", "1px"},
                                                                  {"border-top-right-radius", "2px"},
                                                                  {"border-bottom-right-radius", "3px"},
                                                                  {"border-bottom-left-radius", "2px"}});
}

TEST_CASE("Border sets every side's width and colour, and no style is no border", "[ui][stylesheet]")
{
    INFO("a width, a style and a colour set all four sides");
    REQUIRE(Expand("border: 2px solid #ff0000;") == Declarations{{"border-top-width", "2px"},
                                                                 {"border-right-width", "2px"},
                                                                 {"border-bottom-width", "2px"},
                                                                 {"border-left-width", "2px"},
                                                                 {"border-top-color", "#ff0000"},
                                                                 {"border-right-color", "#ff0000"},
                                                                 {"border-bottom-color", "#ff0000"},
                                                                 {"border-left-color", "#ff0000"}});
    INFO("a style with no width is CSS's medium, 3px, and no colour is the initial one");
    const Declarations styleOnly = Expand("border: dashed;");
    REQUIRE(styleOnly[0] == std::pair<std::string, std::string>{"border-top-width", "3px"});
    REQUIRE(styleOnly[4] == std::pair<std::string, std::string>{"border-top-color", "black"});
    INFO("with the style none, or none given, the widths are zero, as CSS computes them");
    REQUIRE(Expand("border: none;")[0] == std::pair<std::string, std::string>{"border-top-width", "0"});
    REQUIRE(Expand("border: 4px red;")[0] == std::pair<std::string, std::string>{"border-top-width", "0"});
}

TEST_CASE("Background sets its colour, image, position and size, and resets those it does not name", "[ui][stylesheet]")
{
    INFO("a colour alone resets the image, position and size");
    REQUIRE(Expand("background: red;") == Declarations{{"background-color", "red"},
                                                       {"background-image", "none"},
                                                       {"background-position", "0% 0%"},
                                                       {"background-size", "stretch"}});
    INFO("an image, a position and a size after a slash, and a colour in any order");
    REQUIRE(Expand("background: url(ui/panel.png) center / cover #102030;") ==
            Declarations{{"background-color", "#102030"},
                         {"background-image", "url(ui/panel.png)"},
                         {"background-position", "center"},
                         {"background-size", "cover"}});
    INFO("a gradient is an image, its commas inside its parentheses");
    REQUIRE(Expand("background: linear-gradient(90deg, red, blue);")[1] ==
            std::pair<std::string, std::string>{"background-image", "linear-gradient(90deg, red, blue)"});
}

TEST_CASE("Flex expands its keywords and number forms as CSS does", "[ui][stylesheet]")
{
    const auto flex = [](std::string grow, std::string shrink, std::string basis) {
        return Declarations{
            {"flex-grow", std::move(grow)}, {"flex-shrink", std::move(shrink)}, {"flex-basis", std::move(basis)}};
    };
    INFO("flex: 1 grows and shrinks from a zero basis");
    REQUIRE(Expand("flex: 1;") == flex("1", "1", "0%"));
    INFO("two numbers are grow and shrink");
    REQUIRE(Expand("flex: 2 3;") == flex("2", "3", "0%"));
    INFO("a length alone is the basis, growing and shrinking by one");
    REQUIRE(Expand("flex: 40px;") == flex("1", "1", "40px"));
    INFO("all three");
    REQUIRE(Expand("flex: 2 0 25%;") == flex("2", "0", "25%"));
    INFO("the keywords");
    REQUIRE(Expand("flex: none;") == flex("0", "0", "auto"));
    REQUIRE(Expand("flex: auto;") == flex("1", "1", "auto"));
    REQUIRE(Expand("flex: initial;") == flex("0", "1", "auto"));
}

TEST_CASE("Transition expands a list into the four transition longhands", "[ui][stylesheet]")
{
    INFO("the first time is the duration and the second the delay; what is left out takes its initial value");
    REQUIRE(Expand("transition: opacity 0.2s ease-in 50ms, width 1s;") ==
            Declarations{{"transition-property", "opacity, width"},
                         {"transition-duration", "0.2s, 1s"},
                         {"transition-timing-function", "ease-in, ease"},
                         {"transition-delay", "50ms, 0s"}});
    INFO("cubic-bezier() and steps() are timing functions, and no property is all");
    REQUIRE(Expand("transition: 1s cubic-bezier(0.1, 0.7, 1, 0.1);") ==
            Declarations{{"transition-property", "all"},
                         {"transition-duration", "1s"},
                         {"transition-timing-function", "cubic-bezier(0.1, 0.7, 1, 0.1)"},
                         {"transition-delay", "0s"}});
    REQUIRE(Expand("transition: background-color 2s steps(4, jump-end);")[2] ==
            std::pair<std::string, std::string>{"transition-timing-function", "steps(4, jump-end)"});
}

TEST_CASE("Animation expands into the seven animation longhands", "[ui][stylesheet]")
{
    INFO("keywords find their longhand in any order, and the name is the identifier left over");
    REQUIRE(Expand("animation: spin 2s linear 1s infinite alternate forwards;") ==
            Declarations{{"animation-name", "spin"},
                         {"animation-duration", "2s"},
                         {"animation-timing-function", "linear"},
                         {"animation-delay", "1s"},
                         {"animation-iteration-count", "infinite"},
                         {"animation-direction", "alternate"},
                         {"animation-fill-mode", "forwards"}});
    INFO("what is left out takes its initial value");
    REQUIRE(Expand("animation: 3 pulse 500ms;") == Declarations{{"animation-name", "pulse"},
                                                                {"animation-duration", "500ms"},
                                                                {"animation-timing-function", "ease"},
                                                                {"animation-delay", "0s"},
                                                                {"animation-iteration-count", "3"},
                                                                {"animation-direction", "normal"},
                                                                {"animation-fill-mode", "none"}});
}

TEST_CASE("Every kind of value the property set takes parses with no diagnostic", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse(R"(
        panel {
            width: 50%; height: 120px; min-width: 0; max-height: none;
            background-color: #abc; border-top-color: #aabbccdd; border-left-color: rgb(10, 20, 30);
            border-right-color: rgba(10, 20, 30, 0.5); border-bottom-color: hsl(120, 50%, 50%);
            background-image: url(ui/frame.png);
            border-image-source: url("ui/nine.png");
            box-shadow: 0 2px 4px rgba(0, 0, 0, 0.5), 1px 1px 0 red;
            transform: translate(10px, 5%) rotate(45deg) scale(2);
            transform-origin: left top;
            color: cornflowerblue; font-size: 18px; text-shadow: 1px 1px 2px black;
            filter: blur(4px); backdrop-filter: blur(8px); background-position: center;
            scrollbar-color: red blue; scrollbar-width: 6px;
            transition-duration: 1s; animation-name: spin;
        }
        .a { background-image: radial-gradient(red, blue); }
        .b { background-image: linear-gradient(to right, red 0%, blue 100%); }
    )",
                                                               "values");
    for (const Fenrir::StyleDiagnostic& diagnostic : sheet.diagnostics)
    {
        UNSCOPED_INFO("line " << diagnostic.line << ": " << diagnostic.message);
    }
    INFO("lengths, colours, url(), gradients, shadows, transforms and the properties later tickets style all parse");
    REQUIRE(sheet.diagnostics.empty());
    REQUIRE(sheet.rules.size() == 3);
    REQUIRE(sheet.rules[0].declarations.size() == 24);
}

TEST_CASE("A malformed declaration is skipped with its line, and the rest of the rule stands", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse("panel {\n"
                                                               "  width: 10px;\n"
                                                               "  height 20px;\n"
                                                               "  opacity: ;\n"
                                                               "  margin-top: 3furlongs;\n"
                                                               "  flex: 1 2 3 4;\n"
                                                               "  left: 5px;\n"
                                                               "}\n",
                                                               "bad");
    INFO("a declaration with no colon is malformed, on line 3");
    REQUIRE(HasDiagnosticOnLine(sheet, 3, "height"));
    INFO("an empty value is malformed, on line 4");
    REQUIRE(HasDiagnosticOnLine(sheet, 4, "opacity"));
    INFO("a value the property cannot read is refused, on line 5");
    REQUIRE(HasDiagnosticOnLine(sheet, 5, "margin-top"));
    INFO("a shorthand with too many parts is refused, on line 6");
    REQUIRE(HasDiagnosticOnLine(sheet, 6, "flex"));
    REQUIRE(sheet.diagnostics.size() == 4);
    INFO("the declarations around them stand");
    REQUIRE(NamesAndValues(sheet.rules.at(0)) == Declarations{{"width", "10px"}, {"left", "5px"}});
}

TEST_CASE("An unknown property is logged with its line and skipped", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet =
        Fenrir::Stylesheet::Parse("panel {\n  colour: red;\n  width: 1px;\n}\n", "unknown");
    INFO("the property is named, on line 2");
    REQUIRE(sheet.diagnostics.size() == 1);
    REQUIRE(HasDiagnosticOnLine(sheet, 2, "colour"));
    REQUIRE(NamesAndValues(sheet.rules.at(0)) == Declarations{{"width", "1px"}});
}

TEST_CASE("A malformed rule is skipped whole, and the rules after it stand", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse("panel { width: 1px; }\n"
                                                               ".a + .b { width: 2px; }\n"
                                                               ". { width: 3px; }\n"
                                                               "#x:wobbly { width: 4px; }\n"
                                                               "label { width: 5px; }\n",
                                                               "rules");
    INFO("an unsupported combinator skips its rule, on line 2");
    REQUIRE(HasDiagnosticOnLine(sheet, 2, ".a + .b"));
    INFO("a class with no name skips its rule, on line 3");
    REQUIRE(HasDiagnosticOnLine(sheet, 3, "."));
    INFO("an unknown pseudo-class is named and skips its rule, on line 4");
    REQUIRE(HasDiagnosticOnLine(sheet, 4, "wobbly"));
    REQUIRE(sheet.diagnostics.size() == 3);
    INFO("the first and last rules stand");
    REQUIRE(SelectorTexts(sheet) == std::vector<std::string>{"panel", "label"});
}

TEST_CASE("An unclosed block, an unclosed comment and an unknown at-rule are reported with their lines",
          "[ui][stylesheet]")
{
    const Fenrir::Stylesheet atRule =
        Fenrir::Stylesheet::Parse("@media screen { panel { width: 1px; } }\nlabel { width: 2px; }\n", "at");
    INFO("an unknown at-rule is skipped with its block, on line 1");
    REQUIRE(HasDiagnosticOnLine(atRule, 1, "@media"));
    REQUIRE(SelectorTexts(atRule) == std::vector<std::string>{"label"});

    const Fenrir::Stylesheet unclosed =
        Fenrir::Stylesheet::Parse("panel { width: 1px; }\nlabel {\n  width: 2px;\n", "u");
    INFO("a block still open at the end is closed there, and reported at its start");
    REQUIRE(HasDiagnosticOnLine(unclosed, 2, "label"));
    REQUIRE(unclosed.rules.size() == 2);
    REQUIRE(NamesAndValues(unclosed.rules[1]) == Declarations{{"width", "2px"}});

    const Fenrir::Stylesheet comment = Fenrir::Stylesheet::Parse("panel { width: 1px; }\n/* no end\nlabel {}\n", "c");
    INFO("a comment with no end runs to the end, and is reported at its start");
    REQUIRE(HasDiagnosticOnLine(comment, 2, "comment"));
    REQUIRE(SelectorTexts(comment) == std::vector<std::string>{"panel"});
}

TEST_CASE("Comments are ignored anywhere, and lines count through them", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse("/* a\n comment */ panel /* x */ {\n"
                                                               "  width: /* y */ 10px;\n"
                                                               "  height 5px;\n"
                                                               "}\n",
                                                               "comments");
    INFO("the comments are gone from the selector and the value");
    REQUIRE(SelectorTexts(sheet) == std::vector<std::string>{"panel"});
    REQUIRE(NamesAndValues(sheet.rules[0]) == Declarations{{"width", "10px"}});
    INFO("the malformed declaration is on line 4, past a two-line comment");
    REQUIRE(HasDiagnosticOnLine(sheet, 4, "height"));
}

TEST_CASE("Nesting flattens to the selectors CSS nesting means", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse(R"(
        .card, #hero {
            width: 10px;
            &:hover { width: 20px; }
            > .title { width: 30px; }
            label { width: 40px; }
            .theme-dark & { width: 50px; }
            .row {
                &.wide { width: 60px; }
            }
        }
    )",
                                                               "nesting");
    REQUIRE(sheet.diagnostics.empty());
    INFO("the outer rule comes first, then each nested rule in order, each selector of the outer list in turn");
    REQUIRE(SelectorTexts(sheet) == std::vector<std::string>{
                                        ".card", "#hero",                         //
                                        ".card:hover", "#hero:hover",             // &:hover
                                        ".card > .title", "#hero > .title",       // a bare child
                                        ".card label", "#hero label",             // a bare descendant
                                        ".theme-dark .card", ".theme-dark #hero", // & after
                                        ".card .row", "#hero .row",               // a level down
                                        ".card .row.wide", "#hero .row.wide",     // and & there
                                    });
    INFO("each keeps its own declarations");
    REQUIRE(NamesAndValues(sheet.rules[0]) == Declarations{{"width", "10px"}});
    REQUIRE(NamesAndValues(sheet.rules[1]) == Declarations{{"width", "20px"}});
    INFO("a flattened selector has the specificity of what it became");
    REQUIRE(sheet.rules[1].selectors[0].specificity == Specificity(0, 2, 0));
}

TEST_CASE("Custom properties and var() keep their text for the cascade", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse(":root {\n"
                                                               "  --Gap: 8px;\n"
                                                               "  --accent: rgb(1, 2, 3);\n"
                                                               "}\n"
                                                               "panel {\n"
                                                               "  width: var(--size);\n"
                                                               "  height: var(--size, 10px);\n"
                                                               "  margin: var(--Gap) 2px;\n"
                                                               "  padding: var(--Gap, var(--fallback, 1px));\n"
                                                               "  left: var(nope);\n"
                                                               "}\n",
                                                               "vars");
    INFO("a custom property keeps its name's case and its value as written");
    REQUIRE(NamesAndValues(sheet.rules.at(0)) == Declarations{{"--Gap", "8px"}, {"--accent", "rgb(1, 2, 3)"}});
    INFO("a value with var(), with or without a fallback, waits for the cascade; so does a shorthand with one");
    REQUIRE(NamesAndValues(sheet.rules.at(1)) == Declarations{{"width", "var(--size)"},
                                                              {"height", "var(--size, 10px)"},
                                                              {"margin", "var(--Gap) 2px"},
                                                              {"padding", "var(--Gap, var(--fallback, 1px))"}});
    INFO("a var() that names no custom property is malformed, on line 10");
    REQUIRE(HasDiagnosticOnLine(sheet, 10, "left"));
    REQUIRE(sheet.diagnostics.size() == 1);
}

TEST_CASE("Keyframes, font faces and imports are kept", "[ui][stylesheet]")
{
    const Fenrir::Stylesheet sheet = Fenrir::Stylesheet::Parse("@import url(\"base.css\");\n"
                                                               "@import 'theme.css';\n"
                                                               "@keyframes pulse {\n"
                                                               "  from { opacity: 0; }\n"
                                                               "  50%, 75% { opacity: 0.5; margin: 1px; }\n"
                                                               "  to { opacity: 1; }\n"
                                                               "}\n"
                                                               "@font-face {\n"
                                                               "  font-family: \"Body\";\n"
                                                               "  src: url(fonts/body.ttf);\n"
                                                               "  font-weight: 700;\n"
                                                               "}\n"
                                                               "panel { width: 1px; }\n",
                                                               "at-rules");
    REQUIRE(sheet.diagnostics.empty());
    INFO("each import keeps its url and line");
    REQUIRE(sheet.imports.size() == 2);
    REQUIRE(sheet.imports[0].url == "base.css");
    REQUIRE(sheet.imports[0].line == 1);
    REQUIRE(sheet.imports[1].url == "theme.css");

    INFO("a keyframes rule keeps its name, each block's offsets and declarations, shorthands expanded");
    REQUIRE(sheet.keyframes.size() == 1);
    const Fenrir::StyleKeyframes& pulse = sheet.keyframes[0];
    REQUIRE(pulse.name == "pulse");
    REQUIRE(pulse.keyframes.size() == 3);
    REQUIRE(pulse.keyframes[0].offsets == std::vector<float>{0.0f});
    REQUIRE(pulse.keyframes[1].offsets == std::vector<float>{0.5f, 0.75f});
    REQUIRE(pulse.keyframes[1].declarations.size() == 5);
    REQUIRE(pulse.keyframes[2].offsets == std::vector<float>{1.0f});

    INFO("a font face keeps its descriptors as written");
    REQUIRE(sheet.fontFaces.size() == 1);
    REQUIRE(sheet.fontFaces[0].descriptors.size() == 3);
    REQUIRE(sheet.fontFaces[0].descriptors[0].name == "font-family");
    REQUIRE(sheet.fontFaces[0].descriptors[0].value == "\"Body\"");
    REQUIRE(sheet.fontFaces[0].descriptors[1].value == "url(fonts/body.ttf)");

    INFO("the rule after them stands");
    REQUIRE(SelectorTexts(sheet) == std::vector<std::string>{"panel"});
}

TEST_CASE("A later declaration of a property in one rule replaces the earlier", "[ui][stylesheet]")
{
    INFO("one rule holds a property once, its last value, as the cascade would leave it");
    REQUIRE(Expand("width: 1px; height: 2px; width: 3px;") == Declarations{{"height", "2px"}, {"width", "3px"}});
    INFO("a longhand after its shorthand replaces that side alone");
    REQUIRE(
        Expand("margin: 1px; margin-left: 9px;") ==
        Declarations{{"margin-top", "1px"}, {"margin-right", "1px"}, {"margin-bottom", "1px"}, {"margin-left", "9px"}});
}
