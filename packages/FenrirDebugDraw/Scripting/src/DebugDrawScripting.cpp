#include "FenrirDebugDrawScripting/DebugDrawScripting.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

#include "DebugDrawScriptingState.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "Generated/FenrirDebugDrawNatives.hpp"
#include "ScriptOptions.hpp"

// Everything C# sees of debug drawing besides its facade is generated (api/FenrirDebugDraw.binding.toml): the options'
// mappings, checked against the Fenrir.DebugDraw contract whenever a script assembly loads, and from the native
// functions below (described into api/FenrirDebugDraw.natives.json) their C# declarations, DebugDrawNative, and this
// registration of them all.
namespace Fenrir::Generated
{
    void RegisterDebugDraw(ScriptingExtension& extension);
} // namespace Fenrir::Generated

namespace Fenrir
{
    // The native functions, named apart from DebugDraw's own functions. Each is the DebugDraw call of its name on the
    // active scene's DebugDraw, with the options as C# passes them (ScriptOptions.hpp) and the group as text of its
    // own.
    namespace DebugDrawNatives
    {
        /** @brief A line from `from` to `to` (DebugDraw.Line). */
        void Line(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* from,
                  const Math::Vec3* to, const Colour* colour, const ScriptOptions::ShapeOptions* options,
                  std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Line(*from, *to, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A line from `origin` along `direction`, as long as it is (DebugDraw.Ray). */
        void Ray(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* origin,
                 const Math::Vec3* direction, const Colour* colour, const ScriptOptions::ShapeOptions* options,
                 std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Ray(*origin, *direction, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A line from `from` to `to` with a head `headLength` long at `to` (DebugDraw.Arrow). */
        void Arrow(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* from,
                   const Math::Vec3* to, float headLength, const Colour* colour,
                   const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Arrow(*from, *to, headLength, *colour, target->Shape(*options, group));
            }
        }

        /**
         * @brief Lines joining the `count` points from `points` in order, and the last back to the first when `closed`
         * (DebugDraw.Polyline).
         */
        void Polyline(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* points,
                      std::int32_t count, bool closed, const Colour* colour, const ScriptOptions::ShapeOptions* options,
                      std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Polyline(SpanOf(points, count), closed, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A dot at `position`, `pixelSize` pixels across (DebugDraw.Point). */
        void Point(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* position,
                   float pixelSize, const Colour* colour, const ScriptOptions::ShapeOptions* options,
                   std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Point(*position, pixelSize, *colour, target->Shape(*options, group));
            }
        }

        /** @brief The twelve edges of an axis-aligned box (DebugDraw.Box). */
        void Box(const CallContext& context, DebugDrawScripting& debugDraw, const Math::AABB* box, const Colour* colour,
                 const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Box(*box, *colour, target->Shape(*options, group));
            }
        }

        /** @brief The twelve edges of a box `halfExtents` from `centre`, turned by `rotation` (DebugDraw.Box). */
        void OrientedBox(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* centre,
                         const Math::Vec3* halfExtents, const Math::Quat* rotation, const Colour* colour,
                         const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Box(*centre, *halfExtents, *rotation, *colour, target->Shape(*options, group));
            }
        }

        /** @brief Three circles of a sphere (DebugDraw.Sphere). */
        void Sphere(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* centre, float radius,
                    const Colour* colour, const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Sphere(*centre, radius, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A capsule round the segment from `from` to `to` (DebugDraw.Capsule). */
        void Capsule(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* from,
                     const Math::Vec3* to, float radius, const Colour* colour,
                     const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Capsule(*from, *to, radius, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A cylinder round the segment from `from` to `to` (DebugDraw.Cylinder). */
        void Cylinder(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* from,
                      const Math::Vec3* to, float radius, const Colour* colour,
                      const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Cylinder(*from, *to, radius, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A cone from `apex` to a circular base round `baseCentre` (DebugDraw.Cone). */
        void Cone(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* apex,
                  const Math::Vec3* baseCentre, float radius, const Colour* colour,
                  const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Cone(*apex, *baseCentre, radius, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A circle round `centre` in the plane square to `normal` (DebugDraw.Circle). */
        void Circle(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* centre,
                    const Math::Vec3* normal, float radius, const Colour* colour,
                    const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Circle(*centre, *normal, radius, *colour, target->Shape(*options, group));
            }
        }

        /**
         * @brief Part of a circle round `centre`, from `centre + start`, turning `angle` radians about `normal`
         * (DebugDraw.Arc).
         */
        void Arc(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* centre,
                 const Math::Vec3* normal, const Math::Vec3* start, float angle, const Colour* colour,
                 const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Arc(*centre, *normal, *start, angle, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A square `halfSize` from `point`, square to `normal`, and its normal (DebugDraw.Plane). */
        void Plane(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* point,
                   const Math::Vec3* normal, float halfSize, const Colour* colour,
                   const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Plane(*point, *normal, halfSize, *colour, target->Shape(*options, group));
            }
        }

        /**
         * @brief A grid of `cells` by `cells` squares, `cellSize` across, centred on `centre` and turned by `rotation`
         * (DebugDraw.Grid).
         * @param[in] rotation One rotation, not a span of them, though `cells` follows it.
         */
        void Grid(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* centre,
                  const Math::Quat* rotation, std::int32_t cells, float cellSize, const Colour* colour,
                  const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Grid(*centre, *rotation, cells, cellSize, *colour, target->Shape(*options, group));
            }
        }

        /** @brief The frame of `transform`: its axes, `length` long (DebugDraw.Axes). */
        void Axes(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Mat4* transform, float length,
                  const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Axes(*transform, length, target->Shape(*options, group));
            }
        }

        /** @brief The edges of the volume `viewProjection` sees, out to `farDepth` (DebugDraw.Frustum). */
        void Frustum(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Mat4* viewProjection,
                     const Colour* colour, const ScriptOptions::ShapeOptions* options, std::string_view group,
                     float farDepth)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Frustum(*viewProjection, *colour, target->Shape(*options, group), farDepth);
            }
        }

        /** @brief A solid axis-aligned box (DebugDraw.SolidBox). */
        void SolidBox(const CallContext& context, DebugDrawScripting& debugDraw, const Math::AABB* box,
                      const Colour* colour, const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->SolidBox(*box, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A solid box `halfExtents` from `centre`, turned by `rotation` (DebugDraw.SolidBox). */
        void SolidOrientedBox(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* centre,
                              const Math::Vec3* halfExtents, const Math::Quat* rotation, const Colour* colour,
                              const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->SolidBox(*centre, *halfExtents, *rotation, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A solid sphere (DebugDraw.SolidSphere). */
        void SolidSphere(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* centre,
                         float radius, const Colour* colour, const ScriptOptions::ShapeOptions* options,
                         std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->SolidSphere(*centre, radius, *colour, target->Shape(*options, group));
            }
        }

        /** @brief A solid capsule round the segment from `from` to `to` (DebugDraw.SolidCapsule). */
        void SolidCapsule(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* from,
                          const Math::Vec3* to, float radius, const Colour* colour,
                          const ScriptOptions::ShapeOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->SolidCapsule(*from, *to, radius, *colour, target->Shape(*options, group));
            }
        }

        /** @brief The `count` vertices from `vertices` as triangles, every three one (DebugDraw.Triangles). */
        void Triangles(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* vertices,
                       std::int32_t count, const Colour* colour, const ScriptOptions::ShapeOptions* options,
                       std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Triangles(SpanOf(vertices, count), *colour, target->Shape(*options, group));
            }
        }

        /** @brief Text at a pixel position in each window showing the scene (DebugDraw.ScreenText). */
        void ScreenText(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec2* position,
                        std::string_view text, const Colour* colour, const ScriptOptions::TextOptions* options,
                        std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->ScreenText(*position, text, *colour, target->Text(*options, group));
            }
        }

        /** @brief An on-screen message (DebugDraw.Message). */
        void Message(const CallContext& context, DebugDrawScripting& debugDraw, std::string_view text,
                     const Colour* colour, const ScriptOptions::TextOptions* options, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Message(text, *colour, target->Text(*options, group));
            }
        }

        /** @brief An on-screen message under `key`, replacing the message under the same key (DebugDraw.Message). */
        void KeyedMessage(const CallContext& context, DebugDrawScripting& debugDraw, std::int64_t key,
                          std::string_view text, const Colour* colour, const ScriptOptions::TextOptions* options,
                          std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->Message(key, text, *colour, target->Text(*options, group));
            }
        }

        /** @brief Text at a point in the world, facing the screen (DebugDraw.WorldText). */
        void WorldText(const CallContext& context, DebugDrawScripting& debugDraw, const Math::Vec3* position,
                       std::string_view text, const Colour* colour, const ScriptOptions::TextOptions* options,
                       std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context, options->relativeTo))
            {
                target->draw->WorldText(*position, text, *colour, target->Text(*options, group));
            }
        }

        /**
         * @brief Shows the group's items.
         * @facade DebugDraw.ShowGroup
         */
        void ShowGroup(const CallContext& context, DebugDrawScripting& debugDraw, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context))
            {
                target->draw->ShowGroup(group);
            }
        }

