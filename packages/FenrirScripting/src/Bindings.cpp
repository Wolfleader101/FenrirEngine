#include "EngineContext.hpp"
#include "Generated/FenrirScriptingNatives.hpp"
#include "Queries.hpp"

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScripting/NativeFunctionMake.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

// The engine side of the built-in extension's native functions, the contract's Native class: plain functions over C
// types, called with the call context of the running Scripting. The declaration is the contract (ADR-0045): an `in` or
// `out` pointer is never null, and a native's single output is zeroed by its registration, so no native checks or
// zeroes one; text arrives as std::string_view (ADR-0044); a span arrives as its first element and a count, read
// through SpanOf; an entity arrives as C#'s Entity, the mirror of Fenrir::Entity, and is resolved in the active scene
// by NativeGuard's owner rule (ValueOf): one another scene's list owns is never alive here, and one with no owner, as a
// test names by id, is looked up by value. A code a native returns is ChangeBuffer's or the registry's rule, not this
// file's. A C++ exception that escapes one is stopped and logged by Iris, so none can unwind into managed code. None
// runs unless a Scripting is active: C# then gets its @notrunning value, or zero, false or null. Each takes the
// built-in extension's state, the running Scripting's EngineContext, after the call context. Each is written once,
// here: describe-api's natives mode reads them into api/FenrirScripting.natives.json, from which their C# declarations
// and registration are generated.
namespace Fenrir::ScriptingNatives
{
    /**
     * @brief Writes `text` to the engine's logger at `level`: 0 Info, 1 Warn, 2 Error. The text is data, never a
     * format string.
     */
    void Log(const CallContext& context, Detail::EngineContext&, std::int32_t level, std::string_view text)
    {
        const std::string message(text);
        auto& logger = *context.GetApp().Logger();
        switch (level)
        {
        case 1:
            logger.Warn("{}", message);
            break;
        case 2:
            logger.Error("{}", message);
            break;
        default:
            logger.Info("{}", message);
            break;
        }
    }

    /** @brief The length of the frame being run, in seconds. */
    float TimeDeltaTime(const CallContext& context, Detail::EngineContext&)
    {
        return static_cast<float>(context.GetApp().GetTime().deltaTime);
    }

    /** @brief The fixed tick's length, in seconds. */
    float TimeTickRate(const CallContext& context, Detail::EngineContext&)
    {
        return static_cast<float>(context.GetApp().GetTime().tickRate);
    }

    /**
     * @brief Whether the entity is alive in the active scene.
     * @param entity The entity; one another scene's list owns is not alive here.
     */
    bool EntityIsValid(const CallContext& context, Detail::EngineContext&, const Entity* entity)
    {
        EntityList& entities = context.Entities();
        return entities.HasEntity(NativeGuard::ValueOf(*entity, entities));
    }

    /**
     * @brief The entity's world transform (FenrirECS GetWorldTransform), composed through its parents on each call.
     * @param[out] world The world transform; the default Transform when the entity is not alive or has none.
     * @return False when the entity is not alive or has no Transform.
     * @facade Entity.TryGetWorldTransform
     */
    bool EntityWorldTransform(const CallContext& context, Detail::EngineContext&, const Entity* entity,
                              Transform* world)
    {
        EntityList& entities = context.Entities();
        const std::uint32_t value = NativeGuard::ValueOf(*entity, entities);
        if (!entities.HasEntity(value))
        {
            return false;
        }
        const Entity alive = entities.GetEntity(value);
        if (!alive.HasComponent<Transform>())
        {
            return false;
        }
        *world = GetWorldTransform(alive);
        return true;
    }

