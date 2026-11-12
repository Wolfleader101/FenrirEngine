// The style cascade through a headless App (the spec's seam 1): stylesheets matched against a root's elements, by
// specificity and order, under inline style, with inheritance, custom properties and what a change restyles.
#include <array>
#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "FenrirUI/Stylesheet.hpp"
#include "UITestApp.hpp"

namespace
{
    using Fenrir::Length;

    float WidthOf(const Fenrir::UIRoot& root, Fenrir::ElementId element)
    {
        const Fenrir::ResolvedStyle* style = root.GetResolvedStyle(element);
        INFO("the element is alive and its width is a length in px");
        REQUIRE(style != nullptr);
        REQUIRE(style->width.unit == Fenrir::LengthUnit::Px);
        return style->width.value;
    }

    struct SheetTest
    {
        UITest::UITestApp test;
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    };
} // namespace

TEST_CASE("A stylesheet styles elements by type, and a class beats a type and an id a class", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    REQUIRE(root.AddStylesheet("#hero { width: 30px; }\n"
                               ".card { width: 20px; }\n"
                               "panel { width: 10px; }\n",
                               "specificity")
                .IsValid());
    const Fenrir::ElementId plain = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId card = root.Create(root.GetRootElement(), "panel");
    root.AddClass(card, "card");
    const Fenrir::ElementId hero = root.Create(root.GetRootElement(), "panel");
    root.AddClass(hero, "card");
    root.SetIdString(hero, "hero");
    t.test.Frame();

    INFO("the type rule styles a plain panel");
    REQUIRE(WidthOf(root, plain) == 10.0f);
    INFO("the class rule wins over the type rule, though written before it");
    REQUIRE(WidthOf(root, card) == 20.0f);
    INFO("the id rule wins over both");
    REQUIRE(WidthOf(root, hero) == 30.0f);
}

TEST_CASE("At equal specificity the later rule wins, across sheets in the order added", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet(".a { width: 1px; } .b { width: 2px; }", "first");
    root.AddStylesheet(".c { width: 3px; }", "second");
    const Fenrir::ElementId ab = root.Create(root.GetRootElement(), "panel");
    root.AddClass(ab, "b");
    root.AddClass(ab, "a");
    const Fenrir::ElementId ac = root.Create(root.GetRootElement(), "panel");
    root.AddClass(ac, "c");
    root.AddClass(ac, "a");
    t.test.Frame();
    INFO("the order rules were written in decides, not the order of the element's classes");
    REQUIRE(WidthOf(root, ab) == 2.0f);
    INFO("a sheet added later comes after one added before");
    REQUIRE(WidthOf(root, ac) == 3.0f);
}

TEST_CASE("Inline style wins over every rule", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet("#x.y.z { width: 50px; height: 7px; }", "sheet");
    const Fenrir::ElementId element = root.Create(root.GetRootElement(), "panel");
    root.SetIdString(element, "x");
    root.AddClass(element, "y");
    root.AddClass(element, "z");
    root.EditInlineStyle(element)->SetWidth(Length::Px(5.0f));
    t.test.Frame();
    INFO("the inline width wins over an id rule; the property inline style does not give comes from the rule");
    REQUIRE(WidthOf(root, element) == 5.0f);
    REQUIRE(root.GetResolvedStyle(element)->height == Length::Px(7.0f));
}

TEST_CASE("An inherited property reaches down three levels, and a rule below overrides it", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet(".top { cursor: pointer; pointer-events: all; } .stop { cursor: text; }", "inherit");
    const Fenrir::ElementId top = root.Create(root.GetRootElement(), "panel");
    root.AddClass(top, "top");
    const Fenrir::ElementId middle = root.Create(top, "panel");
    const Fenrir::ElementId bottom = root.Create(middle, "panel");
    const Fenrir::ElementId leaf = root.Create(bottom, "panel");
    const Fenrir::ElementId stop = root.Create(middle, "panel");
    root.AddClass(stop, "stop");
    const Fenrir::ElementId below = root.Create(stop, "panel");
    t.test.Frame();
    INFO("the cursor and pointer-events given at the top are every descendant's");
    for (const Fenrir::ElementId element : {middle, bottom, leaf})
    {
        REQUIRE(root.GetResolvedStyle(element)->cursor == Fenrir::Cursor::Pointer);
        REQUIRE(root.GetResolvedStyle(element)->pointerEvents == Fenrir::PointerEvents::All);
    }
    INFO("a rule on a descendant overrides what it inherits, for it and below it");
    REQUIRE(root.GetResolvedStyle(stop)->cursor == Fenrir::Cursor::Text);
    REQUIRE(root.GetResolvedStyle(below)->cursor == Fenrir::Cursor::Text);
    INFO("a property that does not inherit stays at its default below");
    REQUIRE(root.GetResolvedStyle(root.GetRootElement())->cursor == Fenrir::Cursor::Auto);
}

