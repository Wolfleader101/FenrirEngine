#include "Contacts.hpp"

#include <tuple>

#include "Bodies.hpp"
#include "DebugCanvas.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirPhysics/Components.hpp"
#include "SortAndSend.hpp"

namespace Fenrir::Detail
{
    Contacts::Contacts(EntityList& entities, const Backend::World& backend, const Bodies& bodies)
        : m_entities(&entities), m_backend(&backend), m_bodies(&bodies)
    {
    }

    bool Contacts::ReportsContacts(std::uint32_t body) const
    {
        if (!m_entities->HasEntity(body))
        {
            return false;
        }
        Entity entity = m_entities->GetEntity(body);
        return entity.HasComponent<ContactEvents>() && entity.GetComponent<ContactEvents>().enabled;
    }

    void Contacts::Send(App& app, std::span<const std::span<const Backend::Touch>> moves)
    {
        std::vector<ContactBegin> begins;
        std::vector<ContactEnd> ends;
        std::vector<TriggerEnter> enters;
        std::vector<TriggerExit> exits;
        const auto entity = [this](std::uint32_t id) { return m_entities->HandleOf(id); };

        for (const Backend::TouchChange& change : m_backend->TouchChanges())
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
                if (isBegin && key.otherBody == m_bodies->AttachedBodyOf(key.trigger))
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

        TrackCharacterContacts(moves, begins, ends);

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
    }

    std::pair<Contacts::ContactKey, Backend::Touch> Contacts::Oriented(const Backend::Touch& touch)
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

    void Contacts::TrackCharacterContacts(std::span<const std::span<const Backend::Touch>> moves,
                                          std::vector<ContactBegin>& begins, std::vector<ContactEnd>& ends)
    {
        // The move found its contacts before the step, so where both sources begin a pair at once, the move saw it
        // first: its findings go in first, and the first finding of a pair stands.
        std::map<ContactKey, Backend::Touch> now;
        for (const std::span<const Backend::Touch> contacts : moves)
        {
            for (const Backend::Touch& touch : contacts)
            {
                now.insert(Oriented(touch));
            }
        }
        // A character's body meets only dynamic bodies and triggers in the step: kinematic and static ones never
        // collide with it there.
        for (const Backend::Touch& touch : m_backend->Touches())
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

    void Contacts::ForEachContact(const Entity& entity, const std::vector<Backend::Touch>* moved,
                                  const std::function<void(const Contact&)>& func) const
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
        if (moved != nullptr)
        {
            for (const Backend::Touch& touch : *moved)
            {
                report(entity.GetId(), touch);
            }
            return;
        }
        // A child collider's entity stands for the compound body it belongs to.
        const std::uint32_t asked = m_bodies->ResolveBody(entity.GetId());
        for (const Backend::Touch& touch : m_backend->Touches())
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

    void Contacts::Draw(DebugCanvas& canvas) const
    {
        for (const Backend::Touch& touch : m_backend->Touches())
        {
            if (touch.isTrigger1 || touch.isTrigger2)
            {
                continue; // an overlap, not a contact
            }
            canvas.ContactPoint(touch.point, touch.normal, PhysicsDebugColour::Contact);
        }
    }
} // namespace Fenrir::Detail
