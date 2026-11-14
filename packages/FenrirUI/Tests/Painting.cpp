// Box painting through a capturing target (the spec's seams 1 and 2): a laid-out tree in, the root's draw list out.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/PainterVertex.hpp"
#include "UITestApp.hpp"

namespace
{
    constexpr std::uint32_t Rgba8(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
    {
        return r | (g << 8u) | (b << 16u) | (a << 24u);
    }

    std::vector<Fenrir::PainterVertex> Vertices(const Fenrir::DrawList2D& list)
    {
        std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
        std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
        return vertices;
    }

    // Each quad's first vertex, in the order drawn: every painter box is one quad of four.
    std::vector<Fenrir::PainterVertex> Quads(const Fenrir::DrawList2D& list)
    {
        const std::vector<Fenrir::PainterVertex> vertices = Vertices(list);
        std::vector<Fenrir::PainterVertex> quads;
        for (std::size_t i = 0; i < vertices.size(); i += 4)
        {
            quads.push_back(vertices[i]);
        }
        return quads;
    }

    Fenrir::ShapeKind ShapeOf(const Fenrir::PainterVertex& vertex)
    {
        return static_cast<Fenrir::ShapeKind>(vertex.kinds[0]);
    }

    Fenrir::PaintKind PaintOf(const Fenrir::PainterVertex& vertex)
    {
        return static_cast<Fenrir::PaintKind>(vertex.kinds[1]);
    }

    bool IsClipped(const Fenrir::PainterVertex& vertex)
    {
        return (vertex.kinds[2] & Fenrir::PainterFlags::Clipped) != 0;
    }

    // The quad's centre: the mean of its four corners.
    Fenrir::Math::Vec2 CentreOf(const std::vector<Fenrir::PainterVertex>& vertices, std::size_t quad)
    {
        Fenrir::Math::Vec2 sum(0.0f);
        for (std::size_t i = 0; i < 4; ++i)
        {
            sum += vertices[quad * 4 + i].position;
        }
        return sum / 4.0f;
    }

    bool Near(float a, float b, float tolerance = 1e-3f)
    {
        return std::abs(a - b) <= tolerance;
    }

    std::uint32_t Alpha(std::uint32_t rgba8)
    {
        return rgba8 >> 24u;
    }
} // namespace

TEST_CASE("Three nested panels paint their shadow, boxes and clipped children inside one opacity group, in tree order",
          "[ui][paint]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId outer = root.Create(root.GetRootElement(), "panel");
    for (const auto& [name, value] : {std::pair{"margin-left", "10px"},
                                      {"margin-top", "10px"},
                                      {"width", "200px"},
                                      {"height", "100px"},
                                      {"padding-left", "10px"},
                                      {"padding-top", "10px"},
                                      {"padding-right", "10px"},
                                      {"padding-bottom", "10px"},
                                      {"background-color", "#ff0000"},
                                      {"border-top-left-radius", "8px"},
                                      {"border-top-right-radius", "8px"},
                                      {"border-bottom-right-radius", "8px"},
                                      {"border-bottom-left-radius", "8px"},
                                      {"border-top-width", "2px"},
                                      {"border-right-width", "2px"},
                                      {"border-bottom-width", "2px"},
                                      {"border-left-width", "2px"},
                                      {"border-top-color", "#0000ff"},
                                      {"border-right-color", "#0000ff"},
                                      {"border-bottom-color", "#0000ff"},
                                      {"border-left-color", "#0000ff"},
                                      {"box-shadow", "0 4px 8px rgba(0, 0, 0, 0.5)"},
                                      {"opacity", "0.5"},
                                      {"overflow", "hidden"}})
    {
        INFO(name << ": " << value);
        REQUIRE(root.SetStyleProperty(outer, name, value));
    }
    const Fenrir::ElementId middle = root.Create(outer, "panel");
    REQUIRE(root.SetStyleProperty(middle, "flex-grow", "1"));
    REQUIRE(root.SetStyleProperty(middle, "background-color", "#00ff00"));
    root.EditInlineStyle(middle)->SetBorderRadius(Fenrir::CornerRadii::All(4.0f));
    const Fenrir::ElementId inner = root.Create(middle, "panel");
    REQUIRE(root.SetStyleProperty(inner, "width", "20px"));
    REQUIRE(root.SetStyleProperty(inner, "height", "20px"));
    REQUIRE(root.SetStyleProperty(inner, "background-color", "#ffffff"));
    test.Frame();

    REQUIRE(target->lists.size() == 1);
    const Fenrir::DrawList2D& list = target->lists.front();
    INFO("a group, the outer panel's draws, its clipped children's draws, the group's end");
    REQUIRE(list.commands.size() == 4);
    REQUIRE(std::holds_alternative<Fenrir::BeginGroup2D>(list.commands[0]));
    REQUIRE(std::holds_alternative<Fenrir::DrawCommand2D>(list.commands[1]));
    REQUIRE(std::holds_alternative<Fenrir::DrawCommand2D>(list.commands[2]));
    REQUIRE(std::holds_alternative<Fenrir::EndGroup2D>(list.commands[3]));

    const auto& begin = std::get<Fenrir::BeginGroup2D>(list.commands[0]);
    INFO("the group covers the panel and its shadow, 4 down and blurred 8 out: x 2 to 218, y 6 to 122");
    REQUIRE(Near(begin.bounds.x, 2.0f));
    REQUIRE(Near(begin.bounds.y, 6.0f));
    REQUIRE(Near(begin.bounds.width, 216.0f));
    REQUIRE(Near(begin.bounds.height, 116.0f));
    INFO("the group composites at the panel's opacity");
    REQUIRE(Near(std::get<Fenrir::EndGroup2D>(list.commands[3]).opacity, 0.5f));

    const std::vector<Fenrir::PainterVertex> vertices = Vertices(list);
    const std::vector<Fenrir::PainterVertex> quads = Quads(list);
    INFO("five boxes: shadow, background and border of the outer panel, then the middle and inner backgrounds");
    REQUIRE(quads.size() == 5);
    REQUIRE(ShapeOf(quads[0]) == Fenrir::ShapeKind::Shadow);
    REQUIRE(ShapeOf(quads[1]) == Fenrir::ShapeKind::RoundedRectangle);
    REQUIRE(ShapeOf(quads[2]) == Fenrir::ShapeKind::Border);
    REQUIRE(ShapeOf(quads[3]) == Fenrir::ShapeKind::RoundedRectangle);
    REQUIRE(ShapeOf(quads[4]) == Fenrir::ShapeKind::RoundedRectangle);
    INFO("opaque colours: the group, not the vertices, fades them");
    REQUIRE(quads[1].colour == Rgba8(255, 0, 0, 255));
    REQUIRE(quads[2].borderColours[0] == Rgba8(0, 0, 255, 255));
    REQUIRE(quads[3].colour == Rgba8(0, 255, 0, 255));
    REQUIRE(quads[4].colour == Rgba8(255, 255, 255, 255));

    INFO("the outer panel's box is its 200 x 100 border box at (10, 10), corners of 8");
    REQUIRE(Near(quads[1].halfSize.x, 100.0f));
    REQUIRE(Near(quads[1].halfSize.y, 50.0f));
    REQUIRE(Near(quads[1].radii.x, 8.0f));
    REQUIRE(Near(CentreOf(vertices, 1).x, 110.0f));
    REQUIRE(Near(CentreOf(vertices, 1).y, 60.0f));
    INFO("the middle panel fills the outer's content box: 176 x 76 at (22, 22)");
    REQUIRE(Near(quads[3].halfSize.x, 88.0f));
    REQUIRE(Near(quads[3].halfSize.y, 38.0f));
    REQUIRE(Near(CentreOf(vertices, 3).x, 110.0f));
    REQUIRE(Near(CentreOf(vertices, 3).y, 60.0f));

    INFO("the outer panel's own boxes are not clipped");
    REQUIRE_FALSE(IsClipped(quads[1]));
    INFO("its children are clipped to its padding box, 196 x 96, with corners of 8 less the border's 2");
    for (std::size_t quad : {3u, 4u})
    {
        REQUIRE(IsClipped(quads[quad]));
        REQUIRE(Near(quads[quad].clipHalfSize.x, 98.0f));
        REQUIRE(Near(quads[quad].clipHalfSize.y, 48.0f));
        REQUIRE(Near(quads[quad].clipRadii.x, 6.0f));
    }
    const auto& clipped = std::get<Fenrir::DrawCommand2D>(list.commands[2]);
    INFO("and the hardware scissor is the padding box");
    REQUIRE(clipped.scissor.has_value());
    REQUIRE(Near(clipped.scissor->x, 12.0f));
    REQUIRE(Near(clipped.scissor->y, 12.0f));
    REQUIRE(Near(clipped.scissor->width, 196.0f));
    REQUIRE(Near(clipped.scissor->height, 96.0f));
}

TEST_CASE("Without offscreen groups, or with no children, opacity fades each box's colour", "[ui][paint]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId leaf = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(leaf, "height", "10px"));
    REQUIRE(root.SetStyleProperty(leaf, "background-color", "#ffffff"));
    REQUIRE(root.SetStyleProperty(leaf, "opacity", "0.5"));
    test.Frame();

    const Fenrir::DrawList2D& alone = target->lists.back();
    INFO("a leaf needs no group: its colour is faded, premultiplied");
    REQUIRE(std::none_of(alone.commands.begin(), alone.commands.end(),
                         [](const Fenrir::Command2D& c) { return std::holds_alternative<Fenrir::BeginGroup2D>(c); }));
    const std::uint32_t alpha = Alpha(Quads(alone).front().colour);
    REQUIRE((alpha == 127 || alpha == 128));

    const Fenrir::ElementId child = root.Create(leaf, "panel");
    REQUIRE(root.SetStyleProperty(child, "height", "5px"));
    REQUIRE(root.SetStyleProperty(child, "background-color", "#ffffff"));
    test.Roots().settings.offscreenGroups = false;
    test.Frame();
    const Fenrir::DrawList2D& flat = target->lists.back();
    INFO("with groups switched off, no group is drawn and the child is faded by its parent's opacity too");
    REQUIRE(std::none_of(flat.commands.begin(), flat.commands.end(),
                         [](const Fenrir::Command2D& c) { return std::holds_alternative<Fenrir::BeginGroup2D>(c); }));
    const std::vector<Fenrir::PainterVertex> quads = Quads(flat);
    INFO("two boxes, the parent's and the child's");
    REQUIRE(quads.size() == 2);
    const std::uint32_t childAlpha = Alpha(quads[1].colour);
    REQUIRE((childAlpha == 127 || childAlpha == 128));
}

TEST_CASE("The anti-aliasing switch reaches the painter of every root", "[ui][paint]")
{
    UITest::UITestApp test;
    INFO("the UI's anti-aliasing switch is off by default");
    REQUIRE(test.Roots().settings.antiAliasing);
    std::shared_ptr<UITest::CaptureTarget> first;
    std::shared_ptr<UITest::CaptureTarget> second;
    for (Fenrir::UIRoot* root : {&test.MakeRoot(first, {200.0f, 100.0f}), &test.MakeRoot(second, {300.0f, 100.0f})})
    {
        const Fenrir::ElementId panel = root->Create(root->GetRootElement(), "panel");
        REQUIRE(root->SetStyleProperty(panel, "height", "10px"));
        REQUIRE(root->SetStyleProperty(panel, "background-color", "#ff0000"));
        REQUIRE(root->SetStyleProperty(panel, "border-radius", "4px"));
    }
    const auto hardEdged = [](const Fenrir::DrawList2D& list, bool hard) {
        const std::vector<Fenrir::PainterVertex> vertices = Vertices(list);
        return !vertices.empty() && std::ranges::all_of(vertices, [hard](const Fenrir::PainterVertex& vertex) {
            return ((vertex.kinds[2] & Fenrir::PainterFlags::HardEdge) != 0) == hard;
        });
    };

    test.Frame();
    INFO("on by default: every root's box fades over a pixel");
    REQUIRE(hardEdged(first->lists.back(), false));
    REQUIRE(hardEdged(second->lists.back(), false));

    test.Roots().settings.antiAliasing = false;
    test.Frame();
    INFO("off: every root's painter flags its vertices hard-edged");
    REQUIRE(hardEdged(first->lists.back(), true));
    REQUIRE(hardEdged(second->lists.back(), true));

    test.Roots().settings.antiAliasing = true;
    test.Frame();
    INFO("and on again");
    REQUIRE(hardEdged(first->lists.back(), false));
    REQUIRE(hardEdged(second->lists.back(), false));
}

namespace
{
    // A panel 10 high, full width, of `colour`, under the root element.
    Fenrir::ElementId ColouredPanel(Fenrir::UIRoot& root, const char* colour)
    {
        const Fenrir::ElementId element = root.Create(root.GetRootElement(), "panel");
        REQUIRE(root.SetStyleProperty(element, "height", "10px"));
        REQUIRE(root.SetStyleProperty(element, "background-color", colour));
        return element;
    }