    /**
     * @brief Sets the entity's Transform so its world transform is `world` (FenrirECS SetWorldTransform). Component
     * data only, so it takes effect at once.
     * @return False, touching nothing, when the entity is not alive or has no Transform.
     * @facade Entity.TrySetWorldTransform
     */
    bool EntitySetWorldTransform(const CallContext& context, Detail::EngineContext&, const Entity* entity,
                                 const Transform* world)
    {
        EntityList& entities = context.Entities();
        const std::uint32_t value = NativeGuard::ValueOf(*entity, entities);
        if (!entities.HasEntity(value))
        {
            return false;
        }
        const Entity alive = entities.GetEntity(value);
        if (!alive.HasComponent<Transform>())
        {
            return false;
        }
        SetWorldTransform(alive, *world);
        return true;
    }

    /**
     * @brief The id of the native component registered under `name` for the C# type `typeName` (its full name). An id
     * is good only for the registry generation it was looked up in: a new Scripting has a new registry.
     * @return The id, or a RegistryError as its value: NotRegistered when nothing is registered under the name,
     * WrongType when it is registered for a different C# type (so a script cannot claim a native component with a
     * struct of its own).
     * @notrunning static_cast<std::int32_t>(Fenrir::RegistryError::NotRegistered)
     */
    std::int32_t ComponentIdOf(const CallContext&, Detail::EngineContext& engine, std::string_view name,
                               std::string_view typeName)
    {
        return engine.components->Components().Resolve(name, typeName);
    }

    /**
     * @brief A script assembly reports a component type it defines, while it loads.
     * @return False when nothing is loading.
     */
    bool RegisterScriptComponent(const CallContext&, Detail::EngineContext& engine, std::string_view name,
                                 std::int32_t size, std::int32_t alignment)
    {
        if (engine.reporting == nullptr || name.empty() || size <= 0 || alignment <= 0)
        {
            return false;
        }
        engine.reporting->components.push_back(
            {std::string(name), static_cast<std::size_t>(size), static_cast<std::size_t>(alignment)});
        return true;
    }

    /** @brief Which registry ids belong to: it changes every time a registry is created. */
    std::uint32_t RegistryGeneration(const CallContext&, Detail::EngineContext& engine)
    {
        return engine.components->Generation();
    }

    /**
     * @brief A pointer to the entity's component in the engine's storage, good until the engine next changes its
     * storage.
     * @return Null when the entity is not alive or does not have it.
     */
    void* ComponentGet(const CallContext& context, Detail::EngineContext& engine, std::int32_t id, const Entity* entity)
    {
        const auto* component = engine.components->Components().Get(id);
        EntityList& entities = context.Entities();
        return component == nullptr ? nullptr : entities.Get(component->type, NativeGuard::ValueOf(*entity, entities));
    }

    /** @brief Whether the entity is alive and has the component. */
    bool ComponentHas(const CallContext& context, Detail::EngineContext& engine, std::int32_t id, const Entity* entity)
    {
        const auto* component = engine.components->Components().Get(id);
        EntityList& entities = context.Entities();
        return component != nullptr && entities.Has(component->type, NativeGuard::ValueOf(*entity, entities));
    }

    /**
     * @brief A new entity, alive and usable at once; it gets its engine components when changes play back.
     *
     * The structural changes (this, EntityDestroy, ComponentAdd and ComponentRemove) never touch the engine's storage:
     * each is recorded and played back, in order, when the native code that called into the script gets control back,
     * so a Ref<T> taken before stays valid.
     * @param[out] entity The new entity, owned by the active scene's list; no entity (the default Entity, C#'s
     * Entity.None) when none could be made.
     * @return Whether one was made: false when there is no active scene.
     * @facade Entity.TryCreate
     */
    bool EntityCreate(const CallContext& context, Detail::EngineContext& engine, Entity* entity)
    {
        EntityList& entities = context.Entities();
        const std::uint32_t value = engine.changes->Create(entities);
        if (value == Entity::Null)
        {
            return false;
        }
        *entity = entities.HandleOf(value);
        return true;
    }

