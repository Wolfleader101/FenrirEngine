#include "EngineContext.hpp"
#include "Queries.hpp"

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/WorldTransform.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Fenrir::Detail
{
    namespace
    {
        std::atomic<EngineContext*> activeContext{nullptr};

        // The engine side of one native function of the contract's Native class: a plain function over C types, called
        // with the call context of the running Scripting, where pointers and lengths arrive exactly as the C#
        // declaration gives them (the friendly C# wrapper has already done the UTF-8). A C++ exception that escapes one
        // is stopped and logged by Iris, so none can unwind into managed code. None runs unless a Scripting is active.
        EngineContext& Engine(const CallContext& context) noexcept
        {
            return CallContextAccess::Engine(context);
        }

        // Log: level 0 Info, 1 Warn, 2 Error. The text is data, never a format string.
        void Log(const CallContext& context, std::int32_t level, const char* text, std::int32_t length)
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

        float TimeDeltaTime(const CallContext& context)
        {
            return static_cast<float>(context.GetApp().GetTime().deltaTime);
        }

        float TimeTickRate(const CallContext& context)
        {
            return static_cast<float>(context.GetApp().GetTime().tickRate);
        }

        bool EntityIsValid(const CallContext& context, std::uint32_t value)
        {
            return context.Entities().HasEntity(value);
        }

        // EntityWorldTransform: writes the entity's world transform (FenrirECS GetWorldTransform) to `world`; false,
        // writing nothing, if the entity is not alive or has no Transform.
        bool EntityWorldTransform(const CallContext& context, std::uint32_t value, Transform* world)
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
            *world = GetWorldTransform(entity);
            return true;
        }

        // EntitySetWorldTransform: sets the entity's Transform so its world transform is `world` (FenrirECS
        // SetWorldTransform). Component data only, so it takes effect at once. False if the entity is not alive or has
        // no Transform.
        bool EntitySetWorldTransform(const CallContext& context, std::uint32_t value, const Transform* world)
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

        // ComponentIdOf: the id of the component registered under a UTF-8 name for the C# type with the given full
        // name; -1 if nothing is registered under the name, -3 if it is registered for a different C# type.
        std::int32_t ComponentIdOf(const CallContext& context, const char* name, std::int32_t length,
                                   const char* typeName, std::int32_t typeLength)
        {
            if (name == nullptr || length < 0 || typeName == nullptr || typeLength < 0)
            {
                return -1;
            }
            return Engine(context).components->Resolve(
                std::string_view(name, static_cast<std::size_t>(length)),
                std::string_view(typeName, static_cast<std::size_t>(typeLength)));
        }

        // RegisterScriptComponent: a script assembly reports a component type it defines, while it loads. False if
        // nothing is loading.
        bool RegisterScriptComponent(const CallContext& context, const char* name, std::int32_t length,
                                     std::int32_t size, std::int32_t alignment)
        {
            auto& engine = Engine(context);
            if (engine.discovering == nullptr || name == nullptr || length <= 0 || size <= 0 || alignment <= 0)
            {
                return false;
            }
            engine.discovering->push_back({std::string(name, static_cast<std::size_t>(length)),
                                           static_cast<std::size_t>(size), static_cast<std::size_t>(alignment)});
            return true;
        }

        // RegistryGeneration: which registry ids belong to.
        std::uint32_t RegistryGeneration(const CallContext& context)
        {
            return Engine(context).components->Generation();
        }

        // ComponentGet: the entity's component in the engine's storage, or null.
        void* ComponentGet(const CallContext& context, std::int32_t id, std::uint32_t entity)
        {
            const auto* component = Engine(context).components->Get(id);
            return component == nullptr ? nullptr : context.Entities().Get(component->type, entity);
        }

        // ComponentHas: whether the entity is alive and has the component.
        bool ComponentHas(const CallContext& context, std::int32_t id, std::uint32_t entity)
        {
            const auto* component = Engine(context).components->Get(id);
            return component != nullptr && context.Entities().Has(component->type, entity);
        }

        // EntityCreate: a new entity id, usable at once; the entity gets its engine components when changes play back.
        std::uint32_t EntityCreate(const CallContext& context)
        {
            return Engine(context).changes->Create(context.Entities());
        }

        // EntityDestroy, ComponentAdd and ComponentRemove record a change for playback. They return a ChangeError code:
        // 0 recorded, negative for the reason it was refused.
        std::int32_t EntityDestroy(const CallContext& context, std::uint32_t entity)
        {
            return static_cast<std::int32_t>(Engine(context).changes->Destroy(context.Entities(), entity));
        }

        // `value` is the component's bytes, or null for a default one.
        std::int32_t ComponentAdd(const CallContext& context, std::int32_t id, std::uint32_t entity, const void* value)
        {
            auto& engine = Engine(context);
            return static_cast<std::int32_t>(
                engine.changes->Add(context.Entities(), *engine.components, id, entity, value));
        }

        std::int32_t ComponentRemove(const CallContext& context, std::int32_t id, std::uint32_t entity)
        {
            auto& engine = Engine(context);
            return static_cast<std::int32_t>(
                engine.changes->Remove(context.Entities(), *engine.components, id, entity));
        }

        // ChangesApply: plays back the recorded changes, returning how many were applied.
        std::int32_t ChangesApply(const CallContext& context)
        {
            auto& engine = Engine(context);
            return static_cast<std::int32_t>(engine.changes->Apply(context.GetApp(), *engine.components).applied);
        }

        // ChangesDiscard: drops the recorded changes (and destroys the entities they created).
        void ChangesDiscard(const CallContext& context)
        {
            Engine(context).changes->Discard(context.Entities());
        }

        // EventIdOf: the id of the event registered under a UTF-8 name for the C# type with the given full name; -1 if
        // there is none, -3 if it is registered for a different C# type.
        std::int32_t EventIdOf(const CallContext& context, const char* name, std::int32_t length, const char* typeName,
                               std::int32_t typeLength)
        {
            if (name == nullptr || length < 0 || typeName == nullptr || typeLength < 0)
            {
                return -1;
            }
            return Engine(context).components->ResolveEvent(
                std::string_view(name, static_cast<std::size_t>(length)),
                std::string_view(typeName, static_cast<std::size_t>(typeLength)));
        }

        // RegisterScriptEvent: a script assembly reports an event type it defines, while it loads.
        bool RegisterScriptEvent(const CallContext& context, const char* name, std::int32_t length, std::int32_t size,
                                 std::int32_t alignment)
        {
            auto& engine = Engine(context);
            if (engine.discoveringEvents == nullptr || name == nullptr || length <= 0 || size <= 0 || alignment <= 0)
            {
                return false;
            }
            engine.discoveringEvents->push_back({std::string(name, static_cast<std::size_t>(length)),
                                                 static_cast<std::size_t>(size), static_cast<std::size_t>(alignment)});
            return true;
        }

        // EventSend: records an event to send when the script returns (see ChangeBuffer::SendEvent).
        std::int32_t EventSend(const CallContext& context, std::int32_t event, const void* value)
        {
            auto& engine = Engine(context);
            return static_cast<std::int32_t>(
                engine.changes->SendEvent(context.GetApp(), *engine.components, event, value));
        }

        // EventReadAfter: one read of a C# EventReader. `seen` is the reader's cursor, kept in C#, so readers share no
        // state. Fills `view` with the older part's address and count, the newer part's, and how many the reader
        // missed. 0, or -1 if there is no such event. The addresses are good until something is sent to the queue or it
        // swaps.
        std::int32_t EventReadAfter(const CallContext& context, std::int32_t event, std::uint64_t* seen,
                                    std::uintptr_t* view)
        {
            if (seen == nullptr || view == nullptr)
            {
                return -1;
            }
            const auto* registration = Engine(context).components->GetEvent(event);
            const IEventQueue* queue =
                registration == nullptr ? nullptr : context.GetApp().FindEventQueue(registration->name);
            if (queue == nullptr || queue->EventSize() != registration->size)
            {
                return -1;
            }
            auto cursor = static_cast<std::size_t>(*seen);
            const RawEventRange events = queue->ReadRawAfter(cursor);
            *seen = cursor;
            view[0] = reinterpret_cast<std::uintptr_t>(events.older);
            view[1] = events.olderCount;
            view[2] = reinterpret_cast<std::uintptr_t>(events.newer);
            view[3] = events.newerCount;
            view[4] = events.missed;
            return 0;
        }

        // QueryCount and QueryFill: see Queries.hpp.
        std::int32_t QueryCount(const CallContext& context, const std::int32_t* ids, std::int32_t count)
        {
            if (ids == nullptr || count <= 0)
            {
                return -1;
            }
            auto& engine = Engine(context);
            ++engine.queryCalls;
            return CountQueryRows(context.Entities(), *engine.components,
                                  std::span<const std::int32_t>(ids, static_cast<std::size_t>(count)));
        }

        std::int32_t QueryFill(const CallContext& context, const std::int32_t* ids, std::int32_t count,
                               std::uintptr_t* rows, std::int32_t capacity)
        {
            if (ids == nullptr || count <= 0 || rows == nullptr || capacity < 0)
            {
                return -1;
            }
            auto& engine = Engine(context);
            ++engine.queryCalls;
            return FillQueryRows(context.Entities(), *engine.components,
                                 std::span<const std::int32_t>(ids, static_cast<std::size_t>(count)), rows, capacity);
        }
    } // namespace

    const CallContext* ActiveCallContext() noexcept
    {
        const auto* context = activeContext.load();
        return context == nullptr || !context->call ? nullptr : &*context->call;
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

    ScriptingExtension BuiltInExtension(const std::filesystem::path& contract)
    {
        constexpr auto notActive = static_cast<std::int32_t>(ChangeError::NotActive);
        constexpr std::int32_t unknown = -1;
        const std::string prefix = "Fenrir.Scripting.Native.";
        ScriptingExtension extension;
        extension.name = "built-in";
        // Called with no Scripting running, a function gives C# its second argument (zero, false or null when it has
        // none).
        extension.functions = {
            NativeFunction::Make<&Log>(prefix + "Log"),
            NativeFunction::Make<&TimeDeltaTime>(prefix + "TimeDeltaTime"),
            NativeFunction::Make<&TimeTickRate>(prefix + "TimeTickRate"),
            NativeFunction::Make<&EntityIsValid>(prefix + "EntityIsValid"),
            NativeFunction::Make<&EntityWorldTransform>(prefix + "EntityWorldTransform"),
            NativeFunction::Make<&EntitySetWorldTransform>(prefix + "EntitySetWorldTransform"),
            NativeFunction::Make<&ComponentIdOf>(prefix + "ComponentIdOf", unknown),
            NativeFunction::Make<&RegisterScriptComponent>(prefix + "RegisterScriptComponent"),
            NativeFunction::Make<&RegistryGeneration>(prefix + "RegistryGeneration"),
            NativeFunction::Make<&ComponentGet>(prefix + "ComponentGet"),
            NativeFunction::Make<&ComponentHas>(prefix + "ComponentHas"),
            NativeFunction::Make<&EntityCreate>(prefix + "EntityCreate", Entity::Null),
            NativeFunction::Make<&EntityDestroy>(prefix + "EntityDestroy", notActive),
            NativeFunction::Make<&ComponentAdd>(prefix + "ComponentAdd", notActive),
            NativeFunction::Make<&ComponentRemove>(prefix + "ComponentRemove", notActive),
            NativeFunction::Make<&EventIdOf>(prefix + "EventIdOf", unknown),
            NativeFunction::Make<&RegisterScriptEvent>(prefix + "RegisterScriptEvent"),
            NativeFunction::Make<&EventSend>(prefix + "EventSend", notActive),
            NativeFunction::Make<&EventReadAfter>(prefix + "EventReadAfter", unknown),
            NativeFunction::Make<&ChangesApply>(prefix + "ChangesApply"),
            NativeFunction::Make<&ChangesDiscard>(prefix + "ChangesDiscard"),
            NativeFunction::Make<&QueryCount>(prefix + "QueryCount", unknown),
            NativeFunction::Make<&QueryFill>(prefix + "QueryFill", unknown),
        };
        extension.components = EngineComponents();
        extension.sharedAssemblies = {contract};
        return extension;
    }
} // namespace Fenrir::Detail
