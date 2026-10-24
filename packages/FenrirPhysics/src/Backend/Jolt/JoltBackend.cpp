// The Jolt implementation of the physics backend seam (ADR-0010). The only code that includes Jolt.

#include "Backend/Backend.hpp"
#include "Backend/Primitive.hpp"
#include "LayerNames.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "FenrirLogger/ILogger.hpp"
#include "FenrirScheduler/ThreadPool.hpp"

#include <Jolt/Jolt.h>

JPH_SUPPRESS_WARNING_PUSH
JPH_SUPPRESS_WARNINGS
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/FixedSizeFreeList.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemWithBarrier.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollidePointResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Vehicle/MotorcycleController.h>
#include <Jolt/Physics/Vehicle/TrackedVehicleController.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
#include <Jolt/RegisterTypes.h>
JPH_SUPPRESS_WARNING_POP

namespace Fenrir::Backend
{
    namespace
    {
        constexpr std::size_t tempAllocatorBytes = 10 * 1024 * 1024;
        constexpr float wakeMargin = 0.1f;

        // Broad-phase layers, one tree each: static bodies (rarely updated), moving bodies, and triggers.
        enum class BroadPhase : std::uint8_t
        {
            Static,
            Moving,
            Trigger
        };
        constexpr JPH::uint broadPhaseCount = 3;

        BroadPhase BroadPhaseOf(MotionType type)
        {
            return type == MotionType::Static ? BroadPhase::Static : BroadPhase::Moving;
        }

        // Moving bodies pair with everything. Triggers pair with static bodies too (they are kept awake, so they find
        // sleeping and static bodies), but not with each other; static bodies never pair with each other.
        bool BroadPhasesPair(BroadPhase a, BroadPhase b)
        {
            if (a == BroadPhase::Moving || b == BroadPhase::Moving)
            {
                return true;
            }
            return a != b;
        }

        // What an object layer stands for: a broad-phase layer and a collision filter.
        struct LayerKey
        {
            BroadPhase broadPhase;
            std::uint32_t layers;
            std::uint32_t mask;

            auto operator<=>(const LayerKey&) const = default;
        };

        // Each distinct LayerKey in use gets its own object layer, so the filters below evaluate the layer and mask
        // rule on any of the 32 layers (Jolt's ready-made mask filter manages 16). A layer no body uses any more is
        // reused. Changed only between steps, from the thread that runs the App; the filters read it during a step.
        class ObjectLayers
        {
          public:
            ObjectLayers(ILogger& logger, const CollisionLayers* names) : m_logger(&logger), m_names(names)
            {
                // The default filter for each broad-phase layer is always there, the fallback if the layers run out.
                for (JPH::uint i = 0; i < broadPhaseCount; ++i)
                {
                    const LayerKey key{static_cast<BroadPhase>(i), CollisionFilter{}.layers, CollisionFilter{}.mask};
                    m_byKey.emplace(key, static_cast<JPH::ObjectLayer>(m_entries.size()));
                    m_entries.push_back(Entry{key, 1});
                }
            }

            /** The object layer for `key`, counted as used by one more body. */
            JPH::ObjectLayer Acquire(const LayerKey& key)
            {
                JPH::ObjectLayer layer;
                if (const auto it = m_byKey.find(key); it != m_byKey.end())
                {
                    layer = it->second;
                }
                else if (!m_free.empty())
                {
                    layer = m_free.back();
                    m_free.pop_back();
                    m_entries[layer].key = key;
                    m_byKey.emplace(key, layer);
                }
                else if (m_entries.size() < JPH::cObjectLayerInvalid)
                {
                    layer = static_cast<JPH::ObjectLayer>(m_entries.size());
                    m_entries.push_back(Entry{key, 0});
                    m_byKey.emplace(key, layer);
                }
                else
                {
                    m_logger->Error("Physics world uses too many different collision filters at once (" +
                                    std::to_string(JPH::cObjectLayerInvalid) + " at most): a body in " +
                                    LayerNames(m_names, key.layers) + ", colliding with " +
                                    LayerNames(m_names, key.mask) + ", collides with every layer instead");
                    layer = static_cast<JPH::ObjectLayer>(key.broadPhase);
                }
                ++m_entries[layer].users;
                return layer;
            }

            /** One body fewer uses `layer`. */
            void Release(JPH::ObjectLayer layer)
            {
                Entry& entry = m_entries[layer];
                if (--entry.users == 0)
                {
                    m_byKey.erase(entry.key);
                    m_free.push_back(layer);
                }
            }

            [[nodiscard]] const LayerKey& Key(JPH::ObjectLayer layer) const
            {
                return m_entries[layer].key;
            }

          private:
            struct Entry
            {
                LayerKey key;
                std::uint32_t users;
            };

            ILogger* m_logger;
            const CollisionLayers* m_names;
            std::vector<Entry> m_entries;
            std::map<LayerKey, JPH::ObjectLayer> m_byKey;
            std::vector<JPH::ObjectLayer> m_free;
        };

        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
          public:
            explicit BroadPhaseLayers(const ObjectLayers& layers) : m_layers(&layers)
            {
            }

            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return broadPhaseCount;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
            {
                return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(m_layers->Key(layer).broadPhase));
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
            {
                constexpr const char* names[broadPhaseCount] = {"Static", "Moving", "Trigger"};
                return names[layer.GetValue()];
            }
#endif

          private:
            const ObjectLayers* m_layers;
        };

        class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
          public:
            explicit ObjectVsBroadPhase(const ObjectLayers& layers) : m_layers(&layers)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhase) const override
            {
                return BroadPhasesPair(m_layers->Key(layer).broadPhase, static_cast<BroadPhase>(broadPhase.GetValue()));
            }

          private:
            const ObjectLayers* m_layers;
        };

        class ObjectPairs final : public JPH::ObjectLayerPairFilter
        {
          public:
            explicit ObjectPairs(const ObjectLayers& layers) : m_layers(&layers)
            {
            }

            // The layer and mask rule, symmetric as Jolt requires.
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                const LayerKey& first = m_layers->Key(a);
                const LayerKey& second = m_layers->Key(b);
                if (!BroadPhasesPair(first.broadPhase, second.broadPhase))
                {
                    return false;
                }
                return (first.layers & second.mask) != 0 && (second.layers & first.mask) != 0;
            }

          private:
            const ObjectLayers* m_layers;
        };

        // Jolt reports through global functions with no user data, so its messages go to the logger of the newest
        // Context still alive.
        std::vector<ILogger*> s_loggers;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral" // Jolt's messages are printf formats it passes on
#endif
        std::string Format(const char* format, va_list args)
        {
            va_list counting;
            va_copy(counting, args);
            const int length = std::vsnprintf(nullptr, 0, format, counting);
            va_end(counting);
            if (length <= 0)
            {
                return {};
            }
            std::string text(static_cast<std::size_t>(length) + 1, '\0');
            std::vsnprintf(text.data(), text.size(), format, args);
            text.resize(static_cast<std::size_t>(length));
            return text;
        }
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

        void Trace(const char* format, ...)
        {
            va_list args;
            va_start(args, format);
            const std::string message = "Physics backend: " + Format(format, args);
            va_end(args);
            if (!s_loggers.empty())
            {
                s_loggers.back()->Warn(message);
            }
        }

#ifdef JPH_ENABLE_ASSERTS
        bool AssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
        {
            // Jolt asserts when a step runs out of room for body pairs or contacts: a capacity in PhysicsConfig was too
            // small, not a FenrirPhysics bug. World::Step reports it, and the step carries on.
            if (std::string_view(expression) == "errors == EPhysicsUpdateError::None")
            {
                return false;
            }
            const std::string text = "Physics backend assertion failed: " + std::string(expression) + " (" +
                                     (message != nullptr ? message : "") + ") at " + file + ":" + std::to_string(line);
            if (!s_loggers.empty())
            {
                s_loggers.back()->Error(text);
            }
            else
            {
                std::fprintf(stderr, "%s\n", text.c_str());
            }
            return true; // break: a backend assertion is a FenrirPhysics bug
        }
