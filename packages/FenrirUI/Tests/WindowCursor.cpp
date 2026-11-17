// The window's cursor from `cursor` (spec: "The window cursor"; ADR-0065, seam 1): each frame the window root whose
// router has the pointer over an element that takes pointer input sets the window's cursor shape to that element's
// resolved `cursor`, through FenrirWindow, and gives the arrow back once when the pointer leaves every such root; and
// any root's UIRoot::GetHoveredCursor reads what its hovered element wants. The window tests make a hidden window and
// read its last-set shape back, so they are skipped without a display, as FenrirWindow's own are.

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/EntityList.hpp"
#include "FenrirRenderer/WorldTargets2D.hpp"
#include "FenrirUI/UIImageTarget.hpp"
#include "FenrirUI/UIWorldTarget.hpp"
#include "FenrirWindow/Window.hpp"
#include "UITestApp.hpp"

using Fenrir::Cursor;
using Fenrir::CursorMode;
using Fenrir::CursorShape;
using Fenrir::ElementId;
using Fenrir::UIRoot;
using Fenrir::Window;
using Fenrir::Math::Vec2;
using namespace UITest;

namespace
{
    using Declarations = std::vector<std::pair<std::string, std::string>>;

    // The shared hidden-window app, with what the cursor tests do on its window.
    struct WindowCursorApp : WindowUITestApp
    {
        // Skips the calling test when SDL's video could not start: no display to make a window on.
        void RequireDisplay() const
        {
            WindowUITestApp::RequireDisplay("window cursor");
        }

        // A root on the window, over a capture target of the window's size at a scale of 1, so UI units are the
        // window's logical units.
        UIRoot& MakeRoot(int inputPriority = 0, bool applyCursor = true)
        {
            Fenrir::UIRootConfig config;
            config.scaleRule = Fenrir::ScaleRule::ContentScale;
            config.target = std::make_shared<CaptureTarget>(Fenrir::UITargetSize{{320.0f, 240.0f}, 1.0f});
            config.inputPriority = inputPriority;
            config.applyCursor = applyCursor;
            return Roots().CreateRoot(config);
        }

        // Moves the pointer to `position` in the window and runs the frame that routes it.
        void MoveTo(Vec2 position)
        {
            Feed().MouseMove(testWindow, position, {0.0f, 0.0f});
            Frame();
        }

        [[nodiscard]] CursorShape Shape()
        {
            return Window::GetCursorShape(app, Window::MainWindow());
        }

        // The game sets the window's shape itself, as it does with no UI under the pointer.
        void GameSets(CursorShape shape)
        {
            INFO("the window did not take the game's cursor shape " << static_cast<int>(shape));
            REQUIRE(Window::SetCursorShape(app, Window::MainWindow(), shape));
        }

        // The game sets the window's cursor mode, as for mouse look.
        void GameSets(CursorMode mode)
        {
            INFO("the window did not take the game's cursor mode " << static_cast<int>(mode));
            REQUIRE(Window::SetCursorMode(app, Window::MainWindow(), mode));
        }
    };

    // An element of `tag` under the root element, placed absolutely at `x`, `y` and sized `width` by `height`, with
    // `extra` declarations after.
    ElementId Place(UIRoot& root, std::string_view tag, float x, float y, float width, float height,
                    const Declarations& extra = {})
    {
        const ElementId element = root.Create(root.GetRootElement(), tag);
        Declarations declarations{{"position", "absolute"},
                                  {"left", std::to_string(x) + "px"},
                                  {"top", std::to_string(y) + "px"},
                                  {"width", std::to_string(width) + "px"},
                                  {"height", std::to_string(height) + "px"}};
        declarations.insert(declarations.end(), extra.begin(), extra.end());
        for (const auto& [name, value] : declarations)
        {
            INFO("the element's inline style did not take " << name << ": " << value);
            REQUIRE(root.SetStyleProperty(element, name, value));
        }
        return element;
    }

    // A panel that takes pointer input and wants `cursor`.
    ElementId CursorPanel(UIRoot& root, float x, float y, std::string_view cursor)
    {
        return Place(root, "panel", x, y, 60.0f, 40.0f, {{"pointer-events", "all"}, {"cursor", std::string(cursor)}});
    }
} // namespace

