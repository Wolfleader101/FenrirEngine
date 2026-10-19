// A physics world's debug lines, made after each step from FenrirPhysics's own record of its bodies (shape, step pose,
// motion type, trigger flag) and what it asks the backend privately (contacts, bounds, sleep, velocity, the edges of a
// cooked hull or mesh). Never through the backend's own debug renderer, so they look the same whatever the backend.

#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <span>
#include <vector>

#include "Backend/Primitive.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        // Segments in a full circle of a sphere or capsule.
        constexpr int circleSegments = 32;
        // How far a velocity line reaches: the distance travelled in this many seconds.
        constexpr float velocitySeconds = 0.1f;
        // Half the size of a contact point's cross, and the length of its normal.
        constexpr float contactCross = 0.05f;
        constexpr float contactNormal = 0.3f;
        // The length of a joint frame's axes: frameA's long, frameB's short, so both show where they coincide.
        constexpr float jointAxisA = 0.5f;
        constexpr float jointAxisB = 0.25f;
        // The length of a character's ground normal.
        constexpr float groundNormal = 0.5f;

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

            // A circle of `radius` about `centre`, in the plane of the unit axes `u` and `v`, from `start` to `end`
            // radians.
            void Arc(const Math::Vec3& centre, const Math::Vec3& u, const Math::Vec3& v, float radius, float start,
                     float end, int segments) const
            {
                const auto at = [&](float angle) {
                    return centre + radius * (std::cos(angle) * u + std::sin(angle) * v);
                };
                for (int i = 0; i < segments; ++i)
                {
                    const float a = start + (end - start) * static_cast<float>(i) / static_cast<float>(segments);
                    const float b = start + (end - start) * static_cast<float>(i + 1) / static_cast<float>(segments);
                    (*this)(at(a), at(b));
                }
            }

          private:
            Math::Vec3 Place(const Math::Vec3& point) const
            {
                return m_position + m_rotation * point;
            }

            std::vector<DebugLine>* m_lines;
            Math::Vec3 m_position;
            Math::Quat m_rotation;
            std::uint32_t m_colour;
        };

        const Math::Vec3 xAxis(1.0f, 0.0f, 0.0f);
        const Math::Vec3 yAxis(0.0f, 1.0f, 0.0f);
        const Math::Vec3 zAxis(0.0f, 0.0f, 1.0f);
        constexpr float pi = std::numbers::pi_v<float>;

        // The twelve edges of the box from `min` to `max`.
        void Box(const LineWriter& write, const Math::Vec3& min, const Math::Vec3& max)
        {
            const auto corner = [&](int i) {
                return Math::Vec3((i & 1) != 0 ? max.x : min.x, (i & 2) != 0 ? max.y : min.y,
                                  (i & 4) != 0 ? max.z : min.z);
            };
            for (int i = 0; i < 8; ++i)
            {
                for (int axis = 1; axis < 8; axis <<= 1)
                {
                    if ((i & axis) == 0)
                    {
                        write(corner(i), corner(i | axis));
                    }
                }
            }
        }

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
            Box(write, -size, size);
        }

        std::uint32_t ColliderColour(bool isTrigger, MotionType motionType, bool sleeping)
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
    } // namespace

    void PhysicsWorld::SetDebugDraw(DebugDraw categories)
    {
        m_impl->m_debugDraw = categories;
    }

    DebugDraw PhysicsWorld::DebugDrawCategories() const
    {
        return m_impl->m_debugDraw;
    }

    std::span<const DebugLine> PhysicsWorld::DebugLines() const
    {
        return m_impl->m_debugLines;
    }

    void PhysicsWorld::Impl::DrawDebugLines()
    {
        m_debugLines.clear();
        if (m_debugDraw == DebugDraw::None)
        {
            m_shapeEdges.clear();
            return;
        }
        // Hull and mesh edges are asked of the backend once per mesh while they are drawn; ids are never reused.
        std::map<ShapeEdgesKey, std::vector<Math::Vec3>> used;
        if (Includes(m_debugDraw, DebugDraw::Colliders))
        {
            DrawColliders(used);
        }
        if (Includes(m_debugDraw, DebugDraw::Joints))
        {
            DrawJoints();
        }
        if (Includes(m_debugDraw, DebugDraw::Characters))
        {
            DrawCharacters(used);
        }
        m_shapeEdges = std::move(used);
        if (Includes(m_debugDraw, DebugDraw::Contacts))
        {
            for (const Backend::Touch& touch : m_backend.Touches())
            {
                if (touch.isTrigger1 || touch.isTrigger2)
                {
                    continue; // an overlap, not a contact
                }
                const LineWriter write(m_debugLines, touch.point, Math::Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                       DebugColour::Contact);
                for (const Math::Vec3& axis : {xAxis, yAxis, zAxis})
                {
                    write(-contactCross * axis, contactCross * axis);
                }
                write(Math::Vec3(0.0f), contactNormal * touch.normal);
            }
        }
        for (const auto& [id, record] : m_bodies)
        {
            if (!record.Active())
            {
                continue; // no body to draw
            }
            if (Includes(m_debugDraw, DebugDraw::Velocities) && record.motion.motionType != MotionType::Static)
            {
                const Math::Vec3 velocity = m_backend.GetState(record.id).linearVelocity;
                if (velocity != Math::Vec3(0.0f))
                {
                    m_debugLines.push_back(DebugLine{record.world.pos, record.world.pos + velocitySeconds * velocity,
                                                     DebugColour::Velocity});
                }
            }
            if (Includes(m_debugDraw, DebugDraw::Bounds))
            {
                const Math::AABB bounds = m_backend.Bounds(record.id);
                Box(LineWriter(m_debugLines, Math::Vec3(0.0f), Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), DebugColour::Bounds),
                    bounds.min, bounds.max);
            }
        }
    }

    void PhysicsWorld::Impl::DrawColliders(std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used)
    {
        for (const auto& [id, record] : m_bodies)
        {
            if (!record.Active())
            {
                continue;
            }
            const bool sleeping = record.motion.motionType != MotionType::Static && m_backend.IsSleeping(record.id);
            const std::uint32_t colour = ColliderColour(record.isTrigger, record.motion.motionType, sleeping);
            for (const Part& part : record.parts)
            {
                if (part.usable)
                {
                    DrawPart(part, record.world, colour, used);
                }
            }
        }
    }

    void PhysicsWorld::Impl::DrawCharacters(std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used)
    {
        // Only characters with a backend character: disabled and inactive ones have no record, and draw nothing.
        for (const auto& [id, record] : m_characters)
        {
            // The shape it has, not the Collider's: while a shape edit waits, that is the one bodies and queries see.
            const Part part{.entity = id,
                            .shape = record.shape,
                            .pose = Transform(Math::Vec3(0.0f), Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), record.scale),
                            .usable = true,
                            .filtered = false};
            DrawPart(part, record.world, DebugColour::Character, used);

            const Backend::CharacterMoved& moved = record.moved;
            if (moved.ground == GroundState::OnGround || moved.ground == GroundState::OnSteepGround)
            {
                const std::uint32_t colour =
                    moved.ground == GroundState::OnGround ? DebugColour::Ground : DebugColour::SteepGround;
                m_debugLines.push_back(
                    DebugLine{moved.groundPoint, moved.groundPoint + groundNormal * moved.groundNormal, colour});
            }
            if (moved.movedVelocity != Math::Vec3(0.0f))
            {
                m_debugLines.push_back(DebugLine{
                    record.world.pos, record.world.pos + velocitySeconds * moved.movedVelocity, DebugColour::Velocity});
            }
        }
    }

    void PhysicsWorld::Impl::DrawJoints()
    {
        // Every kind draws the same two frames. Shapes of a kind's own (limit arcs, cones) would go here, by kind.
        const auto frame = [this](const JointFrame& local, const Transform* body, const Math::Vec3& scale, float length,
                                  std::array<std::uint32_t, 3> colours) {
            // A frame's position follows its body's scale and its rotation does not, as the constraint was made; the
            // world end's frame is already in world space.
            const Math::Vec3 position =
                body == nullptr ? local.position : body->pos + body->rot * (local.position * scale);
            const Math::Quat rotation = body == nullptr ? local.rotation : body->rot * local.rotation;
            const std::array<Math::Vec3, 3> axes = {xAxis, yAxis, zAxis};
            for (std::size_t i = 0; i < axes.size(); ++i)
            {
                LineWriter(m_debugLines, position, rotation, colours[i])(Math::Vec3(0.0f), length * axes[i]);
            }
        };
        for (const auto& [entity, record] : m_joints)
        {
            const Joint& joint = record.applied;
            if (!record.HasConstraint())
            {
                // Inactive: each frame whose body is still a body, in grey. Never the world's: nothing holds it there.
                constexpr std::array<std::uint32_t, 3> grey = {DebugColour::InactiveJoint, DebugColour::InactiveJoint,
                                                               DebugColour::InactiveJoint};
                if (const Record* a = JointBodyOf(joint.bodyA))
                {
                    frame(joint.frameA, &a->world, a->world.scale, jointAxisA, grey);
                }
                if (const Record* b = JointBodyOf(joint.bodyB))
                {
                    frame(joint.frameB, &b->world, b->world.scale, jointAxisB, grey);
                }
                continue;
            }
            const Record* a = JointBodyOf(joint.bodyA);
            const bool toWorld = joint.bodyB == Entity();
            const Record* b = toWorld ? nullptr : JointBodyOf(joint.bodyB);
            if (a == nullptr || (!toWorld && b == nullptr))
            {
                continue; // not expected: SyncJoints drops a constraint before its bodies go
            }
            // A disabled joint, broken or switched off by game code, is dimmed.
            const auto colour = [&joint](std::uint32_t full) {
                return joint.enabled ? full : DebugColour::Dimmed(full);
            };
            frame(joint.frameA, &a->world, record.scaleA, jointAxisA,
                  {colour(DebugColour::JointAX), colour(DebugColour::JointAY), colour(DebugColour::JointAZ)});
            frame(joint.frameB, toWorld ? nullptr : &b->world, record.scaleB, jointAxisB,
                  {colour(DebugColour::JointBX), colour(DebugColour::JointBY), colour(DebugColour::JointBZ)});
        }
    }

    void PhysicsWorld::Impl::DrawPart(const Part& part, const Transform& body, std::uint32_t colour,
                                      std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used)
    {
        // Each collider at its own world pose: the body's step pose, then the part's pose in the body.
        const Math::Vec3 position = body.pos + body.rot * part.pose.pos;
        const Math::Quat rotation = body.rot * part.pose.rot;
        const Math::Vec3& scale = part.pose.scale;
        const LineWriter write(m_debugLines, position, rotation, colour);
        const CollisionShape& shape = part.shape;
        if (!shape.Is<ConvexHullShape>() && !shape.Is<MeshShape>())
        {
            Primitive(write, Backend::Bake(shape, scale));
            return;
        }
        const ShapeEdgesKey key{shape.Type(), shape.Is<ConvexHullShape>() ? shape.As<ConvexHullShape>().hull.id
                                                                          : shape.As<MeshShape>().mesh.id};
        auto found = used.find(key);
        if (found == used.end())
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
                m_backend.ShapeEdges(shape, collect);
            }
            found = used.emplace(key, std::move(edges)).first;
        }
        // Hulls and meshes take any scale per axis, mirroring included.
        const std::vector<Math::Vec3>& edges = found->second;
        for (std::size_t i = 0; i + 1 < edges.size(); i += 2)
        {
            write(edges[i] * scale, edges[i + 1] * scale);
        }
    }
} // namespace Fenrir
