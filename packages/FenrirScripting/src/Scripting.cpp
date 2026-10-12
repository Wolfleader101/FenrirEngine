#include "FenrirScripting/Scripting.hpp"

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"

#include <Iris/Iris.hpp>

#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace Fenrir
{
    namespace
    {
        std::filesystem::path ExecutableDirectory()
        {
#if defined(_WIN32)
            std::wstring buffer(1024, L'\0');
            while (true)
            {
                const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return std::filesystem::current_path();
                }
                if (length < buffer.size())
                {
                    buffer.resize(length);
                    return std::filesystem::path(buffer).parent_path();
                }
                buffer.resize(buffer.size() * 2);
            }
#elif defined(__APPLE__)
            std::uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string buffer(size, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) != 0)
            {
                return std::filesystem::current_path();
            }
            return std::filesystem::weakly_canonical(buffer.c_str()).parent_path();
#else
            std::error_code error;
            const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
            return error ? std::filesystem::current_path() : path.parent_path();
#endif
        }
    } // namespace

    struct Scripting::State
    {
        // Iris's messages arrive through a sink the host keeps until it is cleared, so the logger is borrowed
        // only until Shutdown clears the sink. The mutex guards the logger against a sink call racing Shutdown.
        std::mutex mutex;
        ILogger* logger = nullptr;
        Detail::ComponentRegistry components;
        Detail::EngineContext context;
        bool contextActive = false;
        std::unique_ptr<Iris::Host> host;
        std::optional<Iris::Component> component;

        void Forward(const Iris::LogMessage& message)
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (logger == nullptr)
            {
                return;
            }
            // The text is data, never a format string: Iris messages can contain braces.
            const std::string text(message.text);
            switch (message.level)
            {
            case Iris::LogLevel::Debug:
                logger->Log("[Iris] {}", text);
                break;
            case Iris::LogLevel::Info:
                logger->Info("[Iris] {}", text);
                break;
            case Iris::LogLevel::Warning:
                logger->Warn("[Iris] {}", text);
                break;
            case Iris::LogLevel::Error:
                logger->Error("[Iris] {}", text);
                break;
            default:
                logger->Log("[Iris] {}", text);
                break;
            }
        }

        void Shutdown() noexcept
        {
            // The script assembly first, then the host that loaded it.
            component.reset();
            if (host != nullptr)
            {
                (void)host->SetLogSink({});
                host.reset();
            }
            if (contextActive)
            {
                Detail::DeactivateEngineContext(&context);
                contextActive = false;
            }
            const std::lock_guard<std::mutex> lock(mutex);
            logger = nullptr;
        }
    };

    Scripting::Scripting(std::shared_ptr<State> state) noexcept : m_state(std::move(state))
    {
    }

    Scripting::~Scripting()
    {
        Shutdown();
    }

    Scripting::Scripting(Scripting&&) noexcept = default;

    Scripting& Scripting::operator=(Scripting&& other) noexcept
    {
        if (this != &other)
        {
            Shutdown();
            m_state = std::move(other.m_state);
        }
        return *this;
    }

    bool Scripting::IsRunning() const noexcept
    {
        return m_state != nullptr && m_state->host != nullptr;
    }

    bool Scripting::HasScripts() const noexcept
    {
        return m_state != nullptr && m_state->component.has_value();
    }

    namespace
    {
        // Checks every registered component against its C# struct. The first mismatch is the answer, naming the field.
        Iris::CallResult<void> ValidateRegistration(const ComponentRegistration& registration, const Iris::Component& component)
        {
            if (!registration.validate)
            {
                return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
            }
            auto validated = registration.validate(component);
            if (!validated.Succeeded())
            {
                validated.diagnostic = "Component '" + registration.name + "' does not match its C# struct: " + validated.diagnostic;
            }
            return validated;
        }

        Iris::CallResult<void> ValidateAll(const std::vector<ComponentRegistration>& registrations, const Iris::Component& component)
        {
            for (const auto& registration : registrations)
            {
                if (auto validated = ValidateRegistration(registration, component); !validated.Succeeded())
                {
                    return validated;
                }
            }
            return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
        }
    } // namespace

    Iris::CallResult<void> Scripting::RegisterComponent(ComponentRegistration registration)
    {
        if (m_state == nullptr)
        {
            return Iris::CallResult<void>{Iris::CallStatus::NotInitialized, "Scripting did not start.", false};
        }
        if (m_state->components.Add(registration) < 0)
        {
            return Iris::CallResult<void>{Iris::CallStatus::InvalidArgument,
                                          "The component name '" + registration.name +
                                              "' is empty, already registered, or has no storage operations.",
                                          false};
        }
        if (!m_state->component.has_value())
        {
            return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
        }
        auto validated = ValidateRegistration(registration, *m_state->component);
        if (!validated.Succeeded())
        {
            if (ILogger* logger = m_state->logger)
            {
                logger->Error("Scripting: {} Releasing the script assembly.", validated.diagnostic);
            }
            m_state->Shutdown();
        }
        return validated;
    }

    Iris::Component* Scripting::GetComponent() noexcept
    {
        return m_state != nullptr && m_state->component.has_value() ? &*m_state->component : nullptr;
    }

    void Scripting::Shutdown() noexcept
    {
        if (m_state != nullptr)
        {
            m_state->Shutdown();
        }
    }

    Scripting Scripting::Install(App& app, ScriptingConfig config)
    {
        // Nothing escapes: a path that cannot be converted, an allocation failure and the like are logged and
        // the app runs without scripts. A half-built State is released as the exception unwinds.
        try
        {
            return InstallImpl(app, std::move(config));
        }
        catch (const std::exception& exception)
        {
            ReportFailure(app, exception.what());
        }
        catch (...)
        {
            ReportFailure(app, "an unknown error");
        }
        return Scripting(nullptr);
    }

    void Scripting::ReportFailure(App& app, const char* what) noexcept
    {
        try
        {
            app.Logger()->Error("Scripting: it could not be started; running without scripts. {}", what);
        }
        catch (...)
        {
        }
    }

    Scripting Scripting::InstallImpl(App& app, ScriptingConfig config)
    {
        auto state = std::make_shared<State>();
        ILogger& logger = *app.Logger();

        // Whatever happens below, the App finishing Run releases everything Iris owns, on the main thread
        // (a sequential system), before Run returns.
        app.AddSequentialSystems(SchedulePriority::Exit, {[state](App&) { state->Shutdown(); }});

        if (config.scriptAssembly.empty())
        {
            return Scripting(std::move(state));
        }

        std::error_code error;
        const auto assembly = std::filesystem::absolute(config.scriptAssembly, error);
        if (error || !std::filesystem::is_regular_file(assembly, error))
        {
            logger.Error("Scripting: the script assembly '{}' does not exist; running without scripts.",
                         config.scriptAssembly.string());
            return Scripting(std::move(state));
        }

        const auto bridgeDirectory = config.bridgeDirectory.empty()
                                         ? ExecutableDirectory()
                                         : std::filesystem::absolute(config.bridgeDirectory, error);
        if (error)
        {
            logger.Error("Scripting: the bridge directory '{}' is not a usable path; running without scripts.",
                         config.bridgeDirectory.string());
            return Scripting(std::move(state));
        }
        // The contract every script compiles against is shared, so its types have one identity across reloads.
        const auto contract = bridgeDirectory / "Fenrir.Scripting.dll";
        if (!std::filesystem::is_regular_file(contract, error))
        {
            logger.Error("Scripting: the contract '{}' is missing; running without scripts.", contract.string());
            return Scripting(std::move(state));
        }
        Iris::HostConfiguration configuration;
        configuration.runtimeConfigPath = bridgeDirectory / "Iris.Bridge.runtimeconfig.json";
        configuration.bridgeAssemblyPath = bridgeDirectory / "Iris.Bridge.dll";
        configuration.dotnetRoot = config.dotnetRoot;
        configuration.sharedAssemblies = config.sharedAssemblies;
        configuration.sharedAssemblies.push_back(contract);

        // The engine's native functions act on this App, and are registered before the contract is shared (sharing
        // binds the contract's function pointers to them).
        state->context.app = &app;
        state->context.components = &state->components;
        if (!Detail::RegisterEngineComponents(state->components))
        {
            logger.Error("Scripting: the engine's own components could not be registered; running without scripts.");
            return Scripting(std::move(state));
        }
        for (auto& registration : config.components)
        {
            if (state->components.Add(std::move(registration)) < 0)
            {
                logger.Error("Scripting: a component in the config has an empty or duplicate name; running without scripts.");
                return Scripting(std::move(state));
            }
        }
        if (!Detail::ActivateEngineContext(&state->context))
        {
            logger.Error("Scripting: another Scripting is already active in this process; running without scripts.");
            return Scripting(std::move(state));
        }
        state->contextActive = true;
        if (const auto bound = Detail::RegisterEngineBindings(); !bound.Succeeded())
        {
            logger.Error("Scripting: the engine's native functions could not be registered; running without "
                         "scripts. {}",
                         bound.diagnostic);
            state->Shutdown();
            return Scripting(std::move(state));
        }

        state->logger = &logger;
        state->host = std::make_unique<Iris::Host>();
        const auto initialized = state->host->Initialize(configuration);
        if (!initialized.Succeeded())
        {
            logger.Error("Scripting: .NET could not be started; running without scripts. {}", initialized.diagnostic);
            state->Shutdown();
            return Scripting(std::move(state));
        }
        // The sink may be called from any managed thread; State::Forward serialises it against Shutdown.
        const auto sink = state->host->SetLogSink([weak = std::weak_ptr<State>(state)](const Iris::LogMessage& message) {
            if (const auto live = weak.lock())
            {
                live->Forward(message);
            }
        });
        if (!sink.Succeeded())
        {
            logger.Warn("Scripting: Iris's log messages will not be shown. {}", sink.diagnostic);
        }

        auto loaded = state->host->LoadComponent(assembly);
        if (!loaded.Succeeded())
        {
            logger.Error("Scripting: the script assembly '{}' could not be loaded; running without scripts. {}",
                         assembly.string(), loaded.diagnostic);
            // Nothing runs, so give back the runtime and the process-wide slot.
            state->Shutdown();
            return Scripting(std::move(state));
        }
        // All or nothing: the assembly stays loaded only if every registered component matches its C# struct.
        if (auto validated = ValidateAll(state->components.All(), *loaded.value); !validated.Succeeded())
        {
            logger.Error("Scripting: the script assembly '{}' was released. {}", assembly.string(), validated.diagnostic);
            state->Shutdown();
            return Scripting(std::move(state));
        }
        state->component.emplace(std::move(*loaded.value));
        logger.Info("Scripting: loaded '{}'.", assembly.filename().string());
        return Scripting(std::move(state));
    }
} // namespace Fenrir
