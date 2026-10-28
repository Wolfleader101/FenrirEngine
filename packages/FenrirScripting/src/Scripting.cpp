#include "FenrirScripting/Scripting.hpp"

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"
#include "ExtensionSet.hpp"
#include "GenerationLifecycle.hpp"
#include "IrisAdapters.hpp"
#include "ScriptComponents.hpp"
#include "ScriptData.hpp"
#include "ScriptSystems.hpp"

#include <Iris/Iris.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
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
#else
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <unistd.h>
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

    namespace
    {
        int ProcessId()
        {
#if defined(_WIN32)
            return static_cast<int>(GetCurrentProcessId());
#else
            return static_cast<int>(getpid());
#endif
        }
    } // namespace

    struct Scripting::State
    {
        // Iris's messages arrive through a sink the host keeps until it is cleared, so the logger is borrowed
        // only until Shutdown clears the sink. The mutex guards the logger against a sink call racing Shutdown.
        std::mutex mutex;

        // A State that goes away without Shutdown (Install threw halfway, say) must not leave the process-wide context
        // pointing at it.
        ~State()
        {
            Shutdown();
        }
        // The App's logger outlives every State; ScriptSystems borrows it from the start, while `logger` below is
        // borrowed only while the Iris sink can call Forward.
        explicit State(ILogger& appLogger) : systems(hostLoader, &appLogger)
        {
        }
        State(const State&) = delete;
        State& operator=(const State&) = delete;

        ILogger* logger = nullptr;
        Detail::ComponentRegistry components;
        Detail::ChangeBuffer changes;
        // The built-in extension's state. While it is the active context, a Scripting is running for every extension's
        // native functions.
        std::shared_ptr<Detail::EngineContext> context = std::make_shared<Detail::EngineContext>();
        bool contextActive = false;
        // Every extension, its native functions registered and its state kept alive as long as they can be called;
        // built after the context is active (registering re-points each native's state at this Install's), and never
        // released (Iris cannot unregister).
        std::optional<Detail::ExtensionSet> extensions;

        // Scripting is live when the engine context points at this state and an App is attached: only then can scripts
        // act on the scene, and only then do changes and reloads mean anything.
        [[nodiscard]] bool IsLive() const noexcept
        {
            return contextActive && context->app != nullptr;
        }
        std::unique_ptr<Iris::Host> host;
        // What the lifecycle loads generations through: the host above. Declared before the lifecycle, which borrows
        // it.
        std::optional<Detail::IrisScriptAssemblyLoader> loader;
        // Where a generation's system host comes from, and the systems themselves: started by the lifecycle's start
        // hook, run once per scheduled phase, stopped by its stop hook before the generation is released. Declared
        // after the loader, whose Of they reach the component through, and before the lifecycle, whose hooks call them.
        Detail::IrisSystemHostLoader hostLoader;
        Detail::ScriptSystems systems;
        // The generations of the script assembly: copies, loading, replacing, retiring, and the file watcher.
        Detail::GenerationLifecycle generations;

        // The assembly named by the config is only ever copied; the copy is what loads.
        std::filesystem::path scriptAssembly;

        // The script data: what the generations define, registered in `components` above, and the data of it that is
        // alive in the ECS and the App.
        Detail::ScriptData scriptData{components};

        // Reloads now, if scripting is running; see GenerationLifecycle::Reload.
        Iris::CallResult<void> Reload();
        void PollForReload();

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
            try
            {
                // The systems say goodbye while the runtime is still up; the generation is released, its data removed
                // and the runtime asked to unload it; then the host that loaded it goes.
                if (generations.HasGeneration() || systems.HasSystems())
                {
                    generations.Unload();
                }
                if (IsLive())
                {
                    scriptData.Drop(*context->app);
                }
                if (host != nullptr && generations.PendingUnloadCount() != 0)
                {
                    generations.Sweep();
                }
            }
            catch (...)
            {
            }
            generations.Release();
            if (host != nullptr)
            {
                (void)host->SetLogSink({});
                host.reset();
            }
            if (contextActive)
            {
                Detail::DeactivateEngineContext(context.get());
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
        return m_state != nullptr && m_state->generations.HasGeneration();
    }

    Iris::CallResult<void> Scripting::State::Reload()
    {
        if (host == nullptr || !IsLive())
        {
            return Iris::CallResult<void>{Iris::CallStatus::NotInitialized,
                                          "Scripting is not running a script assembly that can be reloaded.", false};
        }
        return generations.Reload();
    }

    void Scripting::State::PollForReload()
    {
        if (host == nullptr || !IsLive())
        {
            return;
        }
        generations.ReloadIfReady(std::chrono::steady_clock::now());
    }

    const ComponentRegistration* Scripting::FindComponent(std::string_view name) const
    {
        if (m_state == nullptr)
        {
            return nullptr;
        }
        return m_state->components.Components().Get(m_state->components.Components().Find(name));
    }

    Iris::CallResult<void> Scripting::Reload()
    {
        if (m_state == nullptr)
        {
            return Iris::CallResult<void>{Iris::CallStatus::NotInitialized, "Scripting did not start.", false};
        }
        try
        {
            return m_state->Reload();
        }
        catch (const std::exception& exception)
        {
            return Iris::CallResult<void>{Iris::CallStatus::UnexpectedStatus,
                                          std::string("The reload failed: ") + exception.what(), false};
        }
    }

    int Scripting::Generation() const noexcept
    {
        return m_state == nullptr ? 0 : m_state->generations.Generation();
    }

    std::size_t Scripting::PendingUnloadCount() const noexcept
    {
        return m_state == nullptr ? 0 : m_state->generations.PendingUnloadCount();
    }

    Scripting::Diagnostics Scripting::ReadDiagnostics() const noexcept
    {
        if (m_state == nullptr)
        {
            return {};
        }
        return {m_state->systems.PhaseCalls(), m_state->context->queryCalls, m_state->changes.Size()};
    }

    Scripting::ChangeSummary Scripting::ApplyChanges()
    {
        if (!m_state || !m_state->IsLive())
        {
            return {};
        }
        const auto result = m_state->changes.Apply(*m_state->context->app, m_state->components);
        return {result.applied, result.skipped};
    }

    void Scripting::DiscardChanges()
    {
        if (m_state && m_state->IsLive())
        {
            m_state->changes.Discard(m_state->context->app->GetActiveScene().GetEntityList());
        }
    }

    Iris::Component* Scripting::LoadedComponent() noexcept
    {
        auto* loaded = m_state != nullptr ? m_state->generations.Component() : nullptr;
        return loaded != nullptr ? &Detail::IrisScriptAssemblyLoader::Of(*loaded) : nullptr;
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

    std::string NativeFunction::BindingNameFor(std::string_view name)
    {
        const auto split = name.rfind('.');
        if (split == std::string_view::npos || split == 0 || split + 1 == name.size())
        {
            return {};
        }
        return std::string(name.substr(0, split + 1)) + "__native_" + std::string(name.substr(split + 1));
    }

    std::string_view NativeFunction::NativeNameFor(std::string_view name) noexcept
    {
        const auto method = name.rfind('.');
        if (method == std::string_view::npos)
        {
            return name;
        }
        // The type is what precedes the method, up to the namespace's last dot; a nested type's `+` is part of it.
        const auto type = name.rfind('.', method - 1);
        return type == std::string_view::npos ? name : name.substr(type + 1);
    }

    // Why a stage of starting scripting failed: what went wrong, and the diagnostic that came with it, if any.
    struct StartupFailure
    {
        std::string what;
        std::string detail;
    };
    using StartupStage = std::function<std::optional<StartupFailure>()>;

    Scripting Scripting::InstallImpl(App& app, ScriptingConfig config)
    {
        ILogger& logger = *app.Logger();
        auto state = std::make_shared<State>(logger);

        // Whatever happens below, the App finishing Run releases everything Iris owns, on the main thread
        // (a sequential system), before Run returns.
        app.AddSequentialSystems(SchedulePriority::Exit, {[state](App&) { state->Shutdown(); }});

        if (config.scriptAssembly.empty())
        {
            return Scripting(std::move(state));
        }

        // What the stages build up for the ones after them.
        std::error_code error;
        std::filesystem::path assembly;
        std::filesystem::path bridgeDirectory;
        std::filesystem::path contract;
        std::filesystem::path math;
        Detail::GenerationLifecycle::Settings settings;

        // Starting is a list of stages, run in order, and the first to fail ends it: one place reports why, gives
        // everything back and returns a Scripting without scripts. The order matters where the comments say so.
        const std::vector<StartupStage> stages = {
            // The files scripting needs are there.
            [&]() -> std::optional<StartupFailure> {
                assembly = std::filesystem::absolute(config.scriptAssembly, error);
                if (error || !std::filesystem::is_regular_file(assembly, error))
                {
                    return StartupFailure{"the script assembly '" + config.scriptAssembly.string() + "' does not exist",
                                          {}};
                }
                bridgeDirectory = config.bridgeDirectory.empty()
                                      ? ExecutableDirectory()
                                      : std::filesystem::absolute(config.bridgeDirectory, error);
                if (error)
                {
                    return StartupFailure{
                        "the bridge directory '" + config.bridgeDirectory.string() + "' is not a usable path", {}};
                }
                // The contract every script compiles against is shared, so its types have one identity across
                // reloads.
                contract = bridgeDirectory / "Fenrir.Scripting.dll";
                if (!std::filesystem::is_regular_file(contract, error))
                {
                    return StartupFailure{"the contract '" + contract.string() + "' is missing", {}};
                }
                // The engine's math types are their own contract, which the scripting contract is built on.
                math = bridgeDirectory / "Fenrir.Math.dll";
                if (!std::filesystem::is_regular_file(math, error))
                {
                    return StartupFailure{"the math contract '" + math.string() + "' is missing", {}};
                }
                // The engine's own API is the first extension: its native functions, components and contract are
                // declared, checked and registered like any package's. Being first, its components take their names
                // before anyone else's, and its contracts are shared before any other.
                config.extensions.insert(config.extensions.begin(),
                                         Detail::BuiltInExtension(math, contract, state->context));
                return std::nullopt;
            },
            // The App the native functions act on, and every extension's components, events, shared structs and
            // resources.
            [&]() -> std::optional<StartupFailure> {
                state->context->app = &app;
                state->context->components = &state->components;
                state->context->changes = &state->changes;
                // Every extension's components, events, shared structs and resources, in order, by the registry's one
                // rule for every kind (ComponentRegistry::AddExtension).
                for (auto& extension : config.extensions)
                {
                    if (auto failure = state->components.AddExtension(extension, app))
                    {
                        return StartupFailure{std::move(*failure), {}};
                    }
                }
                return std::nullopt;
            },
            // Only one Scripting is active in a process; then the extension set: every extension's native functions
            // checked (named Namespace.Type.Method, declared once, taking a state their extension carries) and
            // registered with their states, before the host shares the extensions' contracts, since sharing binds their
            // fields to these.
            [&]() -> std::optional<StartupFailure> {
                if (!Detail::ActivateEngineContext(state->context.get()))
                {
                    return StartupFailure{"another Scripting is already active in this process", {}};
                }
                state->contextActive = true;
                Detail::IrisNativeRegistrar registrar;
                auto installed = Detail::ExtensionSet::Install(std::move(config.extensions), registrar);
                if (!installed.Succeeded())
                {
                    return StartupFailure{std::move(installed.diagnostic), {}};
                }
                state->extensions = std::move(*installed);
                return std::nullopt;
            },
            // .NET, with the contract shared, and Iris's messages going to the engine's logger.
            [&]() -> std::optional<StartupFailure> {
                Iris::HostConfiguration configuration;
                configuration.runtimeConfigPath = bridgeDirectory / "Iris.Bridge.runtimeconfig.json";
                configuration.bridgeAssemblyPath = bridgeDirectory / "Iris.Bridge.dll";
                configuration.dotnetRoot = config.dotnetRoot;
                // Every extension's contract, in order, so the built-in extension's (Fenrir.Math, then the scripting
                // contract) come first: the others may refer to them, and each is bound as it is shared.
                // A relative path is beside the scripting contract, where the build deploys a package's contract.
                for (const auto& assembly : state->extensions->SharedAssemblies())
                {
                    configuration.sharedAssemblies.push_back(bridgeDirectory / assembly);
                }

                state->logger = &logger;
                state->host = std::make_unique<Iris::Host>();
                const auto initialized = state->host->Initialize(configuration);
                if (!initialized.Succeeded())
                {
                    return StartupFailure{".NET could not be started", initialized.diagnostic};
                }
                // The sink may be called from any managed thread; State::Forward serialises it against Shutdown.
                const auto sink =
                    state->host->SetLogSink([weak = std::weak_ptr<State>(state)](const Iris::LogMessage& message) {
                        if (const auto live = weak.lock())
                        {
                            live->Forward(message);
                        }
                    });
                if (!sink.Succeeded())
                {
                    logger.Warn("Scripting: Iris's log messages will not be shown. {}", sink.diagnostic);
                }
                return std::nullopt;
            },
            // The generations of the script assembly, and what runs each frame.
            [&]() -> std::optional<StartupFailure> {
                state->scriptAssembly = assembly;
                // Each Install has a directory of its own, so one that is still waiting for a generation to unload
                // never gets in the way of the next.
                static std::atomic<int> installs{0};
                settings.assembly = assembly;
                settings.scratchRoot =
                    (config.reloadDirectory.empty() ? std::filesystem::temp_directory_path(error) /
                                                          ("FenrirScripting-" + std::to_string(ProcessId()))
                                                    : std::filesystem::absolute(config.reloadDirectory, error)) /
                    ("install-" + std::to_string(++installs));
                if (error || settings.scratchRoot.empty())
                {
                    return StartupFailure{"the folder for the script copies could not be chosen", {}};
                }
                settings.hotReload = config.hotReload;
                settings.reloadInterval = config.reloadInterval;
                settings.reloadDebounce = config.reloadDebounce;
                settings.unloadTimeout = config.unloadTimeout;
                // The lifecycle knows nothing of what a generation holds: the State supplies that. It owns the
                // lifecycle, so the raw pointer the hooks keep lives as long as they do.
                State* const self = state.get();
                Detail::GenerationLifecycle::Hooks hooks;
                hooks.reconcile = [self](Detail::LoadedScriptAssembly& loaded) {
                    Detail::IrisScriptAssembly assembly(*self->context, Detail::IrisScriptAssemblyLoader::Of(loaded));
                    return self->scriptData.Admit(*self->context->app, assembly);
                };
                hooks.start = [self](Detail::LoadedScriptAssembly& loaded) { return self->systems.Start(loaded); };
                // The systems say goodbye, and what the generation defined goes.
                hooks.stop = [self] {
                    self->systems.Stop();
                    self->scriptData.Forget();
                };
                hooks.beforeReload = [self] { (void)self->changes.Apply(*self->context->app, self->components); };
                const bool hotReload = settings.hotReload;
                state->loader.emplace(*state->host);
                state->generations.Configure(*state->loader, &logger, std::move(settings), std::move(hooks));

                // The watcher looks before anything else runs in a frame, so a reload happens between frames. The
                // phases are scheduled once; each runs the current generation's systems, if it has any in that phase.
                if (hotReload)
                {
                    app.AddSequentialSystems(SchedulePriority::PreUpdate, {[state](App&) { state->PollForReload(); }});
                }
                constexpr SchedulePriority priorities[] = {SchedulePriority::PreUpdate, SchedulePriority::Tick,
                                                           SchedulePriority::Update, SchedulePriority::PostUpdate};
                for (int phase = 0; phase < 4; ++phase)
                {
                    app.AddSequentialSystems(priorities[phase],
                                             {[state, phase](App&) { state->systems.RunPhase(phase); }});
                }
                return std::nullopt;
            },
            // The first generation.
            [&]() -> std::optional<StartupFailure> {
                if (const auto loaded = state->generations.Load(); !loaded.Succeeded())
                {
                    return StartupFailure{{}, loaded.diagnostic};
                }
                return std::nullopt;
            },
        };

        for (const auto& stage : stages)
        {
            if (const auto failure = stage())
            {
                logger.Error("Scripting: {}running without scripts.{}",
                             failure->what.empty() ? std::string() : failure->what + "; ",
                             failure->detail.empty() ? std::string() : " " + failure->detail);
                // Nothing runs, so give back the runtime and the process-wide slot.
                state->Shutdown();
                return Scripting(std::move(state));
            }
        }
        logger.Info("Scripting: loaded '{}'.", assembly.filename().string());
        return Scripting(std::move(state));
    }
} // namespace Fenrir