TEST_CASE("Over a button the window's cursor is the hand", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& root = test.MakeRoot();
    (void)Place(root, "button", 10.0f, 10.0f, 80.0f, 30.0f);
    test.Frame();
    {
        INFO("the window starts with the arrow");
        REQUIRE(test.Shape() == CursorShape::Arrow);
    }

    test.MoveTo({50.0f, 25.0f});
    INFO("the default stylesheet gives a button `cursor: pointer`, the hand");
    REQUIRE(test.Shape() == CursorShape::Hand);
}

TEST_CASE("Over a text entry the window's cursor is the I-beam", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& root = test.MakeRoot();
    (void)Place(root, "textentry", 10.0f, 100.0f, 120.0f, 30.0f);
    test.Frame();
    {
        INFO("the window starts with the arrow");
        REQUIRE(test.Shape() == CursorShape::Arrow);
    }

    test.MoveTo({50.0f, 115.0f});
    INFO("the default stylesheet gives a text entry `cursor: text`, the I-beam");
    REQUIRE(test.Shape() == CursorShape::Text);
}

TEST_CASE("Each cursor keyword sets its shape on the window", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& root = test.MakeRoot();
    const std::vector<std::pair<std::string_view, CursorShape>> shapes{
        {"default", CursorShape::Arrow},
        {"pointer", CursorShape::Hand},
        {"text", CursorShape::Text},
        {"move", CursorShape::Move},
        {"not-allowed", CursorShape::NotAllowed},
        {"ew-resize", CursorShape::ResizeEastWest},
        {"ns-resize", CursorShape::ResizeNorthSouth},
        {"nwse-resize", CursorShape::ResizeNorthWestSouthEast},
        {"nesw-resize", CursorShape::ResizeNorthEastSouthWest},
        {"none", CursorShape::Hidden},
        {"auto", CursorShape::Arrow},
    };
    // Ten panels a row, 30 wide; each keyword's own panel.
    for (std::size_t i = 0; i < shapes.size(); ++i)
    {
        (void)Place(root, "panel", 30.0f * static_cast<float>(i % 10), 50.0f * static_cast<float>(i / 10), 30.0f, 40.0f,
                    {{"pointer-events", "all"}, {"cursor", std::string(shapes[i].first)}});
    }
    test.Frame();

    for (std::size_t i = 0; i < shapes.size(); ++i)
    {
        // Through the arrow's panel between each, so every keyword's shape differs from the last one set.
        test.MoveTo({15.0f, 20.0f});
        test.MoveTo({30.0f * static_cast<float>(i % 10) + 15.0f, 50.0f * static_cast<float>(i / 10) + 20.0f});
        INFO("`cursor: " << shapes[i].first << "` sets shape " << static_cast<int>(shapes[i].second));
        REQUIRE(test.Shape() == shapes[i].second);
    }
}

TEST_CASE("A shape is set only when it differs from the last FenrirUI set", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& root = test.MakeRoot();
    (void)Place(root, "button", 10.0f, 10.0f, 60.0f, 30.0f);
    (void)Place(root, "button", 100.0f, 10.0f, 60.0f, 30.0f);
    test.Frame();
    test.MoveTo({40.0f, 25.0f});
    INFO("over the first button the window shows the hand");
    REQUIRE(test.Shape() == CursorShape::Hand);

    // The game sets a shape of its own: FenrirUI sets the hand again only if it set something else since.
    test.GameSets(CursorShape::NotAllowed);
    test.MoveTo({130.0f, 25.0f});
    INFO("moving to the second button wants the hand again, which FenrirUI last set, so nothing is set");
    REQUIRE(test.Shape() == CursorShape::NotAllowed);
}

TEST_CASE("Leaving every root gives the window the arrow once, then leaves it to the game", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& root = test.MakeRoot();
    (void)Place(root, "button", 10.0f, 10.0f, 60.0f, 30.0f);
    test.Frame();
    test.MoveTo({40.0f, 25.0f});
    INFO("over the button the window shows the hand");
    REQUIRE(test.Shape() == CursorShape::Hand);

    test.MoveTo({200.0f, 200.0f});
    INFO("off it, over nothing that takes pointer input, the arrow comes back");
    REQUIRE(test.Shape() == CursorShape::Arrow);
    test.GameSets(CursorShape::Text);
    test.MoveTo({210.0f, 200.0f});
    test.Frame();
    INFO("after that the game's shape is left alone");
    REQUIRE(test.Shape() == CursorShape::Text);
}