    // A 2 x 2 white image in the app's asset store.
    Fenrir::AssetHandle<Fenrir::Image> WhiteImage(UITest::UITestApp& test)
    {
        Fenrir::ImageData pixels;
        pixels.width = 2;
        pixels.height = 2;
        pixels.pixels.assign(16, std::byte{0xFF});
        return test.app.GetResource<Fenrir::AssetStore>().Add<Fenrir::Image>(std::move(pixels),
                                                                             Fenrir::AssetOwner::App());
    }
} // namespace

TEST_CASE("Siblings paint by z-index, equal ones in tree order", "[ui][paint]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId red = ColouredPanel(root, "#ff0000");
    ColouredPanel(root, "#00ff00");
    ColouredPanel(root, "#0000ff");
    REQUIRE(root.SetStyleProperty(red, "z-index", "1"));
    test.Frame();

    const std::vector<Fenrir::PainterVertex> quads = Quads(target->lists.back());
    INFO("three boxes: green and blue at z-index 0 in tree order, then red at 1 on top");
    REQUIRE(quads.size() == 3);
    REQUIRE(quads[0].colour == Rgba8(0, 255, 0, 255));
    REQUIRE(quads[1].colour == Rgba8(0, 0, 255, 255));
    REQUIRE(quads[2].colour == Rgba8(255, 0, 0, 255));
}

TEST_CASE("An element with display none paints nothing", "[ui][paint]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    ColouredPanel(root, "#ff0000");
    const Fenrir::ElementId hidden = ColouredPanel(root, "#00ff00");
    REQUIRE(root.SetStyleProperty(hidden, "display", "none"));
    test.Frame();

    const std::vector<Fenrir::PainterVertex> quads = Quads(target->lists.back());
    INFO("only the red box is drawn");
    REQUIRE(quads.size() == 1);
    REQUIRE(quads[0].colour == Rgba8(255, 0, 0, 255));
    INFO("the root element and the red panel are painted, the hidden one not");
    REQUIRE(root.GetStats().elementsPainted == 2);
}

TEST_CASE("A transform turns the element's boxes about its origin", "[ui][paint]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(panel, "width", "100px"));
    REQUIRE(root.SetStyleProperty(panel, "height", "50px"));
    REQUIRE(root.SetStyleProperty(panel, "background-color", "#ffffff"));
    REQUIRE(root.SetStyleProperty(panel, "transform", "translateX(10px) rotate(90deg)"));
    test.Frame();

    const std::vector<Fenrir::PainterVertex> vertices = Vertices(target->lists.back());
    INFO("one box");
    REQUIRE(vertices.size() == 4);
    float minX = 1e9f;
    float maxX = -1e9f;
    float minY = 1e9f;
    float maxY = -1e9f;
    for (const Fenrir::PainterVertex& vertex : vertices)
    {
        minX = std::min(minX, vertex.position.x);
        maxX = std::max(maxX, vertex.position.x);
        minY = std::min(minY, vertex.position.y);
        maxY = std::max(maxY, vertex.position.y);
    }
    INFO("turned a quarter about its centre (50, 25) and moved 10 right, the box is 50 wide and 100 high about (60, "
         "25), with the quad's one-unit anti-aliasing margin round it");
    REQUIRE(Near((minX + maxX) / 2.0f, 60.0f));
    REQUIRE(Near((minY + maxY) / 2.0f, 25.0f));
    REQUIRE(Near(maxX - minX, 52.0f));
    REQUIRE(Near(maxY - minY, 102.0f));
    INFO("layout is not transformed");
    REQUIRE(UITest::Near(root.GetBoxes(panel).border, {0.0f, 0.0f, 100.0f, 50.0f}));
}

TEST_CASE("A background image paints as a gradient or as the image, which its command samples", "[ui][paint]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId gradient = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(gradient, "height", "100px"));
    REQUIRE(root.SetStyleProperty(gradient, "background-image", "linear-gradient(90deg, #000000, #ffffff)"));
    const Fenrir::AssetHandle<Fenrir::Image> image = WhiteImage(test);
    const Fenrir::ElementId picture = root.Create(root.GetRootElement(), "panel");
    root.EditInlineStyle(picture)
        ->SetHeight(Fenrir::Length::Px(100.0f))
        .SetBackgroundImage(Fenrir::BackgroundImage{image});
    test.Frame();

    const Fenrir::DrawList2D& list = target->lists.back();
    const std::vector<Fenrir::PainterVertex> quads = Quads(list);
    INFO("one box each: a linear gradient, then the image");
    REQUIRE(quads.size() == 2);
    REQUIRE(PaintOf(quads[0]) == Fenrir::PaintKind::LinearGradient);
    REQUIRE(PaintOf(quads[1]) == Fenrir::PaintKind::Image);
    bool sampled = false;
    for (const Fenrir::Command2D& command : list.commands)
    {
        if (const auto* draw = std::get_if<Fenrir::DrawCommand2D>(&command))
        {
            for (const Fenrir::TextureSlot2D& slot : draw->textures)
            {
                sampled = sampled || slot.image == image;
            }
        }
    }
    INFO("the image's command samples it");
    REQUIRE(sampled);
}

TEST_CASE("The root scale takes UI units to the list's logical units", "[ui][paint]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    // 1080-height on a 540-high target: half a logical unit per UI unit.
    Fenrir::UIRoot& root = test.MakeRoot(target, {960.0f, 540.0f}, 1.0f, Fenrir::ScaleRule::Height1080);
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(panel, "height", "100px"));
    REQUIRE(root.SetStyleProperty(panel, "background-color", "#ffffff"));
    test.Frame();