    /**
     * @brief Records the entity's destruction, for playback.
     * @return None when recorded, otherwise why it was refused: EntityNotAlive, or NotActive when scripting is not
     * running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError EntityDestroy(const CallContext& context, Detail::EngineContext& engine, const Entity* entity)
    {
        EntityList& entities = context.Entities();
        return engine.changes->Destroy(entities, NativeGuard::ValueOf(*entity, entities));
    }

    /**
     * @brief Records adding the component to the entity, for playback.
     * @param value The component's bytes, or null for a default one.
     * @return None when recorded, otherwise why it was refused: EntityNotAlive, Unknown (no such component), ReadOnly,
     * or NotActive when scripting is not running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError ComponentAdd(const CallContext& context, Detail::EngineContext& engine, std::int32_t id,
                             const Entity* entity, const void* value)
    {
        EntityList& entities = context.Entities();
        return engine.changes->Add(entities, *engine.components, id, NativeGuard::ValueOf(*entity, entities), value);
    }

    /**
     * @brief Records removing the component from the entity, for playback.
     * @return None when recorded, otherwise why it was refused: EntityNotAlive, Unknown (no such component), ReadOnly,
     * Required (the engine needs every entity to have it), or NotActive when scripting is not running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError ComponentRemove(const CallContext& context, Detail::EngineContext& engine, std::int32_t id,
                                const Entity* entity)
    {
        EntityList& entities = context.Entities();
        return engine.changes->Remove(entities, *engine.components, id, NativeGuard::ValueOf(*entity, entities));
    }

    /**
     * @brief Plays back the structural changes recorded since the last time. The system runner calls it after each
     * system.
     * @return How many changes were applied.
     */
    std::int32_t ChangesApply(const CallContext& context, Detail::EngineContext& engine)
    {
        return static_cast<std::int32_t>(engine.changes->Apply(context.GetApp(), *engine.components).applied);
    }

    /** @brief Drops the structural changes recorded since the last time, and destroys the entities they created. */
    void ChangesDiscard(const CallContext& context, Detail::EngineContext& engine)
    {
        engine.changes->Discard(context.Entities());
    }

    /**
     * @brief The id of the event registered under `name` for the C# type `typeName`. Each event is a queue the engine
     * owns: the queue C++ SendEvent and EventReader use for a native event.
     * @return The id, or a RegistryError as its value: NotRegistered when there is none, WrongType when it is
     * registered for a different C# type.
     * @notrunning static_cast<std::int32_t>(Fenrir::RegistryError::NotRegistered)
     */
    std::int32_t EventIdOf(const CallContext&, Detail::EngineContext& engine, std::string_view name,
                           std::string_view typeName)
    {
        return engine.components->Events().Resolve(name, typeName);
    }

    /**
     * @brief A script assembly reports an event type it defines, while it loads.
     * @return False when nothing is loading.
     */
    bool RegisterScriptEvent(const CallContext&, Detail::EngineContext& engine, std::string_view name,
                             std::int32_t size, std::int32_t alignment)
    {
        if (engine.reporting == nullptr || name.empty() || size <= 0 || alignment <= 0)
        {
            return false;
        }
        engine.reporting->events.push_back(
            {std::string(name), static_cast<std::size_t>(size), static_cast<std::size_t>(alignment)});
        return true;
    }

