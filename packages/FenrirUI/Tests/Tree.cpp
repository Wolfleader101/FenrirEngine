// The element tree through a headless App (the spec's seam 1): ids, order, queries, and what a frame re-does.
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "UITestApp.hpp"

namespace
{
    // A leaf whose content measures to a fixed size, counting the measures layout asks of it.
    class FixedSizeElement : public Fenrir::Element
    {
      public:
        bool HasMeasure() const override
        {
            return true;
        }

        Fenrir::Math::Vec2 Measure(const Fenrir::MeasureConstraints&) override
        {
            ++measures;
            return size;
        }

        Fenrir::Math::Vec2 size{40.0f, 30.0f};
        int measures = 0;
    };

    std::vector<Fenrir::ElementId> Children(const Fenrir::UIRoot& root, Fenrir::ElementId element)
    {
        const auto children = root.GetChildren(element);
        return {children.begin(), children.end()};
    }
} // namespace

TEST_CASE("A removed element's id goes stale, and a reused slot does not revive it", "[ui][tree]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId child = root.Create(panel, "panel");
    root.SetIdString(child, "child");

    REQUIRE(root.Remove(panel));
    INFO("the element and everything under it are gone");
    REQUIRE_FALSE(root.IsAlive(panel));
    REQUIRE_FALSE(root.IsAlive(child));
    REQUIRE(root.GetChildren(root.GetRootElement()).empty());
    REQUIRE_FALSE(root.FindById("child").IsValid());

    const Fenrir::ElementId again = root.Create(root.GetRootElement(), "panel");
    INFO("a new element may take the slot, but with another version");
    REQUIRE(root.IsAlive(again));
    REQUIRE_FALSE(root.IsAlive(panel));
    REQUIRE_FALSE(root.IsAlive(child));
    INFO("calls with a stale id do nothing");
    REQUIRE(root.GetElement(panel) == nullptr);
    REQUIRE_FALSE(root.Remove(panel));
    REQUIRE(root.GetTag(panel).empty());
    INFO("the root element cannot be removed");
    REQUIRE_FALSE(root.Remove(root.GetRootElement()));
    REQUIRE(root.IsAlive(root.GetRootElement()));
}

TEST_CASE("Create, reorder and reparent keep children in order", "[ui][tree]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId list = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId a = root.Create(list, "panel");
    const Fenrir::ElementId c = root.Create(list, "panel");
    const Fenrir::ElementId b = root.Create(list, "panel", 1);
    INFO("an index puts a child among its siblings");
    REQUIRE(Children(root, list) == std::vector{a, b, c});

    REQUIRE(root.Reorder(c, 0));
    INFO("reorder moves a child within its parent");
    REQUIRE(Children(root, list) == std::vector{c, a, b});
    REQUIRE(root.Reorder(c, Fenrir::UIRoot::End));
    REQUIRE(Children(root, list) == std::vector{a, b, c});

    const Fenrir::ElementId other = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId x = root.Create(other, "panel");
    REQUIRE(root.Reparent(b, other, 0));
    INFO("reparent takes it out of the old list and into the new one at its index");
    REQUIRE(Children(root, list) == std::vector{a, c});
    REQUIRE(Children(root, other) == std::vector{b, x});
    REQUIRE(root.GetParent(b) == other);

    INFO("an element cannot be moved under itself");
    REQUIRE_FALSE(root.Reparent(other, b));
    REQUIRE(root.GetParent(other) == root.GetRootElement());

    test.Frame();
    INFO("layout follows the order: the second child of a column is below the first");
    REQUIRE(root.SetStyleProperty(b, "height", "10px"));
    REQUIRE(root.SetStyleProperty(x, "height", "20px"));
    test.Frame();
    REQUIRE(root.GetBoxes(x).border.y == root.GetBoxes(b).border.y + 10.0f);
}

TEST_CASE("Elements are found by id string and by class, in tree order", "[ui][tree]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId hud = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId health = root.Create(hud, "panel");
    const Fenrir::ElementId ammo = root.Create(hud, "panel");
    const Fenrir::ElementId deep = root.Create(health, "panel");
    root.SetIdString(ammo, "ammo");
    root.AddClass(ammo, "bar");
    root.AddClass(deep, "bar");
    root.AddClass(deep, "bar");
    root.AddClass(hud, "hud");

    INFO("by id string");
    REQUIRE(root.FindById("ammo") == ammo);
    REQUIRE_FALSE(root.FindById("missing").IsValid());
    INFO("by class, in tree order: the health bar's child comes before the ammo bar");
    REQUIRE(root.FindByClass("bar") == std::vector{deep, ammo});
    INFO("a class is added once");
    REQUIRE(root.GetClasses(deep).size() == 1);
    root.RemoveClass(deep, "bar");
    REQUIRE_FALSE(root.HasClass(deep, "bar"));
    REQUIRE(root.FindByClass("bar") == std::vector{ammo});
    INFO("children, by walking");
    REQUIRE(Children(root, hud) == std::vector{health, ammo});
}