    const std::vector<Fenrir::PainterVertex> vertices = Vertices(target->lists.back());
    INFO("one box");
    REQUIRE(vertices.size() == 4);
    INFO("the 1920 x 100 UI unit box is 960 x 50 logical units, centred at (480, 25)");
    REQUIRE(Near(CentreOf(vertices, 0).x, 480.0f));
    REQUIRE(Near(CentreOf(vertices, 0).y, 25.0f));
}

TEST_CASE("A value or property it cannot read is refused and changes nothing", "[ui][style]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(panel, "width", "10px"));
    INFO("an unknown property");
    REQUIRE_FALSE(root.SetStyleProperty(panel, "colour", "red"));
    INFO("a length without a unit");
    REQUIRE_FALSE(root.SetStyleProperty(panel, "width", "12"));
    INFO("an unknown keyword");
    REQUIRE_FALSE(root.SetStyleProperty(panel, "display", "grid"));
    INFO("a malformed colour");
    REQUIRE_FALSE(root.SetStyleProperty(panel, "background-color", "#12345"));
    test.Frame();
    INFO("the width it had stays");
    REQUIRE(root.GetResolvedStyle(panel)->width == Fenrir::Length::Px(10.0f));
}

TEST_CASE("Colours are read in the forms CSS writes them", "[ui][style]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    const auto colourOf = [&](const char* value) {
        INFO("background-color: " << value);
        REQUIRE(root.SetStyleProperty(panel, "background-color", value));
        test.Frame();
        return root.GetResolvedStyle(panel)->backgroundColor;
    };

    const Fenrir::Colour halfRed = colourOf("rgb(255 0 0 / 50%)");
    INFO("rgb() with spaces and a slash: full red at half alpha");
    REQUIRE(Near(halfRed.r, 1.0f));
    REQUIRE(Near(halfRed.a, 0.5f));
    const Fenrir::Colour green = colourOf("hsl(120, 100%, 50%)");
    INFO("hsl() at 120 degrees: pure green");
    REQUIRE(Near(green.g, 1.0f));
    REQUIRE(Near(green.r, 0.0f));
    const Fenrir::Colour named = colourOf("Navy");
    INFO("a colour name, in any case: navy is sRGB 0x000080");
    REQUIRE(Near(named.b, Fenrir::Colour::FromSrgb(0x000080FFu).b));
    INFO("transparent has no alpha");
    REQUIRE(Near(colourOf("transparent").a, 0.0f));
}