TEST_CASE("Descendant and child selectors match through the tree", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet(".menu .item { width: 1px; } .menu > .item { height: 2px; }", "combinators");
    const Fenrir::ElementId menu = root.Create(root.GetRootElement(), "panel");
    root.AddClass(menu, "menu");
    const Fenrir::ElementId direct = root.Create(menu, "panel");
    root.AddClass(direct, "item");
    const Fenrir::ElementId group = root.Create(menu, "panel");
    const Fenrir::ElementId deep = root.Create(group, "panel");
    root.AddClass(deep, "item");
    const Fenrir::ElementId outside = root.Create(root.GetRootElement(), "panel");
    root.AddClass(outside, "item");
    t.test.Frame();
    INFO("a descendant selector matches a child and a grandchild");
    REQUIRE(WidthOf(root, direct) == 1.0f);
    REQUIRE(WidthOf(root, deep) == 1.0f);
    INFO("a child selector matches the child alone");
    REQUIRE(root.GetResolvedStyle(direct)->height == Length::Px(2.0f));
    REQUIRE(root.GetResolvedStyle(deep)->height == Length::Auto());
    INFO("neither matches outside the menu");
    REQUIRE(root.GetResolvedStyle(outside)->width == Length::Auto());
}

TEST_CASE("A class change restyles its element's subtree and nothing else", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet(".dark panel { width: 9px; }", "subtree");
    const Fenrir::ElementId left = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId leftChild = root.Create(left, "panel");
    const Fenrir::ElementId leftGrandchild = root.Create(leftChild, "panel");
    const Fenrir::ElementId right = root.Create(root.GetRootElement(), "panel");
    root.Create(right, "panel");
    t.test.Frame();
    t.test.Frame();
    INFO("the tree is clean before the change");
    REQUIRE(root.GetStats().stylesResolved == 0);

    root.AddClass(left, "dark");
    t.test.Frame();
    INFO("the element and its two descendants are resolved, and the other subtree is not");
    REQUIRE(root.GetStats().stylesResolved == 3);
    INFO("its descendants now match the descendant rule");
    REQUIRE(WidthOf(root, leftChild) == 9.0f);
    REQUIRE(WidthOf(root, leftGrandchild) == 9.0f);
}

TEST_CASE("A pseudo-class flag flip restyles the element", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet("button { width: 10px; } button:hover { width: 12px; } button:hover:active { width: 14px; }",
                       "pseudo");
    const Fenrir::ElementId button = root.Create(root.GetRootElement(), "button");
    t.test.Frame();
    INFO("with no flag, the plain rule");
    REQUIRE(WidthOf(root, button) == 10.0f);

    root.SetPseudoClass(button, Fenrir::PseudoClass::Hover, true);
    t.test.Frame();
    INFO("hovered, the :hover rule applies");
    REQUIRE(WidthOf(root, button) == 12.0f);
    root.SetPseudoClass(button, Fenrir::PseudoClass::Active, true);
    t.test.Frame();
    INFO("with both flags, the rule needing both applies");
    REQUIRE(WidthOf(root, button) == 14.0f);
    root.SetPseudoClass(button, Fenrir::PseudoClass::Hover, false);
    root.SetPseudoClass(button, Fenrir::PseudoClass::Active, false);
    t.test.Frame();
    INFO("with the flags cleared, the plain rule again");
    REQUIRE(WidthOf(root, button) == 10.0f);
}

