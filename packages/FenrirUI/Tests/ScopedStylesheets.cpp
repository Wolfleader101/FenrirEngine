// Scoped stylesheets (ADR-0070): a sheet parsed with a scope class puts it on every rule's rightmost compound, after
// nesting resolves, `::deep` moves it left, and a root's cascade then styles only the elements carrying the class.
// Files, imports and hot reload of scoped sheets are in StyleFiles.cpp.
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirUI/Stylesheet.hpp"
#include "UITestApp.hpp"

namespace
{
    using Fenrir::ElementId;
    using Fenrir::Stylesheet;

    std::vector<std::string> SelectorTexts(const Stylesheet& sheet)
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

    std::string Diagnostics(const Stylesheet& sheet)
    {
        std::string text;
        for (const Fenrir::StyleDiagnostic& diagnostic : sheet.diagnostics)
        {
            text += std::to_string(diagnostic.line) + ": " + diagnostic.message + "\n";
        }
        return text;
    }

    // `text` parsed scoped to `s-x`, with no diagnostic.
    Stylesheet ParseScoped(std::string_view text)
    {
        Stylesheet sheet = Stylesheet::Parse(text, "scoped", "s-x");
        INFO("the sheet parses with no diagnostic: " << Diagnostics(sheet));
        REQUIRE(sheet.diagnostics.empty());
        return sheet;
    }

    constexpr std::uint32_t Specificity(std::uint32_t ids, std::uint32_t classes, std::uint32_t tags)
    {
        return (ids << 20u) | (classes << 10u) | tags;
    }

    struct SheetTest
    {
        UITest::UITestApp test;
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});

        ElementId Make(ElementId parent, std::string_view tag, std::initializer_list<std::string_view> classes)
        {
            const ElementId element = root.Create(parent, tag);
            for (const std::string_view name : classes)
            {
                root.AddClass(element, name);
            }
            return element;
        }

        void Add(std::string_view text)
        {
            INFO("the scoped sheet is added");
            REQUIRE(root.AddStylesheet(text, "component", "s-x").IsValid());
        }

        float Width(ElementId element) const
        {
            const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(element);
            INFO("the element is alive");
            REQUIRE(style != nullptr);
            return style->width.value;
        }
    };
} // namespace

TEST_CASE("A scope lands on the rightmost compound of every selector", "[ui][stylesheet][scope]")
{
    const Stylesheet sheet =
        ParseScoped(".a .b { width: 1px; }\n.a > label, #x { width: 2px; }\n.c::scrollbar { width: 3px; }\n");
    INFO("the sheet records its scope");
    REQUIRE(sheet.scope == "s-x");
    INFO("descendant and child combinators keep their place; a pseudo-element stays after the scope");
    REQUIRE(SelectorTexts(sheet) ==
            std::vector<std::string>{".a .b.s-x", ".a > label.s-x", "#x.s-x", ".c.s-x::scrollbar"});
}

TEST_CASE("A scope adds one class's weight to a selector's specificity", "[ui][stylesheet][scope]")
{
    const Stylesheet sheet = ParseScoped(".a .b { width: 1px; }\n#x { width: 2px; }\n");
    INFO("two classes and the scope; an id and the scope");
    REQUIRE(sheet.rules[0].selectors[0].specificity == Specificity(0, 3, 0));
    REQUIRE(sheet.rules[1].selectors[0].specificity == Specificity(1, 1, 0));
}

TEST_CASE("Nested rules are scoped after nesting resolves, so only the innermost subject carries the scope",
          "[ui][stylesheet][scope]")
{
    const Stylesheet sheet = ParseScoped(".a {\n"
                                         "  width: 1px;\n"
                                         "  .b { width: 2px; }\n"
                                         "  & > .c { width: 3px; }\n"
                                         "  &:hover { width: 4px; }\n"
                                         "  height: 5px;\n"
                                         "}\n");
    INFO("the outer rule, its nested ones and the declarations after them are each scoped once, at their subject");
    REQUIRE(SelectorTexts(sheet) ==
            std::vector<std::string>{".a.s-x", ".a .b.s-x", ".a > .c.s-x", ".a.s-x:hover", ".a.s-x"});
}