TEST_CASE("Transform lists read their functions and angle units in order", "[ui][style]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    REQUIRE(root.SetStyleProperty(panel, "transform", "rotate(0.25turn) scale(2)"));
    test.Frame();

    const auto& steps = root.GetResolvedStyle(panel)->transform;
    INFO("two steps, in the order written");
    REQUIRE(steps.size() == 2);
    INFO("a quarter turn is a quarter of a rotation");
    REQUIRE(steps[0].kind == Fenrir::TransformStep::Kind::Rotate);
    REQUIRE(Near(steps[0].radians, std::numbers::pi_v<float> / 2.0f));
    INFO("scale() with one factor scales both axes");
    REQUIRE(steps[1].kind == Fenrir::TransformStep::Kind::Scale);
    REQUIRE(Near(steps[1].factors.x, 2.0f));
    REQUIRE(Near(steps[1].factors.y, 2.0f));
}

namespace
{
    // An element whose content is a white rectangle reaching 50 past its border box on every side.
    class OverhangElement : public Fenrir::Element
    {
      public:
        void Paint(Fenrir::Painter& painter, const Fenrir::ElementPaint& paint) override
        {
            const Fenrir::Rect2D& box = paint.boxes.border;
            painter.DrawRect({box.x - 50.0f, box.y - 50.0f, box.width + 100.0f, box.height + 100.0f},
                             Fenrir::Colour::White);
        }
    };
} // namespace

