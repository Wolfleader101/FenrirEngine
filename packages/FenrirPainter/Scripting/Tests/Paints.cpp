// Painting from C# on the window overlay (spec: "Scripting"; seam 1 through the scripting harness and RendererProbe): a
// script is given a painter for a window and layer, paints a rounded rectangle, a stroked path and a word, and when its
// changes play back (as when its system returns) the list reaches the app's Overlay; the renderer's `Overlay 2D` pass
// then draws what the same Painter calls made in C++ draw. A painter ends with its system: a later call on it draws
// nothing.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "Device/CommandList.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirText/TextStyle.hpp"
#include "PainterScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;
using Math::Vec2;

namespace
{
    // The main window, the one the renderer draws.
    constexpr std::uint32_t mainWindow = 1;
    constexpr std::int32_t layer = 3;

    // The fixture's PaintHud, made in C++ with the same values into a list of its own, with the configuration
    // Painter.Overlay gives a painter.
    DrawList2D CppHud(PainterScriptingApp& test)
    {
        DrawList2D list;
        {
            Painter painter(list, {.shader = test.painting.GetShader(),
                                   .blendSpace = BlendSpace2D::Gamma,
                                   .fonts = &test.app.GetResource<FontLibrary>()});
            painter.DrawRoundedRect({10.0f, 10.0f, 120.0f, 40.0f}, CornerRadii::All(8.0f),
                                    Colour{0.1f, 0.2f, 0.4f, 0.8f});
            Path needle;
            needle.MoveTo({20.0f, 45.0f}).QuadTo({60.0f, 5.0f}, {110.0f, 30.0f});
            StrokeStyle stroke;
            stroke.width = 3.0f;
            stroke.cap = LineCap::Round;
            stroke.join = LineJoin::Round;
            painter.StrokePath(needle, stroke, Colour{1.0f, 0.5f, 0.0f, 1.0f});

            Paragraph word;
            TextStyle style;
            style.size = 14.0f;
            style.colour = Colour{1.0f, 1.0f, 1.0f, 1.0f};
            word.SetText("Speed", style);
            word.Layout(test.app.GetResource<FontLibrary>(), UnlimitedWidth);
            painter.DrawText(word, {16.0f, 14.0f});
        }
        return list;
    }
} // namespace

TEST_CASE("A script paints a rounded rectangle, a stroked path and a word on the overlay as C++ does",
          "[painter][scripting]")
{
    PainterScriptingApp test;
    REQUIRE(test.Call<int>(PaintCallsType, "PaintHud", mainWindow, layer) == 1);
    test.ApplyChanges();
    const std::vector<Graphics::CmdDrawIndexed> fromCSharp = test.OverlayDraws();

    const DrawList2D cpp = CppHud(test);
    REQUIRE_FALSE(cpp.commands.empty());
    test.app.GetResource<Overlay>().Submit(WindowId{mainWindow}, layer, cpp);
    const std::vector<Graphics::CmdDrawIndexed> fromCpp = test.OverlayDraws();

    INFO("C# should draw the HUD, every draw as the same C++ calls draw it");
    REQUIRE_FALSE(fromCpp.empty());
    REQUIRE(fromCSharp == fromCpp);
    INFO("The rectangle's quad, the stroke's triangles and the word's five glyph quads are all drawn");
    std::uint32_t indices = 0;
    for (const Graphics::CmdDrawIndexed& draw : fromCSharp)
    {
        indices += draw.indexCount;
    }
    REQUIRE(indices > 6 * (1 + 5));
}

TEST_CASE("A painter ends when its system returns: a later call on it draws nothing", "[painter][scripting]")
{
    PainterScriptingApp test;
    const auto first = test.Call<std::uint64_t>(PaintCallsType, "PainterFor", mainWindow, layer);
    REQUIRE(first != 0);
    test.ApplyChanges();
    test.Call<void>(PaintCallsType, "PaintWith", first);
    test.ApplyChanges();
    {
        INFO("The ended painter's rectangle reaches no overlay");
        REQUIRE(test.OverlayDraws().empty());
    }
    INFO("The next call gets a painter of its own");
    REQUIRE(test.Call<std::uint64_t>(PaintCallsType, "PainterFor", mainWindow, layer) != first);
}