#endif

        JPH::Vec3 ToJolt(const Math::Vec3& v)
        {
            return JPH::Vec3(v.x, v.y, v.z);
        }

        JPH::Quat ToJolt(const Math::Quat& q)
        {
            return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
        }

        Math::Vec3 FromJolt(JPH::Vec3Arg v)
        {
            return Math::Vec3(v.GetX(), v.GetY(), v.GetZ());
        }

        Math::Quat FromJolt(JPH::QuatArg q)
        {
            return Math::Quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
        }

        JPH::EMotionType ToJolt(MotionType type)
        {
            if (type == MotionType::Kinematic)
            {
                return JPH::EMotionType::Kinematic;
            }
            return type == MotionType::Dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static;
        }

        JPH::SpringSettings ToJolt(const JointSpring& spring)
        {
            return JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, spring.frequency, spring.damping);
        }

        JPH::EMotorState ToJolt(JointMotorMode mode)
        {
            if (mode == JointMotorMode::Velocity)
            {
                return JPH::EMotorState::Velocity;
            }
            return mode == JointMotorMode::Position ? JPH::EMotorState::Position : JPH::EMotorState::Off;
        }

        // Jolt's largest float stands for no limit: it multiplies limits by the step time.
        float Finite(float value)
        {
            return std::min(value, FLT_MAX);
        }

        void SetMotor(JPH::MotorSettings& motor, const JointSpring& spring, float maxForce)
        {
            motor.mSpringSettings = ToJolt(spring);
            motor.SetForceLimit(Finite(maxForce));
            motor.SetTorqueLimit(Finite(maxForce));
        }

        // Each kind's settings, already in range (Jolt asserts its ranges). Limits before motor targets: Jolt clamps a
        // position target into the limits it has when the target is set.
        void ApplySettings(JPH::DistanceConstraint& constraint, const DistanceJoint& distance)
        {
            constraint.SetDistance(distance.minDistance, distance.maxDistance);
            constraint.SetLimitsSpringSettings(ToJolt(distance.spring));
        }

        void ApplySettings(JPH::HingeConstraint& constraint, const HingeJoint& hinge)
        {
            const JointLimits& limits = hinge.limits;
            constraint.SetLimits(limits.enabled ? limits.min : -JPH::JPH_PI, limits.enabled ? limits.max : JPH::JPH_PI);
            constraint.SetLimitsSpringSettings(ToJolt(limits.spring));
            constraint.SetMaxFrictionTorque(Finite(hinge.friction));
            SetMotor(constraint.GetMotorSettings(), hinge.motor.spring, hinge.motor.maxForce);
            constraint.SetMotorState(ToJolt(hinge.motor.mode));
            constraint.SetTargetAngularVelocity(hinge.motor.targetVelocity);
            constraint.SetTargetAngle(hinge.motor.targetPosition);
        }

        void ApplySettings(JPH::SliderConstraint& constraint, const SliderJoint& slider)
        {
            const JointLimits& limits = slider.limits;
            constraint.SetLimits(limits.enabled ? limits.min : -FLT_MAX, limits.enabled ? limits.max : FLT_MAX);
            constraint.SetLimitsSpringSettings(ToJolt(limits.spring));
            constraint.SetMaxFrictionForce(Finite(slider.friction));
            SetMotor(constraint.GetMotorSettings(), slider.motor.spring, slider.motor.maxForce);
            constraint.SetMotorState(ToJolt(slider.motor.mode));
            constraint.SetTargetVelocity(slider.motor.targetVelocity);
            constraint.SetTargetPosition(slider.motor.targetPosition);
        }

        void ApplySettings(JPH::SwingTwistConstraint& constraint, const SwingTwistJoint& swingTwist)
        {
            // Jolt's plane cone limits the swing about its normal axis, the frames' Y; its normal cone the swing about
            // its plane axis, the frames' Z.
            constraint.SetPlaneHalfConeAngle(swingTwist.swingYLimit);
            constraint.SetNormalHalfConeAngle(swingTwist.swingZLimit);
            constraint.SetTwistMinAngle(swingTwist.twistMin);
            constraint.SetTwistMaxAngle(swingTwist.twistMax);
            constraint.SetMaxFrictionTorque(Finite(swingTwist.friction));
            const SwingTwistMotor& motor = swingTwist.motor;
            SetMotor(constraint.GetSwingMotorSettings(), motor.spring, motor.maxTorque);
            SetMotor(constraint.GetTwistMotorSettings(), motor.spring, motor.maxTorque);
            constraint.SetSwingMotorState(ToJolt(motor.mode));
            constraint.SetTwistMotorState(ToJolt(motor.mode));
            // Jolt's constraint space is frameA's, and it clamps the target into the cone and twist range.
            constraint.SetTargetOrientationCS(ToJolt(motor.targetRotation));
        }

        // A Velocity motor's target along one axis, cut so that one step at it goes no further than a hard limit
        // `toLimit` away (negative past it): Jolt solves a motor before the limit, so an unbounded motor would
        // otherwise push past the limit. Only a push towards the limit is cut.
        float CutAtLimit(float velocity, float toLimit, float dt)
        {
            const float most = std::max(toLimit, 0.0f) / dt;
            return std::min(velocity, most);
        }

        // Limits a motor cannot push past: enabled and rigid.
        bool IsHard(const JointLimits& limits)
        {
            return limits.enabled && limits.spring.frequency <= 0.0f;
        }

        float CutAtLimits(float velocity, float at, float min, float max, float dt)
        {
            if (velocity > 0.0f)
            {
                return CutAtLimit(velocity, max - at, dt);
            }
            return -CutAtLimit(-velocity, at - min, dt);
        }

        // A swing-twist Velocity motor's target in frameA's axes, cut at the cone and twist limits as CutAtLimits cuts
        // a hinge's, and returned in frameB's axes, as Jolt takes it. `rotation` is frameB's relative to frameA.
        JPH::Vec3 CutAtSwingTwistLimits(JPH::Vec3 velocity, JPH::QuatArg rotation, const SwingTwistJoint& swingTwist,
                                        float dt)
        {
            JPH::Quat swing;
            JPH::Quat twist;
            rotation.GetSwingTwist(swing, twist);

            // The swing as an angle about an axis in frameA's YZ plane, against the ellipse of its two cone limits.
            const float swingSin = JPH::Vec3(0.0f, swing.GetY(), swing.GetZ()).Length();
            if (swingSin > 1e-5f)
            {
                const float angle = 2.0f * std::atan2(swingSin, std::abs(swing.GetW()));
                const float sign = swing.GetW() < 0.0f ? -1.0f : 1.0f;
                const float y = sign * swing.GetY() / swingSin;
                const float z = sign * swing.GetZ() / swingSin;
                // A cone of 0 locks its swing: kept just above it, so the ellipse is defined.
                const float limitY = std::max(swingTwist.swingYLimit, 1e-3f);
                const float limitZ = std::max(swingTwist.swingZLimit, 1e-3f);
                const float reach = 1.0f / std::sqrt((y / limitY) * (y / limitY) + (z / limitZ) * (z / limitZ));
                const JPH::Vec3 outward = JPH::Vec3(0.0f, y / (limitY * limitY), z / (limitZ * limitZ)).Normalized();
                const float out = velocity.Dot(outward);
                const float most = std::max(reach - angle, 0.0f) / dt;
                if (out > most)
                {
                    velocity -= (out - most) * outward;
                }
            }
            JPH::Vec3 inB = rotation.InverseRotate(velocity);
            const float twistAngle = 2.0f * std::atan2(twist.GetX(), twist.GetW());
            const float wrapped = twistAngle > JPH::JPH_PI    ? twistAngle - 2.0f * JPH::JPH_PI
                                  : twistAngle < -JPH::JPH_PI ? twistAngle + 2.0f * JPH::JPH_PI
                                                              : twistAngle;
            inB.SetX(CutAtLimits(inB.GetX(), wrapped, swingTwist.twistMin, swingTwist.twistMax, dt));
            return inB;
        }

        JPH::Ref<JPH::Shape> Make(const Primitive& primitive)
        {
            const Math::Vec3& size = primitive.size;
            if (primitive.type == ShapeType::Sphere)
            {
                return new JPH::SphereShape(size.x);
            }
            if (primitive.type == ShapeType::Capsule)
            {
                return new JPH::CapsuleShape(size.x, size.y);
            }
            // Jolt rounds a box's edges by a convex radius, which must fit inside the box: thin boxes get a smaller
            // one.
            const float convexRadius = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min({size.x, size.y, size.z}));
            return new JPH::BoxShape(ToJolt(size), convexRadius);
        }

        CollisionMesh MeshOf(const CollisionShape& shape)
        {
            return shape.Is<ConvexHullShape>() ? shape.As<ConvexHullShape>().hull : shape.As<MeshShape>().mesh;
        }

        bool IsPrimitive(const CollisionShape& shape)
        {
            return !shape.Is<ConvexHullShape>() && !shape.Is<MeshShape>();
        }

        // A body's mass properties: the shape's own, scaled to `mass`. A triangle mesh has no volume, so it is weighed
        // as the solid box of its bounds.
        JPH::MassProperties MassOf(const JPH::Shape& shape, float mass)
        {
            JPH::MassProperties properties = shape.GetMassProperties();
            if (properties.mMass <= 0.0f)
            {
                const JPH::Vec3 size =
                    JPH::Vec3::sMax(shape.GetLocalBounds().GetSize(), JPH::Vec3::sReplicate(2.0f * minimumSize));
                properties.SetMassAndInertiaOfSolidBox(size, 1.0f);
            }
            properties.ScaleToMass(mass);
            return properties;
        }

        JPH::BodyID ToJolt(BodyId id)
        {
            return JPH::BodyID(id.value);
        }

        std::uint32_t EntityOf(const JPH::Body& body)
        {
            return static_cast<std::uint32_t>(body.GetUserData());
        }

        // The entity of the collider a sub-shape of the body belongs to: a compound's part keeps it, and a body that is
        // not a compound is its own entity's one collider.
        std::uint32_t ColliderOf(const JPH::Body& body, const JPH::SubShapeID& subShape)
        {
            const JPH::Shape* shape = body.GetShape();
            if (shape->GetType() != JPH::EShapeType::Compound)
            {
                return EntityOf(body);
            }
            const auto* compound = static_cast<const JPH::CompoundShape*>(shape);
            JPH::SubShapeID remainder;
            return compound->GetCompoundUserData(compound->GetSubShapeIndexFromID(subShape, remainder));
        }

        // The pairs of bodies joined by an enabled joint that keeps them from colliding, counted by such joints. Jolt
        // gives a body one collision group only, so a body joined to several neighbours needs one filter over all its
        // pairs: each body in such a pair has a group naming this filter, its group ID the body's index. Jolt asks it
        // only after the layer rule allows a pair, so it can only remove pairs, and never for queries. Jolt's job
        // threads call CanCollide during a step: the table changes only between steps, on the stepping thread.
        class JoinedPairs final : public JPH::GroupFilter
        {
          public:
            [[nodiscard]] bool CanCollide(const JPH::CollisionGroup& group1,
                                          const JPH::CollisionGroup& group2) const override
            {
                // A body in no excluded pair, a trigger among them, has no group.
                if (group1.GetGroupFilter() != this || group2.GetGroupFilter() != this)
                {
                    return true;
                }
                return !m_pairs.contains(Key(group1.GetGroupID(), group2.GetGroupID()));
            }

            // Counts a joint excluding the pair; false if the pair was already excluded.
            bool Add(JPH::CollisionGroup::GroupID a, JPH::CollisionGroup::GroupID b)
            {
                return ++m_pairs[Key(a, b)] == 1;
            }

            // Uncounts a joint excluding the pair; false while another still excludes it.
            bool Remove(JPH::CollisionGroup::GroupID a, JPH::CollisionGroup::GroupID b)
            {
                const auto it = m_pairs.find(Key(a, b));
                JPH_ASSERT(it != m_pairs.end());
                if (--it->second > 0)
                {
                    return false;
                }
                m_pairs.erase(it);
                return true;
            }

          private:
            static std::uint64_t Key(JPH::CollisionGroup::GroupID a, JPH::CollisionGroup::GroupID b)
            {
                return (static_cast<std::uint64_t>(std::min(a, b)) << 32u) | std::max(a, b);
            }

            std::unordered_map<std::uint64_t, std::uint32_t> m_pairs;
        };

        // One touching pair of sub-shapes of two bodies.
        struct TouchKey
        {
            std::uint32_t body1;
            std::uint32_t body2;
            std::uint32_t subShape1;
            std::uint32_t subShape2;

            auto operator<=>(const TouchKey&) const = default;
        };

        // Collects the pairs Jolt finds touching during a step. Jolt calls it from its job threads, several at once,
        // in no reproducible order across pairs (callbacks for one pair are serialized), so the step sorts what it
        // collects before using it.
        class TouchListener final : public JPH::ContactListener
        {
          public:
            struct Found
            {
                TouchKey key;
                Touch touch;
            };

            void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                                JPH::ContactSettings&) override
            {
                Record(body1, body2, manifold);
            }

            void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2,
                                    const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
            {
                Record(body1, body2, manifold);
            }

            /** What was found since the last call, ordered by pair, each pair's findings in the order found. */
            std::vector<Found> Take()
            {
                std::vector<Found> found;
                {
                    const std::lock_guard<std::mutex> lock(m_mutex);
                    found.swap(m_found);
                }
                std::stable_sort(found.begin(), found.end(),
                                 [](const Found& a, const Found& b) { return a.key < b.key; });
                return found;
            }

          private:
            static Touch Describe(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold)
            {
                Touch touch;
                touch.body1 = BodyId{body1.GetID().GetIndexAndSequenceNumber()};
                touch.body2 = BodyId{body2.GetID().GetIndexAndSequenceNumber()};
                touch.entity1 = EntityOf(body1);
                touch.entity2 = EntityOf(body2);
                touch.collider1 = ColliderOf(body1, manifold.mSubShapeID1);
                touch.collider2 = ColliderOf(body2, manifold.mSubShapeID2);
                touch.isTrigger1 = body1.IsSensor();
                touch.isTrigger2 = body2.IsSensor();

                // Midway between the bodies' contact points: they differ by the penetration.
                JPH::Vec3 sum = JPH::Vec3::sZero();
                const JPH::uint count = manifold.mRelativeContactPointsOn1.size();
                for (JPH::uint i = 0; i < count; ++i)
                {
                    sum += manifold.mRelativeContactPointsOn1[i] + manifold.mRelativeContactPointsOn2[i];
                }
                const JPH::RVec3 point =
                    count > 0 ? manifold.mBaseOffset + sum / (2.0f * static_cast<float>(count)) : manifold.mBaseOffset;
                touch.point = FromJolt(JPH::Vec3(point));
                touch.normal = FromJolt(manifold.mWorldSpaceNormal);
                touch.relativeSpeed =
                    (body1.GetPointVelocity(point) - body2.GetPointVelocity(point)).Dot(manifold.mWorldSpaceNormal);
                return touch;
            }

            void Record(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold)
            {
                // Jolt does not always name a pair's bodies in the same order: one pair must have one key.
                if (body2.GetID() < body1.GetID())
                {
                    Record(body2, body1, manifold.SwapShapes());
                    return;
                }
                Found found{TouchKey{body1.GetID().GetIndexAndSequenceNumber(),
                                     body2.GetID().GetIndexAndSequenceNumber(), manifold.mSubShapeID1.GetValue(),
                                     manifold.mSubShapeID2.GetValue()},
                            Describe(body1, body2, manifold)};
                const std::lock_guard<std::mutex> lock(m_mutex);
                m_found.push_back(found);
            }

            std::mutex m_mutex;
            std::vector<Found> m_found;
        };

        // How a character's move treats what it touches: a dynamic body never pushes it, so a ball thrown at it, moving
        // into it at the move, is a wall that does not move. Kinematic bodies still do: the move meets them as surfaces
        // moving at their velocity. Jolt pushes nothing itself: it would push a body again at each of the move's
        // collision iterations and stair tests, so a move could push with several times its force (Push does it once).
        //
        // Another character's body, placed after each of its moves at rest, is met moving at that character's last
        // move's velocity, so a character standing on it rides it. It pushes only from beneath: met beside or above, a
        // character pushed out of it would push back on its next move, and the two would shove each other along. A
        // character standing on this one is not met at all: met as a ceiling at rest it would hold this one down, where
        // a jump lifts a rider as it lifts a box on its head.
        class CharacterListener final : public JPH::CharacterContactListener
        {
          public:
            // Each character's body, by its BodyID, with the character and its last move's velocity. Written only
            // between steps, from the thread that moves characters.
            struct Other
            {
                const JPH::CharacterVirtual* character;
                JPH::Vec3 moved;
            };
            std::unordered_map<JPH::uint32, Other> others;

            void OnAdjustBodyVelocity(const JPH::CharacterVirtual*, const JPH::Body& body, JPH::Vec3& linear,
                                      JPH::Vec3& angular) override
            {
                if (const auto it = others.find(body.GetID().GetIndexAndSequenceNumber()); it != others.end())
                {
                    linear = it->second.moved;
                    angular = JPH::Vec3::sZero();
                }
            }

            bool OnContactValidate(const JPH::CharacterVirtual* character,
                                   const JPH::CharacterContact& contact) override
            {
                const auto it = others.find(contact.mBodyB.GetIndexAndSequenceNumber());
                return it == others.end() || !it->second.character->IsSupported() ||
                       it->second.character->GetGroundBodyID() != character->GetInnerBodyID();
            }

            void OnContactAdded(const JPH::CharacterVirtual*, const JPH::CharacterContact& contact,
                                JPH::CharacterContactSettings& settings) override
            {
                Decide(contact, settings);
            }

            void OnContactPersisted(const JPH::CharacterVirtual*, const JPH::CharacterContact& contact,
                                    JPH::CharacterContactSettings& settings) override
            {
                Decide(contact, settings);
            }

            // A standing character's falling speed, taken by the ground, can leave a trace of the ground's push-out
            // pointing back up: Jolt then reads the velocity as reversed and ends the move, before a kinematic body
            // moving into the character has its turn. Jolt's own tolerance for no velocity calls it none.
            void OnContactSolve(const JPH::CharacterVirtual*, const JPH::BodyID&, const JPH::SubShapeID&, JPH::RVec3Arg,
                                JPH::Vec3Arg, JPH::Vec3Arg, const JPH::PhysicsMaterial*, JPH::Vec3Arg,
                                JPH::Vec3& velocity) override
            {
                if (velocity.IsNearZero(1.0e-8f))
                {
                    velocity = JPH::Vec3::sZero();
                }
            }

          private:
            void Decide(const JPH::CharacterContact& contact, JPH::CharacterContactSettings& settings) const
            {
                // Within this of horizontal, a contact's normal faces neither up nor down.
                constexpr float level = 1.0e-3f;
                const bool character = others.contains(contact.mBodyB.GetIndexAndSequenceNumber());
                settings.mCanPushCharacter = contact.mMotionTypeB != JPH::EMotionType::Dynamic &&
                                             (!character || contact.mContactNormal.GetY() > level);
                settings.mCanReceiveImpulses = false;
            }
        };

        // Runs a step's jobs on the App's worker threads. The stepping thread is not one of them: it waits on a
        // barrier, which runs the barrier's jobs itself while it waits (JobSystemWithBarrier). A job already run that
        // way does nothing when a worker reaches it, so the step finishes even with every worker busy elsewhere, or if
        // the stepping thread is itself a worker: there is no deadlock to wait for.
        //
        // A queued job may still be waiting for a worker after the step returns, and releasing it frees it into this
        // job system: each queued job keeps the job system alive (shared ownership) rather than the world waiting.
        class PoolJobs final : public JPH::JobSystemWithBarrier, public std::enable_shared_from_this<PoolJobs>
        {
          public:
            explicit PoolJobs(ThreadPool& pool) : JPH::JobSystemWithBarrier(JPH::cMaxPhysicsBarriers), m_pool(&pool)
            {
                m_jobs.Init(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsJobs);
            }

            // The workers, and the stepping thread working while it waits.
            int GetMaxConcurrency() const override
            {
                return static_cast<int>(m_pool->ThreadCount()) + 1;
            }

            JPH::JobHandle CreateJob(const char* name, JPH::ColorArg color, const JobFunction& function,
                                     JPH::uint32 dependencies) override
            {
                JPH::uint32 index = Jobs::cInvalidObjectIndex;
                while ((index = m_jobs.ConstructObject(name, color, this, function, dependencies)) ==
                       Jobs::cInvalidObjectIndex)
                {
                    std::this_thread::yield(); // all cMaxPhysicsJobs in use: wait for one to be freed
                }
                Job* job = &m_jobs.Get(index);
                JPH::JobHandle handle(job); // the job may finish as soon as it is queued
                if (dependencies == 0)
                {
                    QueueJob(job);
                }
                return handle;
            }

          protected:
            void QueueJob(Job* job) override
            {
                job->AddRef();
                m_pool->Enqueue([self = shared_from_this(), job] {
                    job->Execute();
                    job->Release();
                });
            }

            void QueueJobs(Job** jobs, JPH::uint count) override
            {
                for (JPH::uint i = 0; i < count; ++i)
                {
                    QueueJob(jobs[i]);
                }
            }

            void FreeJob(Job* job) override
            {
                m_jobs.DestructObject(job);
            }

          private:
            using Jobs = JPH::FixedSizeFreeList<Job>;

            ThreadPool* m_pool;
            Jobs m_jobs;
        };

        // The job system a world steps with: the App's worker threads, or none (every job runs as it is queued).
        std::shared_ptr<JPH::JobSystem> MakeJobs(ThreadPool* workers)
        {
            if (workers == nullptr)
            {
                return std::make_shared<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
            }
            return std::make_shared<PoolJobs>(*workers);
        }

        // A tracked vehicle's wheel: the track it is on, and how far back it sits (chassis-local z).
        struct TrackedWheel
        {
            VehicleTrackSide side = VehicleTrackSide::Left;
            float z = 0.0f;
        };
    } // namespace

    Context::Context(ILogger& logger) : m_logger(&logger)
    {
        if (s_loggers.empty())
        {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = Trace;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailed;)
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
        s_loggers.push_back(m_logger);
    }

    Context::~Context()
    {
        // Contexts may end in any order: remove this one's logger, newest first.
        const auto it = std::find(s_loggers.rbegin(), s_loggers.rend(), m_logger);
        if (it != s_loggers.rend())
        {
            s_loggers.erase(std::next(it).base());
        }
        if (s_loggers.empty())
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

    struct CollisionMeshes::Impl
    {
        // A mesh's source data and the shapes cooked from it so far. The source is dropped once both kinds are cooked.
        struct Entry
        {
            JPH::Array<JPH::Vec3> vertices;
            JPH::IndexedTriangleList triangles;
            JPH::Ref<JPH::Shape> hull;
            JPH::Ref<JPH::Shape> mesh;
            bool hullFailed = false;
            bool meshFailed = false;
        };

        explicit Impl(ILogger& meshesLogger) : logger(&meshesLogger)
        {
        }

        // The shape a hull or mesh refers to, cooked the first time; null if the mesh is released or cannot be cooked
        // as that kind (an error the first time).
        const JPH::Shape* Shape(const CollisionShape& shape)
        {
            const CollisionMesh handle = MeshOf(shape);
            const auto it = meshes.find(handle.id);
            if (it == meshes.end())
            {
                return nullptr;
            }
            Entry& entry = it->second;
            const bool hull = shape.Is<ConvexHullShape>();
            JPH::Ref<JPH::Shape>& cooked = hull ? entry.hull : entry.mesh;
            bool& failed = hull ? entry.hullFailed : entry.meshFailed;
            if (cooked == nullptr && !failed)
            {
                cooked = Cook(handle, hull, entry);
                failed = cooked == nullptr;
                if ((entry.hull != nullptr || entry.hullFailed) && (entry.mesh != nullptr || entry.meshFailed))
                {
                    entry.vertices = {};
                    entry.triangles = {};
                }
            }
            return cooked.GetPtr();
        }

        // For a query, which may run beside other queries: the shape a hull or mesh refers to without changing
        // anything. Not yet cooked, it is cooked for this query alone; null if the mesh is released or cannot be cooked
        // as that kind (the error is logged when a body first uses it).
        JPH::Ref<JPH::Shape> QueryShape(const CollisionShape& shape) const
        {
            const auto it = meshes.find(MeshOf(shape).id);
            if (it == meshes.end())
            {
                return nullptr;
            }
            const Entry& entry = it->second;
            const bool hull = shape.Is<ConvexHullShape>();
            if (const JPH::Ref<JPH::Shape>& cooked = hull ? entry.hull : entry.mesh; cooked != nullptr)
            {
                return cooked;
            }
            if ((hull ? entry.hullFailed : entry.meshFailed) || (!hull && entry.triangles.empty()))
            {
                return nullptr;
            }
            const JPH::ShapeSettings::ShapeResult result = Build(hull, entry);
            return result.HasError() ? nullptr : result.Get();
        }

        // Cooks a hull (`hull`) or triangle mesh from a mesh's source data, logging an error if it cannot.
        JPH::Ref<JPH::Shape> Cook(CollisionMesh handle, bool hull, const Entry& entry)
        {
            if (!hull && entry.triangles.empty())
            {
                logger->Error("Collision mesh " + std::to_string(handle.id) +
                              " has no triangles, so it cannot be a triangle mesh");
                return nullptr;
            }
            const JPH::ShapeSettings::ShapeResult result = Build(hull, entry);
            if (result.HasError())
            {
                logger->Error("Collision mesh " + std::to_string(handle.id) + " cannot be cooked as " +
                              (hull ? "a convex hull" : "a triangle mesh") + ": " + std::string(result.GetError()));
                return nullptr;
            }
            return result.Get();
        }

        // Builds a hull (`hull`) or triangle mesh (which needs triangles) from a mesh's source data. Only reads, and
        // logs nothing, so queries may build throwaway shapes side by side.
        static JPH::ShapeSettings::ShapeResult Build(bool hull, const Entry& entry)
        {
            if (hull)
            {
                return JPH::ConvexHullShapeSettings(entry.vertices).Create();
            }
            JPH::VertexList vertices;
            vertices.reserve(entry.vertices.size());
            for (JPH::Vec3Arg v : entry.vertices)
            {
                vertices.push_back(JPH::Float3(v.GetX(), v.GetY(), v.GetZ()));
            }
            return JPH::MeshShapeSettings(std::move(vertices), entry.triangles).Create();
        }

        ILogger* logger;
        // Ids start at 1 (0 is the default handle, no mesh) and only grow, so a released id is never handed out again.
        std::uint64_t nextId = 1;
        std::unordered_map<std::uint64_t, Entry> meshes;
    };

    CollisionMeshes::CollisionMeshes(ILogger& logger) : m_impl(std::make_unique<Impl>(logger))
    {
    }

    CollisionMeshes::~CollisionMeshes() = default;

    CollisionMesh CollisionMeshes::Create(std::span<const Math::Vec3> vertices, std::span<const std::uint32_t> indices)
    {
        const char* problem = nullptr;
        if (vertices.empty())
        {
            problem = "it has no vertices";
        }
        else if (indices.size() % 3 != 0)
        {
            problem = "its index count is not a multiple of three";
        }
        else if (std::any_of(indices.begin(), indices.end(), [&](std::uint32_t i) { return i >= vertices.size(); }))
        {
            problem = "an index is past the last vertex";
        }
        if (problem != nullptr)
        {
            m_impl->logger->Error(std::string("A collision mesh was not made: ") + problem);
            return CollisionMesh{};
        }

        Impl::Entry entry;
        entry.vertices.reserve(vertices.size());
        for (const Math::Vec3& v : vertices)
        {
            entry.vertices.push_back(ToJolt(v));
        }
        entry.triangles.reserve(indices.size() / 3);
        for (std::size_t i = 0; i < indices.size(); i += 3)
        {
            entry.triangles.push_back(JPH::IndexedTriangle(indices[i], indices[i + 1], indices[i + 2]));
        }
        const CollisionMesh mesh{m_impl->nextId++};
        m_impl->meshes.emplace(mesh.id, std::move(entry));
        return mesh;
    }

    bool CollisionMeshes::Release(CollisionMesh mesh)
    {
        if (m_impl->meshes.erase(mesh.id) == 0)
        {
            m_impl->logger->Warn("Collision mesh " + std::to_string(mesh.id) +
                                 " was released, but it is not alive (never made, or already released)");
            return false;
        }
        return true;
    }

    struct World::Impl
    {
        Impl(CollisionMeshes& worldMeshes, ILogger& worldLogger, JPH::uint bodyCapacity, ThreadPool* workers,
             const CollisionLayers* layerNames)
            : meshes(&worldMeshes.Implementation()), logger(&worldLogger), maxBodies(bodyCapacity),
              objectLayers(worldLogger, layerNames), broadPhaseLayers(objectLayers), objectVsBroadPhase(objectLayers),
              objectPairs(objectLayers), jobs(MakeJobs(workers))
        {
        }

        // The backend shape for a collider, the entity's scale baked in. Identical primitives share one shape; hulls
        // and meshes share their cooked shape, wrapped per body when scaled (they take any scale per axis).
        JPH::Ref<JPH::Shape> MakeShape(const CollisionShape& shape, const Math::Vec3& scale)
        {
            if (IsPrimitive(shape))
            {
                const Primitive primitive = Bake(shape, scale);
                if (const auto it = primitives.find(primitive); it != primitives.end())
                {
                    return it->second;
                }
                PruneUnused();
                return primitives.emplace(primitive, Make(primitive)).first->second;
            }
            const JPH::Shape* cooked = meshes->Shape(shape);
            JPH_ASSERT(cooked != nullptr);
            return Scaled(*cooked, scale);
        }

        // A cooked hull or mesh at `scale`: wrapped, so the cooked shape stays shared (hulls and meshes take any scale
        // per axis). Only reads `cooked`.
        static JPH::Ref<JPH::Shape> Scaled(const JPH::Shape& cooked, const Math::Vec3& scale)
        {
            return cooked.ScaleShape(cooked.MakeScaleValid(ToJolt(scale))).Get();
        }

        // A body's shape: its one collider's own shape when that collider is on the body's entity (so identical bodies
        // share it), else a compound of its colliders, each part keeping its collider's entity as its user data.
        JPH::Ref<JPH::Shape> MakeBodyShape(std::span<const ColliderPart> parts, std::uint32_t entity)
        {
            JPH_ASSERT(!parts.empty());
            if (parts.size() == 1 && parts[0].entity == entity)
            {
                return MakeShape(parts[0].shape, parts[0].scale);
            }
            const auto add = [this](JPH::CompoundShapeSettings& compound, const ColliderPart& part) {
                compound.AddShape(ToJolt(part.position), ToJolt(part.rotation), MakeShape(part.shape, part.scale),
                                  part.entity);
            };
            JPH::Shape::ShapeResult result;
            if (parts.size() == 1)
            {
                // A static compound needs two parts at least.
                JPH::MutableCompoundShapeSettings compound;
                add(compound, parts[0]);
                result = compound.Create();
            }
            else
            {
                JPH::StaticCompoundShapeSettings compound;
                for (const ColliderPart& part : parts)
                {
                    add(compound, part);
                }
                result = compound.Create(tempAllocator);
            }
            JPH_ASSERT(result.IsValid());
            return result.Get();
        }

        // The backend shape for a query's shape, by the same rules as MakeShape but touching no cache, so queries can
        // run side by side. Null for a hull or mesh whose collision mesh was released or cannot be cooked.
        JPH::Ref<JPH::Shape> MakeQueryShape(const CollisionShape& shape, const Math::Vec3& scale) const
        {
            if (IsPrimitive(shape))
            {
                return Make(Bake(shape, scale));
            }
            const JPH::Ref<JPH::Shape> cooked = meshes->QueryShape(shape);
            return cooked == nullptr ? nullptr : Scaled(*cooked, scale);
        }

        // Forgets cached shapes no body uses any more, each time the cache has doubled since it was last pruned.
        void PruneUnused()
        {
            if (primitives.size() < pruneAt)
            {
                return;
            }
            std::erase_if(primitives, [](const auto& entry) { return entry.second->GetRefCount() == 1; });
            pruneAt = std::max(minimumPruneAt, 2 * primitives.size());
        }

        static constexpr std::size_t minimumPruneAt = 64;

        CollisionMeshes::Impl* meshes;
        // The private shape cache keyed by value: one backend shape per distinct primitive.
        std::map<Primitive, JPH::Ref<JPH::Shape>> primitives;
        std::size_t pruneAt = minimumPruneAt;
        ILogger* logger;
        JPH::uint maxBodies;
        ObjectLayers objectLayers;
        BroadPhaseLayers broadPhaseLayers;
        ObjectVsBroadPhase objectVsBroadPhase;
        ObjectPairs objectPairs;
        JPH::TempAllocatorImpl tempAllocator{tempAllocatorBytes};
        std::shared_ptr<JPH::JobSystem> jobs;
        JPH::PhysicsSystem system;
        TouchListener listener;

        // The pairs touching after the last step, and what changed in that step.
        std::map<TouchKey, Touch> touching;
        std::vector<Touch> touches;
        std::vector<TouchChange> changes;

        // Brings `touching` up to date after a step. A pair Jolt found is touching. A pair it did not find has
        // separated, unless neither body was awake when the step began: Jolt drops the contacts of sleeping bodies
        // without their separating, and finds them again when one wakes, and a body falling asleep must not end what it
        // touches.
        void UpdateTouches(const std::set<std::uint32_t>& awakeAtStart)
        {
            changes.clear();
            std::set<TouchKey> found;
            for (TouchListener::Found& each : listener.Take())
            {
                found.insert(each.key);
                const auto it = touching.find(each.key);
                if (it == touching.end())
                {
                    // Characters are made and destroyed only between steps, so their bodies are known here.
                    each.touch.isCharacter1 = characterListener.others.contains(each.key.body1);
                    each.touch.isCharacter2 = characterListener.others.contains(each.key.body2);
                    touching.emplace(each.key, each.touch);
                    changes.push_back(TouchChange{TouchChange::Kind::Begin, each.touch});
                }
                else
                {
                    it->second.point = each.touch.point;
                    it->second.normal = each.touch.normal;
                }
            }
            for (auto it = touching.begin(); it != touching.end();)
            {
                const TouchKey& key = it->first;
                const bool ended =
                    !found.contains(key) &&
                    (awakeAtStart.contains(key.body1) || awakeAtStart.contains(key.body2) ||
                     !Bodies().IsAdded(JPH::BodyID(key.body1)) || !Bodies().IsAdded(JPH::BodyID(key.body2)));
                if (ended)
                {
                    changes.push_back(TouchChange{TouchChange::Kind::End, it->second});
                    it = touching.erase(it);
                }
                else
                {
                    ++it;
                }
            }
            touches.clear();
            for (const auto& [key, touch] : touching)
            {
                touches.push_back(touch);
            }
        }

        // Every call comes from the thread that runs the App, never during a step, so no body locks are needed.
        JPH::BodyInterface& Bodies()
        {
            return system.GetBodyInterfaceNoLock();
        }

        JPH::AABox Bounds(BodyId id)
        {
            JPH::BodyLockRead lock(system.GetBodyLockInterfaceNoLock(), ToJolt(id));
            return lock.Succeeded() ? lock.GetBody().GetWorldSpaceBounds() : JPH::AABox();
        }

        // Sleeping bodies are not woken by a body that is removed, moved or reshaped beside them (Jolt wakes only the
        // body changed), so wake whatever is near where it was. Expanded so bodies resting on it are included.
        void WakeAround(const JPH::AABox& bounds)
        {
            if (!bounds.IsValid())
            {
                return;
            }
            JPH::AABox around = bounds;
            around.ExpandBy(JPH::Vec3::sReplicate(wakeMargin));
            Bodies().ActivateBodiesInAABox(around, {}, {});
        }

        JPH::ObjectLayer AcquireLayer(MotionType type, const CollisionFilter& filter)
        {
            return objectLayers.Acquire(LayerKey{BroadPhaseOf(type), filter.layers, filter.mask});
        }

        // Moves a body to the object layer for `key` (after `change` edits its key), waking it and what is near it:
        // pairs it now does or no longer collides with are found again only for awake bodies.
        template <typename Change>
        void Relayer(BodyId id, Change&& change)
        {
            const JPH::ObjectLayer old = Bodies().GetObjectLayer(ToJolt(id));
            LayerKey key = objectLayers.Key(old);
            change(key);
            if (key == objectLayers.Key(old))
            {
                return;
            }
            Bodies().SetObjectLayer(ToJolt(id), objectLayers.Acquire(key));
            objectLayers.Release(old);
            // A vehicle's wheels probe with its body's layer, and the old one may now stand for another key.
            for (VehicleSlot& slot : vehicles)
            {
                if (slot.constraint != nullptr && slot.body.value == id.value)
                {
                    Retest(slot);
                }
            }
            WakeAround(Bounds(id));
        }

        template <typename Func>
        void Write(BodyId id, Func&& func)
        {
            JPH::BodyLockWrite lock(system.GetBodyLockInterfaceNoLock(), ToJolt(id));
            if (lock.Succeeded())
            {
                func(lock.GetBody());
            }
        }

        // The joints' constraints, by JointId, and the bodies each joins (the world's is the default BodyId). A freed
        // slot has no constraint, and is reused.
        struct JointSlot
        {
            JPH::Ref<JPH::TwoBodyConstraint> constraint;
            JointType type = JointType::Fixed;
            // The kind's settings last applied, in range: a Velocity motor's target is cut from them before each step.
            JointKind kind;
            BodyId bodyA;
            BodyId bodyB;
            // Whether it keeps its bodies from colliding: counted in `joinedPairs`.
            bool excludes = false;
        };
        std::vector<JointSlot> joints;
        std::vector<std::uint32_t> freeJoints;

        JPH::Ref<JoinedPairs> joinedPairs = new JoinedPairs();
        // How many excluded pairs each body is in, by its index: a body has a collision group while it is in any.
        std::unordered_map<JPH::uint32, std::uint32_t> excludedPairsOf;

        // The time of the last Step's collision steps: a joint's impulses are per collision step.
        float collisionStepTime = 0.0f;

        // The characters, by CharacterId, each with its body's object layer, which also filters its moves. A freed
        // slot has no character, and is reused.
        struct CharacterSlot
        {
            JPH::Ref<JPH::CharacterVirtual> character;
            JPH::ObjectLayer layer = 0;
        };
        std::vector<CharacterSlot> characters;
        std::vector<std::uint32_t> freeCharacters;
        CharacterListener characterListener;

        // What a vehicle's wheels ask before probing a body: never its own chassis or a character's inner body, so a
        // character is never ground. Triggers the probes skip themselves. Called on job threads during a step, so it
        // reads only what was fixed before the step (characters are added and removed between steps), and never
        // locks.
        class ProbeFilter final : public JPH::BodyFilter
        {
          public:
            ProbeFilter(JPH::BodyID chassis, const CharacterListener& characterBodies)
                : m_chassis(chassis), m_characters(&characterBodies)
            {
            }

            bool ShouldCollide(const JPH::BodyID& body) const override
            {
                return body != m_chassis && !m_characters->others.contains(body.GetIndexAndSequenceNumber());
            }

          private:
            JPH::BodyID m_chassis;
            const CharacterListener* m_characters;
        };

        // The vehicles, by VehicleId, each with its own wheel settings (its wheels read them by reference, so they are
        // patched in place) and the tester and filter its wheels probe with. A freed slot has no constraint, and is
        // reused.
        struct VehicleSlot
        {
            JPH::Ref<JPH::VehicleConstraint> constraint;
            // A tracked vehicle's are WheelSettingsTV, the others' WheelSettingsWV.
            std::vector<JPH::Ref<JPH::WheelSettings>> wheels;
            JPH::Ref<JPH::VehicleCollisionTester> tester;
            std::unique_ptr<ProbeFilter> filter;
            VehicleProbe probe = VehicleProbe::Cylinder;
            BodyId body;
            // Its controller: a motorcycle's is a wheeled one that leans; a tracked vehicle's has no differentials.
            // Each is fixed when the vehicle is made, as are whether it is powered and a motorcycle's lean limit.
            VehicleController controller = VehicleController::Wheeled;
            bool powered = true;
            float maxLeanAngle = 0.0f;
            // A tracked vehicle's wheels, in wheel order, and its tracks' settings, left then right: a wheel's grip is
            // its track's.
            std::vector<TrackedWheel> trackedWheels;
            std::array<VehicleTrack, 2> tracks;
        };
        std::vector<VehicleSlot> vehicles;
        std::vector<std::uint32_t> freeVehicles;

        // Gives a vehicle a new collision tester for its probe, from its body's object layer and the world's up as
        // they are now: the tester holds both. Ray and sphere stop at an 80 degree slope; the sphere is the smallest
        // wheel's size, so it fits every wheel's cast. A motorcycle's cylinder is fully rounded, as in Jolt's sample,
        // so a leaning wheel rolls on its edge.
        void Retest(VehicleSlot& slot)
        {
            const JPH::ObjectLayer layer = Bodies().GetObjectLayer(ToJolt(slot.body));
            const JPH::Vec3 up = (-system.GetGravity()).NormalizedOr(JPH::Vec3::sAxisY());
            const float maxSlope = JPH::DegreesToRadians(80.0f);
            if (slot.probe == VehicleProbe::Ray)
            {
                slot.tester = new JPH::VehicleCollisionTesterRay(layer, up, maxSlope);
            }
            else if (slot.probe == VehicleProbe::Sphere)
            {
                float radius = slot.wheels.front()->mRadius;
                for (const JPH::Ref<JPH::WheelSettings>& wheel : slot.wheels)
                {
                    radius = std::min(radius, wheel->mRadius);
                }
                slot.tester = new JPH::VehicleCollisionTesterCastSphere(layer, radius, up, maxSlope);
            }
            else
            {
                slot.tester = new JPH::VehicleCollisionTesterCastCylinder(
                    layer, slot.controller == VehicleController::Motorcycle ? 1.0f : 0.1f);
            }
            slot.tester->SetBodyFilter(slot.filter.get());
            slot.constraint->SetVehicleCollisionTester(slot.tester);
        }

        // A vehicle made from `desc`, its constraint added to the system and its body woken. Never during a step.
        VehicleSlot MakeVehicle(const VehicleDesc& desc);

        // Takes a vehicle's constraint out of the system, waking its body. Never during a step.
        void UnmakeVehicle(const VehicleSlot& slot)
        {
            system.RemoveStepListener(slot.constraint);
            system.RemoveConstraint(slot.constraint);
            Bodies().ActivateBody(ToJolt(slot.body));
        }

        // Counts or uncounts the joint in `joinedPairs`, giving a body a collision group when it enters its first
        // excluded pair and taking it away when it leaves its last. Never during a step.
        void SetExcludes(JointSlot& slot, bool excludes)
        {
            if (slot.excludes == excludes)
            {
                return;
            }
            slot.excludes = excludes;
            const JPH::uint32 a = ToJolt(slot.bodyA).GetIndex();
            const JPH::uint32 b = ToJolt(slot.bodyB).GetIndex();
            if (excludes ? !joinedPairs->Add(a, b) : !joinedPairs->Remove(a, b))
            {
                return;
            }
            for (const BodyId body : {slot.bodyA, slot.bodyB})
            {
                const JPH::BodyID id = ToJolt(body);
                std::uint32_t& count = excludedPairsOf[id.GetIndex()];
                if (excludes && count++ == 0)
                {
                    Bodies().SetCollisionGroup(id, JPH::CollisionGroup(joinedPairs, id.GetIndex(), 0));
                }
                else if (!excludes && --count == 0)
                {
                    excludedPairsOf.erase(id.GetIndex());
                    Bodies().SetCollisionGroup(id, JPH::CollisionGroup());
                }
            }
        }

        // Jolt wakes nothing when a constraint is added, removed or edited: every change to a joint wakes its bodies.
        void WakeJoint(const JointSlot& slot)
        {
            for (const BodyId body : {slot.bodyA, slot.bodyB})
            {
                if (body.value != BodyId{}.value)
                {
                    Bodies().ActivateBody(ToJolt(body));
                }
            }
        }

        // Before each step: cuts each Velocity motor's target at the hard limits its joint is at or about to reach,
        // so that a hard limit always beats a motor, whatever its strength.
        void CutMotorsAtLimits(float dt)
        {
            for (JointSlot& slot : joints)
            {
                if (slot.constraint == nullptr)
                {
                    continue;
                }
                if (slot.type == JointType::Hinge)
                {
                    const HingeJoint& hinge = slot.kind.As<HingeJoint>();
                    if (hinge.motor.mode == JointMotorMode::Velocity && IsHard(hinge.limits))
                    {
                        auto& constraint = static_cast<JPH::HingeConstraint&>(*slot.constraint);
                        constraint.SetTargetAngularVelocity(CutAtLimits(hinge.motor.targetVelocity,
                                                                        constraint.GetCurrentAngle(), hinge.limits.min,
                                                                        hinge.limits.max, dt));
                    }
                }
                else if (slot.type == JointType::Slider)
                {
                    const SliderJoint& slider = slot.kind.As<SliderJoint>();
                    if (slider.motor.mode == JointMotorMode::Velocity && IsHard(slider.limits))
                    {
                        auto& constraint = static_cast<JPH::SliderConstraint&>(*slot.constraint);
                        constraint.SetTargetVelocity(CutAtLimits(slider.motor.targetVelocity,
                                                                 constraint.GetCurrentPosition(), slider.limits.min,
                                                                 slider.limits.max, dt));
                    }
                }
                else if (slot.type == JointType::SwingTwist)
                {
                    const SwingTwistJoint& swingTwist = slot.kind.As<SwingTwistJoint>();
                    if (swingTwist.motor.mode == JointMotorMode::Velocity)
                    {
                        auto& constraint = static_cast<JPH::SwingTwistConstraint&>(*slot.constraint);
                        constraint.SetTargetAngularVelocityCS(
                            CutAtSwingTwistLimits(ToJolt(swingTwist.motor.targetAngularVelocity),
                                                  constraint.GetRotationInConstraintSpace(), swingTwist, dt));
                    }
                }
            }
        }

        // A frame relative to the body's origin, as relative to its centre of mass, where Jolt's local space is. The
        // world's frame is already in its space.
        JointFrame FromCentreOfMass(BodyId body, const JointFrame& frame)
        {
            if (body.value == BodyId{}.value)
            {
                return frame;
            }
            return JointFrame{frame.position - FromJolt(Bodies().GetShape(ToJolt(body))->GetCenterOfMass()),
                              frame.rotation};
        }
    };

    namespace
    {
        // Jolt cannot go beyond its own limits: a larger capacity is lowered to the limit, and that is logged.
        JPH::uint Capacity(std::uint32_t requested, JPH::uint limit, const char* what, ILogger& logger)
        {
            if (requested <= limit)
            {
                return requested;
            }
            logger.Error("Physics world capacity of " + std::to_string(requested) + " " + what + " is more than the " +
                         std::to_string(limit) + " the backend allows: using " + std::to_string(limit));
            return limit;
        }
    } // namespace

    World::World(const WorldSettings& settings, CollisionMeshes& meshes, ILogger& logger)
        : m_impl(std::make_unique<Impl>(
              meshes, logger, Capacity(settings.maxBodies, JPH::PhysicsSystem::cMaxBodiesLimit, "bodies", logger),
              settings.workers, settings.layerNames))
    {
        m_impl->system.Init(
            m_impl->maxBodies, 0,
            Capacity(settings.maxBodyPairs, JPH::PhysicsSystem::cMaxBodyPairsLimit, "body pairs", logger),
            Capacity(settings.maxContacts, JPH::PhysicsSystem::cMaxContactConstraintsLimit, "contacts", logger),
            m_impl->broadPhaseLayers, m_impl->objectVsBroadPhase, m_impl->objectPairs);
        m_impl->system.SetGravity(ToJolt(settings.gravity));
        m_impl->system.SetContactListener(&m_impl->listener);
    }

    World::~World()
    {
        // Vehicles before their bodies: each points at its body.
        for (const Impl::VehicleSlot& slot : m_impl->vehicles)
        {
            if (slot.constraint != nullptr)
            {
                m_impl->system.RemoveStepListener(slot.constraint);
                m_impl->system.RemoveConstraint(slot.constraint);
            }
        }
        // Characters before any body: each removes and destroys its own body.
        m_impl->characters.clear();
        // Constraints before bodies: Jolt's constraints point at their bodies.
        for (const Impl::JointSlot& slot : m_impl->joints)
        {
            if (slot.constraint != nullptr)
            {
                m_impl->system.RemoveConstraint(slot.constraint);
            }
        }
        // Jolt leaves destroying bodies to its user.
        JPH::BodyIDVector ids;
        m_impl->system.GetBodies(ids);
        if (!ids.empty())
        {
            m_impl->Bodies().RemoveBodies(ids.data(), static_cast<int>(ids.size()));
            m_impl->Bodies().DestroyBodies(ids.data(), static_cast<int>(ids.size()));
        }
    }

    bool World::CanMakeShape(const CollisionShape& shape)
    {
        return IsPrimitive(shape) || m_impl->meshes->Shape(shape) != nullptr;
    }

    void World::CreateBodies(std::span<const BodyDesc> bodies, std::span<BodyId> ids)
    {
        std::vector<JPH::BodyID> created;
        created.reserve(bodies.size());
        for (std::size_t i = 0; i < bodies.size(); ++i)
        {
            const BodyDesc& desc = bodies[i];
            const MotionType motionType = desc.isTrigger ? MotionType::Kinematic : desc.motion.motionType;
            const JPH::ObjectLayer layer =
                desc.isTrigger
                    ? m_impl->objectLayers.Acquire(LayerKey{BroadPhase::Trigger, desc.filter.layers, desc.filter.mask})
                    : m_impl->AcquireLayer(motionType, desc.filter);
            const JPH::Ref<JPH::Shape> shape = m_impl->MakeBodyShape(desc.parts, desc.entity);
            JPH::BodyCreationSettings settings(shape, ToJolt(desc.position), ToJolt(desc.rotation), ToJolt(motionType),
                                               layer);
            settings.mAllowDynamicOrKinematic = desc.canMove;
            // Jolt merges contacts of a body's different parts that face the same way, which would hide all but one of
            // a compound body's colliders from its contacts.
            settings.mUseManifoldReduction = shape->GetType() != JPH::EShapeType::Compound;
            if (desc.isTrigger)
            {
                // A kinematic sensor kept awake sees sleeping bodies, and with this flag static ones too.
                settings.mIsSensor = true;
                settings.mCollideKinematicVsNonDynamic = true;
                settings.mAllowSleeping = false;
            }
            settings.mUserData = desc.entity;
            settings.mLinearVelocity = ToJolt(desc.linearVelocity);
            settings.mAngularVelocity = ToJolt(desc.angularVelocity);
            settings.mLinearDamping = desc.motion.linearDamping;
            settings.mAngularDamping = desc.motion.angularDamping;
            settings.mGravityFactor = desc.motion.gravityScale;
            settings.mFriction = desc.material.friction;
            settings.mRestitution = desc.material.restitution;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride = MassOf(*shape, desc.motion.mass);

            JPH::Body* body = m_impl->Bodies().CreateBody(settings);
            if (body == nullptr)
            {
                m_impl->objectLayers.Release(settings.mObjectLayer);
                m_impl->logger->Error("Physics world is full: a body was not made (" +
                                      std::to_string(m_impl->maxBodies) + " at most)");
                ids[i] = BodyId{};
                continue;
            }
            ids[i] = BodyId{body->GetID().GetIndexAndSequenceNumber()};
            created.push_back(body->GetID());
        }
        if (created.empty())
        {
            return;
        }
        // Prepare may reorder the ids; the BodyIds above were taken before.
        const auto state = m_impl->Bodies().AddBodiesPrepare(created.data(), static_cast<int>(created.size()));
        m_impl->Bodies().AddBodiesFinalize(created.data(), static_cast<int>(created.size()), state,
                                           JPH::EActivation::Activate);
    }

    void World::DestroyBodies(std::span<const BodyId> ids)
    {
        std::vector<JPH::BodyID> bodies;
        bodies.reserve(ids.size());
        for (BodyId id : ids)
        {
            if (id.value != BodyId{}.value)
            {
                bodies.push_back(ToJolt(id));
            }
        }
        if (bodies.empty())
        {
            return;
        }
#ifndef NDEBUG
        // A constraint outliving its body is undefined in Jolt.
        for (const Impl::JointSlot& slot : m_impl->joints)
        {
            for (const BodyId id : ids)
            {
                assert((slot.constraint == nullptr || (slot.bodyA.value != id.value && slot.bodyB.value != id.value)) &&
                       "World::DestroyBodies: a joint's constraint must be destroyed before its bodies");
            }
        }
#endif
        std::vector<JPH::AABox> bounds;
        bounds.reserve(ids.size());
        for (BodyId id : ids)
        {
            bounds.push_back(m_impl->Bounds(id));
        }
        for (JPH::BodyID body : bodies)
        {
            m_impl->objectLayers.Release(m_impl->Bodies().GetObjectLayer(body));
        }
        m_impl->Bodies().RemoveBodies(bodies.data(), static_cast<int>(bodies.size()));
        m_impl->Bodies().DestroyBodies(bodies.data(), static_cast<int>(bodies.size()));
        for (const JPH::AABox& box : bounds)
        {
            m_impl->WakeAround(box);
        }
    }

    JointId World::CreateJoint(const JointDesc& desc)
    {
        const JointFrame a = m_impl->FromCentreOfMass(desc.bodyA, desc.frameA);
        const JointFrame b = m_impl->FromCentreOfMass(desc.bodyB, desc.frameB);
        const JPH::RVec3 pointA(ToJolt(a.position));
        const JPH::RVec3 pointB(ToJolt(b.position));
        const JPH::Quat rotationA = ToJolt(a.rotation);
        const JPH::Quat rotationB = ToJolt(b.rotation);

        JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
        if (desc.kind.Is<FixedJoint>())
        {
            auto* fixed = new JPH::FixedConstraintSettings();
            fixed->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            fixed->mPoint1 = pointA;
            fixed->mAxisX1 = rotationA * JPH::Vec3::sAxisX();
            fixed->mAxisY1 = rotationA * JPH::Vec3::sAxisY();
            fixed->mPoint2 = pointB;
            fixed->mAxisX2 = rotationB * JPH::Vec3::sAxisX();
            fixed->mAxisY2 = rotationB * JPH::Vec3::sAxisY();
            settings = fixed;
        }
        else if (desc.kind.Is<BallJoint>())
        {
            auto* ball = new JPH::PointConstraintSettings();
            ball->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            ball->mPoint1 = pointA;
            ball->mPoint2 = pointB;
            settings = ball;
        }
        else if (desc.kind.Is<DistanceJoint>())
        {
            auto* distance = new JPH::DistanceConstraintSettings();
            distance->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            distance->mPoint1 = pointA;
            distance->mPoint2 = pointB;
            distance->mMinDistance = desc.kind.As<DistanceJoint>().minDistance;
            distance->mMaxDistance = desc.kind.As<DistanceJoint>().maxDistance;
            settings = distance;
        }
        else if (desc.kind.Is<SliderJoint>())
        {
            auto* slider = new JPH::SliderConstraintSettings();
            slider->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            slider->mPoint1 = pointA;
            slider->mSliderAxis1 = rotationA * JPH::Vec3::sAxisX();
            slider->mNormalAxis1 = rotationA * JPH::Vec3::sAxisY();
            slider->mPoint2 = pointB;
            slider->mSliderAxis2 = rotationB * JPH::Vec3::sAxisX();
            slider->mNormalAxis2 = rotationB * JPH::Vec3::sAxisY();
            settings = slider;
        }
        else if (desc.kind.Is<SwingTwistJoint>())
        {
            auto* swingTwist = new JPH::SwingTwistConstraintSettings();
            swingTwist->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            swingTwist->mPosition1 = pointA;
            // Jolt's constraint space is (twist, plane x twist, plane): with the plane axis on the frames' Z, it is the
            // frames' own axes, so its swing about Y and Z, its motor targets and its rotation are in frameA's terms.
            swingTwist->mTwistAxis1 = rotationA * JPH::Vec3::sAxisX();
            swingTwist->mPlaneAxis1 = rotationA * JPH::Vec3::sAxisZ();
            swingTwist->mPosition2 = pointB;
            swingTwist->mTwistAxis2 = rotationB * JPH::Vec3::sAxisX();
            swingTwist->mPlaneAxis2 = rotationB * JPH::Vec3::sAxisZ();
            settings = swingTwist;
        }
        else
        {
            auto* hinge = new JPH::HingeConstraintSettings();
            hinge->mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
            hinge->mPoint1 = pointA;
            hinge->mHingeAxis1 = rotationA * JPH::Vec3::sAxisX();
            hinge->mNormalAxis1 = rotationA * JPH::Vec3::sAxisY();
            hinge->mPoint2 = pointB;
            hinge->mHingeAxis2 = rotationB * JPH::Vec3::sAxisX();
            hinge->mNormalAxis2 = rotationB * JPH::Vec3::sAxisY();
            settings = hinge;
        }
        // Jolt sorts each island's constraints by priority, then by their index in its constraint list, which changes
        // as constraints are removed: distinct priorities keep the solve order to which joints exist.
        settings->mConstraintPriority = desc.priority;

        Impl::JointSlot slot;
        // An invalid BodyID stands for Jolt's fixed-to-world body.
        slot.constraint = m_impl->Bodies().CreateConstraint(settings, ToJolt(desc.bodyA), ToJolt(desc.bodyB));
        slot.type = desc.kind.Type();
        slot.bodyA = desc.bodyA;
        slot.bodyB = desc.bodyB;
        m_impl->system.AddConstraint(slot.constraint);

        std::uint32_t index = 0;
        if (m_impl->freeJoints.empty())
        {
            index = static_cast<std::uint32_t>(m_impl->joints.size());
            m_impl->joints.push_back(std::move(slot));
        }
        else
        {
            index = m_impl->freeJoints.back();
            m_impl->freeJoints.pop_back();
            m_impl->joints[index] = std::move(slot);
        }
        UpdateJoint(JointId{index}, desc.kind, desc.enabled, desc.collideConnected);
        return JointId{index};
    }

    void World::DestroyJoint(JointId id)
    {
        Impl::JointSlot& slot = m_impl->joints[id.value];
        m_impl->SetExcludes(slot, false);
        m_impl->WakeJoint(slot);
        m_impl->system.RemoveConstraint(slot.constraint);
        slot = Impl::JointSlot{};
        m_impl->freeJoints.push_back(id.value);
    }

    void World::UpdateJoint(JointId id, const JointKind& kind, bool enabled, bool collideConnected)
    {
        Impl::JointSlot& slot = m_impl->joints[id.value];
        assert(kind.Type() == slot.type && "World::UpdateJoint: a joint's kind changes only by recreating it");
        slot.kind = kind;
        JPH::TwoBodyConstraint& constraint = *slot.constraint;
        switch (kind.Type())
        {
        case JointType::Distance:
            ApplySettings(static_cast<JPH::DistanceConstraint&>(constraint), kind.As<DistanceJoint>());
            break;
        case JointType::Hinge:
            ApplySettings(static_cast<JPH::HingeConstraint&>(constraint), kind.As<HingeJoint>());
            break;
        case JointType::Slider:
            ApplySettings(static_cast<JPH::SliderConstraint&>(constraint), kind.As<SliderJoint>());
            break;
        case JointType::SwingTwist:
            ApplySettings(static_cast<JPH::SwingTwistConstraint&>(constraint), kind.As<SwingTwistJoint>());
            break;
        default:
            break;
        }
        slot.constraint->SetEnabled(enabled);
        // A joint to the world has no pair to exclude.
        m_impl->SetExcludes(slot, enabled && !collideConnected && slot.bodyB.value != BodyId{}.value);
        m_impl->WakeJoint(slot);
    }

    void World::DisableJoint(JointId id)
    {
        Impl::JointSlot& slot = m_impl->joints[id.value];
        slot.constraint->SetEnabled(false);
        m_impl->SetExcludes(slot, false);
    }

    namespace
    {
        // Private tuning (ADR-0014): how far a character keeps from what it touches. Jolt places a character's shape
        // this far above its position, so that position is the collider's centre less it.
        constexpr float characterPadding = 0.02f;

        // Jolt keeps a character's contacts sorted, so its moves are reproducible, only while they fit in this many.
        constexpr JPH::uint characterHits = 256;

        // A contact whose normal is within this of horizontal faces neither up nor down: a wall's.
        constexpr float level = 1.0e-3f;

        JPH::RVec3 CharacterPosition(const Math::Vec3& centre)
        {
            return JPH::RVec3(ToJolt(centre) - characterPadding * JPH::Vec3::sAxisY());
        }

        Math::Vec3 CentreOf(const JPH::CharacterVirtual& character)
        {
            return FromJolt(JPH::Vec3(character.GetPosition()) + characterPadding * JPH::Vec3::sAxisY());
        }

        // A character's convex shape's support function, relative to its centre of mass: a hull's at its scale.
        const JPH::ConvexShape::Support* SupportOf(const JPH::Shape& shape, JPH::ConvexShape::ESupportMode mode,
                                                   JPH::ConvexShape::SupportBuffer& buffer)
        {
            const JPH::Shape* convex = &shape;
            JPH::Vec3 scale = JPH::Vec3::sOne();
            if (shape.GetSubType() == JPH::EShapeSubType::Scaled)
            {
                const auto& scaled = static_cast<const JPH::ScaledShape&>(shape);
                convex = scaled.GetInnerShape();
                scale = scaled.GetScale();
            }
            return static_cast<const JPH::ConvexShape*>(convex)->GetSupportFunction(mode, buffer, scale);
        }

        // Which contacts can be a character's ground: Jolt's supporting volume, a plane in the character's own space
        // (at its position and rotation) with world +Y as its normal, below which a contact supports it. It lies where
        // the shape's rounding begins at its bottom: a capsule's lower hemisphere, a sphere's lower half, a box's or
        // hull's rounded bottom edge.
        JPH::Plane SupportingVolume(const JPH::Shape& shape, JPH::QuatArg rotation)
        {
            const JPH::Vec3 up = rotation.Conjugated() * JPH::Vec3::sAxisY();
            JPH::ConvexShape::SupportBuffer buffer;
            const JPH::ConvexShape::Support* support =
                SupportOf(shape, JPH::ConvexShape::ESupportMode::ExcludeConvexRadius, buffer);
            // The lowest point of the shape's core, inside its rounding: the shape sits at its centre of mass, raised
            // by the padding.
            const JPH::Vec3 lowest = shape.GetCenterOfMass() + characterPadding * up + support->GetSupport(-up);
            return JPH::Plane(up, -up.Dot(lowest));
        }

        // How far a character's shape at `rotation` reaches from its position along the world unit `direction`: to its
        // farthest point, rounding included.
        float Reach(const JPH::Shape& shape, JPH::QuatArg rotation, JPH::Vec3Arg direction)
        {
            JPH::ConvexShape::SupportBuffer buffer;
            const JPH::ConvexShape::Support* support =
                SupportOf(shape, JPH::ConvexShape::ESupportMode::IncludeConvexRadius, buffer);
            const JPH::Vec3 farthest = shape.GetCenterOfMass() + support->GetSupport(rotation.Conjugated() * direction);
            return direction.Dot(rotation * farthest);
        }

        GroundState FromJolt(JPH::CharacterBase::EGroundState state)
        {
            if (state == JPH::CharacterBase::EGroundState::OnGround)
            {
                return GroundState::OnGround;
            }
            if (state == JPH::CharacterBase::EGroundState::OnSteepGround)
            {
                return GroundState::OnSteepGround;
            }
            return state == JPH::CharacterBase::EGroundState::NotSupported ? GroundState::Touching : GroundState::InAir;
        }

        // `velocity` less its part going into a surface along `normal` (towards the character), measured against the
        // surface's own velocity.
        JPH::Vec3 Trim(JPH::Vec3Arg velocity, JPH::Vec3Arg normal, JPH::Vec3Arg surface)
        {
            const float into = (velocity - surface).Dot(normal);
            return into < 0.0f ? velocity - into * normal : velocity;
        }

        // How steep a character's contact is, judged by the face it touches rather than its contact normal (the spec's
        // rounded-edge rule): on a box's rounded edge Jolt's surface normal is the edge's own once it is steeper than
        // the side face's, so a step's edge would read as steep ground. The face of the touched shape facing up is
        // taken instead when the contact lies on it or on its rounding (FenrirPhysics rounds by at most Jolt's default
        // convex radius), and it faces up more. Otherwise Jolt's surface normal stands.
        JPH::Vec3 FaceNormal(const JPH::PhysicsSystem& system, const JPH::CharacterContact& contact)
        {
            const JPH::Vec3 up = JPH::Vec3::sAxisY();
            // Met from the side, it is a side face and not its rounding; another character has no faces.
            if (contact.mContactNormal.Dot(up) <= level || !contact.mCharacterIDB.IsInvalid())
            {
                return contact.mSurfaceNormal;
            }
            JPH::BodyLockRead lock(system.GetBodyLockInterfaceNoLock(), contact.mBodyB);
            if (!lock.Succeeded())
            {
                return contact.mSurfaceNormal;
            }
            // The face's corners relative to the contact.
            JPH::Shape::SupportingFace face;
            lock.GetBody().GetTransformedShape().GetSupportingFace(contact.mSubShapeIDB, -up, contact.mPosition, face);
            if (face.size() < 3)
            {
                return contact.mSurfaceNormal;
            }
            JPH::Vec3 normal = (face[1] - face[0]).Cross(face[2] - face[0]).NormalizedOr(JPH::Vec3::sZero());
            normal = normal.Dot(up) < 0.0f ? -normal : normal;
            if (normal.Dot(up) <= contact.mSurfaceNormal.Dot(up))
            {
                return contact.mSurfaceNormal;
            }
            constexpr float reach = JPH::cDefaultConvexRadius + 1.0e-3f;
            if (std::abs(normal.Dot(face[0])) > reach)
            {
                return contact.mSurfaceNormal;
            }
            JPH::Vec3 centre = JPH::Vec3::sZero();
            for (JPH::Vec3Arg corner : face)
            {
                centre += corner;
            }
            centre /= static_cast<float>(face.size());
            for (JPH::uint i = 0; i < face.size(); ++i)
            {
                const JPH::Vec3 corner = face[i];
                JPH::Vec3 outward =
                    (face[(i + 1) % face.size()] - corner).Cross(normal).NormalizedOr(JPH::Vec3::sZero());
                outward = outward.Dot(corner - centre) < 0.0f ? -outward : outward;
                if (-corner.Dot(outward) > reach)
                {
                    return contact.mSurfaceNormal;
                }
            }
            return normal;
        }

        // What a character stands on, from the contacts Jolt found, each judged by FaceNormal: Jolt's ground state,
        // except that ground Jolt reads as steep is walkable when the face of one of its supporting contacts is.
        struct Ground
        {
            GroundState state = GroundState::InAir;
            // OnGround and OnSteepGround only: the most upward-facing supporting contact, and the faces' normals
            // averaged, as Jolt averages its surface normals.
            const JPH::CharacterContact* contact = nullptr;
            JPH::Vec3 normal = JPH::Vec3::sZero();
        };

        Ground GroundOf(const JPH::PhysicsSystem& system, const JPH::CharacterVirtual& character)
        {
            Ground ground;
            ground.state = FromJolt(character.GetGroundState());
            if (!character.IsSupported())
            {
                return ground;
            }
            const JPH::Vec3 up = JPH::Vec3::sAxisY();
            const JPH::RMat44 toCharacter =
                JPH::RMat44::sInverseRotationTranslation(character.GetRotation(), character.GetPosition());
            bool walkable = false;
            float best = -FLT_MAX;
            JPH::Vec3 averaged = JPH::Vec3::sZero();
            for (const JPH::CharacterContact& contact : character.GetActiveContacts())
            {
                if (!contact.mHadCollision || contact.mWasDiscarded ||
                    character.GetSupportingVolume().SignedDistance(JPH::Vec3(toCharacter * contact.mPosition)) > 0.0f)
                {
                    continue;
                }
                const JPH::Vec3 face = FaceNormal(system, contact);
                walkable = walkable || !character.IsSlopeTooSteep(face);
                if (face.Dot(up) > best)
                {
                    best = face.Dot(up);
                    ground.contact = &contact;
                    ground.normal = face;
                }
                // As Jolt: faces within 85° of up are averaged.
                if (face.Dot(up) >= 0.08f)
                {
                    averaged += face;
                }
            }
            ground.normal = averaged.NormalizedOr(ground.normal);
            if (walkable)
            {
                ground.state = GroundState::OnGround;
            }
            return ground;
        }

        // Pushes the dynamic bodies a character's move hit, as Jolt's CharacterVirtual would: an impulse along each
        // contact's normal aiming at 0.9 × the closing speed at the contact (and pushing a body out of the shape it is
        // in), never downward, but with `force × dt` at most for each body over the whole move.
        void Push(JPH::PhysicsSystem& system, const JPH::CharacterVirtual& character, JPH::Vec3Arg velocity,
                  float force, float dt)
        {
            constexpr float damping = 0.9f;
            constexpr float penetrationResolution = 0.4f;
            const JPH::Vec3 up = JPH::Vec3::sAxisY();
            // What each body pushed so far may still take, in order of first hit.
            std::vector<std::pair<JPH::BodyID, float>> left;
            for (const JPH::CharacterContact& contact : character.GetActiveContacts())
            {
                if (!contact.mHadCollision || contact.mWasDiscarded || contact.mIsSensorB ||
                    contact.mMotionTypeB != JPH::EMotionType::Dynamic)
                {
                    continue;
                }
                const float closing = (velocity - contact.mLinearVelocity).Dot(contact.mContactNormal);
                const float wanted =
                    -closing * damping - std::min(contact.mDistance, 0.0f) * penetrationResolution / dt;
                if (wanted <= 0.0f)
                {
                    continue;
                }
                auto budget = std::find_if(left.begin(), left.end(),
                                           [&](const auto& each) { return each.first == contact.mBodyB; });
                if (budget == left.end())
                {
                    budget = left.insert(left.end(), {contact.mBodyB, force * dt});
                }
                JPH::BodyLockWrite lock(system.GetBodyLockInterfaceNoLock(), contact.mBodyB);
                if (!lock.SucceededAndIsInBroadPhase() || budget->second <= 0.0f)
                {
                    continue;
                }
                const JPH::Body& body = lock.GetBody();
                // The body's mass as the contact feels it along the normal, turning included.
                const JPH::Vec3 arm = JPH::Vec3(contact.mPosition - body.GetCenterOfMassPosition());
                const JPH::Vec3 jacobian = arm.Cross(contact.mContactNormal);
                const float inverseMass = body.GetInverseInertia().Multiply3x3(jacobian).Dot(jacobian) +
                                          body.GetMotionProperties()->GetInverseMass();
                const float impulse = std::min(wanted / inverseMass, budget->second);
                budget->second -= impulse;
                JPH::Vec3 pushed = -impulse * contact.mContactNormal;
                pushed -= std::min(pushed.Dot(up), 0.0f) * up;
                system.GetBodyInterfaceNoLock().AddImpulse(contact.mBodyB, pushed, contact.mPosition);
            }
        }

        // What a character touches at the end of its move: the contacts it collided with, one per collider touched (the
        // first in Jolt's sorted order), as its body would. `moving` is its velocity at the start of the move.
        std::vector<Touch> ContactsOf(const JPH::PhysicsSystem& system, const CharacterListener& listener,
                                      const JPH::CharacterVirtual& character, JPH::Vec3Arg moving)
        {
            std::vector<Touch> contacts;
            const auto self = static_cast<std::uint32_t>(character.GetUserData());
            const BodyId inner{character.GetInnerBodyID().GetIndexAndSequenceNumber()};
            for (const JPH::CharacterContact& contact : character.GetActiveContacts())
            {
                if (!contact.mHadCollision || contact.mWasDiscarded || contact.mIsSensorB)
                {
                    continue;
                }
                JPH::BodyLockRead lock(system.GetBodyLockInterfaceNoLock(), contact.mBodyB);
                if (!lock.Succeeded())
                {
                    continue;
                }
                const std::uint32_t entity = EntityOf(lock.GetBody());
                const std::uint32_t collider = ColliderOf(lock.GetBody(), contact.mSubShapeIDB);
                if (std::ranges::any_of(contacts, [&](const Touch& each) {
                        return each.entity2 == entity && each.collider2 == collider;
                    }))
                {
                    continue;
                }
                // Jolt's contact normal points from what it touches to the character.
                const JPH::Vec3 normal = -contact.mContactNormal;
                contacts.push_back(
                    Touch{.body1 = inner,
                          .body2 = BodyId{contact.mBodyB.GetIndexAndSequenceNumber()},
                          .entity1 = self,
                          .entity2 = entity,
                          .collider1 = self,
                          .collider2 = collider,
                          .isCharacter1 = true,
                          .isCharacter2 = listener.others.contains(contact.mBodyB.GetIndexAndSequenceNumber()),
                          .point = FromJolt(JPH::Vec3(contact.mPosition)),
                          .normal = FromJolt(normal),
                          .relativeSpeed = (moving - contact.mLinearVelocity).Dot(normal)});
            }
            return contacts;
        }

        // A character moves against what its body collides with, but through triggers, which see it through its body.
        class CharacterBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
        {
          public:
            CharacterBroadPhaseFilter(const JPH::ObjectVsBroadPhaseLayerFilter& pairs, JPH::ObjectLayer layer)
                : m_pairs(pairs, layer)
            {
            }

            bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
            {
                constexpr JPH::BroadPhaseLayer triggers(static_cast<JPH::BroadPhaseLayer::Type>(BroadPhase::Trigger));
                return layer != triggers && m_pairs.ShouldCollide(layer);
            }

          private:
            JPH::DefaultBroadPhaseLayerFilter m_pairs;
        };
    } // namespace

    void World::CreateCharacters(std::span<const CharacterDesc> characters, std::span<CharacterId> ids)
    {
        for (std::size_t i = 0; i < characters.size(); ++i)
        {
            const CharacterDesc& desc = characters[i];
            const JPH::Quat rotation = ToJolt(desc.rotation);
            JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
            settings->mShape = m_impl->MakeShape(desc.shape, desc.scale);
            settings->mInnerBodyShape = settings->mShape;
            settings->mInnerBodyLayer = m_impl->AcquireLayer(MotionType::Kinematic, desc.filter);
            // From the entity, not Jolt's process-wide counter: the same scene gives the same IDs on every run. The
            // inner body's BodyID comes from the body allocator in batch order, as every body's does.
            settings->mID = JPH::CharacterID(desc.entity);
            settings->mCharacterPadding = characterPadding;
            settings->mMaxNumHits = characterHits;
            settings->mSupportingVolume = SupportingVolume(*settings->mShape, rotation);
            settings->mEnhancedInternalEdgeRemoval = true;

            JPH::Ref<JPH::CharacterVirtual> character = new JPH::CharacterVirtual(
                settings, CharacterPosition(desc.position), rotation, desc.entity, &m_impl->system);
            if (character->GetInnerBodyID().IsInvalid())
            {
                m_impl->objectLayers.Release(settings->mInnerBodyLayer);
                m_impl->logger->Error("Physics world is full: a character was not made (" +
                                      std::to_string(m_impl->maxBodies) + " bodies at most)");
                ids[i] = CharacterId{};
                continue;
            }
            character->SetListener(&m_impl->characterListener);
            const JPH::BodyID inner = character->GetInnerBodyID();
            m_impl->Bodies().SetFriction(inner, desc.material.friction);
            m_impl->Bodies().SetRestitution(inner, desc.material.restitution);
            m_impl->characterListener.others.emplace(inner.GetIndexAndSequenceNumber(),
                                                     CharacterListener::Other{character.GetPtr(), JPH::Vec3::sZero()});
            const CharacterBroadPhaseFilter broadPhase(m_impl->objectVsBroadPhase, settings->mInnerBodyLayer);
            const JPH::DefaultObjectLayerFilter layer(m_impl->objectPairs, settings->mInnerBodyLayer);
            character->RefreshContacts(broadPhase, layer, {}, {}, m_impl->tempAllocator);

            std::uint32_t index;
            if (m_impl->freeCharacters.empty())
            {
                index = static_cast<std::uint32_t>(m_impl->characters.size());
                m_impl->characters.emplace_back();
            }
            else
            {
                index = m_impl->freeCharacters.back();
                m_impl->freeCharacters.pop_back();
            }
            m_impl->characters[index] = Impl::CharacterSlot{character, settings->mInnerBodyLayer};
            ids[i] = CharacterId{index};
        }
    }

    void World::DestroyCharacters(std::span<const CharacterId> ids)
    {
        for (const CharacterId id : ids)
        {
            if (id.value == CharacterId{}.value)
            {
                continue;
            }
            Impl::CharacterSlot& slot = m_impl->characters[id.value];
            const JPH::uint32 inner = slot.character->GetInnerBodyID().GetIndexAndSequenceNumber();
            const JPH::AABox bounds = m_impl->Bounds(BodyId{inner});
            m_impl->characterListener.others.erase(inner);
            slot.character = nullptr;
            m_impl->objectLayers.Release(slot.layer);
            m_impl->freeCharacters.push_back(id.value);
            m_impl->WakeAround(bounds);
        }
    }

    CharacterMoved World::MoveCharacter(CharacterId id, const CharacterMove& move, float dt)
    {
        const Impl::CharacterSlot& slot = m_impl->characters[id.value];
        JPH::CharacterVirtual& character = *slot.character;
        const JPH::Vec3 up = JPH::Vec3::sAxisY();
        const JPH::Vec3 velocity = ToJolt(move.velocity);
        character.SetMaxSlopeAngle(move.maxSlope);
        // Its mass presses on the ground under the gravity passed below, which the move uses for nothing else.
        character.SetMass(move.mass);

        // Riding: the last move ended on walkable ground, whose velocity is added to this move and never to game
        // code's. Read now, after this tick's MoveKinematic: Jolt's cached value is a tick old.
        JPH::Vec3 ground = JPH::Vec3::sZero();
        const Ground before = GroundOf(m_impl->system, character);
        const bool riding = before.state == GroundState::OnGround;
        if (riding)
        {
            character.UpdateGroundVelocity();
            ground = character.GetGroundVelocity();
        }
        JPH::Vec3 carried = ToJolt(move.carried);
        // What the character moves at, and so pushes at. On walkable ground with stopOnSlope the falling part moves it
        // no more than the trim below leaves it in `velocity`: pressed into a slope, it would slide down it.
        JPH::Vec3 moving = velocity + carried + ground;
        if (riding && move.stopOnSlope)
        {
            moving -= std::min(velocity.Dot(up), 0.0f) * up;
        }
        character.SetLinearVelocity(moving);

        JPH::CharacterVirtual::ExtendedUpdateSettings update;
        update.mStickToFloorStepDown = -move.floorSnap * up;
        update.mWalkStairsStepUp = move.stepHeight * up;
        const CharacterBroadPhaseFilter broadPhase(m_impl->objectVsBroadPhase, slot.layer);
        const JPH::DefaultObjectLayerFilter layer(m_impl->objectPairs, slot.layer);
        const JPH::RVec3 start = character.GetPosition();
        character.ExtendedUpdate(dt, m_impl->system.GetGravity(), update, broadPhase, layer, {}, {},
                                 m_impl->tempAllocator);
        Push(m_impl->system, character, moving, move.pushForce, dt);

        CharacterMoved moved;
        moved.position = CentreOf(character);
        const JPH::Vec3 displaced = JPH::Vec3(character.GetPosition() - start) / dt;
        moved.movedVelocity = FromJolt(displaced);
        // What the other characters' moves see its body moving at, until its next move.
        m_impl->characterListener.others.at(character.GetInnerBodyID().GetIndexAndSequenceNumber()).moved = displaced;
        const Ground after = GroundOf(m_impl->system, character);
        moved.ground = after.state;
        if (after.contact != nullptr)
        {
            JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterfaceNoLock(), after.contact->mBodyB);
            if (lock.Succeeded())
            {
                moved.groundEntity = EntityOf(lock.GetBody());
                moved.groundCollider = ColliderOf(lock.GetBody(), after.contact->mSubShapeIDB);
            }
            moved.groundPoint = FromJolt(JPH::Vec3(after.contact->mPosition));
            moved.groundNormal = FromJolt(after.normal);
            // Jolt found it at the move's end from this tick's body velocities, at the character.
            moved.groundVelocity = FromJolt(character.GetGroundVelocity());
            if (lock.Succeeded())
            {
                moved.groundAngularVelocity = FromJolt(lock.GetBody().GetAngularVelocity());
            }
        }

        // Trimmed against what it touches at the end of the move, relative to each surface's own velocity, and never
        // measured from displacement, so a stair step-up or a ground snap does not show (the trim rule):
        //  - what faces up (ground, a slope, a step's edge) takes only the falling part, so walking never turns into
        //    climbing speed: all of it on walkable ground with stopOnSlope, otherwise its part into the surface, so
        //    the rest slides the character downhill;
        //  - a wall or a ceiling takes the part going into it, along its normal;
        //  - all of it in the frame of the ground it rode, so a descending lift's own speed is never taken for falling.
        // Carried velocity is trimmed first, and only loses its part going into a surface, down to none: it never takes
        // on a surface's own push, so a kinematic body shoving the character shows in `velocity`. What still goes into
        // a surface after it is game code's.
        JPH::Vec3 kept = velocity;
        const auto into = [&](JPH::Vec3Arg part, JPH::Vec3Arg normal, JPH::Vec3Arg surface) {
            const float along = carried.Dot(normal);
            if ((carried - surface).Dot(normal) < 0.0f)
            {
                carried += (std::min(surface.Dot(normal), std::max(along, 0.0f)) - along) * normal;
            }
            return Trim(part + carried, normal, surface) - carried;
        };
        const auto touch = [&](JPH::Vec3Arg contactNormal, bool tooSteep, JPH::Vec3Arg surfaceVelocity) {
            const JPH::Vec3 surface = surfaceVelocity - ground;
            if (contactNormal.Dot(up) <= level)
            {
                kept = into(kept, contactNormal, surface);
            }
            else if (move.stopOnSlope && !tooSteep)
            {
                kept = into(kept, up, surface);
            }
            else
            {
                const JPH::Vec3 vertical = kept.Dot(up) * up;
                kept = kept - vertical + into(vertical, contactNormal, surface);
            }
        };
        for (const JPH::CharacterContact& contact : character.GetActiveContacts())
        {
            if (!contact.mHadCollision || contact.mWasDiscarded || contact.mIsSensorB)
            {
                continue;
            }
            // Judged by its face, as the ground is: a step's rounded edge is neither steep nor a wall.
            const bool tooSteep = character.IsSlopeTooSteep(contact.mSurfaceNormal) &&
                                  character.IsSlopeTooSteep(FaceNormal(m_impl->system, contact));
            touch(contact.mContactNormal, tooSteep, contact.mLinearVelocity);
            const float facing = contact.mContactNormal.Dot(up);
            if (facing < -level)
            {
                moved.onCeiling = true;
            }
            else if (&contact != after.contact && (facing <= level || tooSteep))
            {
                moved.onWall = true;
            }
        }
        if (after.contact != nullptr)
        {
            // A character standing still hits nothing in its move: its ground still takes what presses into it.
            touch(after.normal, after.state == GroundState::OnSteepGround, character.GetGroundVelocity());
        }
        moved.velocity = FromJolt(kept);
        // Landing clears carried velocity; leaving walkable ground (a jump included) carries all of what it rode.
        if (moved.ground == GroundState::OnGround)
        {
            carried = JPH::Vec3::sZero();
        }
        else if (riding)
        {
            carried = ground;
        }
        moved.carriedVelocity = FromJolt(carried);
        moved.contacts = ContactsOf(m_impl->system, m_impl->characterListener, character, moving);
        return moved;
    }

    void World::TeleportCharacter(CharacterId id, const Math::Vec3& position, const Math::Quat& rotation)
    {
        const Impl::CharacterSlot& slot = m_impl->characters[id.value];
        JPH::CharacterVirtual& character = *slot.character;
        character.SetPosition(CharacterPosition(position));
        TurnCharacter(id, rotation);
        // Otherwise the next move starts from the contacts where it was: stuck to a floor it left, say.
        const CharacterBroadPhaseFilter broadPhase(m_impl->objectVsBroadPhase, slot.layer);
        const JPH::DefaultObjectLayerFilter layer(m_impl->objectPairs, slot.layer);
        character.RefreshContacts(broadPhase, layer, {}, {}, m_impl->tempAllocator);
    }

    void World::SetCharacterMaterial(CharacterId id, const PhysicsMaterial& material)
    {
        SetMaterial(BodyId{m_impl->characters[id.value].character->GetInnerBodyID().GetIndexAndSequenceNumber()},
                    material);
    }

    void World::SetCharacterFilter(CharacterId id, const CollisionFilter& filter)
    {
        Impl::CharacterSlot& slot = m_impl->characters[id.value];
        const BodyId inner{slot.character->GetInnerBodyID().GetIndexAndSequenceNumber()};
        SetFilter(inner, filter);
        // Its moves are filtered through its body's layer.
        slot.layer = m_impl->Bodies().GetObjectLayer(ToJolt(inner));
    }

    void World::TurnCharacter(CharacterId id, const Math::Quat& rotation)
    {
        JPH::CharacterVirtual& character = *m_impl->characters[id.value].character;
        const JPH::Quat turned = ToJolt(rotation);
        character.SetRotation(turned);
        // The ground rule is in the character's own space, and measured along world +Y.
        character.SetSupportingVolume(SupportingVolume(*character.GetShape(), turned));
    }

    bool World::ReshapeCharacter(CharacterId id, const CollisionShape& shape, const Math::Vec3& scale, bool keepLowest)
    {
        const Impl::CharacterSlot& slot = m_impl->characters[id.value];
        JPH::CharacterVirtual& character = *slot.character;
        const JPH::Ref<JPH::Shape> made = m_impl->MakeShape(shape, scale);
        const JPH::Quat rotation = character.GetRotation();
        const JPH::Vec3 kept = keepLowest ? -JPH::Vec3::sAxisY() : JPH::Vec3::sAxisY();
        const JPH::RVec3 before = character.GetPosition();
        const JPH::AABox bounds = m_impl->Bounds(BodyId{character.GetInnerBodyID().GetIndexAndSequenceNumber()});

        // Tried where it would be, its kept end where the old shape's is. Jolt's sample allows a little more overlap
        // than the solver leaves behind, so resting contacts never block.
        const float shift = Reach(*character.GetShape(), rotation, kept) - Reach(*made, rotation, kept);
        character.SetPosition(before + JPH::RVec3(shift * kept));
        const float allowed = 1.5f * m_impl->system.GetPhysicsSettings().mPenetrationSlop;
        const CharacterBroadPhaseFilter broadPhase(m_impl->objectVsBroadPhase, slot.layer);
        const JPH::DefaultObjectLayerFilter layer(m_impl->objectPairs, slot.layer);
        if (!character.SetShape(made, allowed, broadPhase, layer, {}, {}, m_impl->tempAllocator))
        {
            character.SetPosition(before);
            return false;
        }
        character.SetInnerBodyShape(made);
        character.SetSupportingVolume(SupportingVolume(*made, rotation));
        // What rested on or beside the old shape may now fall or be pushed.
        m_impl->WakeAround(bounds);
        m_impl->WakeAround(m_impl->Bounds(BodyId{character.GetInnerBodyID().GetIndexAndSequenceNumber()}));
        return true;
    }

    void World::Teleport(BodyId id, const Math::Vec3& position, const Math::Quat& rotation)
    {
        const JPH::AABox before = m_impl->Bounds(id);
        m_impl->Bodies().SetPositionAndRotation(ToJolt(id), ToJolt(position), ToJolt(rotation),
                                                JPH::EActivation::Activate);
        m_impl->WakeAround(before);
        m_impl->WakeAround(m_impl->Bounds(id));
    }

    void World::MoveKinematic(BodyId id, const Math::Vec3& position, const Math::Quat& rotation, float dt)
    {
        m_impl->Bodies().MoveKinematic(ToJolt(id), ToJolt(position), ToJolt(rotation), dt);
    }

    void World::SetVelocity(BodyId id, const Math::Vec3& linear, const Math::Vec3& angular)
    {
        m_impl->Bodies().SetLinearAndAngularVelocity(ToJolt(id), ToJolt(linear), ToJolt(angular));
    }

    void World::SetShape(BodyId id, std::span<const ColliderPart> parts, std::uint32_t entity, float mass)
    {
        const JPH::AABox before = m_impl->Bounds(id);
        const JPH::Ref<JPH::Shape> made = m_impl->MakeBodyShape(parts, entity);
        const JPH::Vec3 centreBefore = m_impl->Bodies().GetShape(ToJolt(id))->GetCenterOfMass();
        m_impl->Bodies().SetShape(ToJolt(id), made, false, JPH::EActivation::Activate);
        // Constraints hold their anchors relative to the centre of mass: told how far it moved, they keep them where
        // they were relative to the body's origin, where a joint's frames are.
        const JPH::Vec3 centreMoved = made->GetCenterOfMass() - centreBefore;
        for (const Impl::JointSlot& slot : m_impl->joints)
        {
            if (slot.constraint != nullptr && (slot.bodyA.value == id.value || slot.bodyB.value == id.value))
            {
                slot.constraint->NotifyShapeChanged(ToJolt(id), centreMoved);
            }
        }
        m_impl->Bodies().SetUseManifoldReduction(ToJolt(id), made->GetType() != JPH::EShapeType::Compound);
        // The body keeps its own mass; its inertia follows the new shape.
        m_impl->Write(id, [&made, mass](JPH::Body& body) {
            if (JPH::MotionProperties* motion = body.GetMotionPropertiesUnchecked())
            {
                motion->SetMassProperties(JPH::EAllowedDOFs::All, MassOf(*made, mass));
            }
        });
        m_impl->WakeAround(before);
        m_impl->WakeAround(m_impl->Bounds(id));
    }

    void World::SetMaterial(BodyId id, const PhysicsMaterial& material)
    {
        m_impl->Bodies().SetFriction(ToJolt(id), material.friction);
        m_impl->Bodies().SetRestitution(ToJolt(id), material.restitution);
    }

    void World::SetFilter(BodyId id, const CollisionFilter& filter)
    {
        m_impl->Relayer(id, [&filter](LayerKey& key) {
            key.layers = filter.layers;
            key.mask = filter.mask;
        });
    }

    void World::SetMotion(BodyId id, const Motion& motion)
    {
        m_impl->WakeAround(m_impl->Bounds(id));
        m_impl->Relayer(id, [&motion](LayerKey& key) { key.broadPhase = BroadPhaseOf(motion.motionType); });
        JPH::BodyInterface& bodies = m_impl->Bodies();
        bodies.SetMotionType(ToJolt(id), ToJolt(motion.motionType), JPH::EActivation::Activate);
        m_impl->Write(id, [&motion](JPH::Body& body) {
            JPH::MotionProperties* properties = body.GetMotionPropertiesUnchecked();
            if (properties == nullptr)
            {
                return;
            }
            properties->SetLinearDamping(motion.linearDamping);
            properties->SetAngularDamping(motion.angularDamping);
            properties->SetGravityFactor(motion.gravityScale);
            properties->ScaleToMass(motion.mass);
        });
    }

    std::size_t World::DistinctShapeCount() const
    {
        JPH::BodyIDVector ids;
        m_impl->system.GetBodies(ids);
        std::set<const JPH::Shape*> shapes;
        for (const JPH::BodyID id : ids)
        {
            JPH::BodyLockRead lock(m_impl->system.GetBodyLockInterfaceNoLock(), id);
            if (lock.Succeeded())
            {
                shapes.insert(lock.GetBody().GetShape());
            }
        }
        return shapes.size();
    }

    void World::AddImpulse(BodyId id, const Math::Vec3& impulse)
    {
        m_impl->Bodies().AddImpulse(ToJolt(id), ToJolt(impulse));
    }

    void World::AddForce(BodyId id, const Math::Vec3& force)
    {
        // Jolt clears a body's accumulated force after each step.
        m_impl->Bodies().AddForce(ToJolt(id), ToJolt(force));
    }

    void World::AddTorque(BodyId id, const Math::Vec3& torque)
    {
        m_impl->Bodies().AddTorque(ToJolt(id), ToJolt(torque));
    }

    void World::AddAngularImpulse(BodyId id, const Math::Vec3& impulse)
    {
        m_impl->Bodies().AddAngularImpulse(ToJolt(id), ToJolt(impulse));
    }

    Math::Vec3 World::Gravity() const
    {
        return FromJolt(m_impl->system.GetGravity());
    }

    void World::SetGravity(const Math::Vec3& gravity)
    {
        m_impl->system.SetGravity(ToJolt(gravity));
        // A ray or sphere probe holds the world's up.
        for (Impl::VehicleSlot& slot : m_impl->vehicles)
        {
            if (slot.constraint != nullptr && slot.probe != VehicleProbe::Cylinder)
            {
                m_impl->Retest(slot);
            }
        }
        // Jolt does not wake sleeping bodies for a gravity change, and a body at rest in zero gravity falls asleep.
        JPH::BodyIDVector ids;
        m_impl->system.GetBodies(ids);
        if (!ids.empty())
        {
            m_impl->Bodies().ActivateBodies(ids.data(), static_cast<int>(ids.size()));
        }
    }

    BodyState World::GetState(BodyId id) const
    {
        const JPH::BodyID body = ToJolt(id);
        JPH::BodyInterface& bodies = m_impl->Bodies();
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(body, position, rotation);
        return BodyState{FromJolt(position), FromJolt(rotation), FromJolt(bodies.GetLinearVelocity(body)),
                         FromJolt(bodies.GetAngularVelocity(body))};
    }

    namespace
    {
        // Each kind's impulses on body 2, which is bodyB, rebuilt along the axes its constraint parts derive, from the
        // bodies' poses after the step. Jolt applies each part's lambda to body 2 and its negative to body 1. The
        // rotation parts give the torque about the anchor; the position parts' lever arms are left out.

        // The world-space axes of the constraint's frame on body 1: Jolt's local axes there, turned with the body.
        JPH::Mat44 ConstraintToWorld1(const JPH::TwoBodyConstraint& constraint)
        {
            return JPH::Mat44::sRotation(constraint.GetBody1()->GetRotation()) *
                   constraint.GetConstraintToBody1Matrix();
        }

        JointLoad FixedLoad(const JPH::FixedConstraint& fixed)
        {
            return JointLoad{FromJolt(fixed.GetTotalLambdaPosition()), FromJolt(fixed.GetTotalLambdaRotation())};
        }

        JointLoad PointLoad(const JPH::PointConstraint& ball)
        {
            return JointLoad{FromJolt(ball.GetTotalLambdaPosition()), Math::Vec3(0.0f)};
        }

        // Along the line from body 1's point to body 2's, as DistanceConstraint finds it. Two points together give
        // no line, and no direction to report the impulse in.
        JointLoad DistanceLoad(const JPH::DistanceConstraint& distance)
        {
            const JPH::RVec3 point1 = distance.GetBody1()->GetCenterOfMassTransform() *
                                      distance.GetConstraintToBody1Matrix().GetTranslation();
            const JPH::RVec3 point2 = distance.GetBody2()->GetCenterOfMassTransform() *
                                      distance.GetConstraintToBody2Matrix().GetTranslation();
            const JPH::Vec3 delta(point2 - point1);
            const float length = delta.Length();
            if (length <= 0.0f)
            {
                return JointLoad{};
            }
            return JointLoad{FromJolt(delta / length * distance.GetTotalLambdaPosition()), Math::Vec3(0.0f)};
        }

        // The hinge's rotation impulse is along two axes its HingeRotationConstraintPart works out: rebuilt the same
        // way. Its limits and motor turn about body 1's hinge axis.
        JointLoad HingeLoad(const JPH::HingeConstraint& hinge)
        {
            const JPH::Vec3 a1 = (hinge.GetBody1()->GetRotation() * hinge.GetLocalSpaceHingeAxis1()).Normalized();
            JPH::Vec3 a2 = (hinge.GetBody2()->GetRotation() * hinge.GetLocalSpaceHingeAxis2()).Normalized();
            const float dot = a1.Dot(a2);
            if (dot <= 1.0e-3f)
            {
                JPH::Vec3 perp = a2 - dot * a1;
                if (perp.LengthSq() < 1.0e-6f)
                {
                    perp = a1.GetNormalizedPerpendicular();
                }
                a2 = (0.99f * perp.Normalized() + 0.01f * a1).Normalized();
            }
            const JPH::Vec3 b2 = a2.GetNormalizedPerpendicular();
            const JPH::Vec3 c2 = a2.Cross(b2);
            const JPH::Vector<2> rotation = hinge.GetTotalLambdaRotation();
            const JPH::Vec3 torque = b2.Cross(a1) * rotation[0] + c2.Cross(a1) * rotation[1] +
                                     a1 * (hinge.GetTotalLambdaRotationLimits() + hinge.GetTotalLambdaMotor());
            return JointLoad{FromJolt(hinge.GetTotalLambdaPosition()), FromJolt(torque)};
        }

        // The slider holds body 2 on its axis along body 1's two normals, and limits and drives it along body 1's
        // slider axis: the columns of its constraint frame on body 1.
        JointLoad SliderLoad(const JPH::SliderConstraint& slider)
        {
            const JPH::Mat44 axes = ConstraintToWorld1(slider);
            const JPH::Vector<2> position = slider.GetTotalLambdaPosition();
            const JPH::Vec3 force =
                axes.GetAxisY() * position[0] + axes.GetAxisZ() * position[1] +
                axes.GetAxisX() * (slider.GetTotalLambdaPositionLimits() + slider.GetTotalLambdaMotor());
            return JointLoad{FromJolt(force), FromJolt(slider.GetTotalLambdaRotation())};
        }

        // As Jolt's SwingTwistConstraintPart judges it: a limit within half a degree of 0 is locked.
        constexpr float swingTwistLocked = JPH::DegreesToRadians(0.5f);

        // The swing-twist limits push along axes SwingTwistConstraintPart works out from the rotation, flipped on a
        // limit's negative side so that their impulses are never positive: rebuilt here, the side from the rotation's
        // sign. A cone limit pushes the twist axis back towards the rest pose, about the axis from there to where it
        // is.
        JointLoad SwingTwistLoad(const JPH::SwingTwistConstraint& swingTwist)
        {
            const JPH::Quat toWorld1 = swingTwist.GetBody1()->GetRotation() * swingTwist.GetConstraintToBody1();
            const JPH::Quat toWorld2 = swingTwist.GetBody2()->GetRotation() * swingTwist.GetConstraintToBody2();
            JPH::Quat swing;
            JPH::Quat twist;
            (toWorld1.Conjugated() * toWorld2).GetSwingTwist(swing, twist);
            // As SwingTwistConstraintPart reads them: turns of less than a half turn either way.
            if (swing.GetW() < 0.0f)
            {
                swing = -swing;
            }
            if (twist.GetW() < 0.0f)
            {
                twist = -twist;
            }
            const JPH::Quat swingToWorld = toWorld1 * swing;
            const auto side = [](float component) { return component < 0.0f ? -1.0f : 1.0f; };

            JPH::Vec3 torque = JPH::Vec3::sZero();
            const bool lockedY = swingTwist.GetPlaneHalfConeAngle() < swingTwistLocked;
            const bool lockedZ = swingTwist.GetNormalHalfConeAngle() < swingTwistLocked;
            const float swingY = swingTwist.GetTotalLambdaSwingY();
            const float swingZ = swingTwist.GetTotalLambdaSwingZ();
            if (lockedY || lockedZ)
            {
                torque += swingToWorld.RotateAxisY() * (lockedY ? swingY : swingY * side(swing.GetY()));
                torque += swingToWorld.RotateAxisZ() * (lockedZ ? swingZ : swingZ * side(swing.GetZ()));
            }
            else
            {
                const JPH::Vec3 axis = toWorld1.RotateAxisX().Cross(swingToWorld.RotateAxisX());
                if (axis.LengthSq() > 0.0f)
                {
                    torque += axis.Normalized() * swingY;
                }
            }
            const bool lockedTwist =
                swingTwist.GetTwistMinAngle() > -swingTwistLocked && swingTwist.GetTwistMaxAngle() < swingTwistLocked;
            torque += swingToWorld.RotateAxisX() *
                      (swingTwist.GetTotalLambdaTwist() * (lockedTwist ? 1.0f : side(twist.GetX())));

            // The motor and friction turn about body 2's constraint axes.
            const JPH::Mat44 motorAxes = JPH::Mat44::sRotation(toWorld2);
            const JPH::Vec3 motor = swingTwist.GetTotalLambdaMotor();
            torque += motorAxes.GetAxisX() * motor.GetX() + motorAxes.GetAxisY() * motor.GetY() +
                      motorAxes.GetAxisZ() * motor.GetZ();
            return JointLoad{FromJolt(swingTwist.GetTotalLambdaPosition()), FromJolt(torque)};
        }
    } // namespace

    JointLoad World::Load(JointId id) const
    {
        const Impl::JointSlot& slot = m_impl->joints[id.value];
        const float dt = m_impl->collisionStepTime;
        if (!slot.constraint->GetEnabled() || dt <= 0.0f)
        {
            return JointLoad{};
        }
        const JPH::TwoBodyConstraint& constraint = *slot.constraint;
        JointLoad impulse;
        switch (slot.type)
        {
        case JointType::Fixed:
            impulse = FixedLoad(static_cast<const JPH::FixedConstraint&>(constraint));
            break;
        case JointType::Ball:
            impulse = PointLoad(static_cast<const JPH::PointConstraint&>(constraint));
            break;
        case JointType::Distance:
            impulse = DistanceLoad(static_cast<const JPH::DistanceConstraint&>(constraint));
            break;
        case JointType::Hinge:
            impulse = HingeLoad(static_cast<const JPH::HingeConstraint&>(constraint));
            break;
        case JointType::Slider:
            impulse = SliderLoad(static_cast<const JPH::SliderConstraint&>(constraint));
            break;
        case JointType::SwingTwist:
            impulse = SwingTwistLoad(static_cast<const JPH::SwingTwistConstraint&>(constraint));
            break;
        }
        return JointLoad{impulse.force / dt, impulse.torque / dt};
    }

    namespace
    {
        // A query's filter, at each of Jolt's three levels.
        class QueryBroadPhaseFilter final : public JPH::BroadPhaseLayerFilter
        {
          public:
            explicit QueryBroadPhaseFilter(const QueryFilter& filter) : m_filter(&filter)
            {
            }

            bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
            {
                constexpr JPH::BroadPhaseLayer triggers(static_cast<JPH::BroadPhaseLayer::Type>(BroadPhase::Trigger));
                return m_filter->hitTriggers || layer != triggers;
            }

          private:
            const QueryFilter* m_filter;
        };

        class QueryLayerFilter final : public JPH::ObjectLayerFilter
        {
          public:
            QueryLayerFilter(const QueryFilter& filter, const ObjectLayers& layers)
                : m_filter(&filter), m_layers(&layers)
            {
            }

            bool ShouldCollide(JPH::ObjectLayer layer) const override
            {
                const LayerKey& key = m_layers->Key(layer);
                return (key.layers & m_filter->mask) != 0 &&
                       (m_filter->layers == 0u || (key.mask & m_filter->layers) != 0);
            }

          private:
            const QueryFilter* m_filter;
            const ObjectLayers* m_layers;
        };

        class QueryBodyFilter final : public JPH::BodyFilter
        {
          public:
            explicit QueryBodyFilter(const QueryFilter& filter) : m_filter(&filter)
            {
            }

            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                if (body.IsSensor() && !m_filter->hitTriggers)
                {
                    return false;
                }
                return body.GetUserData() != m_filter->ignoreEntity;
            }

          private:
            const QueryFilter* m_filter;
        };

        // The three filters of one query, built together.
        struct QueryFilters
        {
            QueryFilters(const QueryFilter& filter, const ObjectLayers& layers)
                : broadPhase(filter), layer(filter, layers), body(filter)
            {
            }

            QueryBroadPhaseFilter broadPhase;
            QueryLayerFilter layer;
            QueryBodyFilter body;
        };

        // Hands each hit to a sink, narrowing the query to the distance the sink still wants.
        class RayCollector final : public JPH::CastRayCollector
        {
          public:
            RayCollector(const JPH::RRayCast& ray, float maxDistance, HitSink sink)
                : m_ray(&ray), m_maxDistance(maxDistance), m_sink(sink)
            {
            }

            void OnBody(const JPH::Body& body) override
            {
                m_body = &body;
            }

            void AddHit(const JPH::RayCastResult& result) override
            {
                const JPH::Vec3 point = m_ray->GetPointOnRay(result.mFraction);
                const JPH::Vec3 direction = m_ray->mDirection.Normalized();
                // A ray that starts inside a solid shape has no surface to take a normal from.
                const JPH::Vec3 normal = result.mFraction > 0.0f
                                             ? GetContext()->GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point)
                                             : -direction;
                const float wanted =
                    m_sink(QueryHit{EntityOf(*m_body), ColliderOf(*m_body, result.mSubShapeID2), FromJolt(point),
                                    FromJolt(normal), result.mFraction * m_maxDistance});
                const float fraction = wanted / m_maxDistance;
                if (fraction < GetEarlyOutFraction())
                {
                    UpdateEarlyOutFraction(fraction);
                }
            }

          private:
            const JPH::RRayCast* m_ray;
            float m_maxDistance;
            HitSink m_sink;
            const JPH::Body* m_body = nullptr;
        };

        class ShapeCastCollector final : public JPH::CastShapeCollector
        {
          public:
            // `motion`, when given, is the cast's whole displacement: only hits that block a move along it are kept.
            ShapeCastCollector(float maxDistance, HitSink sink, const JPH::Vec3* motion = nullptr)
                : m_maxDistance(maxDistance), m_sink(sink), m_motion(motion)
            {
            }

            void OnBody(const JPH::Body& body) override
            {
                m_body = &body;
            }

            void AddHit(const JPH::ShapeCastResult& result) override
            {
                // As CharacterVirtual's sweep: a body the shape starts in or touching is not in the way (a mover
                // resting on the ground slides along it), nor is one it moves away from.
                if (m_motion != nullptr &&
                    (!(result.mFraction > 0.0f) || !(result.mPenetrationAxis.Dot(*m_motion) > 0.0f)))
                {
                    return;
                }
                const JPH::Vec3 normal = -result.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero());
                const float wanted = m_sink(QueryHit{EntityOf(*m_body), ColliderOf(*m_body, result.mSubShapeID2),
                                                     FromJolt(result.mContactPointOn1), FromJolt(normal),
                                                     result.mFraction * m_maxDistance});
                // Jolt orders shapes that start overlapping by negative penetration depth, below fraction 0: a sink
                // that wants nothing farther than distance 0 still sees them.
                const float fraction = wanted / m_maxDistance;
                if (fraction > 0.0f && fraction < GetEarlyOutFraction())
                {
                    UpdateEarlyOutFraction(fraction);
                }
            }

          private:
            float m_maxDistance;
            HitSink m_sink;
            const JPH::Vec3* m_motion;
            const JPH::Body* m_body = nullptr;
        };

        // Reports each collider of a body once, however many times it is hit; stops when the sink says so.
        template <typename Base>
        class BodyCollector final : public Base
        {
          public:
            explicit BodyCollector(BodySink sink) : m_sink(sink)
            {
            }

            void OnBody(const JPH::Body& body) override
            {
                m_body = &body;
                m_reported.clear();
            }

            void AddHit(const typename Base::ResultType& result) override
            {
                const std::uint32_t collider = ColliderOf(*m_body, result.mSubShapeID2);
                if (std::ranges::find(m_reported, collider) != m_reported.end())
                {
                    return;
                }
                m_reported.push_back(collider);
                if (!m_sink(EntityOf(*m_body), collider))
                {
                    this->ForceEarlyOut();
                }
            }

          private:
            BodySink m_sink;
            const JPH::Body* m_body = nullptr;
            std::vector<std::uint32_t> m_reported;
        };

        // Hands each overlap with a depth to a sink.
        class PenetrationCollector final : public JPH::CollideShapeCollector
        {
          public:
            explicit PenetrationCollector(PenetrationSink sink) : m_sink(sink)
            {
            }

            void OnBody(const JPH::Body& body) override
            {
                m_body = &body;
            }

            void AddHit(const JPH::CollideShapeResult& result) override
            {
                // Shapes only touching are reported at depth 0: nothing to get out of.
                if (!(result.mPenetrationDepth > 0.0f))
                {
                    return;
                }
                // The axis moves the body out of the shape; the shape goes the other way.
                const JPH::Vec3 out = -result.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero());
                m_sink(Penetration{EntityOf(*m_body), ColliderOf(*m_body, result.mSubShapeID2),
                                   FromJolt(result.mContactPointOn1), FromJolt(out), result.mPenetrationDepth});
            }

          private:
            PenetrationSink m_sink;
            const JPH::Body* m_body = nullptr;
        };

        JPH::Mat44 ToJolt(const Math::Vec3& position, const Math::Quat& rotation)
        {
            return JPH::Mat44::sRotationTranslation(ToJolt(rotation), ToJolt(position));
        }
    } // namespace

    void World::CastRay(const Math::Vec3& origin, const Math::Vec3& direction, float maxDistance,
                        const QueryFilter& filter, HitSink sink) const
    {
        if (!(maxDistance > 0.0f))
        {
            return;
        }
        const JPH::RRayCast ray(ToJolt(origin), ToJolt(direction) * maxDistance);
        RayCollector collector(ray, maxDistance, sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        // What the seam promises, set here rather than left to Jolt's defaults: a ray starting inside a solid shape
        // (any convex one) hits it at distance 0, and triangles' back faces are not hit.
        JPH::RayCastSettings settings;
        settings.mTreatConvexAsSolid = true;
        settings.SetBackFaceMode(JPH::EBackFaceMode::IgnoreBackFaces);
        m_impl->system.GetNarrowPhaseQueryNoLock().CastRay(ray, settings, collector, filters.broadPhase, filters.layer,
                                                           filters.body);
    }

    void World::CastShape(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                          const Math::Quat& rotation, const Math::Vec3& direction, float maxDistance,
                          const QueryFilter& filter, HitSink sink) const
    {
        if (!(maxDistance > 0.0f))
        {
            return;
        }
        const JPH::Ref<JPH::Shape> cast = m_impl->MakeQueryShape(shape, scale);
        if (cast == nullptr)
        {
            return;
        }
        const JPH::RShapeCast shapeCast = JPH::RShapeCast::sFromWorldTransform(
            cast, JPH::Vec3::sOne(), ToJolt(position, rotation), ToJolt(direction) * maxDistance);
        ShapeCastCollector collector(maxDistance, sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        m_impl->system.GetNarrowPhaseQueryNoLock().CastShape(shapeCast, JPH::ShapeCastSettings(), JPH::RVec3::sZero(),
                                                             collector, filters.broadPhase, filters.layer,
                                                             filters.body);
    }

    void World::CastMove(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                         const Math::Quat& rotation, const Math::Vec3& direction, float maxDistance,
                         const QueryFilter& filter, HitSink sink) const
    {
        if (!(maxDistance > 0.0f))
        {
            return;
        }
        const JPH::Ref<JPH::Shape> cast = m_impl->MakeQueryShape(shape, scale);
        if (cast == nullptr)
        {
            return;
        }
        const JPH::Vec3 motion = ToJolt(direction) * maxDistance;
        const JPH::RShapeCast shapeCast =
            JPH::RShapeCast::sFromWorldTransform(cast, JPH::Vec3::sOne(), ToJolt(position, rotation), motion);
        ShapeCastCollector collector(maxDistance, sink, &motion);
        const QueryFilters filters(filter, m_impl->objectLayers);
        // CharacterVirtual's sweep settings: a convex face is only met from the front, so a shape leaving a body
        // through it is not stopped; a triangle stops it from either side, so it cannot pass through a thin floor.
        JPH::ShapeCastSettings settings;
        settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mBackFaceModeConvex = JPH::EBackFaceMode::IgnoreBackFaces;
        m_impl->system.GetNarrowPhaseQueryNoLock().CastShape(shapeCast, settings, JPH::RVec3::sZero(), collector,
                                                             filters.broadPhase, filters.layer, filters.body);
    }

    void World::CollideShape(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                             const Math::Quat& rotation, const QueryFilter& filter, BodySink sink) const
    {
        const JPH::Ref<JPH::Shape> overlap = m_impl->MakeQueryShape(shape, scale);
        if (overlap == nullptr)
        {
            return;
        }
        const JPH::Mat44 centreOfMass = ToJolt(position, rotation).PreTranslated(overlap->GetCenterOfMass());
        BodyCollector<JPH::CollideShapeCollector> collector(sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        m_impl->system.GetNarrowPhaseQueryNoLock().CollideShape(
            overlap, JPH::Vec3::sOne(), centreOfMass, JPH::CollideShapeSettings(), JPH::RVec3::sZero(), collector,
            filters.broadPhase, filters.layer, filters.body);
    }

    void World::CollideDeep(const CollisionShape& shape, const Math::Vec3& scale, const Math::Vec3& position,
                            const Math::Quat& rotation, const QueryFilter& filter, PenetrationSink sink) const
    {
        const JPH::Ref<JPH::Shape> overlap = m_impl->MakeQueryShape(shape, scale);
        if (overlap == nullptr)
        {
            return;
        }
        const JPH::Mat44 centreOfMass = ToJolt(position, rotation).PreTranslated(overlap->GetCenterOfMass());
        PenetrationCollector collector(sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        m_impl->system.GetNarrowPhaseQueryNoLock().CollideShape(
            overlap, JPH::Vec3::sOne(), centreOfMass, JPH::CollideShapeSettings(), JPH::RVec3::sZero(), collector,
            filters.broadPhase, filters.layer, filters.body);
    }

    void World::CollidePoint(const Math::Vec3& point, const QueryFilter& filter, BodySink sink) const
    {
        BodyCollector<JPH::CollidePointCollector> collector(sink);
        const QueryFilters filters(filter, m_impl->objectLayers);
        m_impl->system.GetNarrowPhaseQueryNoLock().CollidePoint(ToJolt(point), collector, filters.broadPhase,
                                                                filters.layer, filters.body);
    }

    bool World::IsSleeping(BodyId id) const
    {
        const JPH::Body* body = m_impl->system.GetBodyLockInterfaceNoLock().TryGetBody(ToJolt(id));
        // A static body never moves, so it neither sleeps nor wakes.
        return body != nullptr && !body->IsStatic() && !body->IsActive();
    }

    Math::AABB World::Bounds(BodyId id) const
    {
        const JPH::AABox bounds = m_impl->Bounds(id);
        return Math::AABB(FromJolt(bounds.mMin), FromJolt(bounds.mMax));
    }

    void World::ShapeEdges(const CollisionShape& shape, EdgeSink sink) const
    {
        if (IsPrimitive(shape))
        {
            return;
        }
        const JPH::Ref<JPH::Shape> made = m_impl->meshes->QueryShape(shape);
        if (made == nullptr)
        {
            return;
        }
        if (shape.Is<ConvexHullShape>())
        {
            // Jolt keeps a hull's points about its centre of mass. Each edge is shared by two faces, which run round
            // it in opposite directions: drawn once, from the face that has it from the lower point index.
            const auto& hull = static_cast<const JPH::ConvexHullShape&>(*made);
            const JPH::Vec3 centre = hull.GetCenterOfMass();
            std::vector<JPH::uint> face;
            for (JPH::uint f = 0; f < hull.GetNumFaces(); ++f)
            {
                face.resize(hull.GetNumVerticesInFace(f));
                hull.GetFaceVertices(f, static_cast<JPH::uint>(face.size()), face.data());
                for (std::size_t i = 0; i < face.size(); ++i)
                {
                    const JPH::uint a = face[i];
                    const JPH::uint b = face[(i + 1) % face.size()];
                    if (a < b)
                    {
                        sink(FromJolt(hull.GetPoint(a) + centre), FromJolt(hull.GetPoint(b) + centre));
                    }
                }
            }
            return;
        }
        // A mesh: every triangle, in the shape's own space (its centre of mass placed where it is in that space).
        JPH::Shape::GetTrianglesContext context;
        const JPH::AABox everything(JPH::Vec3::sReplicate(-1.0e30f), JPH::Vec3::sReplicate(1.0e30f));
        made->GetTrianglesStart(context, everything, made->GetCenterOfMass(), JPH::Quat::sIdentity(),
                                JPH::Vec3::sOne());
        constexpr int batch = JPH::Shape::cGetTrianglesMinTrianglesRequested;
        std::array<JPH::Float3, 3 * batch> vertices;
        const auto vertex = [&vertices](std::size_t i) {
            const JPH::Float3& v = vertices[i];
            return Math::Vec3(v.x, v.y, v.z);
        };
        for (int count = made->GetTrianglesNext(context, batch, vertices.data()); count > 0;
             count = made->GetTrianglesNext(context, batch, vertices.data()))
        {
            for (std::size_t t = 0; t < static_cast<std::size_t>(count); ++t)
            {
                const Math::Vec3 a = vertex(3 * t);
                const Math::Vec3 b = vertex(3 * t + 1);
                const Math::Vec3 c = vertex(3 * t + 2);
                sink(a, b);
                sink(b, c);
                sink(c, a);
            }
        }
    }

    // Vehicles: chassis-local +Y up and -Z forward, so +X is right. Jolt's steer angle is + left; ours is + right.

    namespace
    {
        const JPH::Vec3 vehicleUp(0.0f, 1.0f, 0.0f);
        const JPH::Vec3 vehicleForward(0.0f, 0.0f, -1.0f);

        // Forward ratios spaced geometrically from first to top.
        JPH::Array<float> GearRatios(const VehicleTransmission& transmission)
        {
            JPH::Array<float> ratios;
            const int count = transmission.gearCount;
            for (int i = 0; i < count; ++i)
            {
                const float t = count == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
                ratios.push_back(transmission.firstGear * std::pow(transmission.topGear / transmission.firstGear, t));
            }
            return ratios;
        }

        void Fill(JPH::VehicleEngineSettings& out, const VehicleEngine& engine)
        {
            out.mMaxTorque = engine.maxTorque;
            out.mMinRPM = engine.minRpm;
            out.mMaxRPM = engine.maxRpm;
            out.mInertia = engine.inertia;
            out.mAngularDamping = engine.angularDamping;
            // Jolt's curve is over RPM / maxRpm, in fractions of maxTorque.
            out.mNormalizedTorque.Clear();
            out.mNormalizedTorque.AddPoint(engine.minRpm / engine.maxRpm, engine.torqueAtMinRpm);
            out.mNormalizedTorque.AddPoint(engine.peakTorqueRpm / engine.maxRpm, 1.0f);
            out.mNormalizedTorque.AddPoint(1.0f, engine.torqueAtMaxRpm);
        }

        void Fill(JPH::VehicleTransmissionSettings& out, const VehicleTransmission& transmission)
        {
            out.mMode = transmission.mode == TransmissionMode::Manual ? JPH::ETransmissionMode::Manual
                                                                      : JPH::ETransmissionMode::Auto;
            out.mGearRatios = GearRatios(transmission);
            out.mReverseGearRatios = {-transmission.reverseGear};
            out.mSwitchTime = transmission.switchTime;
            out.mClutchReleaseTime = transmission.clutchReleaseTime;
            out.mSwitchLatency = transmission.switchLatency;
            out.mShiftUpRPM = transmission.shiftUpRpm;
            out.mShiftDownRPM = transmission.shiftDownRpm;
            out.mClutchStrength = transmission.clutchStrength;
        }

        // Finishes an automatic shift under way at once, the clutch engaged: Jolt counts a shift down only in Auto, so
        // a gearbox switched to Manual mid-shift would otherwise stay shifting (and its body awake) for good. Each
        // update clears one of the shift's timers, keeping the gear: driven its own way, at an RPM that shifts neither
        // up nor down.
        void FinishShift(JPH::VehicleTransmission& transmission)
        {
            const int gear = transmission.GetCurrentGear();
            const float direction = gear > 0 ? 1.0f : (gear < 0 ? -1.0f : 0.0f);
            while (!transmission.AllowSleep())
            {
                transmission.Update(std::numeric_limits<float>::max(), transmission.mShiftDownRPM, direction, false);
            }
        }

        // Friction from 0 to the peak, down to the slide value, and level from there; `slipScale` turns the slips
        // into Jolt's units.
        void Fill(JPH::LinearCurve& out, const VehicleTyre& tyre, float slipScale)
        {
            out.Clear();
            out.AddPoint(0.0f, 0.0f);
            out.AddPoint(tyre.peakSlip * slipScale, tyre.peakFriction);
            out.AddPoint(tyre.slideSlip * slipScale, tyre.slideFriction);
        }

        // A wheel for Jolt, as every kind's has it: the suspension down its steering axis, and the wheel turned by its
        // camber and toe.
        void FillWheel(JPH::WheelSettings& out, const VehicleWheelDesc& wheel)
        {
            const JPH::Vec3 axis = ToJolt(wheel.steeringAxis).Normalized();
            out.mSteeringAxis = axis;
            out.mSuspensionDirection = -axis;
            out.mWheelUp = ToJolt(wheel.rest * Math::Vec3(0.0f, 1.0f, 0.0f)).Normalized();
            out.mWheelForward = ToJolt(wheel.rest * Math::Vec3(0.0f, 0.0f, -1.0f)).Normalized();
            const bool rigid = wheel.suspension.travel <= 0.0f || wheel.suspension.spring.frequency <= 0.0f;
            const float travel = rigid ? 0.0f : wheel.suspension.travel;
            // Jolt's hard point is a margin above the top of the travel: its bump stop engages only below its min
            // length, and a probe starting inside the ground finds it at length 0, so with no margin a bottomed-out
            // wheel never stops the chassis sinking.
            const float margin = wheel.radius;
            out.mPosition = ToJolt(wheel.position) + axis * (travel + margin);
            out.mSuspensionMinLength = margin;
            out.mSuspensionMaxLength = travel + margin;
            out.mSuspensionPreloadLength = wheel.suspension.preload;
            // A rigid suspension's spring never acts (its travel is 0), but Jolt wants a frequency above 0.
            out.mSuspensionSpring =
                JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping,
                                    rigid ? 1.0f : wheel.suspension.spring.frequency, wheel.suspension.spring.damping);
            out.mRadius = wheel.radius;
            out.mWidth = wheel.width;
        }

        // A wheeled vehicle's or a motorcycle's wheel: with its own inertia, steering, brakes and tyres.
        void Fill(JPH::WheelSettingsWV& out, const VehicleWheelDesc& wheel)
        {
            FillWheel(out, wheel);
            out.mInertia = wheel.inertia;
            out.mAngularDamping = wheel.angularDamping;
            out.mMaxSteerAngle = wheel.maxSteerAngle;
            out.mMaxBrakeTorque = wheel.maxBrakeTorque;
            out.mMaxHandBrakeTorque = wheel.maxHandbrakeTorque;
            Fill(out.mLongitudinalFriction, wheel.longitudinal, 1.0f);
            // Jolt's lateral slip is in degrees.
            Fill(out.mLateralFriction, wheel.lateral, 180.0f / JPH::JPH_PI);
        }

        // A vehicle's differentials, their torque shares normalised: Jolt asserts that they sum to 1 within 1e-6. None
        // drives each of `wheelCount` wheels equally, by a differential of its own with the defaults.
        void Fill(JPH::Array<JPH::VehicleDifferentialSettings>& out, std::span<const VehicleDifferentialDesc> given,
                  std::size_t wheelCount)
        {
            std::vector<VehicleDifferentialDesc> differentials(given.begin(), given.end());
            if (differentials.empty())
            {
                for (std::size_t i = 0; i < wheelCount; ++i)
                {
                    differentials.push_back(VehicleDifferentialDesc{.leftWheel = static_cast<int>(i)});
                }
            }
            double total = 0.0;
            for (const VehicleDifferentialDesc& differential : differentials)
            {
                total += static_cast<double>(differential.torqueShare);
            }
            out.clear();
            float shared = 0.0f;
            for (std::size_t i = 0; i < differentials.size(); ++i)
            {
                const VehicleDifferentialDesc& differential = differentials[i];
                JPH::VehicleDifferentialSettings& made = out.emplace_back();
                made.mLeftWheel = differential.leftWheel;
                made.mRightWheel = differential.rightWheel;
                made.mDifferentialRatio = differential.ratio;
                made.mLeftRightSplit = differential.leftRightSplit;
                made.mLimitedSlipRatio = differential.limitedSlipRatio;
                // All shares 0: shared equally. The last takes what is left, so the shares sum to 1 exactly.
                const double share = total > 0.0 ? static_cast<double>(differential.torqueShare) / total
                                                 : 1.0 / static_cast<double>(differentials.size());
                made.mEngineTorqueRatio =
                    i + 1 == differentials.size() ? std::max(1.0f - shared, 0.0f) : static_cast<float>(share);
                shared += made.mEngineTorqueRatio;
            }
        }

        // A motorcycle's lean switches; its lean limit is set only when it is made.
        void Fill(JPH::MotorcycleController& out, const MotorcycleLean& lean)
        {
            out.EnableLeanController(lean.enabled);
            out.EnableLeanSteeringLimit(lean.limitSteering);
        }

        // Jolt applies a motorcycle's lean damping in every velocity iteration, and diverges once damping × the
        // collision-step time exceeds the chassis's roll inertia (16.7 kg m² at Jolt's 1000 and 60 Hz; a light bike's
        // box can have 10.4). So the damping is capped at half the roll inertia over the collision-step time. Jolt's
        // spring of 5000 is kept: scaled down with the inertia, it is too weak to lean the bike against a turn.
        void CapLeanDamping(JPH::MotorcycleController& bike, const JPH::Body& body, float collisionStepTime)
        {
            const float jolts = JPH::MotorcycleControllerSettings().mLeanSpringDamping;
            const JPH::MotionProperties& motion = *body.GetMotionProperties();
            const float inverse = motion.GetLocalSpaceInverseInertia().Multiply3x3(vehicleForward).Dot(vehicleForward);
            // A chassis that cannot roll (an inverse inertia of 0) has no limit.
            bike.SetLeanSpringDamping(inverse > 0.0f ? std::min(jolts, 0.5f / (inverse * collisionStepTime)) : jolts);
        }

        // A tracked vehicle's wheel: its geometry, and its track's grip. Its track holds its inertia and brakes.
        void Fill(JPH::WheelSettingsTV& out, const VehicleWheelDesc& desc, const VehicleTrack& track)
        {
            FillWheel(out, desc);
            out.mLongitudinalFriction = track.longitudinalFriction;
            out.mLateralFriction = track.lateralFriction;
        }

        // A track's own settings; which wheels it has is Assign's.
        void Fill(JPH::VehicleTrackSettings& out, const VehicleTrack& track)
        {
            out.mInertia = track.inertia;
            out.mAngularDamping = track.angularDamping;
            out.mMaxBrakeTorque = track.maxBrakeTorque;
            out.mDifferentialRatio = track.differentialRatio;
        }

        // The wheels on one side's track, and its driven wheel: the rearmost (the largest z, as forward is -Z), the
        // first of those level. A track with no wheels keeps wheel 0 as its driven wheel, which Jolt never reads.
        void Assign(JPH::VehicleTrackSettings& out, VehicleTrackSide side, std::span<const TrackedWheel> wheels)
        {
            out.mWheels.clear();
            out.mDrivenWheel = 0;
            float rearmost = -std::numeric_limits<float>::max();
            for (std::size_t i = 0; i < wheels.size(); ++i)
            {
                if (wheels[i].side != side)
                {
                    continue;
                }
                out.mWheels.push_back(static_cast<JPH::uint>(i));
                if (wheels[i].z > rearmost)
                {
                    rearmost = wheels[i].z;
                    out.mDrivenWheel = static_cast<JPH::uint>(i);
                }
            }
        }

        constexpr std::array<VehicleTrackSide, 2> trackSides = {VehicleTrackSide::Left, VehicleTrackSide::Right};

        // Jolt's two tracks, left then right, held in a plain array, to index by side.
        template <typename Track>
        std::span<Track, 2> Sides(Track (&tracks)[2])
        {
            return tracks;
        }

        // Calls `func` with the vehicle's controller as its own type, wheeled (a motorcycle's included) or tracked:
        // both have an engine and a gearbox.
        template <typename Constraint, typename Func>
        void WithController(Constraint& constraint, VehicleController controller, Func&& func)
        {
            constexpr bool constant = std::is_const_v<Constraint>;
            using Tracked =
                std::conditional_t<constant, const JPH::TrackedVehicleController, JPH::TrackedVehicleController>;
            using Wheeled =
                std::conditional_t<constant, const JPH::WheeledVehicleController, JPH::WheeledVehicleController>;
            if (controller == VehicleController::Tracked)
            {
                func(static_cast<Tracked&>(*constraint.GetController()));
            }
            else
            {
                func(static_cast<Wheeled&>(*constraint.GetController()));
            }
        }

        // Jolt refuses a track ratio of 0: one this close to it is this tiny one, which holds the track still against
        // the other.
        float TrackRatio(float ratio)
        {
            constexpr float tiny = 1e-3f;
            return std::abs(ratio) < tiny ? tiny : ratio;
        }

        void Fill(JPH::VehicleAntiRollBars& out, std::span<const VehicleAntiRollBarDesc> given)
        {
            out.clear();
            for (const VehicleAntiRollBarDesc& bar : given)
            {
                JPH::VehicleAntiRollBar& made = out.emplace_back();
                made.mLeftWheel = bar.leftWheel;
                made.mRightWheel = bar.rightWheel;
                made.mStiffness = bar.stiffness;
            }
        }
    } // namespace

    World::Impl::VehicleSlot World::Impl::MakeVehicle(const VehicleDesc& desc)
    {
        const VehicleDriveDesc& drive = desc.drive;
        const bool motorcycle = drive.controller == VehicleController::Motorcycle;
        const bool tracked = drive.controller == VehicleController::Tracked;
        JPH::VehicleConstraintSettings settings;
        settings.mUp = vehicleUp;
        settings.mForward = vehicleForward;
        settings.mConstraintPriority = desc.priority;
        Fill(settings.mAntiRollBars, desc.antiRollBars);
        settings.mMaxPitchRollAngle = desc.maxTilt;
        VehicleSlot slot;
        slot.controller = drive.controller;
        slot.powered = drive.powered;
        slot.maxLeanAngle = drive.lean.maxAngle;
        if (tracked)
        {
            slot.tracks = {drive.leftTrack, drive.rightTrack};
        }
        // Each vehicle its own wheel settings: its wheels read them by reference.
        for (const VehicleWheelDesc& wheel : desc.wheels)
        {
            JPH::Ref<JPH::WheelSettings> made;
            if (tracked)
            {
                auto* onTrack = new JPH::WheelSettingsTV();
                Fill(*onTrack, wheel, slot.tracks[static_cast<std::size_t>(wheel.track)]);
                made = onTrack;
                slot.trackedWheels.push_back(TrackedWheel{.side = wheel.track, .z = wheel.position.z});
            }
            else
            {
                auto* rolling = new JPH::WheelSettingsWV();
                Fill(*rolling, wheel);
                made = rolling;
            }
            settings.mWheels.emplace_back(made);
            slot.wheels.push_back(made);
        }
        if (tracked)
        {
            auto* controller = new JPH::TrackedVehicleControllerSettings();
            Fill(controller->mEngine, drive.engine);
            Fill(controller->mTransmission, drive.transmission);
            for (const VehicleTrackSide side : trackSides)
            {
                JPH::VehicleTrackSettings& track = Sides(controller->mTracks)[static_cast<std::size_t>(side)];
                Fill(track, slot.tracks[static_cast<std::size_t>(side)]);
                Assign(track, side, slot.trackedWheels);
            }
            settings.mController = controller;
        }
        else
        {
            JPH::WheeledVehicleControllerSettings* controller = nullptr;
            if (motorcycle)
            {
                auto* bike = new JPH::MotorcycleControllerSettings();
                bike->mMaxLeanAngle = drive.lean.maxAngle;
                controller = bike;
            }
            else
            {
                controller = new JPH::WheeledVehicleControllerSettings();
            }
            Fill(controller->mEngine, drive.engine);
            Fill(controller->mTransmission, drive.transmission);
            // An unpowered vehicle ignores game code's differentials. Jolt asserts on a controller with no differential
            // driving a wheel, so it keeps the default ones, which its gearbox in neutral leaves undriven: Jolt's
            // clutch carries nothing at a gear ratio of 0.
            Fill(controller->mDifferentials,
                 drive.powered ? std::span<const VehicleDifferentialDesc>(desc.differentials)
                               : std::span<const VehicleDifferentialDesc>(),
                 desc.wheels.size());
            controller->mDifferentialLimitedSlipRatio = drive.limitedSlipRatio;
            settings.mController = controller;
        }

        slot.body = desc.body;
        slot.probe = desc.probe;
        {
            JPH::BodyLockWrite lock(system.GetBodyLockInterfaceNoLock(), ToJolt(desc.body));
            JPH_ASSERT(lock.Succeeded());
            slot.constraint = new JPH::VehicleConstraint(lock.GetBody(), settings);
        }
        if (motorcycle)
        {
            Fill(static_cast<JPH::MotorcycleController&>(*slot.constraint->GetController()), drive.lean);
        }
        if (desc.carried)
        {
            // An unpowered vehicle has no engine or gear to carry: it stays at idle in neutral.
            if (drive.powered)
            {
                WithController(*slot.constraint, slot.controller, [&desc](auto& made) {
                    made.GetEngine().SetCurrentRPM(desc.carried->engineRpm);
                    made.GetTransmission().Set(desc.carried->gear, 1.0f - desc.carried->clutch);
                });
            }
            for (std::size_t i = 0; i < desc.carried->wheels.size(); ++i)
            {
                JPH::Wheel& wheel = *slot.constraint->GetWheel(static_cast<JPH::uint>(i));
                wheel.SetAngularVelocity(desc.carried->wheels[i].angularVelocity);
                wheel.SetRotationAngle(desc.carried->wheels[i].spinAngle);
            }
            // A track turns as its driven wheel did; its other wheels follow it.
            if (tracked)
            {
                auto& made = static_cast<JPH::TrackedVehicleController&>(*slot.constraint->GetController());
                for (JPH::VehicleTrack& track : made.GetTracks())
                {
                    if (!track.mWheels.empty() && track.mDrivenWheel < desc.carried->wheels.size())
                    {
                        track.mAngularVelocity = desc.carried->wheels[track.mDrivenWheel].angularVelocity;
                    }
                }
            }
        }
        slot.filter = std::make_unique<ProbeFilter>(ToJolt(desc.body), characterListener);
        Retest(slot);

        system.AddConstraint(slot.constraint);
        system.AddStepListener(slot.constraint);
        // Jolt wakes nothing when a constraint is added.
        Bodies().ActivateBody(ToJolt(desc.body));
        return slot;
    }

    VehicleId World::CreateVehicle(const VehicleDesc& desc)
    {
        Impl& impl = *m_impl;
        Impl::VehicleSlot slot = impl.MakeVehicle(desc);
        if (impl.freeVehicles.empty())
        {
            impl.vehicles.push_back(std::move(slot));
            return VehicleId{static_cast<std::uint32_t>(impl.vehicles.size() - 1)};
        }
        const std::uint32_t index = impl.freeVehicles.back();
        impl.freeVehicles.pop_back();
        impl.vehicles[index] = std::move(slot);
        return VehicleId{index};
    }

    void World::DestroyVehicle(VehicleId id)
    {
        Impl::VehicleSlot& slot = m_impl->vehicles[id.value];
        m_impl->UnmakeVehicle(slot);
        slot = Impl::VehicleSlot{};
        m_impl->freeVehicles.push_back(id.value);
    }

    bool World::UpdateVehicle(VehicleId id, const VehicleDesc& desc)
    {
        Impl& impl = *m_impl;
        Impl::VehicleSlot& slot = impl.vehicles[id.value];
        const VehicleDriveDesc& drive = desc.drive;
        // Jolt fixes a vehicle's controller and a motorcycle's lean limit when it is made, and an unpowered vehicle is
        // made with the default differentials: one changing any of them is made again in its place, carrying on from
        // where it reads, a new wheel still.
        if (drive.controller != slot.controller || drive.powered != slot.powered ||
            (drive.controller == VehicleController::Motorcycle &&
             std::bit_cast<std::uint32_t>(drive.lean.maxAngle) != std::bit_cast<std::uint32_t>(slot.maxLeanAngle)))
        {
            const VehicleRead read = ReadVehicle(id);
            VehicleDesc remade = desc;
            remade.carried = VehicleCarry{.engineRpm = read.state.engineRpm,
                                          .gear = std::min(read.state.gear, drive.transmission.gearCount),
                                          .clutch = read.state.clutch,
                                          .wheels = {}};
            for (const VehicleWheelRead& wheel : read.wheels)
            {
                remade.carried->wheels.push_back(
                    VehicleWheelSpin{.angularVelocity = wheel.angularVelocity, .spinAngle = wheel.spinAngle});
            }
            impl.UnmakeVehicle(slot);
            slot = Impl::VehicleSlot{};
            slot = impl.MakeVehicle(remade);
            return true;
        }

        // Jolt's engine and gearbox are their own settings, read every step.
        WithController(*slot.constraint, slot.controller, [&drive](auto& controller) {
            JPH::VehicleEngine& engine = controller.GetEngine();
            Fill(engine, drive.engine);
            engine.ClampRPM();
            JPH::VehicleTransmission& transmission = controller.GetTransmission();
            if (transmission.mMode == JPH::ETransmissionMode::Auto &&
                drive.transmission.mode == TransmissionMode::Manual)
            {
                FinishShift(transmission);
            }
            Fill(transmission, drive.transmission);
            // Fewer gears than the one engaged: the top one.
            if (transmission.GetCurrentGear() > drive.transmission.gearCount)
            {
                transmission.Set(drive.transmission.gearCount, transmission.GetClutchFriction());
            }
        });
        const bool tracked = slot.controller == VehicleController::Tracked;
        if (tracked)
        {
            // A track's own settings in place, keeping its wheels and its spin; its grip is its wheels'.
            slot.tracks = {drive.leftTrack, drive.rightTrack};
            auto& controller = static_cast<JPH::TrackedVehicleController&>(*slot.constraint->GetController());
            for (const VehicleTrackSide side : trackSides)
            {
                Fill(Sides(controller.GetTracks())[static_cast<std::size_t>(side)],
                     slot.tracks[static_cast<std::size_t>(side)]);
            }
        }
        else
        {
            auto& controller = static_cast<JPH::WheeledVehicleController&>(*slot.constraint->GetController());
            controller.SetDifferentialLimitedSlipRatio(drive.limitedSlipRatio);
            if (slot.controller == VehicleController::Motorcycle)
            {
                Fill(static_cast<JPH::MotorcycleController&>(controller), drive.lean);
            }
        }

        // The wheels in place, their spin and suspension carrying on. Jolt reads them by reference, and its checks ran
        // only when each was made: physics checked these values.
        bool resized = false;
        for (std::size_t i = 0; i < slot.wheels.size(); ++i)
        {
            const VehicleWheelDesc& wheel = desc.wheels[i];
            resized = resized || std::bit_cast<std::uint32_t>(slot.wheels[i]->mRadius) !=
                                     std::bit_cast<std::uint32_t>(wheel.radius);
            if (tracked)
            {
                Fill(static_cast<JPH::WheelSettingsTV&>(*slot.wheels[i]), wheel,
                     slot.tracks[static_cast<std::size_t>(wheel.track)]);
                slot.trackedWheels[i] = TrackedWheel{.side = wheel.track, .z = wheel.position.z};
            }
            else
            {
                Fill(static_cast<JPH::WheelSettingsWV&>(*slot.wheels[i]), wheel);
            }
        }
        // A wheel moved, or moved to the other track, can change both tracks' wheels and driven wheels: Jolt reads
        // which wheels each track has every step.
        if (tracked)
        {
            auto& controller = static_cast<JPH::TrackedVehicleController&>(*slot.constraint->GetController());
            for (const VehicleTrackSide side : trackSides)
            {
                Assign(Sides(controller.GetTracks())[static_cast<std::size_t>(side)], side, slot.trackedWheels);
            }
        }
        // A new tester only for a new probe, or a sphere sized from wheels that changed size.
        if (desc.probe != slot.probe || (resized && slot.probe == VehicleProbe::Sphere))
        {
            slot.probe = desc.probe;
            impl.Retest(slot);
        }
        slot.constraint->SetMaxPitchRollAngle(desc.maxTilt);
        // An unpowered vehicle keeps the default differentials, undriven in neutral. A tracked vehicle's tracks are its
        // drive.
        if (slot.powered && !tracked)
        {
            auto& controller = static_cast<JPH::WheeledVehicleController&>(*slot.constraint->GetController());
            Fill(controller.GetDifferentials(), desc.differentials, slot.constraint->GetWheels().size());
        }
        Fill(slot.constraint->GetAntiRollBars(), desc.antiRollBars);
        return false;
    }

    void World::SetVehicleInput(VehicleId id, const VehicleInput& input, bool wake)
    {
        const Impl::VehicleSlot& slot = m_impl->vehicles[id.value];
        if (slot.controller == VehicleController::Tracked)
        {
            auto& controller = static_cast<JPH::TrackedVehicleController&>(*slot.constraint->GetController());
            controller.SetDriverInput(input.forward, TrackRatio(input.leftTrack), TrackRatio(input.rightTrack),
                                      input.brake);
        }
        else
        {
            auto& controller = static_cast<JPH::WheeledVehicleController&>(*slot.constraint->GetController());
            // Jolt's right is ours.
            controller.SetDriverInput(input.forward, input.steer, input.brake, input.handbrake);
        }
        // In Manual the gear and clutch are game code's; Jolt's clutch is the friction it gives, 1 engaged.
        WithController(*slot.constraint, slot.controller, [&input](auto& controller) {
            JPH::VehicleTransmission& transmission = controller.GetTransmission();
            if (transmission.mMode == JPH::ETransmissionMode::Manual)
            {
                transmission.Set(input.gear, 1.0f - input.clutch);
            }
        });
        // Jolt does not wake a vehicle's body on input.
        if (wake)
        {
            m_impl->Bodies().ActivateBody(ToJolt(slot.body));
        }
    }

    VehicleRead World::ReadVehicle(VehicleId id) const
    {
        const Impl& impl = *m_impl;
        const Impl::VehicleSlot& slot = impl.vehicles[id.value];
        const JPH::VehicleConstraint& constraint = *slot.constraint;
        const JPH::BodyLockInterfaceNoLock& locks = impl.system.GetBodyLockInterfaceNoLock();
        VehicleRead read;

        {
            JPH::BodyLockRead lock(locks, ToJolt(slot.body));
            JPH_ASSERT(lock.Succeeded());
            const JPH::Body& body = lock.GetBody();
            read.state.forwardSpeed = body.GetLinearVelocity().Dot(body.GetRotation() * vehicleForward);
            if (slot.controller == VehicleController::Motorcycle)
            {
                // + right: a bike leaning right has its right side down.
                const JPH::Vec3 right = body.GetRotation() * JPH::Vec3::sAxisX();
                read.state.leanAngle = -std::asin(std::clamp(right.Dot(constraint.GetWorldUp()), -1.0f, 1.0f));
            }
        }
        // An unpowered vehicle's idling engine and neutral gearbox are the backend's, not its: its fields stay 0.
        if (slot.powered)
        {
            WithController(constraint, slot.controller, [&read](const auto& controller) {
                read.state.engineRpm = controller.GetEngine().GetCurrentRPM();
                read.state.gear = controller.GetTransmission().GetCurrentGear();
                read.state.clutch = 1.0f - controller.GetTransmission().GetClutchFriction();
                read.state.shifting = controller.GetTransmission().IsSwitchingGear();
            });
        }
        const bool tracked = slot.controller == VehicleController::Tracked;
        if (tracked)
        {
            // A track's speed is its driven wheel's rim speed.
            const auto& controller = static_cast<const JPH::TrackedVehicleController&>(*constraint.GetController());
            const auto speedOf = [&constraint](const JPH::VehicleTrack& track) {
                return track.mWheels.empty()
                           ? 0.0f
                           : track.mAngularVelocity * constraint.GetWheel(track.mDrivenWheel)->GetSettings()->mRadius;
            };
            read.state.leftTrackSpeed = speedOf(Sides(controller.GetTracks())[0]);
            read.state.rightTrackSpeed = speedOf(Sides(controller.GetTracks())[1]);
        }

        for (const JPH::Wheel* each : constraint.GetWheels())
        {
            const JPH::Wheel& wheel = *each;
            const JPH::WheelSettings& settings = *wheel.GetSettings();
            VehicleWheelRead out;
            out.compression = std::clamp(settings.mSuspensionMaxLength - wheel.GetSuspensionLength(), 0.0f,
                                         settings.mSuspensionMaxLength - settings.mSuspensionMinLength);
            out.angularVelocity = wheel.GetAngularVelocity();
            out.steerAngle = -wheel.GetSteerAngle();
            out.spinAngle = wheel.GetRotationAngle();
            // A tracked wheel has no slip.
            if (!tracked)
            {
                const auto& rolling = static_cast<const JPH::WheelWV&>(wheel);
                out.longitudinalSlip = rolling.mLongitudinalSlip;
                out.lateralSlip = rolling.mLateralSlip;
            }
            // The suspension's impulse over the last collision step.
            out.suspensionForce =
                impl.collisionStepTime > 0.0f ? wheel.GetSuspensionLambda() / impl.collisionStepTime : 0.0f;
            if (wheel.HasContact())
            {
                JPH::BodyLockRead lock(locks, wheel.GetContactBodyID());
                // A ground body destroyed since the step is no ground.
                if (lock.Succeeded())
                {
                    out.onGround = true;
                    ++read.state.wheelsOnGround;
                    out.groundEntity = EntityOf(lock.GetBody());
                    out.groundCollider = ColliderOf(lock.GetBody(), wheel.GetContactSubShapeID());
                    out.contactPoint = FromJolt(JPH::Vec3(wheel.GetContactPosition()));
                    out.contactNormal = FromJolt(wheel.GetContactNormal());
                    out.groundVelocity = FromJolt(wheel.GetContactPointVelocity());
                }
            }
            read.wheels.push_back(out);
        }
        return read;
    }

    void World::Step(float dt, int collisionSteps)
    {
        JPH::BodyIDVector awake;
        m_impl->system.GetActiveBodies(JPH::EBodyType::RigidBody, awake);
        std::set<std::uint32_t> awakeAtStart;
        for (const JPH::BodyID& id : awake)
        {
            awakeAtStart.insert(id.GetIndexAndSequenceNumber());
        }

        m_impl->CutMotorsAtLimits(dt);
        for (Impl::VehicleSlot& slot : m_impl->vehicles)
        {
            if (slot.constraint != nullptr && slot.controller == VehicleController::Motorcycle)
            {
                CapLeanDamping(static_cast<JPH::MotorcycleController&>(*slot.constraint->GetController()),
                               *slot.constraint->GetVehicleBody(), dt / static_cast<float>(collisionSteps));
            }
        }
        const JPH::EPhysicsUpdateError error =
            m_impl->system.Update(dt, collisionSteps, &m_impl->tempAllocator, m_impl->jobs.get());
        m_impl->collisionStepTime = dt / static_cast<float>(collisionSteps);
        // Jolt lets a vehicle sleep with its wheels turning below 10°/s, and turns them on while it sleeps, unbraked:
        // a sleeping vehicle's wheels are stopped, so it keeps its pose.
        for (Impl::VehicleSlot& slot : m_impl->vehicles)
        {
            if (slot.constraint != nullptr && !m_impl->Bodies().IsActive(ToJolt(slot.body)))
            {
                for (JPH::Wheel* wheel : slot.constraint->GetWheels())
                {
                    wheel->SetAngularVelocity(0.0f);
                }
                // A tracked vehicle's wheels turn with their tracks.
                if (slot.controller == VehicleController::Tracked)
                {
                    auto& controller = static_cast<JPH::TrackedVehicleController&>(*slot.constraint->GetController());
                    for (JPH::VehicleTrack& track : controller.GetTracks())
                    {
                        track.mAngularVelocity = 0.0f;
                    }
                }
            }
        }
        if (error != JPH::EPhysicsUpdateError::None)
        {
            m_impl->logger->Warn("Physics step ran out of room for body pairs or contacts (error " +
                                 std::to_string(static_cast<unsigned>(error)) + "); some contacts were ignored");
        }
        m_impl->UpdateTouches(awakeAtStart);
    }

    std::span<const TouchChange> World::TouchChanges() const
    {
        return m_impl->changes;
    }

    std::span<const Touch> World::Touches() const
    {
        return m_impl->touches;
    }
} // namespace Fenrir::Backend
