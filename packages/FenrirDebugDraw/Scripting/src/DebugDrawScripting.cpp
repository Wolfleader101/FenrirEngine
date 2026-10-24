#include "FenrirDebugDrawScripting/DebugDrawScripting.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include "FenrirApp/App.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScene/Scene.hpp"

namespace Fenrir
{
    namespace
    {
        // ShapeOptions and TextOptions as C# passes them (Fenrir.DebugDraw's NativeShapeOptions and
        // NativeTextOptions), without the group, which comes as text of its own. The entity is its 32-bit value, and
        // the depth mode is widened to 32 bits so neither side has padding to agree on.
        struct ScriptShapeOptions
        {
            float duration;
            std::uint32_t relativeTo;
            float thickness;
            std::uint32_t depth;
            std::int32_t segments;
        };
        static_assert(std::is_standard_layout_v<ScriptShapeOptions> && sizeof(ScriptShapeOptions) == 20,
                      "ScriptShapeOptions must match Fenrir.DebugDraw's NativeShapeOptions");

        struct ScriptTextOptions
        {
            float duration;
            std::uint32_t relativeTo;
            float size;
        };
        static_assert(std::is_standard_layout_v<ScriptTextOptions> && sizeof(ScriptTextOptions) == 12,
                      "ScriptTextOptions must match Fenrir.DebugDraw's NativeTextOptions");

        // A warning logged once for each place in this file that notices a call doing nothing, and each reason, in
        // debug builds only.
        void WarnOnce(const CallContext& context, const std::string& message, std::source_location where)
        {
#ifndef NDEBUG
            static std::mutex mutex;
            static std::set<std::tuple<std::string_view, std::uint_least32_t, std::uint_least32_t, std::string>> warned;
            {
                const std::lock_guard<std::mutex> lock(mutex);
                if (!warned.emplace(where.file_name(), where.line(), where.column(), message).second)
                {
                    return;
                }
            }
            context.GetApp().Logger()->Warn("[debug draw scripting] {} (warned once)", message);
#else
            (void)context;
            (void)message;
            (void)where;
#endif
        }

        // Where a call draws: the active scene's DebugDraw, and the relativeTo entity C# named (a null Entity for
        // none).
        struct Target
        {
            DebugDraw* draw;
            Entity relativeTo;

            [[nodiscard]] ShapeOptions Shape(const ScriptShapeOptions& options, std::string_view group) const
            {
                return {.group = group,
                        .duration = options.duration,
                        .relativeTo = relativeTo,
                        .thickness = options.thickness,
                        .depth = options.depth == static_cast<std::uint32_t>(DepthMode::OnTop) ? DepthMode::OnTop
                                                                                               : DepthMode::Tested,
                        .segments = options.segments};
            }

            [[nodiscard]] TextOptions Text(const ScriptTextOptions& options, std::string_view group) const
            {
                return {.group = group, .duration = options.duration, .relativeTo = relativeTo, .size = options.size};
            }
        };

        // The active scene's DebugDraw and the entity `relativeTo` (Entity::Null for none), unless the scene has no
        // DebugDraw or the entity is not alive: then nothing is drawn, and the caller's call site warns once.
        std::optional<Target> TargetFor(const CallContext& context, std::string_view function,
                                        std::uint32_t relativeTo = Entity::Null,
                                        std::source_location where = std::source_location::current())
        {
            DebugDraw* draw = context.GetApp().GetActiveScene().GetResource<DebugDraw>();
            if (draw == nullptr)
            {
                WarnOnce(context,
                         std::string(function) + ": the active scene has no DebugDraw (FenrirDebugDraw is not "
                                                 "installed), so it draws nothing",
                         where);
                return std::nullopt;
            }
            if (relativeTo == Entity::Null)
            {
                return Target{draw, Entity()};
            }
            EntityList& entities = context.Entities();
            if (!entities.HasEntity(relativeTo))
            {
                WarnOnce(context, std::string(function) + ": the relativeTo entity is not alive, so it draws nothing",
                         where);
                return std::nullopt;
            }
            return Target{draw, entities.GetEntity(relativeTo)};
        }

