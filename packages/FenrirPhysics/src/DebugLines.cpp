// A physics world's debug lines, made after each step from FenrirPhysics's own records (shape, step pose, motion type,
// trigger flag) and what it asks the backend privately (contacts, bounds, sleep, velocity, the edges of a cooked hull
// or mesh, each vehicle's wheels), drawn on the world's DebugCanvas. The bodies and the contacts draw their own; the
// joints, characters and vehicles are drawn here.

#include <array>
#include <cmath>
#include <span>

#include "DebugCanvas.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        using Detail::xAxis;
        using Detail::yAxis;
        using Detail::zAxis;

        // The length of a joint frame's axes: frameA's long, frameB's short, so both show where they coincide.
        constexpr float jointAxisA = 0.5f;
        constexpr float jointAxisB = 0.25f;
        // The length of a character's ground normal.
        constexpr float groundNormal = 0.5f;
        // Half the size of the cross-tick at a vehicle wheel's centre on its suspension.
        constexpr float suspensionTick = 0.08f;

        // A wheel's contact colour: the worse of its two slips against its tyres'.
        std::uint32_t GripColour(const VehicleWheel& settings, const Backend::VehicleWheelRead& read)
        {
            const float along = std::abs(read.longitudinalSlip);
            const float across = std::abs(read.lateralSlip);
            if (along > settings.longitudinal.slideSlip || across > settings.lateral.slideSlip)
            {
                return DebugColour::PastSlideSlip;
            }
            if (along > settings.longitudinal.peakSlip || across > settings.lateral.peakSlip)
            {
                return DebugColour::PastPeakSlip;
            }
            return DebugColour::Gripping;
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
        return m_impl->m_canvas.Lines();
    }

    void PhysicsWorld::Impl::DrawDebugLines()
    {
        if (m_debugDraw == DebugDraw::None)
        {
            m_canvas.Clear();
            return;
        }
        m_canvas.Begin();
        if (Includes(m_debugDraw, DebugDraw::Colliders))
        {
            m_bodies.DrawColliders(m_canvas);
        }
        if (Includes(m_debugDraw, DebugDraw::Joints))
        {
            DrawJoints();
        }
        if (Includes(m_debugDraw, DebugDraw::Characters))
        {
            DrawCharacters();
        }
        if (Includes(m_debugDraw, DebugDraw::Vehicles))
        {
            DrawVehicles();
        }
        if (Includes(m_debugDraw, DebugDraw::Contacts))
        {
            m_contacts.Draw(m_canvas);
        }
        m_bodies.DrawMotion(m_canvas, Includes(m_debugDraw, DebugDraw::Velocities),
                            Includes(m_debugDraw, DebugDraw::Bounds));
        m_canvas.End();
    }

    void PhysicsWorld::Impl::DrawCharacters()
    {
        // Only characters with a backend character: disabled and inactive ones have no record, and draw nothing.
        for (const auto& [id, record] : m_characters)
        {
            // The shape it has, not the Collider's: while a shape edit waits, that is the one bodies and queries see.
            m_canvas.Collider(record.shape,
                              Transform(Math::Vec3(0.0f), Math::Quat(1.0f, 0.0f, 0.0f, 0.0f), record.scale),
                              record.world, DebugColour::Character);

            const Backend::CharacterMoved& moved = record.moved;
            if (moved.ground == GroundState::OnGround || moved.ground == GroundState::OnSteepGround)
            {
                const std::uint32_t colour =
                    moved.ground == GroundState::OnGround ? DebugColour::Ground : DebugColour::SteepGround;
                m_canvas.Line(moved.groundPoint, moved.groundPoint + groundNormal * moved.groundNormal, colour);
            }
            if (moved.movedVelocity != Math::Vec3(0.0f))
            {
                m_canvas.Line(record.world.pos, record.world.pos + Detail::velocitySeconds * moved.movedVelocity,
                              DebugColour::Velocity);
            }
        }
    }

    void PhysicsWorld::Impl::DrawVehicles()
    {
        // Only vehicles with a backend vehicle: disabled and inactive ones have none, and draw nothing.
        for (const auto& [chassis, record] : m_vehicles)
        {
            if (!record.Made())
            {
                continue;
            }
            const Transform& body = BodyView().RecordOf(chassis)->world;
            const Backend::VehicleRead read = m_backend.ReadVehicle(record.id);
            for (std::size_t i = 0; i < record.given.size(); ++i)
            {
                const Backend::VehicleWheelDesc& given = record.given[i];
                const Backend::VehicleWheelRead& wheel = read.wheels[i];
                const Transform pose = WheelPose(given, wheel);

                // The suspension in the chassis's axes: from the top of its travel down to full extension, with a
                // cross-tick across it at the wheel's centre, in the axes of the wheel's mount.
                const Detail::LineWriter suspension = m_canvas.Writer(body.pos, body.rot, DebugColour::Suspension);
                suspension(given.position + given.steeringAxis * given.settings.suspension.travel, given.position);
                for (const Math::Vec3& axis : {xAxis, zAxis})
                {
                    const Math::Vec3 across = suspensionTick * (given.rest * axis);
                    suspension(pose.pos - across, pose.pos + across);
                }

                // The wheel in its own plane, Y and Z about its axle X, with a spoke up its Y.
                const Detail::LineWriter rim =
                    m_canvas.Writer(body.pos + body.rot * pose.pos, body.rot * pose.rot, DebugColour::Wheel);
                const float radius = given.settings.radius;
                rim.Arc(Math::Vec3(0.0f), yAxis, zAxis, radius, 0.0f, 2.0f * Detail::pi, Detail::circleSegments);
                rim(Math::Vec3(0.0f), radius * yAxis);

                if (wheel.onGround)
                {
                    m_canvas.ContactPoint(wheel.contactPoint, wheel.contactNormal, GripColour(given.settings, wheel));
                }
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
                m_canvas.Writer(position, rotation, colours[i])(Math::Vec3(0.0f), length * axes[i]);
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
} // namespace Fenrir
