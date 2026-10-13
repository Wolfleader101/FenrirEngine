#include "EngineContext.hpp"
#include "Queries.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace Fenrir::Detail
{
    namespace
    {
        std::atomic<EngineContext*> activeContext{nullptr};

        // The App the active Scripting serves, or null when none is active (a script call after Shutdown, say). Each
        // function below is the engine side of one internal call: a plain function over C types, where pointers and
        // lengths arrive exactly as the C# Native class declares them (the friendly C# wrapper has already done the
        // UTF-8). A C++ exception that escapes one is stopped and logged by Iris, so none can unwind into managed code.
        App* ActiveApp() noexcept
        {
            const auto* context = activeContext.load();
            return context == nullptr ? nullptr : context->app;
        }

        // Native.Log: level 0 Info, 1 Warn, 2 Error. The text is data, never a format string.
        void Log(std::int32_t level, const char* text, std::int32_t length)
        {
            auto* app = ActiveApp();
            if (app == nullptr || text == nullptr || length < 0)
            {
                return;
            }
            const std::string message(text, static_cast<std::size_t>(length));
            auto& logger = *app->Logger();
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

        float TimeDeltaTime()
        {
            const auto* app = ActiveApp();
            return app == nullptr ? 0.0F : static_cast<float>(app->GetTime().deltaTime);
        }

        float TimeTickRate()
        {
            const auto* app = ActiveApp();
            return app == nullptr ? 0.0F : static_cast<float>(app->GetTime().tickRate);
        }

        bool EntityIsValid(std::uint32_t value)
        {
            auto* app = ActiveApp();
            return app != nullptr && app->GetActiveScene().GetEntityList().HasEntity(value);
        }

        // Native.ComponentIdOf: the id of the component registered under a UTF-8 name for the C# type with the given full
        // name; -1 if nothing is registered under the name, -3 if it is registered for a different C# type.
        std::int32_t ComponentIdOf(const char* name, std::int32_t length, const char* typeName, std::int32_t typeLength)
        {
            const auto* context = activeContext.load();
            if (context == nullptr || context->components == nullptr || name == nullptr || length < 0 || typeName == nullptr ||
                typeLength < 0)
            {
                return -1;
            }
            return context->components->Resolve(std::string_view(name, static_cast<std::size_t>(length)),
                                                std::string_view(typeName, static_cast<std::size_t>(typeLength)));
        }

        // Native.RegisterScriptComponent: a script assembly reports a component type it defines, while it loads. False if
        // nothing is loading.
        bool RegisterScriptComponent(const char* name, std::int32_t length, std::int32_t size, std::int32_t alignment)
        {
            auto* context = activeContext.load();
            if (context == nullptr || context->discovering == nullptr || name == nullptr || length <= 0 || size <= 0 ||
                alignment <= 0)
            {
                return false;
            }
            context->discovering->push_back({std::string(name, static_cast<std::size_t>(length)),
                                             static_cast<std::size_t>(size), static_cast<std::size_t>(alignment)});
            return true;
        }

        // Native.RegistryGeneration: which registry ids belong to. Zero when none is active.
        std::uint32_t RegistryGeneration()
        {
            const auto* context = activeContext.load();
            return context == nullptr || context->components == nullptr ? 0U : context->components->Generation();
        }

        // Native.ComponentGet: the entity's component in the engine's storage, or null.
        void* ComponentGet(std::int32_t id, std::uint32_t entity)
        {
            const auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->components == nullptr)
            {
                return nullptr;
            }
            const auto* component = context->components->Get(id);
            return component == nullptr ? nullptr
                                        : component->get(context->app->GetActiveScene().GetEntityList(), entity);
        }

        // Native.ComponentHas: whether the entity is alive and has the component.
        bool ComponentHas(std::int32_t id, std::uint32_t entity)
        {
            const auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->components == nullptr)
            {
                return false;
            }
            const auto* component = context->components->Get(id);
            return component != nullptr && component->has(context->app->GetActiveScene().GetEntityList(), entity);
        }

        // Native.EntityCreate: a new entity id, usable at once; the entity gets its engine components when changes play back.
        std::uint32_t EntityCreate()
        {
            const auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->changes == nullptr)
            {
                return Entity::Null;
            }
            return context->changes->Create(context->app->GetActiveScene().GetEntityList());
        }

        // Native.EntityDestroy, ComponentAdd and ComponentRemove record a change for playback. They return a ChangeError code:
        // 0 recorded, negative for the reason it was refused.
        std::int32_t Refuse() noexcept
        {
            return static_cast<std::int32_t>(ChangeError::NotActive);
        }

        std::int32_t EntityDestroy(std::uint32_t entity)
        {
            const auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->changes == nullptr)
            {
                return Refuse();
            }
            return static_cast<std::int32_t>(context->changes->Destroy(context->app->GetActiveScene().GetEntityList(), entity));
        }

        // `value` is the component's bytes, or null for a default one.
        std::int32_t ComponentAdd(std::int32_t id, std::uint32_t entity, const void* value)
        {
            const auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->changes == nullptr || context->components == nullptr)
            {
                return Refuse();
            }
            return static_cast<std::int32_t>(context->changes->Add(context->app->GetActiveScene().GetEntityList(),
                                                                   *context->components, id, entity, value));
        }

        std::int32_t ComponentRemove(std::int32_t id, std::uint32_t entity)
        {
            const auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->changes == nullptr || context->components == nullptr)
            {
                return Refuse();
            }
            return static_cast<std::int32_t>(context->changes->Remove(context->app->GetActiveScene().GetEntityList(),
                                                                      *context->components, id, entity));
        }

        // Native.ChangesApply: plays back the recorded changes, returning how many were applied.
        std::int32_t ChangesApply()
        {
            auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->components == nullptr || context->changes == nullptr)
            {
                return 0;
            }
            return static_cast<std::int32_t>(context->changes->Apply(*context->app, *context->components).applied);
        }

        // Native.ChangesDiscard: drops the recorded changes (and destroys the entities they created).
        void ChangesDiscard()
        {
            auto* context = activeContext.load();
            if (context != nullptr && context->app != nullptr && context->changes != nullptr)
            {
                context->changes->Discard(context->app->GetActiveScene().GetEntityList());
            }
        }

        // Native.QueryCount and Native.QueryFill: see Queries.hpp.
        std::int32_t QueryCount(const std::int32_t* ids, std::int32_t count)
        {
            auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->components == nullptr || ids == nullptr || count <= 0)
            {
                return -1;
            }
            ++context->queryCalls;
            return CountQueryRows(context->app->GetActiveScene().GetEntityList(), *context->components,
                                      std::span<const std::int32_t>(ids, static_cast<std::size_t>(count)));
        }

        std::int32_t QueryFill(const std::int32_t* ids, std::int32_t count, std::uintptr_t* rows, std::int32_t capacity)
        {
            auto* context = activeContext.load();
            if (context == nullptr || context->app == nullptr || context->components == nullptr || ids == nullptr || count <= 0 ||
                rows == nullptr || capacity < 0)
            {
                return -1;
            }
            ++context->queryCalls;
            return FillQueryRows(context->app->GetActiveScene().GetEntityList(), *context->components,
                                     std::span<const std::int32_t>(ids, static_cast<std::size_t>(count)), rows, capacity);
        }
    } // namespace

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

    Iris::CallResult<void> RegisterEngineBindings()
    {
        const std::string prefix = "Fenrir.Scripting.Native.";
        Iris::CallResult<void> results[] = {
            Iris::RegisterInternalCall<&Log>(prefix + "Log"),
            Iris::RegisterInternalCall<&TimeDeltaTime>(prefix + "TimeDeltaTime"),
            Iris::RegisterInternalCall<&TimeTickRate>(prefix + "TimeTickRate"),
            Iris::RegisterInternalCall<&EntityIsValid>(prefix + "EntityIsValid"),
            Iris::RegisterInternalCall<&ComponentIdOf>(prefix + "ComponentIdOf"),
            Iris::RegisterInternalCall<&RegisterScriptComponent>(prefix + "RegisterScriptComponent"),
            Iris::RegisterInternalCall<&RegistryGeneration>(prefix + "RegistryGeneration"),
            Iris::RegisterInternalCall<&ComponentGet>(prefix + "ComponentGet"),
            Iris::RegisterInternalCall<&ComponentHas>(prefix + "ComponentHas"),
            Iris::RegisterInternalCall<&EntityCreate>(prefix + "EntityCreate"),
            Iris::RegisterInternalCall<&EntityDestroy>(prefix + "EntityDestroy"),
            Iris::RegisterInternalCall<&ComponentAdd>(prefix + "ComponentAdd"),
            Iris::RegisterInternalCall<&ComponentRemove>(prefix + "ComponentRemove"),
            Iris::RegisterInternalCall<&ChangesApply>(prefix + "ChangesApply"),
            Iris::RegisterInternalCall<&ChangesDiscard>(prefix + "ChangesDiscard"),
            Iris::RegisterInternalCall<&QueryCount>(prefix + "QueryCount"),
            Iris::RegisterInternalCall<&QueryFill>(prefix + "QueryFill"),
        };
        for (auto& result : results)
        {
            if (!result.Succeeded())
            {
                return std::move(result);
            }
        }
        return std::move(results[0]);
    }
} // namespace Fenrir::Detail
