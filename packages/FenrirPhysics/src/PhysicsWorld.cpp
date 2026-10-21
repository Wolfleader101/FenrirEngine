#include "FenrirPhysics/PhysicsWorld.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <tuple>
#include <utility>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirScene/Scene.hpp"
#include "WorldImpl.hpp"

namespace Fenrir
{
    namespace
    {
        template <typename TEvent, typename Key>
        void SortAndSend(App& app, std::vector<TEvent>& events, Key key)
        {
            std::sort(events.begin(), events.end(),
                      [&key](const TEvent& a, const TEvent& b) { return key(a) < key(b); });
            for (const TEvent& event : events)
            {
                app.SendEvent(event);
            }
        }

        // Enough collision steps that none is longer than a 60 Hz step.
        int CollisionSteps(float dt)
        {
            return std::max(1, static_cast<int>(std::ceil(dt * 60.0f - 1e-4f)));
        }
    } // namespace

    PhysicsWorld::PhysicsWorld(Key /*key*/, std::unique_ptr<Impl> impl) : m_impl(std::move(impl))
    {
    }

    std::size_t PhysicsWorld::DistinctShapeCount() const
    {
        return m_impl->m_backend.DistinctShapeCount();
    }

    PhysicsWorld::~PhysicsWorld() = default;

    PhysicsWorld* PhysicsWorld::Of(Scene& scene)
    {
        return scene.GetResource<PhysicsWorld>();
    }

    void PhysicsWorld::AddImpulse(const Entity& entity, const Math::Vec3& impulse)
    {
        m_impl->Queue(entity, &Backend::World::AddImpulse, impulse);
    }

    void PhysicsWorld::AddForce(const Entity& entity, const Math::Vec3& force)
    {
        m_impl->Queue(entity, &Backend::World::AddForce, force);
    }

    void PhysicsWorld::AddTorque(const Entity& entity, const Math::Vec3& torque)
    {
        m_impl->Queue(entity, &Backend::World::AddTorque, torque);
    }

    void PhysicsWorld::AddAngularImpulse(const Entity& entity, const Math::Vec3& impulse)
    {
        m_impl->Queue(entity, &Backend::World::AddAngularImpulse, impulse);
    }

    Math::Vec3 PhysicsWorld::Gravity() const
    {
        return m_impl->Gravity();
    }

    void PhysicsWorld::SetGravity(const Math::Vec3& gravity)
    {
        m_impl->SetGravity(gravity);
    }

    PhysicsWorld::Impl::Impl(EntityList& entities, const Backend::WorldSettings& settings,
                             Backend::CollisionMeshes& meshes, ILogger& logger)
        : m_entities(&entities), m_logger(&logger), m_backend(settings, meshes, logger), m_poses(entities),
          m_bodies(entities, logger, m_backend, m_poses), m_jointEntities(entities, ComponentType::Native<Joint>()),
          m_characterEntities(entities, ComponentType::Native<CharacterBody>()),
          m_vehicleEntities(entities, ComponentType::Native<Vehicle>()),
          m_wheelEntities(entities, ComponentType::Native<VehicleWheel>()),
          m_differentialEntities(entities, ComponentType::Native<VehicleDifferential>()),
          m_antiRollBarEntities(entities, ComponentType::Native<VehicleAntiRollBar>()), m_canvas(m_backend)
    {
        for (const std::uint32_t entity : m_differentialEntities.Entities())
        {
            m_differentials.try_emplace(entity);
        }
        for (const std::uint32_t entity : m_antiRollBarEntities.Entities())
        {
            m_antiRollBars.try_emplace(entity);
        }
    }

    void PhysicsWorld::ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const
    {
        m_impl->ForEachContact(entity, func);
    }

    void PhysicsWorld::Impl::Step(App& app, float dt)
    {
        // Drain and rebuild. A joint's constraint and a vehicle go before their bodies, and characters go before the
        // bodies' batch and are made after it, so the bodies come in a reproducible order.
        const Detail::Bodies::Rebuild rebuild = m_bodies.Plan();
        DrainJoints();
        DrainVehicles();
        RemoveJointsOf(rebuild.destroyed);
        RemoveVehiclesOf(rebuild.destroyed);
        DropCharacters(rebuild.changed);
        m_bodies.Apply(rebuild);
        MakeCharacters();

        // Edits. After the bodies': a body's scale decides whether its joints are recreated, and a vehicle is made
        // after its body and its body's joints.
        m_bodies.PushEdits(dt);
        PushCharacterEdits();
        SyncJoints(dt);
        SyncVehicles();
        m_bodies.ApplyPushes();

        // Moves: before the step, so a character's pushes act in it and it moves against the bodies the step will
        // move.
        MoveCharacters(dt);

        m_backend.Step(dt, CollisionSteps(dt));

        // Write-back. After the chassis: wheels are posed against its new pose.
        WriteBack();
        WriteBackVehicles(dt);

        // Joints after the step.
        MeasureJoints();
        BreakJoints();

        // Events, then the shown poses and the debug lines.
        SendEvents(app);
        m_poses.Advance();
        DrawDebugLines();
    }

