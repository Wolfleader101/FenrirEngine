#pragma once

// What a physics world's debug lines are drawn on: the lines a step makes, the line helpers and colours the features
// share, and the edges of the hulls and meshes drawn, asked of the backend once per collision mesh while it is drawn.
// Never through the backend's own debug renderer, so the lines look the same whatever the backend. Private to the
// FenrirPhysics package.

#include <cstdint>
#include <map>
#include <numbers>
#include <span>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirECS/DefaultComponents.hpp"
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

    // Adds lines through a pose: points given in the shape's own space come out in world space.
    class LineWriter
    {
      public:
        LineWriter(std::vector<DebugLine>& lines, const Math::Vec3& position, const Math::Quat& rotation,
                   std::uint32_t colour)
            : m_lines(&lines), m_position(position), m_rotation(rotation), m_colour(colour)
        {
        }

        void operator()(const Math::Vec3& from, const Math::Vec3& to) const
        {
            m_lines->push_back(DebugLine{Place(from), Place(to), m_colour});
        }

        /** @brief A circle of `radius` about `centre`, in the plane of the unit axes `u` and `v`, from `start` to `end`
         * radians. */
        void Arc(const Math::Vec3& centre, const Math::Vec3& u, const Math::Vec3& v, float radius, float start,
                 float end, int segments) const;

        /** @brief The twelve edges of the box from `min` to `max`. */
        void Box(const Math::Vec3& min, const Math::Vec3& max) const;

      private:
        [[nodiscard]] Math::Vec3 Place(const Math::Vec3& point) const
        {
            return m_position + m_rotation * point;
        }

        std::vector<DebugLine>* m_lines;
        Math::Vec3 m_position;
        Math::Quat m_rotation;
        std::uint32_t m_colour;
    };

    class DebugCanvas
    {
      public:
        /** @brief A canvas asking `backend` for the edges of the hulls and meshes it draws. */
        explicit DebugCanvas(Backend::World& backend);

        /** @brief With no category on: no lines, and no shape edges kept. */
        void Clear();
        /** @brief Before a step's lines: the last step's go. */
        void Begin();
        /** @brief After a step's lines: keeps the edges of the hulls and meshes drawn this time, and forgets the rest.
         */
        void End();

        [[nodiscard]] std::span<const DebugLine> Lines() const
        {
            return m_lines;
        }

        /** @brief Adds a line from `from` to `to`, in world space. */
        void Line(const Math::Vec3& from, const Math::Vec3& to, std::uint32_t colour);

        /** @brief A writer of lines through the pose at `position` and `rotation`. */
        [[nodiscard]] LineWriter Writer(const Math::Vec3& position, const Math::Quat& rotation, std::uint32_t colour);

        /** @brief A contact point: a small cross at `point` and its normal out from it. */
        void ContactPoint(const Math::Vec3& point, const Math::Vec3& normal, std::uint32_t colour);

        /** @brief A collider of `shape`, at its pose `pose` in a body whose step pose is `body`. */
        void Collider(const CollisionShape& shape, const Transform& pose, const Transform& body, std::uint32_t colour);

        /** @brief A collider's colour: by its body's kind, dimmed while it sleeps. */
        [[nodiscard]] static std::uint32_t ColliderColour(bool isTrigger, MotionType motionType, bool sleeping);

      private:
        struct ShapeEdgesKey
        {
            ShapeType type;
            std::uint64_t mesh;

            auto operator<=>(const ShapeEdgesKey&) const = default;
        };

        Backend::World* m_backend;
        std::vector<DebugLine> m_lines;
        // The edges of the hulls and meshes drawn by the last step, and by this one so far, in their own space before
        // scale, as pairs of ends.
        std::map<ShapeEdgesKey, std::vector<Math::Vec3>> m_shapeEdges;
        std::map<ShapeEdgesKey, std::vector<Math::Vec3>> m_used;
    };
} // namespace Fenrir::Detail
