#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "ComponentRegistry.hpp"
#include "FenrirApp/App.hpp"

namespace Fenrir::Detail
{
    // What a script asked for that the buffer refused at once, as the code C# turns into a message.
    enum class ChangeError : std::int32_t
    {
        None = 0,
        EntityNotAlive = -1,
        UnknownComponent = -2,
        ReadOnlyComponent = -3,
        RequiredComponent = -4,
        NotActive = -5, // Scripting is not running, so there is no scene to change.
        UnknownEvent = -6,
    };

    struct ApplyResult
    {
        std::size_t applied = 0;
        std::size_t skipped = 0;
    };

    // The structural changes a script makes while it runs (create, destroy, add, remove), kept in the order they were
    // made and played back together when the script returns. Until then the storage does not move, so every Ref<T> a
    // script took stays valid. Reads are never buffered: they see the live state.
    class ChangeBuffer
    {
      public:
        // A new entity id, usable straight away (it is alive, with no components yet). It gets Transform, Name and
        // Relationship at playback, like any entity the engine creates.
        [[nodiscard]] std::uint32_t Create(EntityList& entities);

        [[nodiscard]] ChangeError Destroy(EntityList& entities, std::uint32_t entity);

        // `value` is one component's bytes (registration.size of them), or null for a default one.
        [[nodiscard]] ChangeError Add(EntityList& entities, const ComponentRegistry& registry, std::int32_t component,
                                      std::uint32_t entity, const void* value);

        [[nodiscard]] ChangeError Remove(EntityList& entities, const ComponentRegistry& registry, std::int32_t component,
                                         std::uint32_t entity);

        // Records an event to send: `value` is registration.size bytes of event, copied now. It is put in the event's queue at
        // playback, so a system never reads its own sends and a view it took stays valid.
        [[nodiscard]] ChangeError SendEvent(App& app, const ComponentRegistry& registry, std::int32_t event, const void* value);

        // Plays every recorded change back in order and empties the buffer. A change whose entity died in the meantime
        // (an earlier change in the buffer destroyed it) is skipped with a warning, never applied to a reused slot.
        ApplyResult Apply(App& app, const ComponentRegistry& registry);

        // Forgets every recorded change. Entities created and not yet played back are destroyed, so none are left behind.
        void Discard(EntityList& entities);

        [[nodiscard]] bool Empty() const noexcept
        {
            return m_changes.empty();
        }

        [[nodiscard]] std::size_t Size() const noexcept
        {
            return m_changes.size();
        }

      private:
        enum class Kind : std::uint8_t
        {
            Create,
            Destroy,
            Add,
            Remove,
            SendEvent,
        };

        struct Change
        {
            Kind kind;
            bool hasValue;
            std::int32_t component;
            std::uint32_t entity;
            std::size_t valueOffset;
        };

        std::vector<Change> m_changes;
        std::vector<std::byte> m_values; // The bytes of every Add that carried a value, back to back.
    };
} // namespace Fenrir::Detail
