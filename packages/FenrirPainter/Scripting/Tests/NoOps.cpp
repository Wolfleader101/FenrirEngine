// The no-op rules (spec: "Scripting"), with no .NET: through NativeCallHarness, which runs the text and painter
// extensions as shipped (Painting::ScriptingExtension(text)), each native function is called from C++ by its registered
// name through the registration's own thunk. With no Overlay in the app or no builtin:painter in its asset store, a
// painter whose system returned or that was never given, a path destroyed or never made, or a paragraph the text
// extension does not have, a call does nothing and returns zero, and a debug build warns once per native and reason,
// however often it is called, in the words PainterScripting and TextScripting name.

#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirAssets/Assets.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirPainter/Painting.hpp"
#include "FenrirPainter/Path.hpp"
#include "FenrirPainterScripting/PainterScripting.hpp"
#include "FenrirPainterScripting/ScriptPainter.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirTextScripting/ScriptText.hpp"
#include "FenrirTextScripting/TextScripting.hpp"
#include "FenrirTextScripting/TextScriptingState.hpp"
#include "PainterScriptingState.hpp"

namespace
{
    using namespace Fenrir;
    using namespace std::string_view_literals;
    using ScriptPainter::PainterId;
    using ScriptPainter::Path2D;

    const Rect2D box{0.0f, 0.0f, 10.0f, 10.0f};
    const CornerRadii corners = CornerRadii::All(2.0f);
    const ScriptPainter::Paint paint;
    const ScriptPainter::StrokeStyle stroke;
    const Math::Vec2 origin{0.0f, 0.0f};
    const std::array<Math::Vec2, 3> triangle{Math::Vec2(0.0f, 0.0f), Math::Vec2(4.0f, 0.0f), Math::Vec2(0.0f, 4.0f)};

    // The main window, the one the renderer draws.
    constexpr WindowId mainWindow{1};

    // The text extension, and the painter's sharing its state.
    std::vector<ScriptingExtension> Extensions()
    {
        ScriptingExtension text = Text::ScriptingExtension();
        ScriptingExtension painter = Painting::ScriptingExtension(text);
        return {std::move(text), std::move(painter)};
    }

    // A painter over the main window's overlay at `layer`, given through the native as C#'s Painter.Overlay gives
    // one: none when it gives none.
    PainterId OverlayPainter(Testing::NativeCallHarness& harness, std::int32_t layer = 0)
    {
        PainterId painter;
        harness.Call<void>("PainterNative.Overlay", mainWindow, layer, &painter);
        return painter;
    }

    // What a painter needs in the app: assets, the painter's shader and an Overlay.
    struct Installed
    {
        explicit Installed(App& app) : assets(Assets::Install(app, {})), painting(Painting::Install(app))
        {
            app.AddResource<Fenrir::Overlay>();
        }

        Assets assets;
        Painting painting;
    };

    // A path made through the native, as C#'s Path2D.Create makes one.
    Path2D CreatePath(Testing::NativeCallHarness& harness)
    {
        Path2D path;
        harness.Call<void>("PainterNative.CreatePath", &path);
        return path;
    }