    /**
     * @brief Records an event to send when the system returns (see ChangeBuffer::SendEvent).
     * @return None when recorded, otherwise why it was refused: Unknown (the event has no queue), or NotActive when
     * scripting is not running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError EventSend(const CallContext& context, Detail::EngineContext& engine, std::int32_t event,
                          const void* value)
    {
        return engine.changes->SendEvent(context.GetApp(), *engine.components, event, value);
    }

    /**
     * @brief One read of a C# EventReader. Readers share no state: each keeps its own cursor in C#. The addresses are
     * good until something is sent to the queue or it swaps.
     * @param[in,out] seen The reader's cursor: how many of the queue's events it has gone past (all ones, the engine's
     * NewReaderCursor, before its first read), moved past what the read yields.
     * @param[in,out] view Five values written: the older part's address and count, the newer part's address and count,
     * and how many events the reader missed.
     * @return False when there is no such event.
     * @notrunning false
     */
    bool EventReadAfter(const CallContext& context, Detail::EngineContext& engine, std::int32_t event,
                        std::uint64_t* seen, Detail::NativeAddress* view)
    {
        const auto* registration = engine.components->Events().Get(event);
        const IEventQueue* queue =
            registration == nullptr ? nullptr : context.GetApp().FindEventQueue(registration->name);
        if (queue == nullptr || queue->EventSize() != registration->size)
        {
            return false;
        }
        auto cursor = static_cast<std::size_t>(*seen);
        const RawEventRange events = queue->ReadRawAfter(cursor);
        *seen = cursor;
        view[0] = reinterpret_cast<Detail::NativeAddress>(events.older);
        view[1] = events.olderCount;
        view[2] = reinterpret_cast<Detail::NativeAddress>(events.newer);
        view[3] = events.newerCount;
        view[4] = events.missed;
        return true;
    }

    /**
     * @brief How many entities have every component in `ids`: an upper bound on the rows QueryFill writes. A query is
     * this call and QueryFill however many entities match; a row is the entity (two words: a Fenrir::Entity, with its
     * scene's list set) followed by the address of its component for each id, in order.
     * @return The count; negative when an id is unknown.
     * @notrunning -1
     */
    std::int32_t QueryCount(const CallContext& context, Detail::EngineContext& engine, const std::int32_t* ids,
                            std::int32_t count)
    {
        const std::span<const std::int32_t> idList = SpanOf(ids, count);
        if (idList.empty())
        {
            return -1;
        }
        ++engine.queryCalls;
        return Detail::CountQueryRows(context.Entities(), *engine.components, idList);
    }

    /**
     * @brief Writes up to `capacity` rows of the entities that have every component in `ids` (see QueryCount).
     * @return How many rows it wrote; negative when an id is unknown.
     * @notrunning -1
     */
    std::int32_t QueryFill(const CallContext& context, Detail::EngineContext& engine, const std::int32_t* ids,
                           std::int32_t count, Detail::NativeAddress* rows, std::int32_t capacity)
    {
        const std::span<const std::int32_t> idList = SpanOf(ids, count);
        const std::span<Detail::NativeAddress> rowList = SpanOf(rows, capacity);
        if (idList.empty())
        {
            return -1;
        }
        ++engine.queryCalls;
        return Detail::FillQueryRows(context.Entities(), *engine.components, idList, rowList.data(),
                                     static_cast<std::int32_t>(rowList.size()));
    }

    /**
     * @brief The id of the native resource registered under `name` for the C# type `typeName` (its full name), good
     * only for the registry generation it was looked up in.
     * @return The id, or a RegistryError as its value: NotRegistered when nothing is registered under the name,
     * WrongType when it is registered for a different C# type.
     * @notrunning static_cast<std::int32_t>(Fenrir::RegistryError::NotRegistered)
     */
    std::int32_t ResourceIdOf(const CallContext&, Detail::EngineContext& engine, std::string_view name,
                              std::string_view typeName)
    {
        return engine.components->Resources().Resolve(name, typeName);
    }

    /**
     * @brief A pointer to the resource in the engine's storage (the active scene's, for a scene resource), good until
     * the engine next changes its storage.
     * @return Null when there is none now.
     */
    void* ResourceGet(const CallContext& context, Detail::EngineContext& engine, std::int32_t id)
    {
        const auto* resource = engine.components->Resources().Get(id);
        return resource == nullptr ? nullptr : resource->find(context.GetApp());
    }

