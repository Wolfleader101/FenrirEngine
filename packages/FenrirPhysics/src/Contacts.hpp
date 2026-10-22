#pragma once

// A physics world's contact and trigger events: what game code has been told is touching, turned from the backend's
// touch changes and touches and each character's end-of-move contacts into ContactBegin, ContactEnd, TriggerEnter and
// TriggerExit, and the contacts ForEachContact answers with. Reads the bodies through Bodies' const members. Knows the
// ECS, not Jolt: it talks to Backend::World. Private to the FenrirPhysics package.

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include "Backend/Backend.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"

namespace Fenrir
{
    class App;
    class Entity;
    class EntityList;
} // namespace Fenrir

namespace Fenrir::Detail
{
    class Bodies;
    class DebugCanvas;

    class Contacts
    {
      public:
        /** @brief The events of the bodies in `entities`, as `backend` finds them touching and `bodies` resolves
         * them. */
        Contacts(EntityList& entities, const Backend::World& backend, const Bodies& bodies);

        /** @brief Events: sends the step's ContactBegins, ContactEnds, TriggerEnters and TriggerExits to `app`, in
         * that order, each kind sorted by entity. `moves` are each character's contacts at the end of its last move,
         * in character entity order. */
        void Send(App& app, std::span<const std::span<const Backend::Touch>> moves);

        /** @brief Calls `func` for each contact of `entity`'s body touching now. `moved` is `entity`'s contacts at the
         * end of its last move if it is a character, and null otherwise. */
        void ForEachContact(const Entity& entity, const std::vector<Backend::Touch>* moved,
                            const std::function<void(const Contact&)>& func) const;

        /** @brief Debug lines: a point and normal at each contact the backend found in the last step. */
        void Draw(DebugCanvas& canvas) const;

      private:
        // A contact reported by a ContactBegin and not yet ended, A and B ordered as in the event.
        struct ContactKey
        {
            std::uint32_t bodyA;
            std::uint32_t bodyB;
            std::uint32_t colliderA;
            std::uint32_t colliderB;

            auto operator<=>(const ContactKey&) const = default;
        };

        // An overlap reported by a TriggerEnter and not yet exited.
        struct OverlapKey
        {
            std::uint32_t trigger;
            std::uint32_t otherBody;
            std::uint32_t otherCollider;

            auto operator<=>(const OverlapKey&) const = default;
        };

        /** @brief A contact's key, A and B in entity order so the order does not depend on the backend's body ids, and
         * the touch turned to match: its normal from A to B. */
        [[nodiscard]] static std::pair<ContactKey, Backend::Touch> Oriented(const Backend::Touch& touch);

        /** @brief Before the events are sorted: brings m_characterTouches up to date from `moves`, each character's
         * end-of-move contacts, and its body's step contacts, adding the begins and ends of the pairs reported. */
        void TrackCharacterContacts(std::span<const std::span<const Backend::Touch>> moves,
                                    std::vector<ContactBegin>& begins, std::vector<ContactEnd>& ends);

        [[nodiscard]] bool ReportsContacts(std::uint32_t body) const;

        EntityList* m_entities;
        const Backend::World* m_backend;
        const Bodies* m_bodies;

        // What game code has been told is touching, so every begin and enter gets its end and exit.
        std::set<ContactKey> m_reportedContacts;
        // The pairs with a character touching after the last step, whether reported or not, oriented as their key:
        // touching while either the character's last move or its body's step finds them.
        std::map<ContactKey, Backend::Touch> m_characterTouches;
        std::set<OverlapKey> m_reportedOverlaps;
    };
} // namespace Fenrir::Detail