        // The `count` points from `first` C# passed for a span; empty for none.
        std::span<const Math::Vec3> Points(const Math::Vec3* first, std::int32_t count)
        {
            if (first == nullptr || count <= 0)
            {
                return {};
            }
            // C# passes a span as its first element and its length, so the bounds are only as good as C#'s.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage-in-container"
#endif
            return {first, static_cast<std::size_t>(count)};
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
        }

        // The native functions, named apart from DebugDraw's own functions. Each is the DebugDraw call of its name.
        namespace Native
        {
            using Opts = ScriptShapeOptions;

            void Line(const CallContext& context, const Math::Vec3* from, const Math::Vec3* to, const Colour* colour,
                      const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Line", options->relativeTo))
                {
                    target->draw->Line(*from, *to, *colour, target->Shape(*options, group));
                }
            }

            void Ray(const CallContext& context, const Math::Vec3* origin, const Math::Vec3* direction,
                     const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Ray", options->relativeTo))
                {
                    target->draw->Ray(*origin, *direction, *colour, target->Shape(*options, group));
                }
            }

            void Arrow(const CallContext& context, const Math::Vec3* from, const Math::Vec3* to, float headLength,
                       const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Arrow", options->relativeTo))
                {
                    target->draw->Arrow(*from, *to, headLength, *colour, target->Shape(*options, group));
                }
            }

            void Polyline(const CallContext& context, const Math::Vec3* points, std::int32_t count, bool closed,
                          const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Polyline", options->relativeTo))
                {
                    target->draw->Polyline(Points(points, count), closed, *colour, target->Shape(*options, group));
                }
            }

            void Point(const CallContext& context, const Math::Vec3* position, float pixelSize, const Colour* colour,
                       const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Point", options->relativeTo))
                {
                    target->draw->Point(*position, pixelSize, *colour, target->Shape(*options, group));
                }
            }

            void Box(const CallContext& context, const Math::AABB* box, const Colour* colour, const Opts* options,
                     std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Box", options->relativeTo))
                {
                    target->draw->Box(*box, *colour, target->Shape(*options, group));
                }
            }

            void OrientedBox(const CallContext& context, const Math::Vec3* centre, const Math::Vec3* halfExtents,
                             const Math::Quat* rotation, const Colour* colour, const Opts* options,
                             std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Box", options->relativeTo))
                {
                    target->draw->Box(*centre, *halfExtents, *rotation, *colour, target->Shape(*options, group));
                }
            }

            void Sphere(const CallContext& context, const Math::Vec3* centre, float radius, const Colour* colour,
                        const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Sphere", options->relativeTo))
                {
                    target->draw->Sphere(*centre, radius, *colour, target->Shape(*options, group));
                }
            }

            void Capsule(const CallContext& context, const Math::Vec3* from, const Math::Vec3* to, float radius,
                         const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Capsule", options->relativeTo))
                {
                    target->draw->Capsule(*from, *to, radius, *colour, target->Shape(*options, group));
                }
            }

            void Cylinder(const CallContext& context, const Math::Vec3* from, const Math::Vec3* to, float radius,
                          const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Cylinder", options->relativeTo))
                {
                    target->draw->Cylinder(*from, *to, radius, *colour, target->Shape(*options, group));
                }
            }

            void Cone(const CallContext& context, const Math::Vec3* apex, const Math::Vec3* baseCentre, float radius,
                      const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Cone", options->relativeTo))
                {
                    target->draw->Cone(*apex, *baseCentre, radius, *colour, target->Shape(*options, group));
                }
            }

            void Circle(const CallContext& context, const Math::Vec3* centre, const Math::Vec3* normal, float radius,
                        const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Circle", options->relativeTo))
                {
                    target->draw->Circle(*centre, *normal, radius, *colour, target->Shape(*options, group));
                }
            }