TEST_CASE("A registered tag makes its custom element, whose measure sizes the leaf", "[ui][tree]")
{
    UITest::UITestApp test;
    test.Roots().RegisterElement("fixed", [] { return std::make_unique<FixedSizeElement>(); });
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId row = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(row, "flex-direction", "row"));
    REQUIRE(root.SetStyleProperty(row, "align-items", "flex-start"));
    const Fenrir::ElementId leaf = root.Create(row, "fixed");
    REQUIRE(root.SetStyleProperty(leaf, "padding-left", "5px"));
    test.Frame();

    auto* fixed = dynamic_cast<FixedSizeElement*>(root.GetElement(leaf));
    INFO("the registry made the subclass");
    REQUIRE(fixed != nullptr);
    REQUIRE(root.GetTag(leaf) == "fixed");
    REQUIRE(fixed->GetId() == leaf);
    INFO("its content box is what it measured, its padding round it");
    REQUIRE(UITest::Near(root.GetBoxes(leaf).border, {0.0f, 0.0f, 45.0f, 30.0f}));
    REQUIRE(UITest::Near(root.GetBoxes(leaf).content, {5.0f, 0.0f, 40.0f, 30.0f}));

    fixed->size = {60.0f, 10.0f};
    root.InvalidateMeasure(leaf);
    test.Frame();
    INFO("an invalidated measure is asked again");
    REQUIRE(UITest::Near(root.GetBoxes(leaf).border, {0.0f, 0.0f, 65.0f, 10.0f}));
    INFO("an unregistered tag makes a plain element");
    const Fenrir::ElementId plain = root.Create(row, "anything");
    REQUIRE(root.GetElement(plain) != nullptr);
    REQUIRE(dynamic_cast<FixedSizeElement*>(root.GetElement(plain)) == nullptr);
}

namespace
{
    // Two panels under the root element, each with one fixed-size leaf, laid out once.
    struct TwoSubtrees
    {
        explicit TwoSubtrees(UITest::UITestApp& test)
        {
            test.Roots().RegisterElement("fixed", [] { return std::make_unique<FixedSizeElement>(); });
            root = &test.MakeRoot(target, {800.0f, 600.0f});
            left = root->Create(root->GetRootElement(), "panel");
            right = root->Create(root->GetRootElement(), "panel");
            leftLeaf = root->Create(left, "fixed");
            rightLeaf = root->Create(right, "fixed");
            test.Frame();
        }

        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot* root = nullptr;
        Fenrir::ElementId left;
        Fenrir::ElementId right;
        Fenrir::ElementId leftLeaf;
        Fenrir::ElementId rightLeaf;
    };
} // namespace

TEST_CASE("The first frame resolves every style and lays out once", "[ui][tree]")
{
    UITest::UITestApp test;
    TwoSubtrees tree(test);
    INFO("the root element and its four elements are resolved, and Yoga lays out once");
    REQUIRE(tree.root->GetStats().stylesResolved == 5);
    REQUIRE(tree.root->GetStats().layouts == 1);
}

TEST_CASE("A clean root resolves no style and calls no layout, and still paints", "[ui][tree]")
{
    UITest::UITestApp test;
    TwoSubtrees tree(test);
    test.Frame();
    INFO("nothing changed: no style is resolved, Yoga does not lay out and no leaf is measured");
    REQUIRE(tree.root->GetStats().stylesResolved == 0);
    REQUIRE(tree.root->GetStats().layouts == 0);
    REQUIRE(tree.root->GetStats().measures == 0);
    INFO("a list is submitted every frame");
    REQUIRE(tree.target->lists.size() == 2);
}

