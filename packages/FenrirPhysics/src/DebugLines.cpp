// A physics world's debug lines, made after each step from FenrirPhysics's own records (shape, step pose, motion type,
// trigger flag) and what it asks the backend privately (contacts, bounds, sleep, velocity, the edges of a cooked hull
// or mesh, each vehicle's wheels), drawn on the world's DebugCanvas. The bodies, joints, characters and contacts draw
// their own; the vehicles are drawn here.

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
            m_joints.Draw(m_canvas);
        }
        if (Includes(m_debugDraw, DebugDraw::Characters))
        {
            m_characters.Draw(m_canvas);
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
} // namespace Fenrir