            void Arc(const CallContext& context, const Math::Vec3* centre, const Math::Vec3* normal,
                     const Math::Vec3* start, float angle, const Colour* colour, const Opts* options,
                     std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Arc", options->relativeTo))
                {
                    target->draw->Arc(*centre, *normal, *start, angle, *colour, target->Shape(*options, group));
                }
            }

            void Plane(const CallContext& context, const Math::Vec3* point, const Math::Vec3* normal, float halfSize,
                       const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Plane", options->relativeTo))
                {
                    target->draw->Plane(*point, *normal, halfSize, *colour, target->Shape(*options, group));
                }
            }

            void Grid(const CallContext& context, const Math::Vec3* centre, const Math::Quat* rotation,
                      std::int32_t cells, float cellSize, const Colour* colour, const Opts* options,
                      std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Grid", options->relativeTo))
                {
                    target->draw->Grid(*centre, *rotation, cells, cellSize, *colour, target->Shape(*options, group));
                }
            }

            void Axes(const CallContext& context, const Math::Mat4* transform, float length, const Opts* options,
                      std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Axes", options->relativeTo))
                {
                    target->draw->Axes(*transform, length, target->Shape(*options, group));
                }
            }

            void Frustum(const CallContext& context, const Math::Mat4* viewProjection, const Colour* colour,
                         const Opts* options, std::string_view group, float farDepth)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Frustum", options->relativeTo))
                {
                    target->draw->Frustum(*viewProjection, *colour, target->Shape(*options, group), farDepth);
                }
            }

            void SolidBox(const CallContext& context, const Math::AABB* box, const Colour* colour, const Opts* options,
                          std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.SolidBox", options->relativeTo))
                {
                    target->draw->SolidBox(*box, *colour, target->Shape(*options, group));
                }
            }

            void SolidOrientedBox(const CallContext& context, const Math::Vec3* centre, const Math::Vec3* halfExtents,
                                  const Math::Quat* rotation, const Colour* colour, const Opts* options,
                                  std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.SolidBox", options->relativeTo))
                {
                    target->draw->SolidBox(*centre, *halfExtents, *rotation, *colour, target->Shape(*options, group));
                }
            }

            void SolidSphere(const CallContext& context, const Math::Vec3* centre, float radius, const Colour* colour,
                             const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.SolidSphere", options->relativeTo))
                {
                    target->draw->SolidSphere(*centre, radius, *colour, target->Shape(*options, group));
                }
            }

            void SolidCapsule(const CallContext& context, const Math::Vec3* from, const Math::Vec3* to, float radius,
                              const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.SolidCapsule", options->relativeTo))
                {
                    target->draw->SolidCapsule(*from, *to, radius, *colour, target->Shape(*options, group));
                }
            }

            void Triangles(const CallContext& context, const Math::Vec3* vertices, std::int32_t count,
                           const Colour* colour, const Opts* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Triangles", options->relativeTo))
                {
                    target->draw->Triangles(Points(vertices, count), *colour, target->Shape(*options, group));
                }
            }

            void ScreenText(const CallContext& context, const Math::Vec2* position, std::string_view text,
                            const Colour* colour, const ScriptTextOptions* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.ScreenText", options->relativeTo))
                {
                    target->draw->ScreenText(*position, text, *colour, target->Text(*options, group));
                }
            }

            void Message(const CallContext& context, std::string_view text, const Colour* colour,
                         const ScriptTextOptions* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Message", options->relativeTo))
                {
                    target->draw->Message(text, *colour, target->Text(*options, group));
                }
            }

            void KeyedMessage(const CallContext& context, std::int64_t key, std::string_view text, const Colour* colour,
                              const ScriptTextOptions* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Message", options->relativeTo))
                {
                    target->draw->Message(key, text, *colour, target->Text(*options, group));
                }
            }

            void WorldText(const CallContext& context, const Math::Vec3* position, std::string_view text,
                           const Colour* colour, const ScriptTextOptions* options, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.WorldText", options->relativeTo))
                {
                    target->draw->WorldText(*position, text, *colour, target->Text(*options, group));
                }
            }