TEST_CASE("::deep puts the scope on the compound before it and leaves a descendant combinator",
          "[ui][stylesheet][scope]")
{
    const Stylesheet sheet = ParseScoped(".list ::deep label { width: 1px; }\n"
                                         ".a .list::deep .item > .x { width: 2px; }\n"
                                         ".outer { ::deep .inner { width: 4px; } }\n");
    INFO("with or without a space before it, and through nesting");
    REQUIRE(SelectorTexts(sheet) ==
            std::vector<std::string>{".list.s-x label", ".a .list.s-x .item > .x", ".outer.s-x", ".outer.s-x .inner"});
    INFO("the scope's weight is the compound's it moved to");
    REQUIRE(sheet.rules[0].selectors[0].specificity == Specificity(0, 2, 1));
}

TEST_CASE("A leading ::deep scopes the selector to any element carrying the scope", "[ui][stylesheet][scope]")
{
    const Stylesheet sheet = ParseScoped("::deep .any { width: 3px; }\n");
    INFO("the scope is a compound of its own, an ancestor of the subject");
    REQUIRE(SelectorTexts(sheet) == std::vector<std::string>{".s-x .any"});
}

TEST_CASE("::deep in an unscoped sheet is an error for its rule with its line", "[ui][stylesheet][scope]")
{
    const Stylesheet sheet = Stylesheet::Parse(".ok { width: 1px; }\n"
                                               ".list ::deep label { width: 2px; }\n"
                                               ".also { width: 3px; }\n",
                                               "unscoped");
    INFO("one diagnostic, at line 2, naming ::deep: " << Diagnostics(sheet));
    REQUIRE(sheet.diagnostics.size() == 1);
    REQUIRE(sheet.diagnostics[0].line == 2);
    REQUIRE(sheet.diagnostics[0].message.find("::deep") != std::string::npos);
    INFO("the rule is skipped; the rest of the sheet stands");
    REQUIRE(SelectorTexts(sheet) == std::vector<std::string>{".ok", ".also"});
}

TEST_CASE("A ::deep with nothing after it, or a second one, is an error for its rule", "[ui][stylesheet][scope]")
{
    const Stylesheet sheet = Stylesheet::Parse(".a ::deep { width: 1px; }\n"
                                               ".a ::deep ::deep .b { width: 2px; }\n",
                                               "bad", "s-x");
    INFO("each rule is a diagnostic and skipped: " << Diagnostics(sheet));
    REQUIRE(sheet.diagnostics.size() == 2);
    REQUIRE(sheet.rules.empty());
}

TEST_CASE("@keyframes and @font-face are not scoped", "[ui][stylesheet][scope]")
{
    const Stylesheet sheet = ParseScoped("@keyframes pulse { from { opacity: 0; } to { opacity: 1; } }\n"
                                         "@font-face { font-family: Body; src: url(body.ttf); }\n"
                                         ".a { animation: pulse 1s; }\n");
    INFO("the keyframes keep their name, the font face is kept, and the style rule alone is scoped");
    REQUIRE(sheet.keyframes.size() == 1);
    REQUIRE(sheet.keyframes[0].name == "pulse");
    REQUIRE(sheet.fontFaces.size() == 1);
    REQUIRE(SelectorTexts(sheet) == std::vector<std::string>{".a.s-x"});
}

