// A physics world's debug lines, drawn after each step into its scene's DebugDraw (ADR-0025) from FenrirPhysics's own
// record of its bodies (shape, step pose, motion type, trigger flag) and what it asks the backend privately (contacts,
// bounds, sleep, velocity, the edges of a cooked hull or mesh, each vehicle's wheels). Never through the backend's own
// debug renderer, so they look the same whatever the backend.

#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <numbers>
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
        // Half the size of the cross-tick at a vehicle wheel's centre on its suspension.
        constexpr float suspensionTick = 0.08f;

        // How every debug line is drawn: in the Physics group, with duration 0, so a step's lines last until the
        // next tick.
        const ShapeOptions lineOptions{.group = PhysicsDebugGroup};

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
                m_draw->Line(Place(from), Place(to), m_colour, lineOptions);
            }

            // A circle of `radius` about `centre`, in the plane of the unit axes `u` and `v`, from `start` to `end`
            // radians: `segments` lines, drawn as one polyline.
            void Arc(const Math::Vec3& centre, const Math::Vec3& u, const Math::Vec3& v, float radius, float start,
                     float end, int segments) const
            {
                std::vector<Math::Vec3> points;
                points.reserve(static_cast<std::size_t>(segments) + 1);
                for (int i = 0; i <= segments; ++i)
                {
                    const float angle = start + (end - start) * static_cast<float>(i) / static_cast<float>(segments);
                    points.push_back(Place(centre + radius * (std::cos(angle) * u + std::sin(angle) * v)));
                }
                m_draw->Polyline(points, false, m_colour, lineOptions);
            }

          private:
            Math::Vec3 Place(const Math::Vec3& point) const
            {
                return m_position + m_rotation * point;
            }

            DebugDraw* m_draw;
            Math::Vec3 m_position;
            Math::Quat m_rotation;
            Colour m_colour;
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

        Colour ColliderColour(bool isTrigger, MotionType motionType, bool sleeping)
        {
            Colour colour = PhysicsDebugColour::Static;
            if (isTrigger)
            {
                colour = PhysicsDebugColour::Trigger;
            }
            else if (motionType == MotionType::Kinematic)
            {
                colour = PhysicsDebugColour::Kinematic;
            }
            else if (motionType == MotionType::Dynamic)
            {
                colour = PhysicsDebugColour::Dynamic;
            }
            return sleeping ? PhysicsDebugColour::Dimmed(colour) : colour;
        }

        // A wheel's contact colour: the worse of its two slips against its tyres'.
        Colour GripColour(const VehicleWheel& settings, const Backend::VehicleWheelRead& read)
        {
            const float along = std::abs(read.longitudinalSlip);
            const float across = std::abs(read.lateralSlip);
            if (along > settings.longitudinal.slideSlip || across > settings.lateral.slideSlip)
            {
                return PhysicsDebugColour::PastSlideSlip;
            }
            if (along > settings.longitudinal.peakSlip || across > settings.lateral.peakSlip)
            {
                return PhysicsDebugColour::PastPeakSlip;
            }
            return PhysicsDebugColour::Gripping;
        }
    } // namespace

    void PhysicsWorld::SetDebugDraw(PhysicsDebugCategories categories)
    {
        m_impl->m_debugDraw = categories;
    }

    PhysicsDebugCategories PhysicsWorld::DebugDrawCategories() const
    {
        return m_impl->m_debugDraw;
    }

    void PhysicsWorld::Impl::DrawDebugLines(DebugDraw* draw)
    {
        if (draw == nullptr || m_debugDraw == PhysicsDebugCategories::None)
        {
            m_shapeEdges.clear();
            return;
        }
        // Hull and mesh edges are asked of the backend once per mesh while they are drawn; ids are never reused.
        std::map<ShapeEdgesKey, std::vector<Math::Vec3>> used;
        if (Includes(m_debugDraw, PhysicsDebugCategories::Colliders))
        {
            DrawColliders(*draw, used);
        }
        if (Includes(m_debugDraw, PhysicsDebugCategories::Joints))
        {
            DrawJoints(*draw);
        }
        if (Includes(m_debugDraw, PhysicsDebugCategories::Characters))
        {
            DrawCharacters(*draw, used);
        }
        if (Includes(m_debugDraw, PhysicsDebugCategories::Vehicles))
        {
            DrawVehicles(*draw);
        }
        m_shapeEdges = std::move(used);
        if (Includes(m_debugDraw, PhysicsDebugCategories::Contacts))
        {
            for (const Backend::Touch& touch : m_backend.Touches())
            {
                if (touch.isTrigger1 || touch.isTrigger2)
                {
                    continue; // an overlap, not a contact
                }
                const LineWriter write(*draw, touch.point, Math::Quat(1.0f, 0.0f, 0.0f, 0.0f),
                                       PhysicsDebugColour::Contact);
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
            if (Includes(m_debugDraw, PhysicsDebugCategories::Velocities) &&
                record.motion.motionType != MotionType::Static)
            {
                const Math::Vec3 velocity = m_backend.GetState(record.id).linearVelocity;
                if (velocity != Math::Vec3(0.0f))
                {
                    draw->Line(record.world.pos, record.world.pos + velocitySeconds * velocity,
                               PhysicsDebugColour::Velocity, lineOptions);
                }
            }
            if (Includes(m_debugDraw, PhysicsDebugCategories::Bounds))
            {
                const Math::AABB bounds = m_backend.Bounds(record.id);
                Box(LineWriter(*draw, Math::Vec3(0.0f), Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), PhysicsDebugColour::Bounds),
                    bounds.min, bounds.max);
            }
        }
    }

    void PhysicsWorld::Impl::DrawColliders(DebugDraw& draw, std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used)
    {
        for (const auto& [id, record] : m_bodies)
        {
            if (!record.Active())
            {
                continue;
            }
            const bool sleeping = record.motion.motionType != MotionType::Static && m_backend.IsSleeping(record.id);
            const Colour colour = ColliderColour(record.isTrigger, record.motion.motionType, sleeping);
            for (const Part& part : record.parts)
            {
                if (part.usable)
                {
                    DrawPart(draw, part, record.world, colour, used);
                }
            }
        }
    }

    void PhysicsWorld::Impl::DrawCharacters(DebugDraw& draw, std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used)
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
            DrawPart(draw, part, record.world, PhysicsDebugColour::Character, used);

            const Backend::CharacterMoved& moved = record.moved;
            if (moved.ground == GroundState::OnGround || moved.ground == GroundState::OnSteepGround)
            {
                const Colour& colour = moved.ground == GroundState::OnGround ? PhysicsDebugColour::Ground
                                                                             : PhysicsDebugColour::SteepGround;
                draw.Line(moved.groundPoint, moved.groundPoint + groundNormal * moved.groundNormal, colour,
                          lineOptions);
            }
            if (moved.movedVelocity != Math::Vec3(0.0f))
            {
                draw.Line(record.world.pos, record.world.pos + velocitySeconds * moved.movedVelocity,
                          PhysicsDebugColour::Velocity, lineOptions);
            }
        }
    }

    void PhysicsWorld::Impl::DrawVehicles(DebugDraw& draw)
    {
        // Only vehicles with a backend vehicle: disabled and inactive ones have none, and draw nothing.
        const Math::Quat none(1.0f, 0.0f, 0.0f, 0.0f);
        for (const auto& [chassis, record] : m_vehicles)
        {
            if (!record.Made())
            {
                continue;
            }
            const Transform& body = m_bodies.at(chassis).world;
            const Backend::VehicleRead read = m_backend.ReadVehicle(record.id);
            for (std::size_t i = 0; i < record.given.size(); ++i)
            {
                const Backend::VehicleWheelDesc& given = record.given[i];
                const Backend::VehicleWheelRead& wheel = read.wheels[i];
                const Transform pose = WheelPose(given, wheel);

                // The suspension in the chassis's axes: from the top of its travel down to full extension, with a
                // cross-tick across it at the wheel's centre, in the axes of the wheel's mount.
                const LineWriter suspension(draw, body.pos, body.rot, PhysicsDebugColour::Suspension);
                suspension(given.position + given.steeringAxis * given.settings.suspension.travel, given.position);
                for (const Math::Vec3& axis : {xAxis, zAxis})
                {
                    const Math::Vec3 across = suspensionTick * (given.rest * axis);
                    suspension(pose.pos - across, pose.pos + across);
                }

                // The wheel in its own plane, Y and Z about its axle X, with a spoke up its Y.
                const LineWriter rim(draw, body.pos + body.rot * pose.pos, body.rot * pose.rot,
                                     PhysicsDebugColour::Wheel);
                const float radius = given.settings.radius;
                rim.Arc(Math::Vec3(0.0f), yAxis, zAxis, radius, 0.0f, 2.0f * pi, circleSegments);
                rim(Math::Vec3(0.0f), radius * yAxis);

                if (wheel.onGround)
                {
                    const LineWriter contact(draw, wheel.contactPoint, none, GripColour(given.settings, wheel));
                    for (const Math::Vec3& axis : {xAxis, yAxis, zAxis})
                    {
                        contact(-contactCross * axis, contactCross * axis);
                    }
                    contact(Math::Vec3(0.0f), contactNormal * wheel.contactNormal);
                }
            }
        }
    }

    void PhysicsWorld::Impl::DrawJoints(DebugDraw& draw)
    {
        // Every kind draws the same two frames. Shapes of a kind's own (limit arcs, cones) would go here, by kind.
        const auto frame = [&draw](const JointFrame& local, const Transform* body, const Math::Vec3& scale,
                                   float length, const std::array<Colour, 3>& colours) {
            // A frame's position follows its body's scale and its rotation does not, as the constraint was made; the
            // world end's frame is already in world space.
            const Math::Vec3 position =
                body == nullptr ? local.position : body->pos + body->rot * (local.position * scale);
            const Math::Quat rotation = body == nullptr ? local.rotation : body->rot * local.rotation;
            const std::array<Math::Vec3, 3> axes = {xAxis, yAxis, zAxis};
            for (std::size_t i = 0; i < axes.size(); ++i)
            {
                LineWriter(draw, position, rotation, colours[i])(Math::Vec3(0.0f), length * axes[i]);
            }
        };
        for (const auto& [entity, record] : m_joints)
        {
            const Joint& joint = record.applied;
            if (!record.HasConstraint())
            {
                // Inactive: each frame whose body is still a body, in grey. Never the world's: nothing holds it there.
                const std::array<Colour, 3> grey = {PhysicsDebugColour::InactiveJoint,
                                                    PhysicsDebugColour::InactiveJoint,
                                                    PhysicsDebugColour::InactiveJoint};
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
            const auto colour = [&joint](const Colour& full) {
                return joint.enabled ? full : PhysicsDebugColour::Dimmed(full);
            };
            frame(joint.frameA, &a->world, record.scaleA, jointAxisA,
                  {colour(PhysicsDebugColour::JointAX), colour(PhysicsDebugColour::JointAY),
                   colour(PhysicsDebugColour::JointAZ)});
            frame(joint.frameB, toWorld ? nullptr : &b->world, record.scaleB, jointAxisB,
                  {colour(PhysicsDebugColour::JointBX), colour(PhysicsDebugColour::JointBY),
                   colour(PhysicsDebugColour::JointBZ)});
        }
    }

    void PhysicsWorld::Impl::DrawPart(DebugDraw& draw, const Part& part, const Transform& body, const Colour& colour,
                                      std::map<ShapeEdgesKey, std::vector<Math::Vec3>>& used)
    {
        // Each collider at its own world pose: the body's step pose, then the part's pose in the body.
        const Math::Vec3 position = body.pos + body.rot * part.pose.pos;
        const Math::Quat rotation = body.rot * part.pose.rot;
        const Math::Vec3& scale = part.pose.scale;
        const LineWriter write(draw, position, rotation, colour);
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