    // Calls every draw and state native on `painter`, with `path` where one takes a path.
    void DrawEverything(Testing::NativeCallHarness& harness, const PainterId& painter, const Path2D& path)
    {
        const Transform2D identity;
        const Border border = Border::Uniform(1.0f, Colour::White);
        const Shadow shadow;
        const ScriptPainter::ImageDraw image;
        const TextShadow textShadow;
        const GroupFilter filter;
        harness.Call<void>("PainterNative.Save", &painter);
        harness.Call<void>("PainterNative.Translate", &painter, &origin);
        harness.Call<void>("PainterNative.Scale", &painter, &origin);
        harness.Call<void>("PainterNative.Rotate", &painter, 1.0f);
        harness.Call<void>("PainterNative.Concat", &painter, &identity);
        harness.Call<void>("PainterNative.ClipRect", &painter, &box);
        harness.Call<void>("PainterNative.ClipRoundedRect", &painter, &box, &corners);
        harness.Call<void>("PainterNative.DrawRect", &painter, &box, &paint);
        harness.Call<void>("PainterNative.DrawRoundedRect", &painter, &box, &corners, &paint);
        harness.Call<void>("PainterNative.DrawBorder", &painter, &box, &corners, &border);
        harness.Call<void>("PainterNative.DrawShadow", &painter, &box, &corners, &shadow);
        harness.Call<void>("PainterNative.DrawCircle", &painter, &origin, 2.0f, &paint);
        harness.Call<void>("PainterNative.DrawArc", &painter, &origin, 2.0f, 0.0f, 1.0f, 1.0f, &paint);
        harness.Call<void>("PainterNative.DrawRing", &painter, &origin, 2.0f, 1.0f, &paint);
        harness.Call<void>("PainterNative.DrawImage", &painter, &box, &image);
        harness.Call<void>("PainterNative.FillPath", &painter, &path, &paint);
        harness.Call<void>("PainterNative.FillPolygon", &painter, triangle.data(), std::int32_t{3}, &paint);
        harness.Call<void>("PainterNative.StrokePath", &painter, &path, &stroke, &paint);
        harness.Call<void>("PainterNative.StrokePolyline", &painter, triangle.data(), std::int32_t{3}, true, &stroke,
                           &paint);
        const ScriptText::Paragraph none;
        harness.Call<void>("PainterNative.DrawText", &painter, &none, &origin);
        harness.Call<void>("PainterNative.DrawTextShadowed", &painter, &none, &origin, &textShadow);
        harness.Call<void>("PainterNative.BeginGroup", &painter, &box, 1.0f, GroupBlend2D::Normal, &filter);
        harness.Call<void>("PainterNative.EndGroup", &painter);
        harness.Call<void>("PainterNative.BeginMask", &painter, &path);
        harness.Call<void>("PainterNative.EndMask", &painter);
        harness.Call<void>("PainterNative.BackdropBlur", &painter, &box, 2.0f);
        harness.Call<void>("PainterNative.Restore", &painter);
    }

    // The natives that take a painter first and return nothing.
    constexpr std::array painterNatives{
        "PainterNative.Save"sv,
        "PainterNative.Translate"sv,
        "PainterNative.Scale"sv,
        "PainterNative.Rotate"sv,
        "PainterNative.Concat"sv,
        "PainterNative.ClipRect"sv,
        "PainterNative.ClipRoundedRect"sv,
        "PainterNative.DrawRect"sv,
        "PainterNative.DrawRoundedRect"sv,
        "PainterNative.DrawBorder"sv,
        "PainterNative.DrawShadow"sv,
        "PainterNative.DrawCircle"sv,
        "PainterNative.DrawArc"sv,
        "PainterNative.DrawRing"sv,
        "PainterNative.DrawImage"sv,
        "PainterNative.FillPath"sv,
        "PainterNative.FillPolygon"sv,
        "PainterNative.StrokePath"sv,
        "PainterNative.StrokePolyline"sv,
        "PainterNative.DrawText"sv,
        "PainterNative.DrawTextShadowed"sv,
        "PainterNative.BeginGroup"sv,
        "PainterNative.EndGroup"sv,
        "PainterNative.BeginMask"sv,
        "PainterNative.EndMask"sv,
        "PainterNative.BackdropBlur"sv,
        "PainterNative.Restore"sv,
    };
} // namespace

TEST_CASE("With no Overlay or no builtin:painter in the app, no painter is given, saying so once",
          "[painter][scripting][noops]")
{
    Testing::NativeCallHarness harness(Extensions());
    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": no Overlay, so no painter");
        REQUIRE(OverlayPainter(harness).value == 0);
    }
    harness.GetApp().AddResource<Fenrir::Overlay>();
    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": an Overlay but no asset store, so no painter");
        REQUIRE(OverlayPainter(harness).value == 0);
    }
    const Assets assets = Assets::Install(harness.GetApp(), {});
    REQUIRE(OverlayPainter(harness).value == 0);
    REQUIRE(harness.PendingChanges() == 0);
    INFO("Overlay should warn once for each reason in a debug build, and never in a release build");
    REQUIRE(harness.WarnedOnce("PainterNative.Overlay", PainterScripting::NoOverlay));
    REQUIRE(harness.WarnedOnce("PainterNative.Overlay", PainterScripting::NoPainterShader));
}

