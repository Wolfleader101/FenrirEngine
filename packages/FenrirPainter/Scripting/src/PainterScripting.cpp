#include "FenrirPainterScripting/PainterScripting.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirPainter/Paint.hpp"
#include "FenrirPainter/Painter.hpp"
#include "FenrirPainter/Path.hpp"
#include "FenrirPainterScripting/PainterScriptingState.hpp"
#include "FenrirPainterScripting/ScriptPainter.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirTextScripting/ScriptText.hpp"
#include "Generated/FenrirPainterNatives.hpp"

// Everything C# sees of the painter is generated (api/FenrirPainter.binding.toml): the mirrors of ScriptPainter.hpp's
// structs and of the painter's plain data, their mappings, checked against the Fenrir.Painter contract whenever a
// script assembly loads, and from the native functions below (described into api/FenrirPainter.natives.json) their C#
// declarations, PainterNative, the Painter facade and the Path2D mirror's methods, and this registration of them all.

namespace Fenrir
{
    namespace
    {
        /** @brief A gradient's stops, the first `stopCount` of the eight it has room for. */
        std::vector<GradientStop> StopsOf(const ScriptPainter::Paint& paint)
        {
            const std::size_t count = std::clamp<std::int32_t>(paint.stopCount, 0, 8);
            return {std::begin(paint.stops), std::begin(paint.stops) + count};
        }

        /** @brief The painter's Paint for C#'s. */
        Paint PaintOf(const ScriptPainter::Paint& paint)
        {
            switch (paint.kind)
            {
            case ScriptPainter::PaintKind::LinearGradient:
                return LinearGradient{paint.start, paint.end, StopsOf(paint), paint.spread, paint.transform};
            case ScriptPainter::PaintKind::RadialGradient:
                return RadialGradient{paint.centre,   paint.radius, paint.focal,
                                      StopsOf(paint), paint.spread, paint.transform};
            case ScriptPainter::PaintKind::ImagePattern:
                return ImagePattern{paint.image, paint.sampler, paint.rect, paint.colour, paint.transform};
            case ScriptPainter::PaintKind::Solid:
                break;
            }
            return paint.colour;
        }

        /** @brief The painter's StrokeStyle for C#'s: its first `dashCount` dashes of the eight it has room for. */
        StrokeStyle StrokeOf(const ScriptPainter::StrokeStyle& style)
        {
            const std::size_t count = std::clamp<std::int32_t>(style.dashCount, 0, 8);
            return {.width = style.width,
                    .cap = style.cap,
                    .join = style.join,
                    .miterLimit = style.miterLimit,
                    .dashes = {std::begin(style.dashes), std::begin(style.dashes) + count},
                    .dashOffset = style.dashOffset};
        }

        /** @brief The painter's ImageDraw for C#'s. */
        ImageDraw ImageOf(const ScriptPainter::ImageDraw& image)
        {
            return {.image = image.image,
                    .sampler = image.sampler,
                    .nineSlice = image.nineSliced ? std::optional<NineSlice>(image.nineSlice) : std::nullopt,
                    .tint = image.tint};
        }
    } // namespace

