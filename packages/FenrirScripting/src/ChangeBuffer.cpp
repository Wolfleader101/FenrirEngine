#include "ChangeBuffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <utility>

namespace Fenrir::Detail
{
    void ChangeBuffer::AppendValue(const void* value, std::size_t size)
    {
        if (size == 0)
        {
            return;
        }
        const std::size_t start = m_values.size();
        m_values.resize(start + size);
        std::memcpy(&m_values[start], value, size);
    }

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
        const auto* registration = registry.Components().Get(component);
        if (registration == nullptr)
        {
            return ChangeError::Unknown;
        }
        if (registration->readOnly)
        {
            return ChangeError::ReadOnly;
        }
        if (!entities.HasEntity(entity))
        {
            return ChangeError::EntityNotAlive;
        }
        const std::size_t size = registration->type.Size();
        const Change change{Kind::Add, value != nullptr && size > 0, component, entity, m_values.size()};
        if (change.hasValue)
        {
            AppendValue(value, size);
        }
        m_changes.push_back(change);
        return ChangeError::None;
    }

    ChangeError ChangeBuffer::SendEvent(App& app, const ComponentRegistry& registry, std::int32_t event,
                                        const void* value)
    {
        const auto* registration = registry.Events().Get(event);
        if (registration == nullptr || value == nullptr)
        {
            return ChangeError::Unknown;
        }
        const IEventQueue* queue = app.FindEventQueue(registration->name);
        if (queue == nullptr || queue->EventSize() != registration->size)
        {
            return ChangeError::Unknown;
        }
        m_changes.push_back({Kind::SendEvent, true, event, 0, m_values.size()});
        AppendValue(value, registration->size);
        return ChangeError::None;
    }

    ChangeError ChangeBuffer::Remove(EntityList& entities, const ComponentRegistry& registry, std::int32_t component,
                                     std::uint32_t entity)
    {
        const auto* registration = registry.Components().Get(component);
        if (registration == nullptr)
        {
            return ChangeError::Unknown;
        }
        if (registration->readOnly)
        {
            return ChangeError::ReadOnly;
        }
        if (registration->required)
        {
            return ChangeError::Required;
        }
        if (!entities.HasEntity(entity))
        {
            return ChangeError::EntityNotAlive;
        }
        m_changes.push_back({Kind::Remove, false, component, entity, 0});
        return ChangeError::None;
    }

    ChangeError ChangeBuffer::InsertResource(const ComponentRegistry& registry, std::int32_t resource,
                                             const void* value)
    {
        const auto* registration = registry.Resources().Get(resource);
        if (registration == nullptr)
        {
            return ChangeError::Unknown;
        }
        if (registration->readOnly)
        {
            return ChangeError::ReadOnly;
        }
        const Change change{Kind::InsertResource, value != nullptr && registration->size > 0, resource, 0,
                            m_values.size()};
        if (change.hasValue)
        {
            AppendValue(value, registration->size);
        }
        m_changes.push_back(change);
        return ChangeError::None;
    }

    ChangeError ChangeBuffer::RemoveResource(const ComponentRegistry& registry, std::int32_t resource)
    {
        const auto* registration = registry.Resources().Get(resource);
        if (registration == nullptr)
        {
            return ChangeError::Unknown;
        }
        if (registration->readOnly)
        {
            return ChangeError::ReadOnly;
        }
        m_changes.push_back({Kind::RemoveResource, false, resource, 0, 0});
        return ChangeError::None;
    }

    void ChangeBuffer::RecordNative(std::function<bool(App&)> change)
    {
        m_changes.push_back({Kind::Native, false, -1, 0, m_native.size()});
        m_native.push_back(std::move(change));
    }

    ApplyResult ChangeBuffer::Apply(App& app, const ComponentRegistry& registry)
    {
        // Taken first, so the buffer is empty whatever happens below, and a change played back may record more.
        const std::vector<Change> changes = std::move(m_changes);
        const std::vector<std::byte> values = std::move(m_values);
        const std::vector<std::function<bool(App&)>> native = std::move(m_native);
        m_changes.clear();
        m_values.clear();
        m_native.clear();

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
            case Kind::Add: {
                const auto* registration = registry.Components().Get(change.component);
                if (registration == nullptr || !entities.HasEntity(change.entity) ||
                    !entities.Add(registration->type, change.entity))
                {
                    skip("adding a component", change.entity);
                    break;
                }
                if (change.hasValue)
                {
                    if (void* target = entities.Get(registration->type, change.entity); target != nullptr)
                    {
                        std::memcpy(target, &values[change.valueOffset], registration->type.Size());
                    }
                }
                ++result.applied;
                break;
            }
            case Kind::SendEvent: {
                const auto* registration = registry.Events().Get(change.component);
                IEventQueue* queue = registration == nullptr ? nullptr : app.FindEventQueue(registration->name);
                if (queue == nullptr || queue->EventSize() != registration->size ||
                    !queue->SendRaw(&values[change.valueOffset]))
                {
                    ++result.skipped;
                    app.Logger()->Warn("Scripting: skipped sending an event: its queue no longer exists.");
                    break;
                }
                ++result.applied;
                break;
            }
            case Kind::InsertResource: {
                const auto* registration = registry.Resources().Get(change.component);
                void* target = registration == nullptr ? nullptr : registration->insert(app);
                if (target == nullptr)
                {
                    ++result.skipped;
                    app.Logger()->Warn("Scripting: skipped inserting a resource: it is no longer registered.");
                    break;
                }
                if (change.hasValue)
                {
                    std::memcpy(target, &values[change.valueOffset], registration->size);
                }
                ++result.applied;
                break;
            }
            case Kind::RemoveResource: {
                const auto* registration = registry.Resources().Get(change.component);
                if (registration == nullptr)
                {
                    ++result.skipped;
                    app.Logger()->Warn("Scripting: skipped removing a resource: it is no longer registered.");
                    break;
                }
                registration->remove(app);
                ++result.applied;
                break;
            }
            case Kind::Native:
                if (native[change.valueOffset](app))
                {
                    ++result.applied;
                }
                else
                {
                    ++result.skipped;
                    app.Logger()->Warn(
                        "Scripting: skipped a change a native function recorded: its entity no longer exists.");
                }
                break;
            case Kind::Remove: {
                const auto* registration = registry.Components().Get(change.component);
                if (registration == nullptr || !entities.HasEntity(change.entity) ||
                    !entities.Remove(registration->type, change.entity))
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
        m_native.clear();
    }

    void ChangeBuffer::DiscardFrom(EntityList& entities, std::size_t mark)
    {
        if (mark >= m_changes.size())
        {
            return;
        }
        // The bytes and native changes the forgotten ones own are at the end of their vectors, after the kept ones'.
        std::size_t values = m_values.size();
        std::size_t native = m_native.size();
        for (std::size_t index = mark; index < m_changes.size(); ++index)
        {
            const Change& change = m_changes[index];
            if (change.kind == Kind::Create)
            {
                entities.DestroyEntity(change.entity);
            }
            else if (change.kind == Kind::Native)
            {
                native = std::min(native, change.valueOffset);
            }
            else if (change.hasValue)
            {
                values = std::min(values, change.valueOffset);
            }
        }
        m_changes.resize(mark);
        m_values.resize(values);
        m_native.erase(m_native.begin() + static_cast<std::ptrdiff_t>(native), m_native.end());
    }
} // namespace Fenrir::Detail
