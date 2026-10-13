#include "FenrirScripting/Scripting.hpp"

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"
#include "ScriptComponents.hpp"

#include <Iris/Iris.hpp>

#include <cassert>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
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
        Detail::ChangeBuffer changes;
        Detail::EngineContext context;
        bool contextActive = false;
        std::unique_ptr<Iris::Host> host;
        std::optional<Iris::Component> component;

        // The script's systems: one managed object (the generated SystemHost) the engine calls once per phase. Released
        // before the component, which cannot be released while an object it made is alive.
        std::optional<Iris::Object> runner;
        std::optional<Iris::InstanceMethod<void(std::int32_t)>> runnerRun;
        std::optional<Iris::InstanceMethod<void()>> runnerDestroy;
        std::thread::id mainThread;
        std::size_t phaseCalls = 0;

        // Makes the runner if the script assembly has systems, schedules one sequential native system for each phase that has
        // any, and runs OnCreate. Nothing to do (and success) when the script defines no systems.
        Iris::CallResult<void> StartSystems(App& app, const std::shared_ptr<State>& self)
        {
            using Result = Iris::CallResult<void>;
            const auto type = component->FindType("Fenrir.Scripting.Generated.SystemHost");
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
                    return Result{*found, "The script's system host is missing a method: " + mask.diagnostic + create.diagnostic +
                                              run.diagnostic + destroy.diagnostic,
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
            constexpr SchedulePriority priorities[] = {SchedulePriority::PreUpdate, SchedulePriority::Tick, SchedulePriority::Update,
                                                       SchedulePriority::PostUpdate};
            for (int phase = 0; phase < 4; ++phase)
            {
                if ((*phases.value & (1 << phase)) != 0)
                {
                    app.AddSequentialSystems(priorities[phase], {[self, phase](App&) { self->RunPhase(phase); }});
                }
            }
            const auto created = create.value->Invoke(*runner);
            if (!created.Succeeded())
            {
                return Result{created.status, "The script's systems could not start: " + created.diagnostic,
                              created.diagnosticTruncated};
            }
            return Result{Iris::CallStatus::Success, {}, false};
        }

        // One transition into managed code for the whole phase. Only ever on the main thread: Iris calls, script calls and
        // the internal calls they make all assume it.
        void RunPhase(int phase)
        {
            if (!runner.has_value() || !runnerRun.has_value())
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
            // The systems say goodbye while the runtime is still up, then let go of their object; then the script assembly, then
            // the host that loaded it.
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

    namespace
    {
        using Description = Detail::ScriptComponentDescription;

        Iris::CallResult<void> Succeeded()
        {
            return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
        }

        Iris::CallResult<void> Failed(std::string diagnostic)
        {
            return Iris::CallResult<void>{Iris::CallStatus::InvalidArgument, std::move(diagnostic), false};
        }

        // Runs the table the SDK's generator wrote into the script assembly, which reports each component type the script
        // defines (full name, size, alignment) through an internal call. A script assembly without the table defines none.
        Iris::CallResult<void> DiscoverScriptComponents(const Iris::Component& component, Detail::EngineContext& context,
                                                        std::vector<Description>& found)
        {
            const auto table = component.FindType("Fenrir.Scripting.Generated.ComponentTable");
            if (table.status == Iris::CallStatus::NotFound)
            {
                return Succeeded();
            }
            if (!table.Succeeded())
            {
                return Iris::CallResult<void>{table.status, "The component table could not be read: " + table.diagnostic, false};
            }
            const auto report = table.value->FindMethod<void()>("Register");
            if (!report.Succeeded())
            {
                return Iris::CallResult<void>{report.status, "The component table has no Register method: " + report.diagnostic, false};
            }
            context.discovering = &found;
            const auto ran = report.value->Invoke();
            context.discovering = nullptr;
            if (!ran.Succeeded())
            {
                return Iris::CallResult<void>{ran.status, "The script assembly could not report its components: " + ran.diagnostic,
                                              ran.diagnosticTruncated};
            }
            return Succeeded();
        }

        // Whether what the script reported can be registered next to what the engine already has.
        Iris::CallResult<void> CheckDiscovered(const std::vector<Description>& found, const Detail::ComponentRegistry& registry)
        {
            std::unordered_map<std::uint32_t, std::string> storageIds;
            std::unordered_set<std::string> names;
            for (const auto& description : found)
            {
                if (!names.insert(description.name).second)
                {
                    return Failed("The script reported the component '" + description.name + "' more than once.");
                }
                if (!Detail::ToDynamicType(description).IsValid())
                {
                    return Failed("The script component '" + description.name + "' reported a size of " +
                                  std::to_string(description.size) + " and an alignment of " +
                                  std::to_string(description.alignment) + ", which no C# struct has.");
                }
                if (registry.Find(description.name) >= 0)
                {
                    return Failed("The script defines a component '" + description.name +
                                  "', but the engine already has a component of that name.");
                }
                const auto [at, added] = storageIds.emplace(Detail::ToDynamicType(description).Id(), description.name);
                if (!added)
                {
                    return Failed("The script components '" + at->second + "' and '" + description.name +
                                  "' map to the same storage; rename one.");
                }
            }
            return Succeeded();
        }

        // Everything that must hold before a freshly loaded script assembly is kept, in the order the design gives. Nothing
        // is registered unless all of it passes, so a failure leaves the registry exactly as it was:
        //   1. discover the components the script defines, and check them;
        //   2. check every native component's mapping against its C# struct;
        //   3. only then register the script's components.
        // (Systems join between 1 and 2 in a later ticket.)
        Iris::CallResult<void> CompleteLoad(Detail::EngineContext& context, Detail::ComponentRegistry& registry,
                                            const Iris::Component& component)
        {
            std::vector<Description> found;
            if (auto discovered = DiscoverScriptComponents(component, context, found); !discovered.Succeeded())
            {
                return discovered;
            }
            if (auto checked = CheckDiscovered(found, registry); !checked.Succeeded())
            {
                return checked;
            }
            if (auto validated = ValidateAll(registry.All(), component); !validated.Succeeded())
            {
                return validated;
            }
            std::vector<ComponentRegistration> registrations;
            registrations.reserve(found.size());
            for (const auto& description : found)
            {
                registrations.push_back(Detail::MakeScriptComponent(description));
            }
            // All or none: the checks above make a refusal impossible, but if one happens nothing is half-registered.
            if (!registry.AddAll(std::move(registrations)))
            {
                return Failed("The script's components could not be registered together.");
            }
            return Succeeded();
        }
    } // namespace

    const ComponentRegistration* Scripting::FindComponent(std::string_view name) const
    {
        if (m_state == nullptr)
        {
            return nullptr;
        }
        return m_state->components.Get(m_state->components.Find(name));
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

    std::size_t Scripting::PhaseCallCount() const noexcept
    {
        return m_state == nullptr ? 0 : m_state->phaseCalls;
    }

    std::size_t Scripting::QueryCallCount() const noexcept
    {
        return m_state == nullptr ? 0 : m_state->context.queryCalls;
    }

    std::size_t Scripting::PendingChangeCount() const noexcept
    {
        return m_state == nullptr ? 0 : m_state->changes.Size();
    }

    Scripting::ChangeSummary Scripting::ApplyChanges()
    {
        if (m_state == nullptr || !m_state->contextActive || m_state->context.app == nullptr)
        {
            return {};
        }
        const auto result = m_state->changes.Apply(*m_state->context.app, m_state->components);
        return {result.applied, result.skipped};
    }

    void Scripting::DiscardChanges()
    {
        if (m_state != nullptr && m_state->contextActive && m_state->context.app != nullptr)
        {
            m_state->changes.Discard(m_state->context.app->GetActiveScene().GetEntityList());
        }
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
        // All or nothing: the assembly stays loaded only if its components are discovered and every native component
        // matches its C# struct.
        if (auto validated = CompleteLoad(state->context, state->components, *loaded.value); !validated.Succeeded())
        {
            logger.Error("Scripting: the script assembly '{}' was released. {}", assembly.string(), validated.diagnostic);
            state->Shutdown();
            return Scripting(std::move(state));
        }
        state->component.emplace(std::move(*loaded.value));
        if (auto started = state->StartSystems(app, state); !started.Succeeded())
        {
            logger.Error("Scripting: the script assembly '{}' was released. {}", assembly.string(), started.diagnostic);
            state->Shutdown();
            return Scripting(std::move(state));
        }
        logger.Info("Scripting: loaded '{}'.", assembly.filename().string());
        return Scripting(std::move(state));
    }
} // namespace Fenrir
