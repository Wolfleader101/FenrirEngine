#include "ChangeBuffer.hpp"

#include <utility>

namespace Fenrir::Detail
{
    std::uint32_t ChangeBuffer::Create(EntityList& entities)
    {
        const std::uint32_t entity = entities.ReserveEntity();
        m_changes.push_back({Kind::Create, false, -1, entity, 0});
        return entity;
    }

    ChangeError ChangeBuffer::Destroy(EntityList& entities, std::uint32_t entity)
    {
        if (!entities.HasEntity(entity))
        {
            return ChangeError::EntityNotAlive;
        }
        m_changes.push_back({Kind::Destroy, false, -1, entity, 0});
        return ChangeError::None;
    }

    ChangeError ChangeBuffer::Add(EntityList& entities, const ComponentRegistry& registry, std::int32_t component,
                                  std::uint32_t entity, const void* value)
    {
        const auto* registration = registry.Get(component);
        if (registration == nullptr)
        {
            return ChangeError::UnknownComponent;
        }
        if (registration->readOnly || !registration->add)
        {
            return ChangeError::ReadOnlyComponent;
        }
        if (!entities.HasEntity(entity))
        {
            return ChangeError::EntityNotAlive;
        }
        const Change change{Kind::Add, value != nullptr && registration->size > 0, component, entity, m_values.size()};
        if (change.hasValue)
        {
            const auto* bytes = static_cast<const std::byte*>(value);
            m_values.insert(m_values.end(), bytes, bytes + registration->size);
        }
        m_changes.push_back(change);
        return ChangeError::None;
    }

    ChangeError ChangeBuffer::SendEvent(App& app, const ComponentRegistry& registry, std::int32_t event, const void* value)
    {
        const auto* registration = registry.GetEvent(event);
        if (registration == nullptr || value == nullptr)
        {
            return ChangeError::UnknownEvent;
        }
        const IEventQueue* queue = app.FindEventQueue(registration->name);
        if (queue == nullptr || queue->EventSize() != registration->size)
        {
            return ChangeError::UnknownEvent;
        }
        m_changes.push_back({Kind::SendEvent, true, event, 0, m_values.size()});
        const auto* bytes = static_cast<const std::byte*>(value);
        m_values.insert(m_values.end(), bytes, bytes + registration->size);
        return ChangeError::None;
    }

    ChangeError ChangeBuffer::Remove(EntityList& entities, const ComponentRegistry& registry, std::int32_t component,
                                     std::uint32_t entity)
    {
        const auto* registration = registry.Get(component);
        if (registration == nullptr)
        {
            return ChangeError::UnknownComponent;
        }
        if (registration->readOnly || !registration->remove)
        {
            return ChangeError::ReadOnlyComponent;
        }
        if (registration->required)
        {
            return ChangeError::RequiredComponent;
        }
        if (!entities.HasEntity(entity))
        {
            return ChangeError::EntityNotAlive;
        }
        m_changes.push_back({Kind::Remove, false, component, entity, 0});
        return ChangeError::None;
    }

    ApplyResult ChangeBuffer::Apply(App& app, const ComponentRegistry& registry)
    {
        // Taken first, so the buffer is empty whatever happens below.
        const std::vector<Change> changes = std::move(m_changes);
        const std::vector<std::byte> values = std::move(m_values);
        m_changes.clear();
        m_values.clear();

        EntityList& entities = app.GetActiveScene().GetEntityList();
        ApplyResult result;
        const auto skip = [&](const char* what, std::uint32_t entity) {
            ++result.skipped;
            app.Logger()->Warn("Scripting: skipped {} for entity {}: it no longer exists.", what, entity);
        };

        for (const Change& change : changes)
        {
            switch (change.kind)
            {
            case Kind::Create:
                if (entities.CompleteEntity(change.entity))
                {
                    ++result.applied;
                }
                else
                {
                    skip("creating", change.entity);
                }
                break;
            case Kind::Destroy:
                // Destroying one twice is the same as once.
                entities.DestroyEntity(change.entity);
                ++result.applied;
                break;
            case Kind::Add:
            {
                const auto* registration = registry.Get(change.component);
                if (registration == nullptr || !entities.HasEntity(change.entity) ||
                    !registration->add(entities, change.entity))
                {
                    skip("adding a component", change.entity);
                    break;
                }
                if (change.hasValue)
                {
                    if (void* target = registration->get(entities, change.entity); target != nullptr)
                    {
                        std::memcpy(target, values.data() + change.valueOffset, registration->size);
                    }
                }
                ++result.applied;
                break;
            }
            case Kind::SendEvent:
            {
                const auto* registration = registry.GetEvent(change.component);
                IEventQueue* queue = registration == nullptr ? nullptr : app.FindEventQueue(registration->name);
                if (queue == nullptr || queue->EventSize() != registration->size || !queue->SendRaw(values.data() + change.valueOffset))
                {
                    ++result.skipped;
                    app.Logger()->Warn("Scripting: skipped sending an event: its queue no longer exists.");
                    break;
                }
                ++result.applied;
                break;
            }
            case Kind::Remove:
            {
                const auto* registration = registry.Get(change.component);
                if (registration == nullptr || !entities.HasEntity(change.entity) ||
                    !registration->remove(entities, change.entity))
                {
                    skip("removing a component", change.entity);
                    break;
                }
                ++result.applied;
                break;
            }
            }
        }
        return result;
    }

    void ChangeBuffer::Discard(EntityList& entities)
    {
        for (const Change& change : m_changes)
        {
            if (change.kind == Kind::Create)
            {
                entities.DestroyEntity(change.entity);
            }
        }
        m_changes.clear();
        m_values.clear();
    }
} // namespace Fenrir::Detail