TEST_CASE("Overflow hidden clips an element's own content to its padding box", "[ui][paint]")
{
    UITest::UITestApp test;
    test.Roots().RegisterElement("overhang", [] { return std::make_unique<OverhangElement>(); });
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId element = root.Create(root.GetRootElement(), "overhang");
    for (const auto& [name, value] : {std::pair{"margin-left", "100px"},
                                      {"margin-top", "100px"},
                                      {"width", "40px"},
                                      {"height", "30px"},
                                      {"border-left-width", "5px"},
                                      {"overflow", "hidden"}})
    {
        INFO(name << ": " << value);
        REQUIRE(root.SetStyleProperty(element, name, value));
    }
    test.Frame();

    const Fenrir::DrawList2D& list = target->lists.back();
    const std::vector<Fenrir::PainterVertex> quads = Quads(list);
    INFO("the border and the content: two boxes");
    REQUIRE(quads.size() == 2);
    INFO("the border is the element's own box, not clipped");
    REQUIRE_FALSE(IsClipped(quads[0]));
    INFO("the content is clipped to the padding box inside the 5-wide left border: 35 x 30");
    REQUIRE(IsClipped(quads[1]));
    REQUIRE(Near(quads[1].clipHalfSize.x, 17.5f));
    REQUIRE(Near(quads[1].clipHalfSize.y, 15.0f));
}

