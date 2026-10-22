#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirECS/Entity.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    namespace Detail
    {
        /** @brief What DebugDrawing::Install set up. Defined in FenrirDebugDraw's private headers. */
        struct DebugDrawState;
    } // namespace Detail

    /** @brief Whether a shape is hidden behind what is in front of it. */
    enum class DepthMode : std::uint8_t
    {
        /** @brief Tested against the scene's depth, so geometry in front hides it (the default). */
        Tested,
        /** @brief Drawn over everything, never hidden. */
        OnTop,
    };

    /**
     * @brief How a debug draw item of a shape is kept and drawn. Every field has a default, so a call names only what
     * it changes: `draw.Line(a, b, Colour::Red, {.duration = 2.0f})`.
     */
    struct ShapeOptions
    {
        /** @brief The debug group the item belongs to. */
        std::string_view group = "Default";
        /**
         * @brief How long the item is kept. 0 (the default) draws it once: until the next tick when drawn from
         * `PreTick`, `Tick` or `PostTick`, otherwise until the end of this frame's `Render`. A positive duration keeps
         * it that many seconds of frame time, and DebugDraw::Forever until it is cleared. A negative one counts as 0.
         */
        float duration = 0.0f;
        /**
         * @brief An entity the positions are relative to, or none. The item follows the entity's world transform
         * while it lives, and is dropped when the entity is destroyed.
         */
        Entity relativeTo = Entity();
        /** @brief Line thickness in pixels. */
        float thickness = 1.0f;
        /** @brief Whether the shape is depth tested or drawn on top. */
        DepthMode depth = DepthMode::Tested;
        /** @brief How many straight segments make a full circle of a round shape (at least 3). */
        int segments = 24;
    };

    /** @brief How a debug draw item of text is kept and drawn. As ShapeOptions, without the shape-only fields. */
    struct TextOptions
    {
        /** @brief The debug group the item belongs to. */
        std::string_view group = "Default";
        /** @brief How long the item is kept, as ShapeOptions::duration. */
        float duration = 0.0f;
        /**
         * @brief An entity the item is relative to, or none. World text follows the entity's world transform; screen
         * text and messages stay where they are. Every one is dropped when the entity is destroyed.
         */
        Entity relativeTo = Entity();
        /** @brief The height of the text in pixels. */
        float size = 16.0f;
    };

    /** @brief One vertex of a tessellated debug batch, in the scene's world space. */
    struct DebugVertex
    {
        Math::Vec3 position;
        Colour colour;
    };

    /**
     * @brief Lines that draw alike: every two vertices are one line. A point is a line whose two vertices are the
     * same, drawn as a dot `thickness` pixels across.
     */
    struct DebugLineBatch
    {
        DepthMode depth = DepthMode::Tested;
        /** @brief Line thickness in pixels. */
        float thickness = 1.0f;
        std::vector<DebugVertex> vertices;
    };

    /** @brief Solid triangles that draw alike, with alpha: every three vertices are one triangle. */
    struct DebugTriangleBatch
    {
        DepthMode depth = DepthMode::Tested;
        std::vector<DebugVertex> vertices;
    };

    /** @brief Where a run of debug text is drawn. */
    enum class DebugTextSpace : std::uint8_t
    {
        /** @brief At a pixel position in the window, from its top-left. */
        Screen,
        /** @brief An on-screen message, stacked with the others from the window's top-left. */
        Message,
        /** @brief At a point in the world, facing the screen. */
        World,
    };

    /** @brief One run of debug text. How it is laid out and drawn is the renderer's. */
    struct DebugTextRun
    {
        DebugTextSpace space = DebugTextSpace::Screen;
        std::string text;
        /** @brief Screen: the pixel position (z is 0). World: the point in world space. Message: unused (zero). */
        Math::Vec3 position = Math::Vec3(0.0f);
        /** @brief The height of the text in pixels. */
        float size = 16.0f;
        Colour colour;
    };

    /**
     * @brief Everything a scene's DebugDraw shows this frame as plain data: its live items in shown groups. Line and
     * triangle batches are split by depth mode and thickness, in the order those first occur. Text runs are screen
     * text in the order drawn, then on-screen messages newest first, then world text in the order drawn.
     */
    struct DebugBatches
    {
        std::vector<DebugLineBatch> lines;
        std::vector<DebugTriangleBatch> triangles;
        std::vector<DebugTextRun> texts;

        /** @brief Whether there is nothing to draw. */
        [[nodiscard]] bool Empty() const noexcept
        {
            return lines.empty() && triangles.empty() && texts.empty();
        }
    };

    /**
     * @brief A scene's debug drawing: the shapes and text game code and packages ask to see, in the scene's world
     * space (ADR-0025). A scene resource, one per scene, made by the systems DebugDrawing::Install adds; reach it with
     * `scene.GetResource<DebugDraw>()`.
     *
     * Every call records a debug draw item, kept for its duration (ShapeOptions::duration) and in a debug group. From
     * a system the scheduler runs in parallel, the item is held until the system's batch has run, then recorded in
     * the order the systems were registered, as App::SendEvent does; elsewhere it is recorded at once. Nothing here
     * needs a renderer: items record, expire and toggle without one, and Tessellate gives what a renderer would draw.
     */
    class DebugDraw
    {
      public:
        /** @brief The duration that keeps an item until Clear() or Clear(group). */
        static constexpr float Forever = std::numeric_limits<float>::infinity();

        /** @brief Made by the systems DebugDrawing::Install adds, for each scene. */
        explicit DebugDraw(const Detail::DebugDrawState& state);
        ~DebugDraw();
        DebugDraw(const DebugDraw&) = delete;
        DebugDraw& operator=(const DebugDraw&) = delete;

        /** @name Wire shapes
         * @{
         */

        /** @brief A line from `from` to `to`. */
        void Line(const Math::Vec3& from, const Math::Vec3& to, const Colour& colour, const ShapeOptions& options = {});

        /** @brief A line from `origin` along `direction`, as long as `direction` is. */
        void Ray(const Math::Vec3& origin, const Math::Vec3& direction, const Colour& colour,
                 const ShapeOptions& options = {});

        /**
         * @brief A line from `from` to `to` with a head at `to`: four lines back from the tip, `headLength` long along
         * the arrow and spread `headLength / 2` either side of it in two directions square to it and to each other.
         */
        void Arrow(const Math::Vec3& from, const Math::Vec3& to, float headLength, const Colour& colour,
                   const ShapeOptions& options = {});

        /** @brief Lines joining `points` in order, and the last back to the first when `closed`. */
        void Polyline(std::span<const Math::Vec3> points, bool closed, const Colour& colour,
                      const ShapeOptions& options = {});

        /** @brief A dot at `position`, `pixelSize` pixels across whatever its distance. ShapeOptions::thickness is
         * unused. */
        void Point(const Math::Vec3& position, float pixelSize, const Colour& colour, const ShapeOptions& options = {});

        /** @brief The twelve edges of an axis-aligned box. */
        void Box(const Math::AABB& box, const Colour& colour, const ShapeOptions& options = {});

        /** @brief The twelve edges of a box `halfExtents` from `centre` along its axes, turned by `rotation`. */
        void Box(const Math::Vec3& centre, const Math::Vec3& halfExtents, const Math::Quat& rotation,
                 const Colour& colour, const ShapeOptions& options = {});

        /** @brief Three circles of a sphere, square to the world's X, Y and Z axes. */
        void Sphere(const Math::Vec3& centre, float radius, const Colour& colour, const ShapeOptions& options = {});

        /**
         * @brief A capsule round the segment from `from` to `to`: a circle at each end, four lines along its sides,
         * and two half circles over each end.
         */
        void Capsule(const Math::Vec3& from, const Math::Vec3& to, float radius, const Colour& colour,
                     const ShapeOptions& options = {});

        /** @brief A cylinder round the segment from `from` to `to`: a circle at each end and four lines along its
         * sides.
         */
        void Cylinder(const Math::Vec3& from, const Math::Vec3& to, float radius, const Colour& colour,
                      const ShapeOptions& options = {});

        /** @brief A cone from `apex` to a circular base round `baseCentre`: the base circle and four lines to the apex.
         */
        void Cone(const Math::Vec3& apex, const Math::Vec3& baseCentre, float radius, const Colour& colour,
                  const ShapeOptions& options = {});

        /** @brief A circle round `centre` in the plane square to `normal`. */
        void Circle(const Math::Vec3& centre, const Math::Vec3& normal, float radius, const Colour& colour,
                    const ShapeOptions& options = {});

        /**
         * @brief Part of a circle round `centre`: from `centre + start` (so `start`'s length is the radius), turning
         * `angle` radians about `normal` (counter-clockwise looking down `normal`). ShapeOptions::segments is for a
         * full circle; the arc uses its share, at least one.
         */
        void Arc(const Math::Vec3& centre, const Math::Vec3& normal, const Math::Vec3& start, float angle,
                 const Colour& colour, const ShapeOptions& options = {});

        /** @brief A square `halfSize` from `point` in the plane square to `normal`, and a line along the normal as long
         * as `halfSize`. */
        void Plane(const Math::Vec3& point, const Math::Vec3& normal, float halfSize, const Colour& colour,
                   const ShapeOptions& options = {});

        /**
         * @brief A grid of `cells` by `cells` squares, `cellSize` across, centred on `centre` in the plane of its local
         * X and Z axes turned by `rotation`.
         */
        void Grid(const Math::Vec3& centre, const Math::Quat& rotation, int cells, float cellSize, const Colour& colour,
                  const ShapeOptions& options = {});

        /** @brief The frame of `transform`: its X, Y and Z axes from its origin, `length` long, in red, green and
         * blue. */
        void Axes(const Math::Mat4& transform, float length, const ShapeOptions& options = {});

        /**
         * @brief The twelve edges of the volume `viewProjection` sees, between its near plane (depth 1) and depth
         * `farDepth`, in ADR-0026's reversed-Z [0, 1] depth. For an infinite far plane give a `farDepth` above 0: the
         * far plane itself is at infinity, so with `farDepth` 0 only the near plane is drawn.
         */
        void Frustum(const Math::Mat4& viewProjection, const Colour& colour, const ShapeOptions& options = {},
                     float farDepth = 0.0f);
        /** @} */

        /** @name Solid shapes
         * Triangles with the colour's alpha, depth tested without writing depth and not sorted.
         * @{
         */

        /** @brief A solid axis-aligned box. */
        void SolidBox(const Math::AABB& box, const Colour& colour, const ShapeOptions& options = {});

        /** @brief A solid box `halfExtents` from `centre` along its axes, turned by `rotation`. */
        void SolidBox(const Math::Vec3& centre, const Math::Vec3& halfExtents, const Math::Quat& rotation,
                      const Colour& colour, const ShapeOptions& options = {});

        /** @brief A solid sphere: ShapeOptions::segments round, half as many from pole to pole. */
        void SolidSphere(const Math::Vec3& centre, float radius, const Colour& colour,
                         const ShapeOptions& options = {});

        /** @brief A solid capsule round the segment from `from` to `to`. */
        void SolidCapsule(const Math::Vec3& from, const Math::Vec3& to, float radius, const Colour& colour,
                          const ShapeOptions& options = {});

        /** @brief The caller's triangles: every three of `vertices` are one. A remainder of one or two is ignored. */
        void Triangles(std::span<const Math::Vec3> vertices, const Colour& colour, const ShapeOptions& options = {});
        /** @} */

        /** @name Text
         * @{
         */

        /** @brief Text at a pixel position in each window showing the scene, from its top-left. */
        void ScreenText(const Math::Vec2& position, std::string_view text, const Colour& colour,
                        const TextOptions& options = {});

        /** @brief An on-screen message: stacked from the window's top-left, newest first. */
        void Message(std::string_view text, const Colour& colour, const TextOptions& options = {});

        /** @brief An on-screen message under `key`: it replaces the message under the same key, in its place. */
        void Message(std::int64_t key, std::string_view text, const Colour& colour, const TextOptions& options = {});

        /** @brief Text at a point in the world, facing the screen at a constant pixel size, never hidden. */
        void WorldText(const Math::Vec3& position, std::string_view text, const Colour& colour,
                       const TextOptions& options = {});
        /** @} */

        /** @name Groups
         * Every group is shown until hidden. A hidden group's items are still recorded and still expire; they are
         * left out of Tessellate until the group is shown again.
         * @{
         */

        /** @brief Shows the group's items. */
        void ShowGroup(std::string_view group);

        /** @brief Hides the group's items. */
        void HideGroup(std::string_view group);

        /** @brief Whether the group's items are shown. */
        [[nodiscard]] bool IsGroupShown(std::string_view group) const;

        /** @brief Drops every item. */
        void Clear();

        /** @brief Drops the group's items. */
        void Clear(std::string_view group);
        /** @} */

        /**
         * @brief The live items of shown groups as plain batches in world space, with each `relativeTo` item placed
         * by its entity's world transform now. Read it on the main thread, from a sequential system.
         */
        [[nodiscard]] DebugBatches Tessellate() const;

      private:
        friend class DebugDrawing;
        struct Records;

        // The expiry system's work at the start of each frame: timed items lose the frame's time, and expired items
        // and those whose relativeTo entity is gone are dropped.
        void Expire(double deltaTime);

        void Record(std::function<void(Records&)> change);

        const Detail::DebugDrawState* m_state;
        std::unique_ptr<Records> m_records;
    };
} // namespace Fenrir