    // The native functions, named apart from the painter's own. Each is the Painter call of its name on an overlay
    // painter a script was given, or the Path call of its name on a path it made.
    namespace PainterNatives
    {
        /**
         * @brief A painter over `window`'s overlay at `layer`, for the C# system that calls it: what it paints is
         * submitted to the app's Overlay when that system's changes play back (as it returns, or as its hosted stage's
         * group ends; Overlay::Submit), and the painter ends. Calls for the same window and layer before then give the
         * same painter; one whose changes were discarded (its system threw) paints nothing after its frame. It draws in
         * the window's logical units at a root scale of 1 and blends in gamma space, as UI on the window does, so a
         * paragraph it draws is laid out at its sizes in logical pixels.
         * @param window The window, as the input and window contracts name it.
         * @param layer The overlay layer: lower layers draw first.
         * @param[out] painter The painter.
         * @return The painter; none when the app has no Overlay (the renderer is not installed) or its asset store has
         * no builtin:painter.
         * @facade Painter.Overlay returns painter
         */
        void Overlay(const CallContext& context, PainterScripting& painting, WindowId window, std::int32_t layer,
                     ScriptPainter::PainterId* painter)
        {
            const NativeGuard guard(context, painting.warnings);
            App& app = context.GetApp();
            if (app.TryGetResource<Fenrir::Overlay>() == nullptr)
            {
                guard.Warn(std::string(PainterScripting::NoOverlay));
                return;
            }
            const std::uint64_t frame = PainterScripting::FrameOf(app);
            painting.DropOtherFrames(frame);
            const std::pair<std::uint32_t, std::int32_t> key{window.value, layer};
            if (const auto given = painting.overlays.find(key); given != painting.overlays.end())
            {
                painter->value = given->second;
                return;
            }
            AssetStore* store = app.TryGetResource<AssetStore>();
            const std::optional<AssetHandle<Shader>> shader =
                store == nullptr ? std::nullopt
                                 : std::optional(store->Load<Shader>("builtin:painter", AssetOwner::App()).handle);
            if (!shader || !store->IsValid(*shader))
            {
                guard.Warn(std::string(PainterScripting::NoPainterShader));
                return;
            }
            auto recording = std::make_unique<PainterScripting::Recording>();
            recording->window = window;
            recording->layer = layer;
            recording->frame = frame;
            recording->painter.emplace(recording->list, PainterConfig{.shader = *shader,
                                                                      .blendSpace = BlendSpace2D::Gamma,
                                                                      .fonts = app.TryGetResource<FontLibrary>()});
            const std::uint64_t id = painting.nextPainter++;
            painting.painters.emplace(id, std::move(recording));
            painting.overlays.emplace(key, id);
            painter->value = id;
            PainterScripting* state = &painting;
            context.Changes().RecordCall([state, id](App& playing) { state->Submit(playing, id); });
        }

        /**
         * @brief Pushes the transform and the clip (Painter::Save).
         * @facade Painter.Save
         */
        void Save(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->Save();
            }
        }