TEST_CASE("An element with pointer-events none under the pointer sets no cursor", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& root = test.MakeRoot();
    (void)Place(root, "panel", 10.0f, 10.0f, 100.0f, 100.0f, {{"cursor", "pointer"}});
    test.Frame();
    test.GameSets(CursorShape::Text);

    test.MoveTo({50.0f, 50.0f});
    test.Frame();
    INFO("a HUD element takes no pointer input, so it is not hovered and the game's shape stays");
    REQUIRE(test.Shape() == CursorShape::Text);
}

TEST_CASE("Nothing is set while the window's cursor mode is not Normal", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& root = test.MakeRoot();
    (void)Place(root, "button", 10.0f, 10.0f, 60.0f, 30.0f);
    test.Frame();
    test.GameSets(CursorMode::Captured);

    test.MoveTo({40.0f, 25.0f});
    INFO("the cursor is captured for mouse look, so the button's hand is not set");
    REQUIRE(test.Shape() == CursorShape::Arrow);

    test.GameSets(CursorMode::Normal);
    test.Frame();
    INFO("with the cursor free again, the hovered button's hand is set");
    REQUIRE(test.Shape() == CursorShape::Hand);
}

TEST_CASE("A root with applyCursor off sets no cursor", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& root = test.MakeRoot(0, false);
    (void)Place(root, "button", 10.0f, 10.0f, 60.0f, 30.0f);
    test.Frame();

    test.MoveTo({40.0f, 25.0f});
    INFO("the root's elements are hovered, but it leaves the window's cursor to the game");
    REQUIRE(root.GetHovered().IsValid());
    REQUIRE(test.Shape() == CursorShape::Arrow);
}

TEST_CASE("Of two window roots under the pointer, the one with the higher input priority sets the cursor",
          "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    UIRoot& below = test.MakeRoot(0);
    UIRoot& above = test.MakeRoot(10);
    (void)Place(below, "panel", 0.0f, 0.0f, 300.0f, 200.0f, {{"pointer-events", "all"}, {"cursor", "ew-resize"}});
    (void)CursorPanel(above, 10.0f, 10.0f, "move");
    test.Frame();

    test.MoveTo({30.0f, 30.0f});
    INFO("both roots hover an element there; the higher priority root's move wins");
    REQUIRE(below.GetHovered().IsValid());
    REQUIRE(test.Shape() == CursorShape::Move);
    test.MoveTo({200.0f, 150.0f});
    INFO("where only the lower root has an element, its east-west resize is set");
    REQUIRE(test.Shape() == CursorShape::ResizeEastWest);
}

TEST_CASE("An image root never sets the window's cursor", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    Fenrir::UIRootConfig config;
    config.target = std::make_shared<Fenrir::UIImageTarget>(test.app, 320, 240);
    config.scaleRule = Fenrir::ScaleRule::ContentScale;
    UIRoot& root = test.Roots().CreateRoot(config);
    (void)Place(root, "button", 0.0f, 0.0f, 320.0f, 240.0f);
    test.Frame();
    test.GameSets(CursorShape::Text);

    test.MoveTo({40.0f, 25.0f});
    test.Frame();
    INFO("an image root takes no window input, so the window's mouse over it sets nothing");
    REQUIRE(test.Shape() == CursorShape::Text);
}

TEST_CASE("A world root reads its hovered button's cursor and never sets the window's", "[ui][cursor][display]")
{
    WindowCursorApp test;
    test.RequireDisplay();
    Fenrir::Entity quad = test.app.GetActiveScene().GetEntityList().CreateEntity();
    quad.AddComponent<Fenrir::WorldQuad2D>(Fenrir::WorldQuad2D{{2.0f, 1.0f}, 100.0f});
    Fenrir::UIRootConfig config;
    config.target = std::make_shared<Fenrir::UIWorldTarget>(test.app, quad);
    UIRoot& root = test.Roots().CreateRoot(config);
    (void)Place(root, "button", 0.0f, 0.0f, 200.0f, 100.0f);
    test.Frame();
    test.GameSets(CursorShape::Text);

    Fenrir::Math::Ray ray;
    ray.origin = Fenrir::Math::Vec3(0.0f, 0.0f, 5.0f);
    ray.dir = Fenrir::Math::Vec3(0.0f, 0.0f, -1.0f);
    INFO("the ray meets the quad over the button");
    REQUIRE(root.SetPointerRay(ray, false));
    test.Frame();
    INFO("the root gives the button's pointer for the game to apply");
    REQUIRE(root.GetHoveredCursor() == Cursor::Pointer);
    INFO("and leaves the window's cursor alone");
    REQUIRE(test.Shape() == CursorShape::Text);
}

