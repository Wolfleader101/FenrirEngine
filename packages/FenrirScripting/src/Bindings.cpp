#include "EngineContext.hpp"

#include <atomic>
#include <cstdint>
#include <string>

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