        /**
         * @brief Hides the group's items.
         * @facade DebugDraw.HideGroup
         */
        void HideGroup(const CallContext& context, DebugDrawScripting& debugDraw, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context))
            {
                target->draw->HideGroup(group);
            }
        }

        /**
         * @brief Whether the group's items are shown; false with no debug drawing in the active scene.
         * @facade DebugDraw.IsGroupShown
         */
        bool IsGroupShown(const CallContext& context, DebugDrawScripting& debugDraw, std::string_view group)
        {
            const auto target = debugDraw.TargetFor(context);
            return target && target->draw->IsGroupShown(group);
        }

        /**
         * @brief Drops every item.
         * @facade DebugDraw.Clear
         */
        void Clear(const CallContext& context, DebugDrawScripting& debugDraw)
        {
            if (const auto target = debugDraw.TargetFor(context))
            {
                target->draw->Clear();
            }
        }

        /**
         * @brief Drops the group's items.
         * @facade DebugDraw.Clear
         */
        void ClearGroup(const CallContext& context, DebugDrawScripting& debugDraw, std::string_view group)
        {
            if (const auto target = debugDraw.TargetFor(context))
            {
                target->draw->Clear(group);
            }
        }
    } // namespace DebugDrawNatives

    Fenrir::ScriptingExtension DebugDrawing::ScriptingExtension()
    {
        return Generated::MakeDebugDrawExtension(std::make_shared<DebugDrawScripting>());
    }
} // namespace Fenrir