TEST_CASE("First-child, last-child and root follow the tree as it changes", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet(":root { height: 1px; } .i:first-child { width: 1px; } .i:last-child { width: 3px; }",
                       "structural");
    const Fenrir::ElementId list = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId a = root.Create(list, "panel");
    const Fenrir::ElementId b = root.Create(list, "panel");
    const Fenrir::ElementId c = root.Create(list, "panel");
    for (const Fenrir::ElementId element : {a, b, c})
    {
        root.AddClass(element, "i");
    }
    t.test.Frame();
    INFO(":root is the root element alone");
    REQUIRE(root.GetResolvedStyle(root.GetRootElement())->height == Length::Px(1.0f));
    REQUIRE(root.GetResolvedStyle(list)->height == Length::Auto());
    INFO("the first and last children match, the middle one neither");
    REQUIRE(WidthOf(root, a) == 1.0f);
    REQUIRE(root.GetResolvedStyle(b)->width == Length::Auto());
    REQUIRE(WidthOf(root, c) == 3.0f);

    root.Reorder(c, 0);
    t.test.Frame();
    INFO("after a reorder the new first and last children match, and the old ones no longer do");
    REQUIRE(WidthOf(root, c) == 1.0f);
    REQUIRE(root.GetResolvedStyle(a)->width == Length::Auto());
    REQUIRE(WidthOf(root, b) == 3.0f);

    root.Remove(b);
    t.test.Frame();
    INFO("after a removal the sibling that is now last matches");
    REQUIRE(WidthOf(root, a) == 3.0f);
}

TEST_CASE("Custom properties inherit and resolve at use, with var()'s fallback", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet(":root { --size: 40px; --pad: 3px; }\n"
                       ".theme { --size: 60px; }\n"
                       "panel { width: var(--size); height: var(--missing, 15px); padding: var(--pad) 1px; }\n"
                       ".broken { width: var(--missing); }\n",
                       "custom");
    const Fenrir::ElementId outer = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId themed = root.Create(outer, "panel");
    root.AddClass(themed, "theme");
    const Fenrir::ElementId inner = root.Create(themed, "panel");
    const Fenrir::ElementId broken = root.Create(outer, "panel");
    root.AddClass(broken, "broken");
    t.test.Frame();
    INFO("a custom property on :root reaches an element two levels down");
    REQUIRE(WidthOf(root, outer) == 40.0f);
    INFO("one given lower is used there and below");
    REQUIRE(WidthOf(root, themed) == 60.0f);
    REQUIRE(WidthOf(root, inner) == 60.0f);
    INFO("a var() of a missing custom property takes its fallback");
    REQUIRE(root.GetResolvedStyle(inner)->height == Length::Px(15.0f));
    INFO("a shorthand with var() expands once the custom property is known");
    REQUIRE(root.GetResolvedStyle(inner)->paddingTop == Length::Px(3.0f));
    REQUIRE(root.GetResolvedStyle(inner)->paddingRight == Length::Px(1.0f));
    INFO("a var() of a missing custom property with no fallback leaves the property as if not declared");
    REQUIRE(root.GetResolvedStyle(broken)->width == Length::Auto());

    root.RemoveClass(themed, "theme");
    t.test.Frame();
    INFO("without the class, the inherited custom property is the root's again");
    REQUIRE(WidthOf(root, inner) == 40.0f);
}

TEST_CASE("A custom property's var() resolves where it is declared, and descendants inherit the result",
          "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    root.AddStylesheet(":root { --base: 10px; --size: var(--base); }\n"
                       ".inner { --base: 99px; width: var(--size); }\n",
                       "declared");
    const Fenrir::ElementId outer = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId inner = root.Create(outer, "panel");
    root.AddClass(inner, "inner");
    t.test.Frame();
    INFO("--size took --base from :root, where it was declared; the inner --base does not reach it");
    REQUIRE(WidthOf(root, inner) == 10.0f);
}

TEST_CASE("Adding and removing a stylesheet restyles the tree", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    const Fenrir::ElementId element = root.Create(root.GetRootElement(), "panel");
    t.test.Frame();
    const Fenrir::StylesheetId sheet = root.AddStylesheet("panel { width: 25px; }", "removable");
    INFO("the sheet's text is kept, readable by its id");
    REQUIRE(root.GetStylesheet(sheet) != nullptr);
    REQUIRE(root.GetStylesheet(sheet)->name == "removable");
    t.test.Frame();
    REQUIRE(WidthOf(root, element) == 25.0f);

    REQUIRE(root.RemoveStylesheet(sheet));
    t.test.Frame();
    INFO("with the sheet gone, the width is its default");
    REQUIRE(root.GetResolvedStyle(element)->width == Length::Auto());
    REQUIRE(root.GetStylesheet(sheet) == nullptr);
    REQUIRE_FALSE(root.RemoveStylesheet(sheet));
}