        /**
         * @brief Pops the transform and the clip Save pushed last; with nothing saved, does nothing (Painter::Restore).
         * @facade Painter.Restore
         */
        void Restore(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->Restore();
            }
        }

        /**
         * @brief Moves what is drawn next by `offset`, in UI units, before the current transform (Painter::Translate).
         * @facade Painter.Translate
         */
        void Translate(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                       const Math::Vec2* offset)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->Translate(*offset);
            }
        }

        /**
         * @brief Scales what is drawn next by `factors`, about the current origin (Painter::Scale).
         * @facade Painter.Scale
         */
        void Scale(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                   const Math::Vec2* factors)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->Scale(*factors);
            }
        }

        /**
         * @brief Turns what is drawn next by `radians` about the current origin, clockwise on screen (Painter::Rotate).
         * @facade Painter.Rotate
         */
        void Rotate(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                    float radians)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->Rotate(radians);
            }
        }

        /**
         * @brief Applies `transform` before the current one (Painter::Concat).
         * @facade Painter.Concat
         */
        void Concat(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                    const Transform2D* transform)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->Concat(*transform);
            }
        }

        /**
         * @brief Clips to `rect` under the current transform, intersected with the clips already set
         * (Painter::ClipRect).
         * @facade Painter.ClipRect
         */
        void ClipRect(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                      const Rect2D* rect)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->ClipRect(*rect);
            }
        }

        /**
         * @brief Clips to `rect` with corners rounded by `radii`, intersected with the clips already set
         * (Painter::ClipRoundedRect).
         * @facade Painter.ClipRoundedRect
         */
        void ClipRoundedRect(const CallContext& context, PainterScripting& painting,
                             const ScriptPainter::PainterId* painter, const Rect2D* rect, const CornerRadii* radii)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->ClipRoundedRect(*rect, *radii);
            }
        }

        /**
         * @brief `rect` filled with `paint` (Painter::DrawRect).
         * @facade Painter.DrawRect
         */
        void DrawRect(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                      const Rect2D* rect, const ScriptPainter::Paint* paint)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->DrawRect(*rect, PaintOf(*paint));
            }
        }

        /**
         * @brief `rect` with corners rounded by `radii`, filled with `paint`; radii larger than the rectangle allows
         * are scaled down together, as CSS does (Painter::DrawRoundedRect).
         * @facade Painter.DrawRoundedRect
         */
        void DrawRoundedRect(const CallContext& context, PainterScripting& painting,
                             const ScriptPainter::PainterId* painter, const Rect2D* rect, const CornerRadii* radii,
                             const ScriptPainter::Paint* paint)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->DrawRoundedRect(*rect, *radii, PaintOf(*paint));
            }
        }

        /**
         * @brief The border inside `rect`, each side its own width and colour, split at the corners' diagonals
         * (Painter::DrawBorder).
         * @facade Painter.DrawBorder
         */
        void DrawBorder(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                        const Rect2D* rect, const CornerRadii* radii, const Border* border)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->DrawBorder(*rect, *radii, *border);
            }
        }

        /**
         * @brief The box shadow of the rounded rectangle `rect` (Painter::DrawShadow).
         * @facade Painter.DrawShadow
         */
        void DrawShadow(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                        const Rect2D* rect, const CornerRadii* radii, const Shadow* shadow)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->DrawShadow(*rect, *radii, *shadow);
            }
        }

        /**
         * @brief The disc of `radius` about `centre`, filled with `paint` (Painter::DrawCircle).
         * @facade Painter.DrawCircle
         */
        void DrawCircle(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                        const Math::Vec2* centre, float radius, const ScriptPainter::Paint* paint)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->DrawCircle(*centre, radius, PaintOf(*paint));
            }
        }

        /**
         * @brief A band `width` wide centred on the circle of `radius`, from `startRadians` clockwise through
         * `sweepRadians`, with flat ends; a sweep of a whole turn or more is a ring (Painter::DrawArc).
         * @facade Painter.DrawArc
         */
        void DrawArc(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                     const Math::Vec2* centre, float radius, float startRadians, float sweepRadians, float width,
                     const ScriptPainter::Paint* paint)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->DrawArc(*centre, radius, startRadians, sweepRadians, width, PaintOf(*paint));
            }
        }

        /**
         * @brief The band between the circles of `outerRadius` and `innerRadius` about `centre` (Painter::DrawRing).
         * @facade Painter.DrawRing
         */
        void DrawRing(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                      const Math::Vec2* centre, float outerRadius, float innerRadius, const ScriptPainter::Paint* paint)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->DrawRing(*centre, outerRadius, innerRadius, PaintOf(*paint));
            }
        }

        /**
         * @brief The image stretched over `rect`, or nine-sliced into it (Painter::DrawImage).
         * @facade Painter.DrawImage
         */
        void DrawImage(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                       const Rect2D* rect, const ScriptPainter::ImageDraw* image)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->DrawImage(*rect, ImageOf(*image));
            }
        }

        /**
         * @brief The region `path` covers by its fill rule, filled with `paint` (Painter::FillPath).
         * @facade Painter.FillPath
         */
        void FillPath(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                      const ScriptPainter::Path2D* path, const ScriptPainter::Paint* paint)
        {
            Painter* found = painting.PainterOf(context, *painter);
            const Path* filled = found == nullptr ? nullptr : painting.FindPath(context, *path);
            if (filled != nullptr)
            {
                found->FillPath(*filled, PaintOf(*paint));
            }
        }

        /**
         * @brief The closed polygon through `points`, filled non-zero with `paint`: FillPath of a path made by one
         * AddPolygon.
         * @param points The polygon's corners, in UI units.
         * @facade Painter.FillPath
         */
        void FillPolygon(const CallContext& context, PainterScripting& painting,
                         const ScriptPainter::PainterId* painter, const Math::Vec2* points, std::int32_t count,
                         const ScriptPainter::Paint* paint)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->FillPath(Path().AddPolygon(SpanOf(points, count)), PaintOf(*paint));
            }
        }

        /**
         * @brief `path` stroked with `style` and `paint` (Painter::StrokePath).
         * @facade Painter.StrokePath
         */
        void StrokePath(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                        const ScriptPainter::Path2D* path, const ScriptPainter::StrokeStyle* style,
                        const ScriptPainter::Paint* paint)
        {
            Painter* found = painting.PainterOf(context, *painter);
            const Path* stroked = found == nullptr ? nullptr : painting.FindPath(context, *path);
            if (stroked != nullptr)
            {
                found->StrokePath(*stroked, StrokeOf(*style), PaintOf(*paint));
            }
        }

        /**
         * @brief The line through `points`, back to the first when `closed`, stroked with `style` and `paint`:
         * StrokePath of a path made by one AddPolyline, or AddPolygon when closed.
         * @param points The line's points, in UI units.
         * @param closed Whether the line closes back to its first point.
         * @facade Painter.StrokePath
         */
        void StrokePolyline(const CallContext& context, PainterScripting& painting,
                            const ScriptPainter::PainterId* painter, const Math::Vec2* points, std::int32_t count,
                            bool closed, const ScriptPainter::StrokeStyle* style, const ScriptPainter::Paint* paint)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                const std::span<const Math::Vec2> line = SpanOf(points, count);
                found->StrokePath(closed ? Path().AddPolygon(line) : Path().AddPolyline(line), StrokeOf(*style),
                                  PaintOf(*paint));
            }
        }

        /**
         * @brief Every glyph run of `paragraph`, a paragraph of the text extension's, from `point`, its top-left in UI
         * units (Painter::DrawText). The paragraph's positions are target pixels: lay it out with its sizes multiplied
         * by the root scale and the target's pixels per unit. One with text that was never laid out is laid out with
         * the app's FontLibrary to no width limit.
         * @param paragraph The paragraph drawn.
         * @param point Where its top-left is drawn, in UI units.
         * @facade Painter.DrawText
         */
        void DrawText(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                      const ScriptText::Paragraph* paragraph, const Math::Vec2* point)
        {
            Painter* found = painting.PainterOf(context, *painter);
            Paragraph* drawn = found == nullptr ? nullptr : painting.FindParagraph(context, *paragraph);
            if (drawn != nullptr)
            {
                found->DrawText(*drawn, *point);
            }
        }

        /**
         * @brief `paragraph` with `shadow` drawn first: every run again, moved and in the shadow's colour
         * (Painter::DrawText).
         * @param paragraph The paragraph drawn.
         * @param point Where its top-left is drawn, in UI units.
         * @param shadow The shadow's offset and colour.
         * @facade Painter.DrawText
         */
        void DrawTextShadowed(const CallContext& context, PainterScripting& painting,
                              const ScriptPainter::PainterId* painter, const ScriptText::Paragraph* paragraph,
                              const Math::Vec2* point, const TextShadow* shadow)
        {
            Painter* found = painting.PainterOf(context, *painter);
            Paragraph* drawn = found == nullptr ? nullptr : painting.FindParagraph(context, *paragraph);
            if (drawn != nullptr)
            {
                found->DrawText(*drawn, *point, *shadow);
            }
        }

        /**
         * @brief Begins a group: what is drawn up to its EndGroup draws into an image of its own covering `bounds`,
         * then composited over what is under it at `opacity`, by `blend`, through `filter` (Painter::BeginGroup).
         * @facade Painter.BeginGroup
         */
        void BeginGroup(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                        const Rect2D* bounds, float opacity, GroupBlend2D blend, const GroupFilter* filter)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->BeginGroup(*bounds, opacity, blend, *filter);
            }
        }

        /**
         * @brief Ends the innermost group, if it was opened inside the innermost mask's reach or no mask is open
         * (Painter::EndGroup).
         * @facade Painter.EndGroup
         */
        void EndGroup(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->EndGroup();
            }
        }

        /**
         * @brief Begins a mask: what is drawn up to EndMask has its colour multiplied by the coverage of `mask` filled
         * opaque (Painter::BeginMask).
         * @facade Painter.BeginMask
         */
        void BeginMask(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter,
                       const ScriptPainter::Path2D* mask)
        {
            Painter* found = painting.PainterOf(context, *painter);
            const Path* filled = found == nullptr ? nullptr : painting.FindPath(context, *mask);
            if (filled != nullptr)
            {
                found->BeginMask(*filled);
            }
        }

        /**
         * @brief Ends the innermost mask's reach, if no group opened inside it is still open (Painter::EndMask).
         * @facade Painter.EndMask
         */
        void EndMask(const CallContext& context, PainterScripting& painting, const ScriptPainter::PainterId* painter)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->EndMask();
            }
        }

        /**
         * @brief Blurs what is already drawn under `rect` by `radius`, a Gaussian's standard deviation in UI units, as
         * a frosted panel's background (Painter::BackdropBlur).
         * @facade Painter.BackdropBlur
         */
        void BackdropBlur(const CallContext& context, PainterScripting& painting,
                          const ScriptPainter::PainterId* painter, const Rect2D* rect, float radius)
        {
            if (Painter* found = painting.PainterOf(context, *painter))
            {
                found->BackdropBlur(*rect, radius);
            }
        }

        /**
         * @brief A new empty path, filled non-zero, kept until it is destroyed.
         * @param[out] path The path made.
         * @return The path made.
         * @facade Path2D.Create returns path
         */
        void CreatePath(const CallContext& context, PainterScripting& painting, ScriptPainter::Path2D* path)
        {
            static_cast<void>(context);
            const std::uint64_t id = painting.nextPath++;
            painting.paths.emplace(id, Path());
            path->id = id;
        }

        /**
         * @brief Destroys the path: its calls do nothing from now on, and its id is never handed out again.
         * @return Whether there was a path to destroy.
         * @facade Path2D.Destroy
         */
        bool DestroyPath(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path)
        {
            if (painting.FindPath(context, *path) == nullptr)
            {
                return false;
            }
            painting.paths.erase(path->id);
            return true;
        }

        /**
         * @brief Removes every step, keeping the fill rule.
         * @return Whether the path exists.
         * @facade Path2D.Clear
         */
        bool ClearPath(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            const FillRule rule = found->GetFillRule();
            *found = Path();
            found->SetFillRule(rule);
            return true;
        }

        /**
         * @brief Starts a subpath at `point` (Path::MoveTo).
         * @return Whether the path exists.
         * @facade Path2D.MoveTo
         */
        bool MoveTo(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                    const Math::Vec2* point)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->MoveTo(*point);
            return true;
        }

        /**
         * @brief A line to `point` (Path::LineTo).
         * @return Whether the path exists.
         * @facade Path2D.LineTo
         */
        bool LineTo(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                    const Math::Vec2* point)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->LineTo(*point);
            return true;
        }

        /**
         * @brief A quadratic curve through `control` to `end` (Path::QuadTo).
         * @return Whether the path exists.
         * @facade Path2D.QuadTo
         */
        bool QuadTo(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                    const Math::Vec2* control, const Math::Vec2* end)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->QuadTo(*control, *end);
            return true;
        }

        /**
         * @brief A cubic curve through `control1` and `control2` to `end` (Path::CubicTo).
         * @return Whether the path exists.
         * @facade Path2D.CubicTo
         */
        bool CubicTo(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                     const Math::Vec2* control1, const Math::Vec2* control2, const Math::Vec2* end)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->CubicTo(*control1, *control2, *end);
            return true;
        }

        /**
         * @brief The arc of the circle of `radius` about `centre` from `startRadians` clockwise through
         * `sweepRadians` (negative sweeps go anticlockwise), with a line to its start from the current point if there
         * is one (Path::Arc).
         * @return Whether the path exists.
         * @facade Path2D.Arc
         */
        bool Arc(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                 const Math::Vec2* centre, float radius, float startRadians, float sweepRadians)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->Arc(*centre, radius, startRadians, sweepRadians);
            return true;
        }

        /**
         * @brief The browser's arcTo: a line from the current point towards `corner` and an arc of `radius` round it,
         * tangent to the lines to `corner` and on to `towards` (Path::ArcTo).
         * @return Whether the path exists.
         * @facade Path2D.ArcTo
         */
        bool ArcTo(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                   const Math::Vec2* corner, const Math::Vec2* towards, float radius)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->ArcTo(*corner, *towards, radius);
            return true;
        }

        /**
         * @brief Closes the current subpath with a line back to its start (Path::Close).
         * @return Whether the path exists.
         * @facade Path2D.Close
         */
        bool Close(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->Close();
            return true;
        }

        /**
         * @brief The rectangle `rect`, a closed subpath wound clockwise (Path::AddRect).
         * @return Whether the path exists.
         * @facade Path2D.AddRect
         */
        bool AddRect(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                     const Rect2D* rect)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->AddRect(*rect);
            return true;
        }

        /**
         * @brief `rect` with corners rounded by `radii`, scaled down together when they do not fit
         * (Path::AddRoundedRect).
         * @return Whether the path exists.
         * @facade Path2D.AddRoundedRect
         */
        bool AddRoundedRect(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                            const Rect2D* rect, const CornerRadii* radii)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->AddRoundedRect(*rect, *radii);
            return true;
        }

        /**
         * @brief The circle of `radius` about `centre` (Path::AddCircle).
         * @return Whether the path exists.
         * @facade Path2D.AddCircle
         */
        bool AddCircle(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                       const Math::Vec2* centre, float radius)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->AddCircle(*centre, radius);
            return true;
        }

        /**
         * @brief The ellipse of `radii` (half its width and height) about `centre` (Path::AddEllipse).
         * @return Whether the path exists.
         * @facade Path2D.AddEllipse
         */
        bool AddEllipse(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                        const Math::Vec2* centre, const Math::Vec2* radii)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->AddEllipse(*centre, *radii);
            return true;
        }

        /**
         * @brief The closed polygon through `points` (Path::AddPolygon).
         * @param points The polygon's corners.
         * @return Whether the path exists.
         * @facade Path2D.AddPolygon
         */
        bool AddPolygon(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                        const Math::Vec2* points, std::int32_t count)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->AddPolygon(SpanOf(points, count));
            return true;
        }

        /**
         * @brief The open polyline through `points` (Path::AddPolyline).
         * @param points The line's points.
         * @return Whether the path exists.
         * @facade Path2D.AddPolyline
         */
        bool AddPolyline(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                         const Math::Vec2* points, std::int32_t count)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->AddPolyline(SpanOf(points, count));
            return true;
        }

        /**
         * @brief Sets the rule its fills use (Path::SetFillRule).
         * @return Whether the path exists.
         * @facade Path2D.SetFillRule
         */
        bool SetFillRule(const CallContext& context, PainterScripting& painting, const ScriptPainter::Path2D* path,
                         FillRule rule)
        {
            Path* found = painting.FindPath(context, *path);
            if (found == nullptr)
            {
                return false;
            }
            found->SetFillRule(rule);
            return true;
        }
    } // namespace PainterNatives

    Fenrir::ScriptingExtension Painting::ScriptingExtension(const Fenrir::ScriptingExtension& text)
    {
        auto state = std::make_shared<PainterScripting>();
        state->text = text.GetState<TextScripting>();
        return Generated::MakePainterExtension(std::move(state));
    }
} // namespace Fenrir