TEST_CASE("A backdrop filter blurs what is under the element's border box before its background", "[ui][paint]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = ColouredPanel(root, "#ff0000");
    REQUIRE(root.SetStyleProperty(panel, "width", "100px"));
    REQUIRE(root.SetStyleProperty(panel, "backdrop-filter", "blur(8px)"));
    test.Frame();

    const Fenrir::DrawList2D& list = target->lists.back();
    INFO("an empty backdrop group over the panel, then the panel's background: " << list.commands.size()
                                                                                 << " commands");
    REQUIRE(list.commands.size() == 3);
    REQUIRE(std::holds_alternative<Fenrir::BeginGroup2D>(list.commands[0]));
    REQUIRE(std::holds_alternative<Fenrir::EndGroup2D>(list.commands[1]));
    REQUIRE(std::holds_alternative<Fenrir::DrawCommand2D>(list.commands[2]));
    const auto& begin = std::get<Fenrir::BeginGroup2D>(list.commands[0]);
    INFO("the group covers the panel's 100 by 10 border box, not " << begin.bounds.width << " by "
                                                                   << begin.bounds.height);
    REQUIRE(Near(begin.bounds.width, 100.0f));
    REQUIRE(Near(begin.bounds.height, 10.0f));
    const auto& end = std::get<Fenrir::EndGroup2D>(list.commands[1]);
    INFO("the group ends as a backdrop blur of radius 8, not " << end.radius);
    REQUIRE(end.filter == Fenrir::GroupFilter2D::BackdropBlur);
    REQUIRE(Near(end.radius, 8.0f));
}

TEST_CASE("Backdrop filter reads blur() or none, and refuses the rest", "[ui][style]")
{
    UITest::UITestApp test;
    std::shared_ptr<UITest::CaptureTarget> target;
    Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});
    const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
    INFO("blur() of a length in px");
    REQUIRE(root.SetStyleProperty(panel, "backdrop-filter", "blur(12px)"));
    INFO("a negative radius");
    REQUIRE_FALSE(root.SetStyleProperty(panel, "backdrop-filter", "blur(-2px)"));
    INFO("a filter function the backdrop does not take");
    REQUIRE_FALSE(root.SetStyleProperty(panel, "backdrop-filter", "grayscale(1)"));
    INFO("a radius without a unit");
    REQUIRE_FALSE(root.SetStyleProperty(panel, "backdrop-filter", "blur(4)"));
    test.Frame();
    INFO("the refused values leave blur(12px)");
    REQUIRE(root.GetResolvedStyle(panel)->backdropBlur == 12.0f);
    REQUIRE(root.SetStyleProperty(panel, "backdrop-filter", "none"));
    test.Frame();
    INFO("none clears it");
    REQUIRE(root.GetResolvedStyle(panel)->backdropBlur == 0.0f);
}