TEST_CASE("Inline style takes shorthands by name, as a sheet does", "[ui][cascade]")
{
    SheetTest t;
    Fenrir::UIRoot& root = t.root;
    const Fenrir::ElementId element = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(element, "margin", "1px 2px"));
    REQUIRE(root.SetStyleProperty(element, "border", "2px solid red"));
    INFO("a shorthand with a value it cannot read sets none of its longhands");
    REQUIRE_FALSE(root.SetStyleProperty(element, "padding", "1px 2px 3px 4px 5px"));
    t.test.Frame();
    const Fenrir::ResolvedStyle& style = *root.GetResolvedStyle(element);
    INFO("margin expanded to its four sides, and border to its widths");
    REQUIRE(style.marginTop == Length::Px(1.0f));
    REQUIRE(style.marginLeft == Length::Px(2.0f));
    REQUIRE(style.borderLeftWidth == 2.0f);
    REQUIRE(style.paddingTop == Length::Px(0.0f));
}

TEST_CASE("A sheet-styled tree lays out as the same tree styled inline", "[ui][cascade][layout]")
{
    const auto build = [](Fenrir::UIRoot& root, bool inlineStyle) {
        const Fenrir::ElementId row = root.Create(root.GetRootElement(), "panel");
        const Fenrir::ElementId a = root.Create(row, "panel");
        const Fenrir::ElementId b = root.Create(row, "panel");
        const Fenrir::ElementId c = root.Create(b, "panel");
        if (inlineStyle)
        {
            root.EditInlineStyle(row)
                ->SetFlexDirection(Fenrir::FlexDirection::Row)
                .SetWidth(Length::Px(400.0f))
                .SetHeight(Length::Px(100.0f))
                .SetPadding(Length::Px(10.0f), Length::Px(10.0f), Length::Px(10.0f), Length::Px(10.0f))
                .SetColumnGap(Length::Px(8.0f))
                .SetRowGap(Length::Px(8.0f));
            root.EditInlineStyle(a)->SetFlexGrow(1.0f).SetFlexShrink(1.0f).SetFlexBasis(Length::Percent(0.0f));
            root.EditInlineStyle(b)
                ->SetFlexGrow(2.0f)
                .SetFlexShrink(1.0f)
                .SetFlexBasis(Length::Percent(0.0f))
                .SetBorderWidths({4.0f, 4.0f, 4.0f, 4.0f});
            root.EditInlineStyle(c)->SetMargin(Length::Px(5.0f), Length::Px(6.0f), Length::Px(5.0f), Length::Px(6.0f));
            root.EditInlineStyle(c)->SetHeight(Length::Percent(50.0f));
        }
        else
        {
            root.AddClass(row, "row");
            root.AddStylesheet(".row { --gap: 8px; flex-direction: row; width: 400px; height: 100px; padding: 10px;"
                               "  gap: var(--gap);"
                               "  > :first-child { flex: 1; }"
                               "  > :last-child { flex: 2; border: 4px solid black; > panel { margin: 5px 6px; "
                               "height: 50%; } }"
                               "}",
                               "layout");
        }
        return std::array{row, a, b, c};
    };

    UITest::UITestApp inlineApp;
    std::shared_ptr<UITest::CaptureTarget> inlineTarget;
    Fenrir::UIRoot& inlineRoot = inlineApp.MakeRoot(inlineTarget, {800.0f, 600.0f});
    const auto inlineIds = build(inlineRoot, true);
    inlineApp.Frame();

    UITest::UITestApp sheetApp;
    std::shared_ptr<UITest::CaptureTarget> sheetTarget;
    Fenrir::UIRoot& sheetRoot = sheetApp.MakeRoot(sheetTarget, {800.0f, 600.0f});
    const auto sheetIds = build(sheetRoot, false);
    sheetApp.Frame();

    for (std::size_t i = 0; i < inlineIds.size(); ++i)
    {
        const Fenrir::ElementBoxes expected = inlineRoot.GetBoxes(inlineIds[i]);
        const Fenrir::ElementBoxes actual = sheetRoot.GetBoxes(sheetIds[i]);
        INFO("element " << i << " has the same border and content boxes either way");
        REQUIRE(UITest::Near(actual.border, expected.border));
        REQUIRE(UITest::Near(actual.content, expected.content));
    }
    INFO("the layout is not trivial: the children share the row by their grow factors, the second the wider");
    REQUIRE(inlineRoot.GetBoxes(inlineIds[1]).border.width > 100.0f);
    REQUIRE(inlineRoot.GetBoxes(inlineIds[2]).border.width > inlineRoot.GetBoxes(inlineIds[1]).border.width);
}