TEST_CASE("A dirty subtree resolves and lays out again, and a clean subtree is not measured again", "[ui][tree]")
{
    UITest::UITestApp test;
    TwoSubtrees tree(test);
    auto* rightElement = dynamic_cast<FixedSizeElement*>(tree.root->GetElement(tree.rightLeaf));
    const int rightMeasures = rightElement->measures;
    REQUIRE(tree.root->SetStyleProperty(tree.leftLeaf, "margin-top", "7px"));
    test.Frame();

    INFO("only the changed element's style is resolved, and the root lays out again");
    REQUIRE(tree.root->GetStats().stylesResolved == 1);
    REQUIRE(tree.root->GetStats().layouts == 1);
    REQUIRE(UITest::Near(tree.root->GetBoxes(tree.leftLeaf).border.y, 7.0f));
    INFO("the other subtree's leaf is not measured again");
    REQUIRE(rightElement->measures == rightMeasures);
}

TEST_CASE("A change that does not lay out resolves the style without a layout", "[ui][tree]")
{
    UITest::UITestApp test;
    TwoSubtrees tree(test);
    REQUIRE(tree.root->SetStyleProperty(tree.right, "background-color", "#ff0000"));
    test.Frame();
    INFO("the panel's style is resolved, and Yoga's style is unchanged, so no layout runs");
    REQUIRE(tree.root->GetStats().stylesResolved == 1);
    REQUIRE(tree.root->GetStats().layouts == 0);
}

TEST_CASE("A pseudo-class change marks the element's style dirty", "[ui][tree]")
{
    UITest::UITestApp test;
    TwoSubtrees tree(test);
    tree.root->SetPseudoClass(tree.right, Fenrir::PseudoClass::Hover, true);
    INFO("the element carries the pseudo-class");
    REQUIRE(tree.root->HasPseudoClass(tree.right, Fenrir::PseudoClass::Hover));
    test.Frame();
    INFO("its style is resolved again");
    REQUIRE(tree.root->GetStats().stylesResolved == 1);
}

TEST_CASE("Inherited properties come from the parent, others from the defaults", "[ui][tree]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId menu = root.Create(root.GetRootElement(), "panel");
    const Fenrir::ElementId item = root.Create(menu, "panel");
    REQUIRE(root.SetStyleProperty(menu, "pointer-events", "all"));
    REQUIRE(root.SetStyleProperty(menu, "cursor", "pointer"));
    REQUIRE(root.SetStyleProperty(menu, "opacity", "0.5"));
    test.Frame();

    INFO("pointer-events and cursor are inherited");
    REQUIRE(root.GetResolvedStyle(item)->pointerEvents == Fenrir::PointerEvents::All);
    REQUIRE(root.GetResolvedStyle(item)->cursor == Fenrir::Cursor::Pointer);
    INFO("opacity is not");
    REQUIRE(root.GetResolvedStyle(item)->opacity == 1.0f);

    REQUIRE(root.SetStyleProperty(menu, "pointer-events", "none"));
    test.Frame();
    INFO("a parent's change reaches the children that inherit it");
    REQUIRE(root.GetResolvedStyle(item)->pointerEvents == Fenrir::PointerEvents::None);
    INFO("the root element's style takes no pointer input by default (ADR-0065)");
    REQUIRE(root.GetResolvedStyle(root.GetRootElement())->pointerEvents == Fenrir::PointerEvents::None);
}

TEST_CASE("Several roots draw in layer order, then the order they were made", "[ui][tree]")
{
    UITest::UITestApp test;
    std::vector<int> order;
    struct OrderTarget : Fenrir::UITarget
    {
        OrderTarget(std::vector<int>& order, int name) : order(order), name(name)
        {
        }
        Fenrir::UITargetSize GetSize(Fenrir::App&) override
        {
            return {{100.0f, 100.0f}, 1.0f};
        }
        void Submit(Fenrir::App&, Fenrir::DrawList2D) override
        {
            order.push_back(name);
        }
        std::vector<int>& order;
        int name;
    };
    const auto make = [&](int name, std::int32_t layer) {
        Fenrir::UIRootConfig config;
        config.layer = layer;
        config.target = std::make_shared<OrderTarget>(order, name);
        return &test.Roots().CreateRoot(config);
    };
    make(1, 5);
    Fenrir::UIRoot* second = make(2, -1);
    make(3, 5);
    test.Frame();
    INFO("layer -1 first, then the two on layer 5 as made");
    REQUIRE(order == std::vector{2, 1, 3});

    REQUIRE(test.Roots().DestroyRoot(*second));
    order.clear();
    test.Frame();
    INFO("a destroyed root draws no more");
    REQUIRE(order == std::vector{1, 3});
}