TEST_CASE("One system's calls for a window and layer share a painter, which ends when the system returns",
          "[painter][scripting][noops]")
{
    Testing::NativeCallHarness harness(Extensions());
    const Installed installed(harness.GetApp());
    const PainterId painter = OverlayPainter(harness);
    REQUIRE(painter.value != 0);
    REQUIRE(OverlayPainter(harness).value == painter.value);
    const PainterId above = OverlayPainter(harness, 1);
    REQUIRE(above.value != 0);
    REQUIRE(above.value != painter.value);
    INFO("Each painter's submission waits for the system to return");
    REQUIRE(harness.PendingChanges() == 2);

    REQUIRE(harness.ApplyChanges().applied == 2);
    const PainterId next = OverlayPainter(harness);
    REQUIRE(next.value != 0);
    REQUIRE(next.value != painter.value);
    REQUIRE(next.value != above.value);
}

TEST_CASE("A painter whose submission never played back ends with its frame and keeps no layer",
          "[painter][scripting][noops]")
{
    Testing::NativeCallHarness harness(Extensions());
    const Installed installed(harness.GetApp());
    const PainterId discarded = OverlayPainter(harness);
    REQUIRE(discarded.value != 0);
    // Its system threw, say: its changes never play back, so its submission never runs.
    harness.GetApp().RunFrame(1.0 / 60.0);

    const PainterId next = OverlayPainter(harness);
    {
        INFO("The next frame's call gets a painter of its own, not the one whose submission was lost");
        REQUIRE(next.value != 0);
        REQUIRE(next.value != discarded.value);
    }
    const Rect2D square{0.0f, 0.0f, 4.0f, 4.0f};
    harness.Call<void>("PainterNative.DrawRect", &discarded, &square, &paint);
    INFO("Drawing with the lost painter does nothing, and warns once in a debug build");
    REQUIRE(harness.WarnedOnce("PainterNative.DrawRect", PainterScripting::NoPainter));
}

TEST_CASE("A painter that ended or was never given draws nothing and says so once per native",
          "[painter][scripting][noops]")
{
    Testing::NativeCallHarness harness(Extensions());
    const Installed installed(harness.GetApp());
    const PainterId ended = OverlayPainter(harness);
    REQUIRE(ended.value != 0);
    REQUIRE(harness.ApplyChanges().applied == 1);
    const Path2D path = CreatePath(harness);

    for (const PainterId& painter : {ended, PainterId{}})
    {
        for (int call = 0; call < 2; ++call)
        {
            INFO("Painter " << painter.value << ", call " << call);
            DrawEverything(harness, painter, path);
        }
    }
    for (const std::string_view native : painterNatives)
    {
        INFO(native << " should warn once for both painters in a debug build, and never in a release build");
        REQUIRE(harness.WarnedOnce(native, PainterScripting::NoPainter));
    }
    INFO("A missing painter is checked first: nothing warns of the path or the paragraph");
    REQUIRE(harness.Warnings("PainterNative.FillPath", PainterScripting::NoPath) == 0);
    REQUIRE(harness.Warnings("PainterNative.DrawText", TextScripting::NoParagraph) == 0);
}