    void PhysicsWorld::Impl::Restore()
    {
        m_poses.Restore();
        RestoreWheels();
    }

    void PhysicsWorld::Impl::Blend(float fraction)
    {
        m_poses.Blend(fraction);
        // After the bodies: wheels are posed on their chassis's blended pose.
        BlendWheels(fraction);
    }

    void PhysicsWorld::Impl::WriteBack()
    {
        m_bodies.HandPoses();
        HandCharacterPoses();
        m_poses.WriteBack();
        m_bodies.ReadBack();
        for (auto& [id, record] : m_characters)
        {
            Entity entity = m_entities->GetEntity(id);
            ReadBackCharacter(entity, record);
        }
        ResetIdleCharacters();
    }

    bool PhysicsWorld::Impl::ReportsContacts(std::uint32_t body) const
    {
        if (!m_entities->HasEntity(body))
        {
            return false;
        }
        Entity entity = m_entities->GetEntity(body);
        return entity.HasComponent<ContactEvents>() && entity.GetComponent<ContactEvents>().enabled;
    }

    void PhysicsWorld::Impl::SendEvents(App& app)
    {
        std::vector<ContactBegin> begins;
        std::vector<ContactEnd> ends;
        std::vector<TriggerEnter> enters;
        std::vector<TriggerExit> exits;
        const auto entity = [this](std::uint32_t id) { return m_entities->HandleOf(id); };

        for (const Backend::TouchChange& change : m_backend.TouchChanges())
        {
            const Backend::Touch& touch = change.touch;
            const bool isBegin = change.kind == Backend::TouchChange::Kind::Begin;
            if (touch.isTrigger1 || touch.isTrigger2)
            {
                const bool firstIsTrigger = touch.isTrigger1;
                const OverlapKey key{firstIsTrigger ? touch.entity1 : touch.entity2,
                                     firstIsTrigger ? touch.entity2 : touch.entity1,
                                     firstIsTrigger ? touch.collider2 : touch.collider1};
                // A trigger beneath a body ignores that body: it moves with it, so it would always be inside it.
                if (isBegin && key.otherBody == BodyView().AttachedBodyOf(key.trigger))
                {
                    continue;
                }
                if (isBegin && m_reportedOverlaps.insert(key).second)
                {
                    enters.push_back(
                        TriggerEnter{entity(key.trigger), entity(key.otherBody), entity(key.otherCollider)});
                }
                else if (!isBegin && m_reportedOverlaps.erase(key) > 0)
                {
                    exits.push_back(TriggerExit{entity(key.trigger), entity(key.otherBody), entity(key.otherCollider)});
                }
                continue;
            }

            if (touch.isCharacter1 || touch.isCharacter2)
            {
                continue; // merged with the characters' moves below
            }
            const auto [key, oriented] = Oriented(touch);
            if (isBegin)
            {
                if ((ReportsContacts(key.bodyA) || ReportsContacts(key.bodyB)) && m_reportedContacts.insert(key).second)
                {
                    begins.push_back(ContactBegin{entity(key.bodyA), entity(key.bodyB), entity(key.colliderA),
                                                  entity(key.colliderB), oriented.point, oriented.normal,
                                                  oriented.relativeSpeed});
                }
            }
            else if (m_reportedContacts.erase(key) > 0)
            {
                ends.push_back(
                    ContactEnd{entity(key.bodyA), entity(key.bodyB), entity(key.colliderA), entity(key.colliderB)});
            }
        }

        TrackCharacterContacts(begins, ends);

        // A contact neither body reports any more (ContactEvents removed or disabled) ends now, as if it separated.
        for (auto it = m_reportedContacts.begin(); it != m_reportedContacts.end();)
        {
            if (ReportsContacts(it->bodyA) || ReportsContacts(it->bodyB))
            {
                ++it;
                continue;
            }
            ends.push_back(
                ContactEnd{entity(it->bodyA), entity(it->bodyB), entity(it->colliderA), entity(it->colliderB)});
            it = m_reportedContacts.erase(it);
        }

        // Sorted by entity, so the order is the same on every run whatever order the backend found them in.
        const auto contactOrder = [](const auto& event) {
            return std::make_tuple(event.bodyA.GetId(), event.bodyB.GetId(), event.colliderA.GetId(),
                                   event.colliderB.GetId());
        };
        const auto overlapOrder = [](const auto& event) {
            return std::make_tuple(event.trigger.GetId(), event.otherBody.GetId(), event.otherCollider.GetId());
        };
        SortAndSend(app, begins, contactOrder);
        SortAndSend(app, ends, contactOrder);
        SortAndSend(app, enters, overlapOrder);
        SortAndSend(app, exits, overlapOrder);
        SortAndSend(app, m_broken, [](const JointBroken& event) { return event.joint.GetId(); });
        m_broken.clear();
    }