    /**
     * @brief Records inserting the resource, replacing the one there is, for playback.
     * @param value The resource's bytes, or null for a default one.
     * @return None when recorded, otherwise why it was refused: Unknown (no such resource), ReadOnly, or NotActive
     * when scripting is not running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError ResourceInsert(const CallContext&, Detail::EngineContext& engine, std::int32_t id, const void* value)
    {
        return engine.changes->InsertResource(*engine.components, id, value);
    }

    /**
     * @brief Records removing the resource, for playback.
     * @return None when recorded, otherwise why it was refused: Unknown (no such resource), ReadOnly, or NotActive
     * when scripting is not running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError ResourceRemove(const CallContext&, Detail::EngineContext& engine, std::int32_t id)
    {
        return engine.changes->RemoveResource(*engine.components, id);
    }
} // namespace Fenrir::ScriptingNatives

namespace Fenrir::Detail
{
    namespace
    {
        std::atomic<EngineContext*> activeContext{nullptr};
    } // namespace

    ActiveScripting ActiveCallContext() noexcept
    {
        const auto* context = activeContext.load();
        return context == nullptr || context->app == nullptr ? ActiveScripting{}
                                                             : ActiveScripting{context->app, context->changes};
    }

    bool ActivateEngineContext(EngineContext* context) noexcept
    {
        EngineContext* none = nullptr;
        return activeContext.compare_exchange_strong(none, context);
    }

    void DeactivateEngineContext(EngineContext* context) noexcept
    {
        EngineContext* expected = context;
        activeContext.compare_exchange_strong(expected, nullptr);
    }

    ScriptingExtension BuiltInExtension(const std::filesystem::path& math, const std::filesystem::path& contract,
                                        std::shared_ptr<EngineContext> engine)
    {
        ScriptingExtension extension;
        extension.name = "built-in";
        Generated::RegisterScripting(extension);
        // Every entity the engine makes has all three engine components, and the renderer and hierarchy rely on it:
        // scripts cannot remove them.
        for (auto& component : extension.components)
        {
            component.required = true;
        }
        extension.SetState(std::move(engine));
        // Fenrir.Math first: the scripting contract and every package's contract may refer to it.
        extension.sharedAssemblies = {math, contract};
        return extension;
    }
} // namespace Fenrir::Detail

namespace Fenrir
{
    ChangeBuffer CallContext::Changes() const noexcept
    {
        return ChangeBuffer(*m_app, *m_changes);
    }

    std::uint32_t ChangeBuffer::CreateEntity() const
    {
        return m_changes->Create(m_app->GetActiveScene().GetEntityList());
    }

    bool ChangeBuffer::DestroyEntity(std::uint32_t entity) const
    {
        return m_changes->Destroy(m_app->GetActiveScene().GetEntityList(), entity) == ChangeError::None;
    }

    bool ChangeBuffer::RecordOn(std::uint32_t entity, std::function<void(Entity&)> change) const
    {
        if (!m_app->GetActiveScene().GetEntityList().HasEntity(entity))
        {
            return false;
        }
        m_changes->RecordNative([entity, apply = std::move(change)](App& app) {
            EntityList& entities = app.GetActiveScene().GetEntityList();
            if (!entities.HasEntity(entity))
            {
                return false;
            }
            Entity target = entities.GetEntity(entity);
            apply(target);
            return true;
        });
        return true;
    }

    bool ChangeBuffer::AddChild(std::uint32_t parent, std::uint32_t child) const
    {
        EntityList& entities = m_app->GetActiveScene().GetEntityList();
        if (!entities.HasEntity(parent) || !entities.HasEntity(child))
        {
            return false;
        }
        m_changes->RecordNative([parent, child](App& app) {
            EntityList& live = app.GetActiveScene().GetEntityList();
            if (!live.HasEntity(parent) || !live.HasEntity(child))
            {
                return false;
            }
            live.GetEntity(parent).AddChild(live.GetEntity(child));
            return true;
        });
        return true;
    }

    void ChangeBuffer::Record(std::function<void(App&)> change) const
    {
        m_changes->RecordNative([apply = std::move(change)](App& app) {
            apply(app);
            return true;
        });
    }
} // namespace Fenrir