TEST_CASE("A path destroyed or never made does nothing, returns zero and says so once per native",
          "[painter][scripting][noops]")
{
    Testing::NativeCallHarness harness(Extensions());
    const Installed installed(harness.GetApp());
    const PainterId painter = OverlayPainter(harness);
    REQUIRE(painter.value != 0);
    const Path2D destroyed = CreatePath(harness);
    REQUIRE(harness.Call<bool>("PainterNative.DestroyPath", &destroyed));
    REQUIRE(CreatePath(harness).id != destroyed.id);

    for (const Path2D& path : {destroyed, Path2D{}})
    {
        for (int call = 0; call < 2; ++call)
        {
            INFO("Path " << path.id << ", call " << call);
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.DestroyPath", &path));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.ClearPath", &path));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.MoveTo", &path, &origin));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.LineTo", &path, &origin));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.QuadTo", &path, &origin, &origin));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.CubicTo", &path, &origin, &origin, &origin));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.Arc", &path, &origin, 1.0f, 0.0f, 1.0f));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.ArcTo", &path, &origin, &origin, 1.0f));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.Close", &path));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.AddRect", &path, &box));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.AddRoundedRect", &path, &box, &corners));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.AddCircle", &path, &origin, 1.0f));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.AddEllipse", &path, &origin, &origin));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.AddPolygon", &path, triangle.data(), std::int32_t{3}));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.AddPolyline", &path, triangle.data(), std::int32_t{3}));
            REQUIRE_FALSE(harness.Call<bool>("PainterNative.SetFillRule", &path, FillRule::EvenOdd));
            harness.Call<void>("PainterNative.FillPath", &painter, &path, &paint);
            harness.Call<void>("PainterNative.StrokePath", &painter, &path, &stroke, &paint);
            harness.Call<void>("PainterNative.BeginMask", &painter, &path);
        }
    }
    for (const std::string_view native :
         {"PainterNative.DestroyPath"sv, "PainterNative.ClearPath"sv, "PainterNative.MoveTo"sv,
          "PainterNative.LineTo"sv, "PainterNative.QuadTo"sv, "PainterNative.CubicTo"sv, "PainterNative.Arc"sv,
          "PainterNative.ArcTo"sv, "PainterNative.Close"sv, "PainterNative.AddRect"sv, "PainterNative.AddRoundedRect"sv,
          "PainterNative.AddCircle"sv, "PainterNative.AddEllipse"sv, "PainterNative.AddPolygon"sv,
          "PainterNative.AddPolyline"sv, "PainterNative.SetFillRule"sv, "PainterNative.FillPath"sv,
          "PainterNative.StrokePath"sv, "PainterNative.BeginMask"sv})
    {
        INFO(native << " should warn once for both paths in a debug build, and never in a release build");
        REQUIRE(harness.WarnedOnce(native, PainterScripting::NoPath));
    }
}

TEST_CASE("Text for a paragraph the text extension does not have draws nothing and says so once",
          "[painter][scripting][noops]")
{
    Testing::NativeCallHarness harness(Extensions());
    const Installed installed(harness.GetApp());
    const PainterId painter = OverlayPainter(harness);
    REQUIRE(painter.value != 0);
    const ScriptText::Paragraph none;
    const TextShadow shadow;
    for (int call = 0; call < 2; ++call)
    {
        harness.Call<void>("PainterNative.DrawText", &painter, &none, &origin);
        harness.Call<void>("PainterNative.DrawTextShadowed", &painter, &none, &origin, &shadow);
    }
    REQUIRE(harness.WarnedOnce("PainterNative.DrawText", TextScripting::NoParagraph));
    REQUIRE(harness.WarnedOnce("PainterNative.DrawTextShadowed", TextScripting::NoParagraph));
}

TEST_CASE("A painter extension made without the text extension draws no text and says so once",
          "[painter][scripting][noops]")
{
    Testing::NativeCallHarness harness({Painting::ScriptingExtension(ScriptingExtension{})});
    const Installed installed(harness.GetApp());
    const PainterId painter = OverlayPainter(harness);
    REQUIRE(painter.value != 0);
    const ScriptText::Paragraph none;
    for (int call = 0; call < 2; ++call)
    {
        harness.Call<void>("PainterNative.DrawText", &painter, &none, &origin);
    }
    REQUIRE(harness.WarnedOnce("PainterNative.DrawText", PainterScripting::NoTextExtension));
}