TEST_CASE("A scoped sheet styles the elements carrying its class and no others", "[ui][cascade][scope]")
{
    SheetTest t;
    t.Add(".a .b { width: 10px; }");
    const ElementId a = t.Make(t.root.GetRootElement(), "panel", {"a"});
    const ElementId scoped = t.Make(a, "panel", {"b", "s-x"});
    const ElementId plain = t.Make(a, "panel", {"b"});
    t.test.Frame();
    INFO("a .b with the scope class under .a matches, and a .b without it does not");
    REQUIRE(t.Width(scoped) == 10.0f);
    REQUIRE(t.Width(plain) != 10.0f);
}

TEST_CASE("A scoped sheet's child combinators and nested rules style only scoped elements", "[ui][cascade][scope]")
{
    SheetTest t;
    t.Add(".a > .c { width: 20px; }\n.a { .d { width: 30px; } }\n");
    const ElementId a = t.Make(t.root.GetRootElement(), "panel", {"a"});
    const ElementId scopedC = t.Make(a, "panel", {"c", "s-x"});
    const ElementId plainC = t.Make(a, "panel", {"c"});
    const ElementId scopedD = t.Make(a, "panel", {"d", "s-x"});
    const ElementId plainD = t.Make(a, "panel", {"d"});
    t.test.Frame();
    INFO("the child rule and the nested rule match the scoped children alone");
    REQUIRE(t.Width(scopedC) == 20.0f);
    REQUIRE(t.Width(plainC) != 20.0f);
    REQUIRE(t.Width(scopedD) == 30.0f);
    REQUIRE(t.Width(plainD) != 30.0f);
}

TEST_CASE("::deep styles an element without the scope under a scoped ancestor", "[ui][cascade][scope]")
{
    SheetTest t;
    t.Add(".list ::deep label { width: 40px; }");
    const ElementId top = t.root.GetRootElement();
    const ElementId scopedList = t.Make(top, "panel", {"list", "s-x"});
    const ElementId child = t.Make(scopedList, "panel", {"s-child"});
    const ElementId reached = t.Make(child, "label", {"s-child"});
    const ElementId plainList = t.Make(top, "panel", {"list"});
    const ElementId missed = t.Make(plainList, "label", {});
    t.test.Frame();
    INFO("a label of a child component, under the component's scoped .list, is styled; one under a plain .list is not");
    REQUIRE(t.Width(reached) == 40.0f);
    REQUIRE(t.Width(missed) != 40.0f);
}

TEST_CASE("A sheet added again with the same name and scope is the one sheet", "[ui][cascade][scope]")
{
    SheetTest t;
    const Fenrir::StylesheetId first = t.root.AddStylesheet(".a { width: 1px; }", "component", "s-x");
    const Fenrir::StylesheetId again = t.root.AddStylesheet(".a { width: 1px; }", "component", "s-x");
    INFO("the first add makes a sheet, and the second gives back its id");
    REQUIRE(first.IsValid());
    REQUIRE(again == first);
}

TEST_CASE("The same sheet name under another scope is a sheet of its own", "[ui][cascade][scope]")
{
    SheetTest t;
    const Fenrir::StylesheetId first = t.root.AddStylesheet(".a { width: 1px; }", "component", "s-x");
    const Fenrir::StylesheetId other = t.root.AddStylesheet(".a { width: 1px; }", "component", "s-y");
    INFO("two sheets, two ids");
    REQUIRE(first.IsValid());
    REQUIRE(other.IsValid());
    REQUIRE(other != first);
}

TEST_CASE("A scoped sheet removed is added afresh by the next add", "[ui][cascade][scope]")
{
    SheetTest t;
    const Fenrir::StylesheetId first = t.root.AddStylesheet(".a { width: 1px; }", "component", "s-x");
    INFO("the sheet is removed");
    REQUIRE(t.root.RemoveStylesheet(first));
    const Fenrir::StylesheetId afresh = t.root.AddStylesheet(".a { width: 1px; }", "component", "s-x");
    INFO("the add after makes a new sheet the root holds");
    REQUIRE(afresh.IsValid());
    REQUIRE(afresh != first);
    REQUIRE(t.root.GetStylesheet(afresh) != nullptr);
}