            void ShowGroup(const CallContext& context, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.ShowGroup"))
                {
                    target->draw->ShowGroup(group);
                }
            }

            void HideGroup(const CallContext& context, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.HideGroup"))
                {
                    target->draw->HideGroup(group);
                }
            }

            bool IsGroupShown(const CallContext& context, std::string_view group)
            {
                const auto target = TargetFor(context, "DebugDraw.IsGroupShown");
                return target && target->draw->IsGroupShown(group);
            }

            void Clear(const CallContext& context)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Clear"))
                {
                    target->draw->Clear();
                }
            }

            void ClearGroup(const CallContext& context, std::string_view group)
            {
                if (const auto target = TargetFor(context, "DebugDraw.Clear"))
                {
                    target->draw->Clear(group);
                }
            }
        } // namespace Native
    } // namespace

    Fenrir::ScriptingExtension DebugDrawing::ScriptingExtension()
    {
        const std::string prefix = "Fenrir.DebugDraw.DebugDrawNative.";
        Fenrir::ScriptingExtension extension;
        extension.name = "FenrirDebugDraw";
        extension.sharedAssemblies = {"Fenrir.DebugDraw.dll"};
        extension.functions = {
            NativeFunction::Make<&Native::Line>(prefix + "Line"),
            NativeFunction::Make<&Native::Ray>(prefix + "Ray"),
            NativeFunction::Make<&Native::Arrow>(prefix + "Arrow"),
            NativeFunction::Make<&Native::Polyline>(prefix + "Polyline"),
            NativeFunction::Make<&Native::Point>(prefix + "Point"),
            NativeFunction::Make<&Native::Box>(prefix + "Box"),
            NativeFunction::Make<&Native::OrientedBox>(prefix + "OrientedBox"),
            NativeFunction::Make<&Native::Sphere>(prefix + "Sphere"),
            NativeFunction::Make<&Native::Capsule>(prefix + "Capsule"),
            NativeFunction::Make<&Native::Cylinder>(prefix + "Cylinder"),
            NativeFunction::Make<&Native::Cone>(prefix + "Cone"),
            NativeFunction::Make<&Native::Circle>(prefix + "Circle"),
            NativeFunction::Make<&Native::Arc>(prefix + "Arc"),
            NativeFunction::Make<&Native::Plane>(prefix + "Plane"),
            NativeFunction::Make<&Native::Grid>(prefix + "Grid"),
            NativeFunction::Make<&Native::Axes>(prefix + "Axes"),
            NativeFunction::Make<&Native::Frustum>(prefix + "Frustum"),
            NativeFunction::Make<&Native::SolidBox>(prefix + "SolidBox"),
            NativeFunction::Make<&Native::SolidOrientedBox>(prefix + "SolidOrientedBox"),
            NativeFunction::Make<&Native::SolidSphere>(prefix + "SolidSphere"),
            NativeFunction::Make<&Native::SolidCapsule>(prefix + "SolidCapsule"),
            NativeFunction::Make<&Native::Triangles>(prefix + "Triangles"),
            NativeFunction::Make<&Native::ScreenText>(prefix + "ScreenText"),
            NativeFunction::Make<&Native::Message>(prefix + "Message"),
            NativeFunction::Make<&Native::KeyedMessage>(prefix + "KeyedMessage"),
            NativeFunction::Make<&Native::WorldText>(prefix + "WorldText"),
            NativeFunction::Make<&Native::ShowGroup>(prefix + "ShowGroup"),
            NativeFunction::Make<&Native::HideGroup>(prefix + "HideGroup"),
            NativeFunction::Make<&Native::IsGroupShown>(prefix + "IsGroupShown"),
            NativeFunction::Make<&Native::Clear>(prefix + "Clear"),
            NativeFunction::Make<&Native::ClearGroup>(prefix + "ClearGroup"),
        };
        return extension;
    }
} // namespace Fenrir
