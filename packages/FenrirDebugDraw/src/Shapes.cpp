#include "Shapes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace Fenrir::Detail::Shapes
{
    namespace
    {
        constexpr float Pi = 3.14159265358979323846f;

        // Two unit directions square to `direction` and to each other, so that cross(direction, u) == v.
        void Basis(const Math::Vec3& direction, Math::Vec3& u, Math::Vec3& v)
        {
            const Math::Vec3 n = Math::Normalized(direction);
            const Math::Vec3 helper =
                std::abs(n.y) < 0.99f ? Math::Vec3(0.0f, 1.0f, 0.0f) : Math::Vec3(1.0f, 0.0f, 0.0f);
            u = Math::Normalized(Math::Cross(n, helper));
            v = Math::Cross(n, u);
        }

        // The unit direction from `from` to `to`, or +Y when they meet.
        Math::Vec3 AxisBetween(const Math::Vec3& from, const Math::Vec3& to)
        {
            const Math::Vec3 axis = to - from;
            return Math::MagnitudeSq(axis) > 0.0f ? Math::Normalized(axis) : Math::Vec3(0.0f, 1.0f, 0.0f);
        }

        void Line(Points& out, const Math::Vec3& a, const Math::Vec3& b)
        {
            out.push_back(a);
            out.push_back(b);
        }

        void Triangle(Points& out, const Math::Vec3& a, const Math::Vec3& b, const Math::Vec3& c)
        {
            out.push_back(a);
            out.push_back(b);
            out.push_back(c);
        }

        // Corner i of a box has its +X half when bit 0 is set, +Y for bit 1 and +Z for bit 2.
        std::array<Math::Vec3, 8> BoxCorners(const Math::Vec3& centre, const Math::Vec3& halfExtents,
                                             const Math::Quat& rotation)
        {
            std::array<Math::Vec3, 8> corners{};
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                const Math::Vec3 local((i & 1u) != 0 ? halfExtents.x : -halfExtents.x,
                                       (i & 2u) != 0 ? halfExtents.y : -halfExtents.y,
                                       (i & 4u) != 0 ? halfExtents.z : -halfExtents.z);
                corners[i] = centre + rotation * local;
            }
            return corners;
        }

        // A point of a sphere-like surface in the frame (u, axis, v): `polar` from the axis, `around` it.
        Math::Vec3 OnSphere(const Math::Vec3& centre, float radius, const Math::Vec3& u, const Math::Vec3& axis,
                            const Math::Vec3& v, float polar, float around)
        {
            const float ring = std::sin(polar) * radius;
            return centre + axis * (std::cos(polar) * radius) + u * (ring * std::cos(around)) +
                   v * (ring * std::sin(around));
        }

        // Bands of triangles between consecutive rings. Ring k is `polar[k]` from the axis round `centres[k]`.
        void Rings(Points& out, std::span<const float> polar, std::span<const Math::Vec3> centres, float radius,
                   const Math::Vec3& axis, int segments)
        {
            Math::Vec3 u;
            Math::Vec3 v;
            Basis(axis, u, v);
            const float step = 2.0f * Pi / static_cast<float>(segments);
            for (std::size_t k = 0; k + 1 < polar.size(); ++k)
            {
                for (int j = 0; j < segments; ++j)
                {
                    const float a0 = step * static_cast<float>(j);
                    const float a1 = step * static_cast<float>(j + 1);
                    const Math::Vec3 p00 = OnSphere(centres[k], radius, u, axis, v, polar[k], a0);
                    const Math::Vec3 p01 = OnSphere(centres[k], radius, u, axis, v, polar[k], a1);
                    const Math::Vec3 p10 = OnSphere(centres[k + 1], radius, u, axis, v, polar[k + 1], a0);
                    const Math::Vec3 p11 = OnSphere(centres[k + 1], radius, u, axis, v, polar[k + 1], a1);
                    // At a pole one side of the quad is a single point: one triangle covers it.
                    if (k != 0)
                    {
                        Triangle(out, p00, p01, p11);
                    }
                    if (k + 2 != polar.size())
                    {
                        Triangle(out, p00, p11, p10);
                    }
                }
            }
        }
    } // namespace

    void WireBox(Points& out, const Math::Vec3& centre, const Math::Vec3& halfExtents, const Math::Quat& rotation)
    {
        const std::array<Math::Vec3, 8> c = BoxCorners(centre, halfExtents, rotation);
        // Each edge joins two corners that differ in one axis.
        for (unsigned i = 0; i < 8; ++i)
        {
            for (const unsigned bit : {1u, 2u, 4u})
            {
                if ((i & bit) == 0)
                {
                    Line(out, c[i], c[i | bit]);
                }
            }
        }
    }

    void Arc(Points& out, const Math::Vec3& centre, const Math::Vec3& normal, const Math::Vec3& start, float angle,
             int segments)
    {
        const Math::Vec3 n = Math::Normalized(normal);
        const Math::Vec3 side = Math::Cross(n, start);
        const float step = angle / static_cast<float>(segments);
        Math::Vec3 previous = centre + start;
        for (int i = 1; i <= segments; ++i)
        {
            const float t = step * static_cast<float>(i);
            const Math::Vec3 next = centre + start * std::cos(t) + side * std::sin(t);
            Line(out, previous, next);
            previous = next;
        }
    }

    void Circle(Points& out, const Math::Vec3& centre, const Math::Vec3& normal, float radius, int segments)
    {
        Math::Vec3 u;
        Math::Vec3 v;
        Basis(normal, u, v);
        Arc(out, centre, normal, u * radius, 2.0f * Pi, segments);
    }

    void WireSphere(Points& out, const Math::Vec3& centre, float radius, int segments)
    {
        Circle(out, centre, Math::Vec3(1.0f, 0.0f, 0.0f), radius, segments);
        Circle(out, centre, Math::Vec3(0.0f, 1.0f, 0.0f), radius, segments);
        Circle(out, centre, Math::Vec3(0.0f, 0.0f, 1.0f), radius, segments);
    }

    void WireCylinder(Points& out, const Math::Vec3& from, const Math::Vec3& to, float radius, int segments)
    {
        const Math::Vec3 axis = AxisBetween(from, to);
        Math::Vec3 u;
        Math::Vec3 v;
        Basis(axis, u, v);
        Circle(out, from, axis, radius, segments);
        Circle(out, to, axis, radius, segments);
        for (const Math::Vec3& side : {u, -u, v, -v})
        {
            Line(out, from + side * radius, to + side * radius);
        }
    }

    void WireCapsule(Points& out, const Math::Vec3& from, const Math::Vec3& to, float radius, int segments)
    {
        WireCylinder(out, from, to, radius, segments);
        const Math::Vec3 axis = AxisBetween(from, to);
        Math::Vec3 u;
        Math::Vec3 v;
        Basis(axis, u, v);
        const int half = std::max(1, segments / 2);
        // Each half circle turns from one side over the end to the other: about cross(side, outward).
        for (const Math::Vec3& side : {u, v})
        {
            Arc(out, to, Math::Cross(side, axis), side * radius, Pi, half);
            Arc(out, from, Math::Cross(side, -axis), side * radius, Pi, half);
        }
    }

    void WireCone(Points& out, const Math::Vec3& apex, const Math::Vec3& baseCentre, float radius, int segments)
    {
        const Math::Vec3 axis = AxisBetween(baseCentre, apex);
        Math::Vec3 u;
        Math::Vec3 v;
        Basis(axis, u, v);
        Circle(out, baseCentre, axis, radius, segments);
        for (const Math::Vec3& side : {u, -u, v, -v})
        {
            Line(out, apex, baseCentre + side * radius);
        }
    }

    void Arrow(Points& out, const Math::Vec3& from, const Math::Vec3& to, float headLength)
    {
        Line(out, from, to);
        if (Math::MagnitudeSq(to - from) <= 0.0f)
        {
            return;
        }
        const Math::Vec3 direction = Math::Normalized(to - from);
        Math::Vec3 u;
        Math::Vec3 v;
        Basis(direction, u, v);
        const Math::Vec3 back = to - direction * headLength;
        for (const Math::Vec3& side : {u, -u, v, -v})
        {
            Line(out, to, back + side * (headLength * 0.5f));
        }
    }

    void Polyline(Points& out, std::span<const Math::Vec3> points, bool closed)
    {
        for (std::size_t i = 0; i + 1 < points.size(); ++i)
        {
            Line(out, points[i], points[i + 1]);
        }
        if (closed && points.size() > 2)
        {
            Line(out, points.back(), points.front());
        }
    }

    void Plane(Points& out, const Math::Vec3& point, const Math::Vec3& normal, float halfSize)
    {
        Math::Vec3 u;
        Math::Vec3 v;
        Basis(normal, u, v);
        u *= halfSize;
        v *= halfSize;
        const Math::Vec3 corners[4] = {point + u + v, point - u + v, point - u - v, point + u - v};
        Polyline(out, corners, true);
        Line(out, point, point + Math::Normalized(normal) * halfSize);
    }

    void Grid(Points& out, const Math::Vec3& centre, const Math::Quat& rotation, int cells, float cellSize)
    {
        const float half = static_cast<float>(cells) * cellSize * 0.5f;
        for (int i = 0; i <= cells; ++i)
        {
            const float at = -half + cellSize * static_cast<float>(i);
            Line(out, centre + rotation * Math::Vec3(at, 0.0f, -half), centre + rotation * Math::Vec3(at, 0.0f, half));
            Line(out, centre + rotation * Math::Vec3(-half, 0.0f, at), centre + rotation * Math::Vec3(half, 0.0f, at));
        }
    }

    void Frustum(Points& out, const Math::Mat4& viewProjection, float farDepth)
    {
        const Math::Mat4 inverse = Math::Inverse(viewProjection);
        // Corners of a face of the clip volume at `depth`, in order round it. False if one is at infinity.
        const auto face = [&inverse](float depth, std::array<Math::Vec3, 4>& corners) {
            constexpr std::array<float, 4> xs = {-1.0f, 1.0f, 1.0f, -1.0f};
            constexpr std::array<float, 4> ys = {-1.0f, -1.0f, 1.0f, 1.0f};
            for (std::size_t i = 0; i < 4; ++i)
            {
                const Math::Vec4 world = inverse * Math::Vec4(xs[i], ys[i], depth, 1.0f);
                if (std::abs(world.w) < 1e-7f || !std::isfinite(world.w))
                {
                    return false;
                }
                corners[i] = Math::Vec3(world) / world.w;
            }
            return true;
        };
        std::array<Math::Vec3, 4> nearFace{};
        if (!face(1.0f, nearFace))
        {
            return;
        }
        Polyline(out, nearFace, true);
        std::array<Math::Vec3, 4> farFace{};
        if (!face(farDepth, farFace))
        {
            return;
        }
        Polyline(out, farFace, true);
        for (std::size_t i = 0; i < 4; ++i)
        {
            Line(out, nearFace[i], farFace[i]);
        }
    }

    void SolidBox(Points& out, const Math::Vec3& centre, const Math::Vec3& halfExtents, const Math::Quat& rotation)
    {
        const std::array<Math::Vec3, 8> c = BoxCorners(centre, halfExtents, rotation);
        // Each face's corners counter-clockwise seen from outside: +X, -X, +Y, -Y, +Z, -Z.
        constexpr std::array<std::array<std::size_t, 4>, 6> faces = {
            {{1, 3, 7, 5}, {0, 4, 6, 2}, {2, 6, 7, 3}, {0, 1, 5, 4}, {4, 5, 7, 6}, {0, 2, 3, 1}}};
        for (const auto& f : faces)
        {
            Triangle(out, c[f[0]], c[f[1]], c[f[2]]);
            Triangle(out, c[f[0]], c[f[2]], c[f[3]]);
        }
    }

    void SolidSphere(Points& out, const Math::Vec3& centre, float radius, int segments)
    {
        const int rings = std::max(2, segments / 2);
        std::vector<float> polar;
        std::vector<Math::Vec3> centres;
        for (int k = 0; k <= rings; ++k)
        {
            polar.push_back(Pi * static_cast<float>(k) / static_cast<float>(rings));
            centres.push_back(centre);
        }
        Rings(out, polar, centres, radius, Math::Vec3(0.0f, 1.0f, 0.0f), segments);
    }

    void SolidCapsule(Points& out, const Math::Vec3& from, const Math::Vec3& to, float radius, int segments)
    {
        const int quarter = std::max(1, segments / 4);
        std::vector<float> polar;
        std::vector<Math::Vec3> centres;
        // The cap round `to` down to its equator, then the cap round `from` from its equator: the band between the two
        // equators is the cylinder.
        for (int k = 0; k <= quarter; ++k)
        {
            polar.push_back(Pi * 0.5f * static_cast<float>(k) / static_cast<float>(quarter));
            centres.push_back(to);
        }
        for (int k = 0; k <= quarter; ++k)
        {
            polar.push_back(Pi * 0.5f + Pi * 0.5f * static_cast<float>(k) / static_cast<float>(quarter));
            centres.push_back(from);
        }
        Rings(out, polar, centres, radius, AxisBetween(from, to), segments);
    }
} // namespace Fenrir::Detail::Shapes
