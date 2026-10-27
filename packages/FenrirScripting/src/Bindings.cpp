#include "EngineContext.hpp"
#include "Queries.hpp"

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/WorldTransform.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// The engine side of the built-in extension's native functions, the contract's Native class: plain functions over C
// types, called with the call context of the running Scripting, where pointers and lengths arrive exactly as the C#
// declaration gives them (a C# string arrives as UTF-8 and its length). A C++ exception that escapes one is stopped and
// logged by Iris, so none can unwind into managed code. None runs unless a Scripting is active: C# then gets its
// @notrunning value, or zero, false or null. Each takes the built-in extension's state, the running Scripting's
// EngineContext, after the call context. Each is written once, here: describe-api's natives mode reads them into
// api/FenrirScripting.natives.json, from which their C# declarations and registration are generated.
namespace Fenrir::ScriptingNatives
{
    /**
     * @brief Writes `text` to the engine's logger at `level`: 0 Info, 1 Warn, 2 Error. The text is data, never a
     * format string.
     */
    void Log(const CallContext& context, Detail::EngineContext&, std::int32_t level, const char* text,
             std::int32_t length)
    {
        if (text == nullptr || length < 0)
        {
            return;
        }
        const std::string message(text, static_cast<std::size_t>(length));
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
     * @param value The engine's full entity value, version bits included.
     */
    bool EntityIsValid(const CallContext& context, Detail::EngineContext&, std::uint32_t value)
    {
        return context.Entities().HasEntity(value);
    }

    /**
     * @brief The active scene's entity list: the owner a native Entity holds beside its value. EntityLink's conversion
     * from an Entity stores it, so C++ reads the link as an entity of the active scene, and never hands it to a
     * script.
     */
    void* EntityLinkOwner(const CallContext& context, Detail::EngineContext&)
    {
        return &context.Entities();
    }

    /**
     * @brief The entity's world transform (FenrirECS GetWorldTransform), composed through its parents on each call.
     * @param[out] world The world transform; the default Transform when the entity is not alive or has none.
     * @return False when the entity is not alive or has no Transform.
     */
    bool EntityWorldTransform(const CallContext& context, Detail::EngineContext&, std::uint32_t value, Transform* world)
    {
        if (world == nullptr)
        {
            return false;
        }
        *world = Transform{};
        if (!context.Entities().HasEntity(value))
        {
            return false;
        }
        const Entity entity = context.Entities().GetEntity(value);
        if (!entity.HasComponent<Transform>())
        {
            return false;
        }
        *world = GetWorldTransform(entity);
        return true;
    }

    /**
     * @brief Sets the entity's Transform so its world transform is `world` (FenrirECS SetWorldTransform). Component
     * data only, so it takes effect at once.
     * @return False, touching nothing, when the entity is not alive or has no Transform.
     */
    bool EntitySetWorldTransform(const CallContext& context, Detail::EngineContext&, std::uint32_t value,
                                 const Transform* world)
    {
        if (world == nullptr || !context.Entities().HasEntity(value))
        {
            return false;
        }
        const Entity entity = context.Entities().GetEntity(value);
        if (!entity.HasComponent<Transform>())
        {
            return false;
        }
        SetWorldTransform(entity, *world);
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
    std::int32_t ComponentIdOf(const CallContext&, Detail::EngineContext& engine, const char* name, std::int32_t length,
                               const char* typeName, std::int32_t typeLength)
    {
        if (name == nullptr || length < 0 || typeName == nullptr || typeLength < 0)
        {
            return static_cast<std::int32_t>(RegistryError::NotRegistered);
        }
        return engine.components->Components().Resolve(
            std::string_view(name, static_cast<std::size_t>(length)),
            std::string_view(typeName, static_cast<std::size_t>(typeLength)));
    }

    /**
     * @brief A script assembly reports a component type it defines, while it loads.
     * @return False when nothing is loading.
     */
    bool RegisterScriptComponent(const CallContext&, Detail::EngineContext& engine, const char* name,
                                 std::int32_t length, std::int32_t size, std::int32_t alignment)
    {
        if (engine.reporting == nullptr || name == nullptr || length <= 0 || size <= 0 || alignment <= 0)
        {
            return false;
        }
        engine.reporting->components.push_back({std::string(name, static_cast<std::size_t>(length)),
                                                static_cast<std::size_t>(size), static_cast<std::size_t>(alignment)});
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
    void* ComponentGet(const CallContext& context, Detail::EngineContext& engine, std::int32_t id, std::uint32_t entity)
    {
        const auto* component = engine.components->Components().Get(id);
        return component == nullptr ? nullptr : context.Entities().Get(component->type, entity);
    }

    /** @brief Whether the entity is alive and has the component. */
    bool ComponentHas(const CallContext& context, Detail::EngineContext& engine, std::int32_t id, std::uint32_t entity)
    {
        const auto* component = engine.components->Components().Get(id);
        return component != nullptr && context.Entities().Has(component->type, entity);
    }

    /**
     * @brief A new entity, alive and usable at once; it gets its engine components when changes play back.
     *
     * The structural changes (this, EntityDestroy, ComponentAdd and ComponentRemove) never touch the engine's storage:
     * each is recorded and played back, in order, when the native code that called into the script gets control back,
     * so a Ref<T> taken before stays valid.
     * @return The entity's value (uint.MaxValue when there is no active scene).
     * @notrunning Fenrir::Entity::Null
     */
    std::uint32_t EntityCreate(const CallContext& context, Detail::EngineContext& engine)
    {
        return engine.changes->Create(context.Entities());
    }

    /**
     * @brief Records the entity's destruction, for playback.
     * @return None when recorded, otherwise why it was refused: EntityNotAlive, or NotActive when scripting is not
     * running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError EntityDestroy(const CallContext& context, Detail::EngineContext& engine, std::uint32_t entity)
    {
        return engine.changes->Destroy(context.Entities(), entity);
    }

    /**
     * @brief Records adding the component to the entity, for playback.
     * @param value The component's bytes, or null for a default one.
     * @return None when recorded, otherwise why it was refused: EntityNotAlive, Unknown (no such component), ReadOnly,
     * or NotActive when scripting is not running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError ComponentAdd(const CallContext& context, Detail::EngineContext& engine, std::int32_t id,
                             std::uint32_t entity, const void* value)
    {
        return engine.changes->Add(context.Entities(), *engine.components, id, entity, value);
    }

    /**
     * @brief Records removing the component from the entity, for playback.
     * @return None when recorded, otherwise why it was refused: EntityNotAlive, Unknown (no such component), ReadOnly,
     * Required (the engine needs every entity to have it), or NotActive when scripting is not running.
     * @notrunning Fenrir::ChangeError::NotActive
     */
    ChangeError ComponentRemove(const CallContext& context, Detail::EngineContext& engine, std::int32_t id,
                                std::uint32_t entity)
    {
        return engine.changes->Remove(context.Entities(), *engine.components, id, entity);
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
    std::int32_t EventIdOf(const CallContext&, Detail::EngineContext& engine, const char* name, std::int32_t length,
                           const char* typeName, std::int32_t typeLength)
    {
        if (name == nullptr || length < 0 || typeName == nullptr || typeLength < 0)
        {
            return static_cast<std::int32_t>(RegistryError::NotRegistered);
        }
        return engine.components->Events().Resolve(std::string_view(name, static_cast<std::size_t>(length)),
                                                   std::string_view(typeName, static_cast<std::size_t>(typeLength)));
    }

    /**
     * @brief A script assembly reports an event type it defines, while it loads.
     * @return False when nothing is loading.
     */
    bool RegisterScriptEvent(const CallContext&, Detail::EngineContext& engine, const char* name, std::int32_t length,
                             std::int32_t size, std::int32_t alignment)
    {
        if (engine.reporting == nullptr || name == nullptr || length <= 0 || size <= 0 || alignment <= 0)
        {
            return false;
        }
        engine.reporting->events.push_back({std::string(name, static_cast<std::size_t>(length)),
                                            static_cast<std::size_t>(size), static_cast<std::size_t>(alignment)});
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
        if (seen == nullptr || view == nullptr)
        {
            return false;
        }
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
     * this call and QueryFill however many entities match; a row is the entity followed by the address of its
     * component for each id, in order.
     * @return The count; negative when an id is unknown.
     * @notrunning -1
     */
    std::int32_t QueryCount(const CallContext& context, Detail::EngineContext& engine, const std::int32_t* ids,
                            std::int32_t count)
    {
        if (ids == nullptr || count <= 0)
        {
            return -1;
        }
        ++engine.queryCalls;
        return Detail::CountQueryRows(context.Entities(), *engine.components,
                                      std::span<const std::int32_t>(ids, static_cast<std::size_t>(count)));
    }

    /**
     * @brief Writes up to `capacity` rows of the entities that have every component in `ids` (see QueryCount).
     * @return How many rows it wrote; negative when an id is unknown.
     * @notrunning -1
     */
    std::int32_t QueryFill(const CallContext& context, Detail::EngineContext& engine, const std::int32_t* ids,
                           std::int32_t count, Detail::NativeAddress* rows, std::int32_t capacity)
    {
        if (ids == nullptr || count <= 0 || rows == nullptr || capacity < 0)
        {
            return -1;
        }
        ++engine.queryCalls;
        return Detail::FillQueryRows(context.Entities(), *engine.components,
                                     std::span<const std::int32_t>(ids, static_cast<std::size_t>(count)), rows,
                                     capacity);
    }

    /**
     * @brief The id of the native resource registered under `name` for the C# type `typeName` (its full name), good
     * only for the registry generation it was looked up in.
     * @return The id, or a RegistryError as its value: NotRegistered when nothing is registered under the name,
     * WrongType when it is registered for a different C# type.
     * @notrunning static_cast<std::int32_t>(Fenrir::RegistryError::NotRegistered)
     */
    std::int32_t ResourceIdOf(const CallContext&, Detail::EngineContext& engine, const char* name, std::int32_t length,
                              const char* typeName, std::int32_t typeLength)
    {
        if (name == nullptr || length < 0 || typeName == nullptr || typeLength < 0)
        {
            return static_cast<std::int32_t>(RegistryError::NotRegistered);
        }
        return engine.components->Resources().Resolve(std::string_view(name, static_cast<std::size_t>(length)),
                                                      std::string_view(typeName, static_cast<std::size_t>(typeLength)));
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

namespace Fenrir::Generated
{
    // The built-in extension's components and native functions: src/Generated/FenrirScriptingRegistration.cpp, from
    // api/FenrirScripting.binding.toml.
    void RegisterScripting(ScriptingExtension& extension);
} // namespace Fenrir::Generated

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
