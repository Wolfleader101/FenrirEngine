#pragma once

// What a physics world's debug lines are drawn through: the scene's DebugDraw (ADR-0025) a step's lines go into, in the
// PhysicsDebugGroup with duration 0, the line helpers and colours the features share, and the edges of the hulls and
// meshes drawn, asked of the backend once per collision mesh while it is drawn. Never through the backend's own debug
// renderer, so the lines look the same whatever the backend. Private to the FenrirPhysics package.

#include <cstdint>
#include <map>
#include <numbers>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirDebugDraw/DebugDraw.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/DebugLines.hpp"

namespace Fenrir::Detail
{
    // Segments in a full circle of a sphere or capsule.
    inline constexpr int circleSegments = 32;
    // How far a velocity line reaches: the distance travelled in this many seconds.
    inline constexpr float velocitySeconds = 0.1f;
    inline constexpr float pi = std::numbers::pi_v<float>;

    inline const Math::Vec3 xAxis(1.0f, 0.0f, 0.0f);
    inline const Math::Vec3 yAxis(0.0f, 1.0f, 0.0f);
    inline const Math::Vec3 zAxis(0.0f, 0.0f, 1.0f);

    // How every debug line is drawn: in the Physics group, with duration 0, so a step's lines last until the next tick.
    inline const ShapeOptions debugLineOptions{.group = PhysicsDebugGroup};

    // Draws lines through a pose: points given in the shape's own space come out in world space.
    class LineWriter
    {
      public:
        LineWriter(DebugDraw& draw, const Math::Vec3& position, const Math::Quat& rotation, const Colour& colour)
            : m_draw(&draw), m_position(position), m_rotation(rotation), m_colour(colour)
        {
        }

        void operator()(const Math::Vec3& from, const Math::Vec3& to) const
        {
            m_draw->Line(Place(from), Place(to), m_colour, debugLineOptions);
        }

        /** @brief A circle of `radius` about `centre`, in the plane of the unit axes `u` and `v`, from `start` to `end`
         * radians: `segments` lines, drawn as one polyline. */
        void Arc(const Math::Vec3& centre, const Math::Vec3& u, const Math::Vec3& v, float radius, float start,
                 float end, int segments) const;

        /** @brief The twelve edges of the box from `min` to `max`. */
        void Box(const Math::Vec3& min, const Math::Vec3& max) const;

      private:
        [[nodiscard]] Math::Vec3 Place(const Math::Vec3& point) const
        {
            return m_position + m_rotation * point;
        }

        DebugDraw* m_draw;
        Math::Vec3 m_position;
        Math::Quat m_rotation;
        Colour m_colour;
    };

    class DebugCanvas
    {
      public:
        /** @brief A canvas asking `backend` for the edges of the hulls and meshes it draws. */
        explicit DebugCanvas(Backend::World& backend);

        /** @brief With no category on, or nothing to draw into: no shape edges kept. */
        void Clear();
        /** @brief Before a step's lines: they are drawn into `draw` until End. */
        void Begin(DebugDraw& draw);
        /** @brief After a step's lines: keeps the edges of the hulls and meshes drawn this time, and forgets the rest.
         */
        void End();

        /** @brief Draws a line from `from` to `to`, in world space. */
        void Line(const Math::Vec3& from, const Math::Vec3& to, const Colour& colour);

        /** @brief A writer of lines through the pose at `position` and `rotation`. */
        [[nodiscard]] LineWriter Writer(const Math::Vec3& position, const Math::Quat& rotation, const Colour& colour);

        /** @brief A contact point: a small cross at `point` and its normal out from it. */
        void ContactPoint(const Math::Vec3& point, const Math::Vec3& normal, const Colour& colour);

        /** @brief A collider of `shape`, at its pose `pose` in a body whose step pose is `body`. */
        void Collider(const CollisionShape& shape, const Transform& pose, const Transform& body, const Colour& colour);

        /** @brief A collider's colour: by its body's kind, dimmed while it sleeps. */
        [[nodiscard]] static Colour ColliderColour(bool isTrigger, MotionType motionType, bool sleeping);

      private:
        struct ShapeEdgesKey
        {
            ShapeType type;
            std::uint64_t mesh;

            auto operator<=>(const ShapeEdgesKey&) const = default;
        };

        Backend::World* m_backend;
        // What a step's lines are drawn into, between Begin and End.
        DebugDraw* m_draw = nullptr;
        // The edges of the hulls and meshes drawn by the last step, and by this one so far, in their own space before
        // scale, as pairs of ends.
        std::map<ShapeEdgesKey, std::vector<Math::Vec3>> m_shapeEdges;
        std::map<ShapeEdgesKey, std::vector<Math::Vec3>> m_used;
    };
} // namespace Fenrir::Detail
