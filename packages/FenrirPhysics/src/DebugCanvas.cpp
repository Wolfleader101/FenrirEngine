#include "DebugCanvas.hpp"

#include <cmath>
#include <utility>

#include "Backend/Primitive.hpp"

namespace Fenrir::Detail
{
    namespace
    {
        // Half the size of a contact point's cross, and the length of its normal.
        constexpr float contactCross = 0.05f;
        constexpr float contactNormal = 0.3f;

        void Primitive(const LineWriter& write, const Backend::Primitive& primitive)
        {
            const Math::Vec3& size = primitive.size;
            const Math::Vec3 centre(0.0f);
            if (primitive.type == ShapeType::Sphere)
            {
                write.Arc(centre, xAxis, yAxis, size.x, 0.0f, 2.0f * pi, circleSegments);
                write.Arc(centre, yAxis, zAxis, size.x, 0.0f, 2.0f * pi, circleSegments);
                write.Arc(centre, zAxis, xAxis, size.x, 0.0f, 2.0f * pi, circleSegments);
                return;
            }
            if (primitive.type == ShapeType::Capsule)
            {
                // Standing along y: a ring and a cross of half circles at each end, joined by four sides.
                const float halfHeight = size.x;
                const float radius = size.y;
                const Math::Vec3 top(0.0f, halfHeight, 0.0f);
                write.Arc(top, xAxis, zAxis, radius, 0.0f, 2.0f * pi, circleSegments);
                write.Arc(-top, xAxis, zAxis, radius, 0.0f, 2.0f * pi, circleSegments);
                write.Arc(top, xAxis, yAxis, radius, 0.0f, pi, circleSegments / 2);
                write.Arc(top, zAxis, yAxis, radius, 0.0f, pi, circleSegments / 2);
                write.Arc(-top, xAxis, yAxis, radius, pi, 2.0f * pi, circleSegments / 2);
                write.Arc(-top, zAxis, yAxis, radius, pi, 2.0f * pi, circleSegments / 2);
                for (const Math::Vec3& side : {xAxis, -xAxis, zAxis, -zAxis})
                {
                    write(top + radius * side, -top + radius * side);
                }
                return;
            }
            write.Box(-size, size);
        }
    } // namespace

    void LineWriter::Arc(const Math::Vec3& centre, const Math::Vec3& u, const Math::Vec3& v, float radius, float start,
                         float end, int segments) const
    {
        const auto at = [&](float angle) { return centre + radius * (std::cos(angle) * u + std::sin(angle) * v); };
        for (int i = 0; i < segments; ++i)
        {
            const float a = start + (end - start) * static_cast<float>(i) / static_cast<float>(segments);
            const float b = start + (end - start) * static_cast<float>(i + 1) / static_cast<float>(segments);
            (*this)(at(a), at(b));
        }
    }

    void LineWriter::Box(const Math::Vec3& min, const Math::Vec3& max) const
    {
        const auto corner = [&](int i) {
            return Math::Vec3((i & 1) != 0 ? max.x : min.x, (i & 2) != 0 ? max.y : min.y, (i & 4) != 0 ? max.z : min.z);
        };
        for (int i = 0; i < 8; ++i)
        {
            for (int axis = 1; axis < 8; axis <<= 1)
            {
                if ((i & axis) == 0)
                {
                    (*this)(corner(i), corner(i | axis));
                }
            }
        }
    }

    DebugCanvas::DebugCanvas(Backend::World& backend) : m_backend(&backend)
    {
    }

    void DebugCanvas::Clear()
    {
        m_lines.clear();
        m_shapeEdges.clear();
    }

    void DebugCanvas::Begin()
    {
        m_lines.clear();
        m_used.clear();
    }

    void DebugCanvas::End()
    {
        // Ids are never reused, so the edges of a mesh not drawn this time are never asked for again.
        m_shapeEdges = std::move(m_used);
        m_used.clear();
    }

    void DebugCanvas::Line(const Math::Vec3& from, const Math::Vec3& to, std::uint32_t colour)
    {
        m_lines.push_back(DebugLine{from, to, colour});
    }

    LineWriter DebugCanvas::Writer(const Math::Vec3& position, const Math::Quat& rotation, std::uint32_t colour)
    {
        return LineWriter(m_lines, position, rotation, colour);
    }

    void DebugCanvas::ContactPoint(const Math::Vec3& point, const Math::Vec3& normal, std::uint32_t colour)
    {
        const LineWriter write = Writer(point, Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), colour);
        for (const Math::Vec3& axis : {xAxis, yAxis, zAxis})
        {
            write(-contactCross * axis, contactCross * axis);
        }
        write(Math::Vec3(0.0f), contactNormal * normal);
    }

    void DebugCanvas::Collider(const CollisionShape& shape, const Transform& pose, const Transform& body,
                               std::uint32_t colour)
    {
        // At its own world pose: the body's step pose, then the collider's pose in the body.
        const Math::Vec3 position = body.pos + body.rot * pose.pos;
        const Math::Quat rotation = body.rot * pose.rot;
        const Math::Vec3& scale = pose.scale;
        const LineWriter write = Writer(position, rotation, colour);
        if (!shape.Is<ConvexHullShape>() && !shape.Is<MeshShape>())
        {
            Primitive(write, Backend::Bake(shape, scale));
            return;
        }
        const ShapeEdgesKey key{shape.Type(), shape.Is<ConvexHullShape>() ? shape.As<ConvexHullShape>().hull.id
                                                                          : shape.As<MeshShape>().mesh.id};
        auto found = m_used.find(key);
        if (found == m_used.end())
        {
            std::vector<Math::Vec3> edges;
            if (auto cached = m_shapeEdges.find(key); cached != m_shapeEdges.end())
            {
                edges = std::move(cached->second);
            }
            else
            {
                auto collect = [&edges](const Math::Vec3& from, const Math::Vec3& to) {
                    edges.push_back(from);
                    edges.push_back(to);
                };
                m_backend->ShapeEdges(shape, collect);
            }
            found = m_used.emplace(key, std::move(edges)).first;
        }
        // Hulls and meshes take any scale per axis, mirroring included.
        const std::vector<Math::Vec3>& edges = found->second;
        for (std::size_t i = 0; i + 1 < edges.size(); i += 2)
        {
            write(edges[i] * scale, edges[i + 1] * scale);
        }
    }

    std::uint32_t DebugCanvas::ColliderColour(bool isTrigger, MotionType motionType, bool sleeping)
    {
        std::uint32_t colour = DebugColour::Static;
        if (isTrigger)
        {
            colour = DebugColour::Trigger;
        }
        else if (motionType == MotionType::Kinematic)
        {
            colour = DebugColour::Kinematic;
        }
        else if (motionType == MotionType::Dynamic)
        {
            colour = DebugColour::Dynamic;
        }
        return sleeping ? DebugColour::Dimmed(colour) : colour;
    }
} // namespace Fenrir::Detail
