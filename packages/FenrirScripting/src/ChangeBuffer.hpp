#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "ComponentRegistry.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirScripting/Errors.hpp"

namespace Fenrir::Detail
{
    struct ApplyResult
    {
        std::size_t applied = 0;
        std::size_t skipped = 0;
    };

    // The structural changes a script makes while it runs (create, destroy, add, remove, insert and remove a resource),
    // and the ones the native functions it calls record, kept in the order they were made and played back together when
    // the script returns. Until then the storage does not move, so every Ref<T> a
    // script took stays valid. Reads are never buffered: they see the live state.
    class ChangeBuffer
    {
      public:
        // A new entity id, usable straight away (it is alive, with no components yet). It gets Transform, Name and
        // Relationship at playback, like any entity the engine creates.
        [[nodiscard]] std::uint32_t Create(EntityList& entities);

        [[nodiscard]] ChangeError Destroy(EntityList& entities, std::uint32_t entity);

        // `value` is one component's bytes (registration.type.Size() of them), or null for a default one.
        [[nodiscard]] ChangeError Add(EntityList& entities, const ComponentRegistry& registry, std::int32_t component,
                                      std::uint32_t entity, const void* value);

        [[nodiscard]] ChangeError Remove(EntityList& entities, const ComponentRegistry& registry,
                                         std::int32_t component, std::uint32_t entity);

        // Records an event to send: `value` is registration.size bytes of event, copied now. It is put in the event's
        // queue at playback, so a system never reads its own sends and a view it took stays valid.
        [[nodiscard]] ChangeError SendEvent(App& app, const ComponentRegistry& registry, std::int32_t event,
                                            const void* value);

        // Records inserting the resource `resource` (registration.size bytes of `value`, copied now, or null for a
        // default one), and removing it. A scene resource is the active scene's at playback.
        [[nodiscard]] ChangeError InsertResource(const ComponentRegistry& registry, std::int32_t resource,
                                                 const void* value);
        [[nodiscard]] ChangeError RemoveResource(const ComponentRegistry& registry, std::int32_t resource);

        // Records a change a native function made through its call context, to run at playback in order with the rest.
        // It returns false when it could not apply (its entity died first), which playback counts as skipped.
        void RecordNative(std::function<bool(App&)> change);

        // Plays every recorded change back in order and empties the buffer. A change whose entity died in the meantime
        // (an earlier change in the buffer destroyed it) is skipped with a warning, never applied to a reused slot.
        ApplyResult Apply(App& app, const ComponentRegistry& registry);

        // Forgets every recorded change. Entities created and not yet played back are destroyed, so none are left
        // behind.
        void Discard(EntityList& entities);

        // Forgets the changes recorded after the first `mark` (a Size() taken earlier), as Discard forgets them all:
        // what a stage system that threw recorded, with what the systems before it recorded kept. A mark at or past
        // the end forgets nothing.
        void DiscardFrom(EntityList& entities, std::size_t mark);

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
            InsertResource,
            RemoveResource,
            Native,
        };

        struct Change
        {
            Kind kind;
            bool hasValue;
            std::int32_t component;
            std::uint32_t entity;
            std::size_t valueOffset;
        };

        // Copies `size` bytes of `value` to the end of m_values.
        void AppendValue(const void* value, std::size_t size);

        std::vector<Change> m_changes;
        std::vector<std::byte> m_values; // The bytes of every Add that carried a value, back to back.
        // The changes native functions recorded; a Native change's valueOffset is its index here.
        std::vector<std::function<bool(App&)>> m_native;
    };
} // namespace Fenrir::Detail