TEST_CASE("GetHoveredCursor resolves auto over a text entry to text, elsewhere to default", "[ui][cursor]")
{
    UITestApp test;
    // Without the default stylesheet a text entry's cursor is auto, the initial value.
    Fenrir::UIRootConfig config;
    config.scaleRule = Fenrir::ScaleRule::ContentScale;
    config.target = std::make_shared<CaptureTarget>(Fenrir::UITargetSize{{320.0f, 240.0f}, 1.0f});
    config.defaultStylesheet = false;
    UIRoot& plain = test.Roots().CreateRoot(config);
    (void)Place(plain, "textentry", 10.0f, 10.0f, 120.0f, 30.0f, {{"pointer-events", "all"}});
    (void)Place(plain, "panel", 10.0f, 100.0f, 120.0f, 30.0f, {{"pointer-events", "all"}});
    test.Frame();
    INFO("with nothing hovered, auto");
    REQUIRE(plain.GetHoveredCursor() == Cursor::Auto);

    test.Feed().MouseMove(testWindow, {50.0f, 25.0f}, {0.0f, 0.0f});
    test.Frame();
    INFO("auto over a text entry is the I-beam");
    REQUIRE(plain.GetHoveredCursor() == Cursor::Text);
    test.Feed().MouseMove(testWindow, {50.0f, 115.0f}, {0.0f, 0.0f});
    test.Frame();
    INFO("auto over a panel is the arrow");
    REQUIRE(plain.GetHoveredCursor() == Cursor::Default);
}

TEST_CASE("GetHoveredCursor gives the inherited cursor of a child under a button", "[ui][cursor]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {320.0f, 240.0f});
    const ElementId button = Place(root, "button", 10.0f, 10.0f, 120.0f, 40.0f);
    const ElementId label = root.Create(button, "label");
    root.SetText(label, "Play");
    test.Frame();

    test.Feed().MouseMove(testWindow, {30.0f, 25.0f}, {0.0f, 0.0f});
    test.Frame();
    INFO("the label inherits the button's pointer, whichever of them is hovered");
    REQUIRE(root.GetHovered().IsValid());
    REQUIRE(root.GetHoveredCursor() == Cursor::Pointer);
}

TEST_CASE("Over a scroll view's thumb the cursor is the thumb's, from the default stylesheet", "[ui][cursor]")
{
    UITestApp test;
    std::shared_ptr<CaptureTarget> target;
    UIRoot& root = test.MakeRoot(target, {320.0f, 240.0f});
    // 100 tall with 400 of content: the vertical bar shows along its right edge, its thumb at the top.
    const ElementId scroll = Place(root, "scroll", 10.0f, 10.0f, 200.0f, 100.0f, {{"padding", "0px"}});
    const ElementId content = root.Create(scroll, "panel");
    root.SetStyleProperty(content, "height", "400px");
    test.Frame();
    const Fenrir::ScrollbarBoxes bars = root.GetScrollbars(scroll);
    INFO("the scroll view shows its vertical bar");
    REQUIRE(bars.vertical);

    const Fenrir::Rect2D thumb = bars.verticalThumb;
    test.Feed().MouseMove(testWindow, {thumb.x + thumb.width / 2.0f, thumb.y + thumb.height / 2.0f}, {0.0f, 0.0f});
    test.Frame();
    INFO("over the thumb, `::scrollbar-thumb { cursor: pointer }` gives the hand");
    REQUIRE(root.GetHovered() == scroll);
    REQUIRE(root.GetHoveredCursor() == Cursor::Pointer);
    test.Feed().MouseMove(testWindow, {50.0f, 50.0f}, {0.0f, 0.0f});
    test.Frame();
    INFO("over the scroll view's content, which inherits its pointer-events and its cursor, auto, the arrow");
    REQUIRE(root.GetHovered() == content);
    REQUIRE(root.GetHoveredCursor() == Cursor::Default);
}
