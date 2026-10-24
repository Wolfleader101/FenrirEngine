#pragma once

#include <span>
#include <vector>

#include "FenrirMath/Math.hpp"

// The geometry of each shape, as plain points: wire shapes append lines (two points each) and solid shapes append
// triangles (three points each, counter-clockwise seen from outside).
namespace Fenrir::Detail::Shapes
{
    using Points = std::vector<Math::Vec3>;

    /** @brief The edges of a box `halfExtents` from `centre` along its axes, turned by `rotation`. */
    void WireBox(Points& out, const Math::Vec3& centre, const Math::Vec3& halfExtents, const Math::Quat& rotation);

    /** @brief A full circle of `segments` lines. */
    void Circle(Points& out, const Math::Vec3& centre, const Math::Vec3& normal, float radius, int segments);

    /** @brief Part of a circle: from `centre + start`, turning `angle` radians about `normal`, in `segments` lines. */
    void Arc(Points& out, const Math::Vec3& centre, const Math::Vec3& normal, const Math::Vec3& start, float angle,
             int segments);

    void WireSphere(Points& out, const Math::Vec3& centre, float radius, int segments);
    void WireCapsule(Points& out, const Math::Vec3& from, const Math::Vec3& to, float radius, int segments);
    void WireCylinder(Points& out, const Math::Vec3& from, const Math::Vec3& to, float radius, int segments);
    void WireCone(Points& out, const Math::Vec3& apex, const Math::Vec3& baseCentre, float radius, int segments);
    void Arrow(Points& out, const Math::Vec3& from, const Math::Vec3& to, float headLength);
    void Polyline(Points& out, std::span<const Math::Vec3> points, bool closed);
    void Plane(Points& out, const Math::Vec3& point, const Math::Vec3& normal, float halfSize);
    void Grid(Points& out, const Math::Vec3& centre, const Math::Quat& rotation, int cells, float cellSize);
    void Frustum(Points& out, const Math::Mat4& viewProjection, float farDepth);

    void SolidBox(Points& out, const Math::Vec3& centre, const Math::Vec3& halfExtents, const Math::Quat& rotation);
    void SolidSphere(Points& out, const Math::Vec3& centre, float radius, int segments);
    void SolidCapsule(Points& out, const Math::Vec3& from, const Math::Vec3& to, float radius, int segments);
} // namespace Fenrir::Detail::Shapes