    std::pair<PhysicsWorld::Impl::ContactKey, Backend::Touch> PhysicsWorld::Impl::Oriented(const Backend::Touch& touch)
    {
        if (std::tie(touch.entity1, touch.collider1) <= std::tie(touch.entity2, touch.collider2))
        {
            return {ContactKey{touch.entity1, touch.entity2, touch.collider1, touch.collider2}, touch};
        }
        Backend::Touch turned = touch;
        std::swap(turned.body1, turned.body2);
        std::swap(turned.entity1, turned.entity2);
        std::swap(turned.collider1, turned.collider2);
        std::swap(turned.isTrigger1, turned.isTrigger2);
        std::swap(turned.isCharacter1, turned.isCharacter2);
        turned.normal = -touch.normal;
        return {ContactKey{turned.entity1, turned.entity2, turned.collider1, turned.collider2}, turned};
    }

    void PhysicsWorld::Impl::TrackCharacterContacts(std::vector<ContactBegin>& begins, std::vector<ContactEnd>& ends)
    {
        // The move found its contacts before the step, so where both sources begin a pair at once, the move saw it
        // first: its findings go in first, and the first finding of a pair stands.
        std::map<ContactKey, Backend::Touch> now;
        for (const auto& [id, record] : m_characters)
        {
            for (const Backend::Touch& touch : record.moved.contacts)
            {
                now.insert(Oriented(touch));
            }
        }
        // A character's body meets only dynamic bodies and triggers in the step: kinematic and static ones never
        // collide with it there.
        for (const Backend::Touch& touch : m_backend.Touches())
        {
            if ((touch.isCharacter1 || touch.isCharacter2) && !touch.isTrigger1 && !touch.isTrigger2)
            {
                now.insert(Oriented(touch));
            }
        }

        const auto entity = [this](std::uint32_t id) { return m_entities->HandleOf(id); };
        for (const auto& [key, touch] : now)
        {
            if (!m_characterTouches.contains(key) && (ReportsContacts(key.bodyA) || ReportsContacts(key.bodyB)) &&
                m_reportedContacts.insert(key).second)
            {
                begins.push_back(ContactBegin{entity(key.bodyA), entity(key.bodyB), entity(key.colliderA),
                                              entity(key.colliderB), touch.point, touch.normal, touch.relativeSpeed});
            }
        }
        // Neither source has it: separated, or the character or the body is gone (a character disabled or inactive
        // has no moves and no body), so every begin has its end.
        for (const auto& [key, touch] : m_characterTouches)
        {
            if (!now.contains(key) && m_reportedContacts.erase(key) > 0)
            {
                ends.push_back(
                    ContactEnd{entity(key.bodyA), entity(key.bodyB), entity(key.colliderA), entity(key.colliderB)});
            }
        }
        m_characterTouches = std::move(now);
    }

    void PhysicsWorld::Impl::ForEachContact(const Entity& entity, const std::function<void(const Contact&)>& func) const
    {
        if (!entity.IsValid())
        {
            return; // a destroyed entity's touches end at the next step, but it has no body to ask about now
        }
        const auto handle = [this](std::uint32_t id) { return m_entities->HandleOf(id); };
        const auto report = [&](std::uint32_t asked, const Backend::Touch& touch) {
            if (touch.entity1 == asked)
            {
                func(Contact{handle(touch.entity1), handle(touch.collider1), handle(touch.entity2),
                             handle(touch.collider2), touch.point, touch.normal});
            }
            else if (touch.entity2 == asked)
            {
                func(Contact{handle(touch.entity2), handle(touch.collider2), handle(touch.entity1),
                             handle(touch.collider1), touch.point, -touch.normal});
            }
        };
        // A character's are its last move's, from before the step.
        if (const auto character = m_characters.find(entity.GetId()); character != m_characters.end())
        {
            for (const Backend::Touch& touch : character->second.moved.contacts)
            {
                report(entity.GetId(), touch);
            }
            return;
        }
        // A child collider's entity stands for the compound body it belongs to.
        const std::uint32_t asked = BodyView().ResolveBody(entity.GetId());
        for (const Backend::Touch& touch : m_backend.Touches())
        {
            if (!touch.isTrigger1 && !touch.isTrigger2 && !touch.isCharacter1 && !touch.isCharacter2)
            {
                report(asked, touch);
            }
        }
        // A body touching a character, as the contact events tell it.
        for (const auto& [key, touch] : m_characterTouches)
        {
            report(asked, touch);
        }
    }
} // namespace Fenrir
