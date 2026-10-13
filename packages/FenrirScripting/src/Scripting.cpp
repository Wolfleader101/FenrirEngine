#include "FenrirScripting/Scripting.hpp"

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"
#include "GenerationLifecycle.hpp"
#include "ScriptComponents.hpp"
#include "ScriptSchema.hpp"

#include <Iris/Iris.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
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
        State() = default;
        State(const State&) = delete;
        State& operator=(const State&) = delete;

        ILogger* logger = nullptr;
        Detail::ComponentRegistry components;
        Detail::ChangeBuffer changes;
        Detail::EngineContext context;
        bool contextActive = false;

        // Scripting is live when the engine context points at this state and an App is attached: only then can scripts
        // act on the scene, and only then do changes and reloads mean anything.
        [[nodiscard]] bool IsLive() const noexcept
        {
            return contextActive && context.app != nullptr;
        }
        std::unique_ptr<Iris::Host> host;
        // The generations of the script assembly: copies, loading, replacing, retiring, and the file watcher.
        Detail::GenerationLifecycle generations;

        // The script's systems: one managed object (the generated SystemHost) the engine calls once per phase. Released
        // before the component, which cannot be released while an object it made is alive.
        std::optional<Iris::Object> runner;
        std::optional<Iris::InstanceMethod<void(std::int32_t)>> runnerRun;
        std::optional<Iris::InstanceMethod<void()>> runnerDestroy;
        std::thread::id mainThread;
        std::size_t phaseCalls = 0;
        int phaseMask = 0; // Bit n is set when the loaded generation has a system in phase n.

        // The assembly named by the config is only ever copied; the copy is what loads.
        std::filesystem::path scriptAssembly;

        // The script components and events whose data is alive in the ECS and the App, by name.
        Detail::OwnedLayouts ownedComponents;
        Detail::OwnedLayouts ownedEvents;

        // Removes the data of every script component and event: they exist only while a script assembly is loaded.
        void DropScriptData();

        // Reloads now, if scripting is running; see GenerationLifecycle::Reload.
        Iris::CallResult<void> Reload();
        void PollForReload();

        // Stops the generation's systems before it is released: they say goodbye, and what they defined goes.
        void StopSystems();

        // Makes the generation's systems if the script assembly has any, and runs OnCreate. Nothing to do (and success)
        // when the script defines no systems. The phases themselves are scheduled once, at Install.
        Iris::CallResult<void> StartSystems(const Iris::Component& component)
        {
            using Result = Iris::CallResult<void>;
            const auto type = component.FindType("Fenrir.Scripting.Generated.SystemHost");
            if (type.status == Iris::CallStatus::NotFound)
            {
                return Result{Iris::CallStatus::Success, {}, false};
            }
            if (!type.Succeeded())
            {
                return Result{type.status, "The script's system host could not be read: " + type.diagnostic, false};
            }
            auto object = type.value->CreateObject();
            if (!object.Succeeded())
            {
                return Result{object.status, "The script's systems could not be created: " + object.diagnostic,
                              object.diagnosticTruncated};
            }
            const auto mask = type.value->FindInstanceMethod<std::int32_t()>("PhaseMask");
            const auto create = type.value->FindInstanceMethod<void()>("OnCreate");
            const auto run = type.value->FindInstanceMethod<void(std::int32_t)>("Run");
            const auto destroy = type.value->FindInstanceMethod<void()>("OnDestroy");
            for (const auto* found : {&mask.status, &create.status, &run.status, &destroy.status})
            {
                if (*found != Iris::CallStatus::Success)
                {
                    return Result{*found,
                                  "The script's system host is missing a method: " + mask.diagnostic +
                                      create.diagnostic + run.diagnostic + destroy.diagnostic,
                                  false};
                }
            }
            runner.emplace(std::move(*object.value));
            runnerRun.emplace(*run.value);
            runnerDestroy.emplace(*destroy.value);
            mainThread = std::this_thread::get_id();

            const auto phases = mask.value->Invoke(*runner);
            if (!phases.Succeeded())
            {
                return Result{phases.status, "The script's systems could not be listed: " + phases.diagnostic,
                              phases.diagnosticTruncated};
            }
            phaseMask = *phases.value;
            const auto created = create.value->Invoke(*runner);
            if (!created.Succeeded())
            {
                return Result{created.status, "The script's systems could not start: " + created.diagnostic,
                              created.diagnosticTruncated};
            }
            return Result{Iris::CallStatus::Success, {}, false};
        }

        // One transition into managed code for the whole phase. Only ever on the main thread: Iris calls, script calls
        // and the internal calls they make all assume it.
        void RunPhase(int phase)
        {
            if (!runner.has_value() || !runnerRun.has_value() || (phaseMask & (1 << phase)) == 0)
            {
                return;
            }
            if (std::this_thread::get_id() != mainThread)
            {
                assert(false && "Scripts must only run on the main thread");
                if (ILogger* log = logger)
                {
                    log->Error("Scripting: a script phase was asked to run off the main thread and was skipped.");
                }
                return;
            }
            ++phaseCalls;
            const auto ran = runnerRun->Invoke(*runner, phase);
            if (!ran.Succeeded() && logger != nullptr)
            {
                logger->Error("Scripting: running the script's systems failed: {}", ran.diagnostic);
            }
        }

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
                if (generations.HasGeneration() || runner.has_value())
                {
                    generations.Unload();
                }
                if (IsLive())
                {
                    DropScriptData();
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
        return m_state != nullptr && m_state->generations.HasGeneration();
    }

    void Scripting::State::StopSystems()
    {
        if (runner.has_value() && runnerDestroy.has_value() && contextActive)
        {
            const auto destroyed = runnerDestroy->Invoke(*runner);
            if (!destroyed.Succeeded() && logger != nullptr)
            {
                logger->Error("Scripting: the script's systems could not shut down cleanly: {}", destroyed.diagnostic);
            }
        }
        runnerRun.reset();
        runnerDestroy.reset();
        runner.reset();
        phaseMask = 0;
        components.RemoveScriptDefined();
    }

    void Scripting::State::DropScriptData()
    {
        App& app = *context.app;
        for (const auto& [name, layout] : ownedComponents)
        {
            const DynamicComponentType type(name, layout.size, layout.alignment);
            app.ForEachScene([&type](Scene& scene) { (void)scene.GetEntityList().ClearDynamic(type); });
        }
        for (const auto& [name, layout] : ownedEvents)
        {
            (void)app.RemoveEventQueue(name);
        }
        ownedComponents.clear();
        ownedEvents.clear();
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
        return m_state->components.Get(m_state->components.Find(name));
    }

    Iris::CallResult<void> Scripting::RegisterEvent(EventRegistration registration)
    {
        if (m_state == nullptr || m_state->context.app == nullptr)
        {
            return Iris::CallResult<void>{Iris::CallStatus::NotInitialized, "Scripting did not start.", false};
        }
        if (registration.name.empty() || m_state->components.FindEvent(registration.name) >= 0 ||
            (registration.bind && !registration.bind(*m_state->context.app)))
        {
            return Iris::CallResult<void>{Iris::CallStatus::InvalidArgument,
                                          "The event name '" + registration.name +
                                              "' is empty, already registered, or already names another queue.",
                                          false};
        }
        const std::string name = registration.name;
        const auto validate = registration.validate;
        if (m_state->components.AddEvent(std::move(registration)) < 0)
        {
            return Iris::CallResult<void>{Iris::CallStatus::InvalidArgument,
                                          "The event '" + name + "' could not be registered.", false};
        }
        if (!m_state->generations.HasGeneration() || !validate)
        {
            return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
        }
        auto validated = validate(*m_state->generations.Component());
        if (!validated.Succeeded())
        {
            validated.diagnostic = "Event '" + name + "' does not match its C# struct: " + validated.diagnostic;
            if (ILogger* logger = m_state->logger)
            {
                logger->Error("Scripting: {} Releasing the script assembly.", validated.diagnostic);
            }
            m_state->Shutdown();
        }
        return validated;
    }

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
        if (!m_state->generations.HasGeneration())
        {
            return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
        }
        auto validated = Detail::ValidateRegistration(registration, *m_state->generations.Component());
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
        return {m_state->phaseCalls, m_state->context.queryCalls, m_state->changes.Size()};
    }

    Scripting::ChangeSummary Scripting::ApplyChanges()
    {
        if (!m_state || !m_state->IsLive())
        {
            return {};
        }
        const auto result = m_state->changes.Apply(*m_state->context.app, m_state->components);
        return {result.applied, result.skipped};
    }

    void Scripting::DiscardChanges()
    {
        if (m_state && m_state->IsLive())
        {
            m_state->changes.Discard(m_state->context.app->GetActiveScene().GetEntityList());
        }
    }

    Iris::Component* Scripting::LoadedComponent() noexcept
    {
        return m_state != nullptr ? m_state->generations.Component() : nullptr;
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
        state->context.changes = &state->changes;
        if (!Detail::RegisterEngineComponents(state->components))
        {
            logger.Error("Scripting: the engine's own components could not be registered; running without scripts.");
            return Scripting(std::move(state));
        }
        for (auto& registration : config.components)
        {
            if (state->components.Add(std::move(registration)) < 0)
            {
                logger.Error(
                    "Scripting: a component in the config has an empty or duplicate name; running without scripts.");
                return Scripting(std::move(state));
            }
        }
        for (auto& registration : config.events)
        {
            if ((registration.bind && !registration.bind(app)) ||
                state->components.AddEvent(std::move(registration)) < 0)
            {
                logger.Error(
                    "Scripting: an event in the config has an empty or duplicate name; running without scripts.");
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

        state->scriptAssembly = assembly;
        // Each Install has a directory of its own, so one that is still waiting for a generation to unload never gets
        // in the way of the next.
        static std::atomic<int> installs{0};
        Detail::GenerationLifecycle::Settings settings;
        settings.assembly = assembly;
        settings.scratchRoot =
            (config.reloadDirectory.empty()
                 ? std::filesystem::temp_directory_path(error) / ("FenrirScripting-" + std::to_string(ProcessId()))
                 : std::filesystem::absolute(config.reloadDirectory, error)) /
            ("install-" + std::to_string(++installs));
        settings.hotReload = config.hotReload;
        settings.reloadInterval = config.reloadInterval;
        settings.reloadDebounce = config.reloadDebounce;
        settings.unloadTimeout = config.unloadTimeout;
        // The lifecycle knows nothing of what a generation holds: the State supplies that. It owns the lifecycle, so
        // the raw pointer the hooks keep lives as long as they do.
        State* const self = state.get();
        Detail::GenerationLifecycle::Hooks hooks;
        hooks.completeLoad = [self](const Iris::Component& loaded) {
            return Detail::CompleteLoad(self->context, self->components, loaded, self->ownedComponents,
                                        self->ownedEvents);
        };
        hooks.start = [self](const Iris::Component& loaded) { return self->StartSystems(loaded); };
        hooks.stop = [self] { self->StopSystems(); };
        hooks.beforeReload = [self] { (void)self->changes.Apply(*self->context.app, self->components); };
        const bool hotReload = settings.hotReload;
        const bool scratchChosen = !error && !settings.scratchRoot.empty();
        state->generations.Configure(state->host.get(), &logger, std::move(settings), std::move(hooks));

        // The watcher looks before anything else runs in a frame, so a reload happens between frames. The phases are
        // scheduled once; each runs the current generation's systems, if it has any in that phase.
        if (hotReload)
        {
            app.AddSequentialSystems(SchedulePriority::PreUpdate, {[state](App&) { state->PollForReload(); }});
        }
        constexpr SchedulePriority priorities[] = {SchedulePriority::PreUpdate, SchedulePriority::Tick,
                                                   SchedulePriority::Update, SchedulePriority::PostUpdate};
        for (int phase = 0; phase < 4; ++phase)
        {
            app.AddSequentialSystems(priorities[phase], {[state, phase](App&) { state->RunPhase(phase); }});
        }

        if (!scratchChosen)
        {
            logger.Error("Scripting: the folder for the script copies could not be chosen; running without scripts.");
            state->Shutdown();
            return Scripting(std::move(state));
        }
        if (const auto loaded = state->generations.Load(); !loaded.Succeeded())
        {
            logger.Error("Scripting: running without scripts. {}", loaded.diagnostic);
            // Nothing runs, so give back the runtime and the process-wide slot.
            state->Shutdown();
            return Scripting(std::move(state));
        }
        logger.Info("Scripting: loaded '{}'.", assembly.filename().string());
        return Scripting(std::move(state));
    }
} // namespace Fenrir
