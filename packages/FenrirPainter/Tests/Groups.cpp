// The painter's offscreen groups and masks by what they append to a 2D draw list (the spec's seam 2): begin and end
// group commands balanced and nested as recorded, their bounds and composites in logical units, and a mask's group,
// its shape and its reach bracketing the commands it masks.

#include <cstddef>
#include <numbers>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"
#include "PainterTest.hpp"

using Fenrir::Colour;
using Fenrir::Rect2D;
using Fenrir::Math::Vec2;
using Fenrir::Math::Vec4;

namespace
{
    bool Near(const Rect2D& a, const Rect2D& b, float tolerance = 1e-4f)
    {
        return PainterTest::Near(a.x, b.x, tolerance) && PainterTest::Near(a.y, b.y, tolerance) &&
               PainterTest::Near(a.width, b.width, tolerance) && PainterTest::Near(a.height, b.height, tolerance);
    }

    /** @brief The list's command `index`, which must be a `T`. */
    template <typename T>
    const T& Command(const Fenrir::DrawList2D& list, std::size_t index)
    {
        INFO("command " << index);
        REQUIRE(index < list.commands.size());
        REQUIRE(std::holds_alternative<T>(list.commands[index]));
        return std::get<T>(list.commands[index]);
    }

    /** @brief The command kinds of the list in order: D draw, B begin group, E end group, M end of a mask's reach. */
    std::string Kinds(const Fenrir::DrawList2D& list)
    {
        std::string kinds;
        for (const Fenrir::Command2D& command : list.commands)
        {
            kinds += "DBEM"[command.index()];
        }
        return kinds;
    }
} // namespace

TEST_CASE("A group's begin and end bracket its draws, its bounds and composite in logical units", "[painter][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.scale = 2.0f;
    Fenrir::Painter painter(list, config);

    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.BeginGroup({5, 5, 40, 20}, 0.5f, Fenrir::GroupBlend2D::Additive);
    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.EndGroup();
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("a draw on either side of a group is not extended across its begin or end");
    REQUIRE(Kinds(list) == "DBDED");
    REQUIRE(Near(Command<Fenrir::BeginGroup2D>(list, 1).bounds, Rect2D{10, 10, 80, 40}));
    const auto& end = Command<Fenrir::EndGroup2D>(list, 3);
    REQUIRE(end.opacity == 0.5f);
    REQUIRE(end.blend == Fenrir::GroupBlend2D::Additive);
    REQUIRE(end.filter == Fenrir::GroupFilter2D::None);
    REQUIRE_FALSE(end.mask);
}

TEST_CASE("Groups nest, each end closing the innermost, as recorded", "[painter][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.BeginGroup({0, 0, 100, 100}, 0.25f);
    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    painter.BeginGroup({10, 10, 50, 50}, 0.75f);
    painter.DrawRect({10, 10, 10, 10}, Colour::White);
    painter.EndGroup();
    painter.EndGroup();

    REQUIRE(Kinds(list) == "BDBDEE");
    INFO("the inner group's end carries the inner opacity, the outer's the outer");
    REQUIRE(Command<Fenrir::EndGroup2D>(list, 4).opacity == 0.75f);
    REQUIRE(Command<Fenrir::EndGroup2D>(list, 5).opacity == 0.25f);
}

TEST_CASE("An end with no group open appends nothing", "[painter][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.EndGroup();
    painter.EndMask();

    REQUIRE(list.commands.empty());
}

TEST_CASE("A group's bounds under a rotation are their axis-aligned bounds", "[painter][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.Translate({50, 50});
    painter.Rotate(std::numbers::pi_v<float> * 0.25f);
    painter.BeginGroup({-10, -10, 20, 20});
    painter.EndGroup();

    const float half = 10.0f * std::numbers::sqrt2_v<float>;
    REQUIRE(
        Near(Command<Fenrir::BeginGroup2D>(list, 0).bounds, Rect2D{50 - half, 50 - half, 2 * half, 2 * half}, 1e-3f));
}

TEST_CASE("A group's bounds are clipped to the current scissor", "[painter][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    painter.ClipRect({0, 0, 30, 30});
    painter.BeginGroup({10, 10, 100, 100});
    painter.EndGroup();

    REQUIRE(Near(Command<Fenrir::BeginGroup2D>(list, 0).bounds, Rect2D{10, 10, 20, 20}));
}

TEST_CASE("A blurred group's radius, and a drop shadow's radius and offset, are in logical units", "[painter][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.scale = 2.0f;
    Fenrir::Painter painter(list, config);

    painter.BeginGroup({0, 0, 10, 10}, 1.0f, Fenrir::GroupBlend2D::Normal, Fenrir::GroupFilter::Blur(4.0f));
    painter.EndGroup();
    painter.Scale({1.5f, 1.5f});
    const Colour shadow{0.1f, 0.2f, 0.3f, 0.5f};
    painter.BeginGroup({0, 0, 10, 10}, 1.0f, Fenrir::GroupBlend2D::Normal,
                       Fenrir::GroupFilter::DropShadow({2.0f, -1.0f}, 3.0f, shadow));
    painter.EndGroup();

    const auto& blur = Command<Fenrir::EndGroup2D>(list, 1);
    REQUIRE(blur.filter == Fenrir::GroupFilter2D::Blur);
    REQUIRE(PainterTest::Near(blur.radius, 8.0f));
    const auto& drop = Command<Fenrir::EndGroup2D>(list, 3);
    REQUIRE(drop.filter == Fenrir::GroupFilter2D::DropShadow);
    INFO("under the root scale 2 and the painter's scale 1.5");
    REQUIRE(PainterTest::Near(drop.radius, 9.0f));
    REQUIRE(PainterTest::Near(drop.offset, Vec2(6.0f, -3.0f)));
    INFO("the shadow's colour is the linear, straight-alpha colour given");
    REQUIRE(PainterTest::Near(drop.shadowColour, Vec4(0.1f, 0.2f, 0.3f, 0.5f)));
}

TEST_CASE("A mask's group holds its shape, and its reach brackets the commands it masks", "[painter][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::PainterConfig config = PainterTest::Config();
    config.scale = 2.0f;
    Fenrir::Painter painter(list, config);

    painter.DrawRect({0, 0, 10, 10}, Colour::White);
    Fenrir::Path star;
    star.MoveTo({10, 0});
    star.LineTo({20, 30});
    star.LineTo({0, 10});
    star.LineTo({20, 10});
    star.LineTo({0, 30});
    star.Close();
    painter.BeginMask(star);
    painter.DrawRect({0, 0, 40, 40}, Colour::Red);
    painter.DrawRect({0, 0, 40, 40}, Colour::Green);
    painter.EndMask();
    painter.DrawRect({0, 0, 10, 10}, Colour::White);

    INFO("before it a draw; the mask's group with its shape; the masked draws (extended into one); its end; a draw");
    REQUIRE(Kinds(list) == "DBDEDMD");
    INFO("the group covers the shape's bounds in logical units, and the pixel of fringe about them");
    REQUIRE(Near(Command<Fenrir::BeginGroup2D>(list, 1).bounds, Rect2D{-1, -1, 42, 62}));
    const auto& end = Command<Fenrir::EndGroup2D>(list, 3);
    REQUIRE(end.mask);
    INFO("the shape is a path's triangles, opaque white");
    const auto& shape = PainterTest::Draw(list, 2);
    const std::vector<Fenrir::PainterVertex> vertices = PainterTest::Vertices(list);
    const Fenrir::PainterVertex& first = vertices[static_cast<std::size_t>(shape.baseVertex)];
    REQUIRE(PainterTest::ShapeOf(first) == Fenrir::ShapeKind::Triangle);
    REQUIRE(first.colour == PainterTest::Rgba8(255, 255, 255, 255));
}

TEST_CASE("An EndGroup in a mask's reach closes only a group opened in it, and EndMask ends the reach",
          "[painter][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::Painter painter(list, PainterTest::Config());

    Fenrir::Path disc;
    disc.AddCircle({50, 50}, 20);
    painter.BeginMask(disc);
    painter.BeginGroup({0, 0, 100, 100}, 0.5f);
    painter.DrawRect({0, 0, 100, 100}, Colour::Red);
    painter.EndGroup();
    INFO("an EndGroup inside the reach closes the inner group, not the mask");
    painter.EndGroup();
    painter.EndMask();

    REQUIRE(Kinds(list) == "BDEBDEM");
}
