#include "FenrirScripting/Scripting.hpp"

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"
#include "GenerationLifecycle.hpp"
#include "ScriptComponents.hpp"
#include "ScriptData.hpp"

#include <Iris/Iris.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <typeindex>
#include <unordered_map>
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

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
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

        // A type's name as its source spells it, for a diagnostic. MSVC's type_info already gives it; the Itanium ABI
        // (GCC, and clang off Windows) gives the mangled name, which is demangled here.
        std::string ReadableName(const std::type_index& type)
        {
#if __has_include(<cxxabi.h>)
            int status = 0;
            const std::unique_ptr<char, decltype(&std::free)> demangled(
                abi::__cxa_demangle(type.name(), nullptr, nullptr, &status), &std::free);
            if (status == 0 && demangled != nullptr)
            {
                return demangled.get();
            }
#endif
            return type.name();
        }
    } // namespace

    namespace
    {
        // The lifecycle's loader over the Iris host: a loaded generation is an Iris component, and this is the one
        // place that knows it. Scripting's hooks reach the component through Of.
        class IrisComponentLoader final : public Detail::ComponentLoader
        {
          public:
            explicit IrisComponentLoader(Iris::Host& host) : m_host(host)
            {
            }

            Iris::CallResult<std::unique_ptr<Detail::LoadedComponent>> Load(const std::filesystem::path& path) override
            {
                auto loaded = m_host.LoadComponent(path);
                if (!loaded.Succeeded())
                {
                    return {loaded.status, std::nullopt, std::move(loaded.diagnostic), loaded.diagnosticTruncated};
                }
                return {Iris::CallStatus::Success, std::make_unique<Loaded>(std::move(*loaded.value)), {}, false};
            }

            // The Iris component behind a generation this loader loaded.
            [[nodiscard]] static Iris::Component& Of(Detail::LoadedComponent& loaded) noexcept
            {
                return static_cast<Loaded&>(loaded).component;
            }

          private:
            struct Loaded final : Detail::LoadedComponent
            {
                explicit Loaded(Iris::Component&& owner) : component(std::move(owner))
                {
                }

                Iris::CallResult<void> Release() override
                {
                    return component.Release();
                }

                Iris::CallResult<Iris::ComponentUnloadState> WaitForUnload(std::chrono::milliseconds timeout) override
                {
                    return component.WaitForUnload(timeout);
                }

                Iris::Component component;
            };

            Iris::Host& m_host;
        };
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
        // The built-in extension's state. While it is the active context, a Scripting is running for every extension's
        // native functions.
        std::shared_ptr<Detail::EngineContext> context = std::make_shared<Detail::EngineContext>();
        bool contextActive = false;
        // Every extension's extension state, kept alive as long as the native functions that receive it can be called.
        std::vector<std::shared_ptr<void>> extensionStates;

        // Scripting is live when the engine context points at this state and an App is attached: only then can scripts
        // act on the scene, and only then do changes and reloads mean anything.
        [[nodiscard]] bool IsLive() const noexcept
        {
            return contextActive && context->app != nullptr;
        }
        std::unique_ptr<Iris::Host> host;
        // What the lifecycle loads generations through: the host above. Declared before the lifecycle, which borrows
        // it.
        std::optional<IrisComponentLoader> loader;
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

        // The script data: what the generations define, registered in `components` above, and the data of it that is
        // alive in the ECS and the App.
        Detail::ScriptData scriptData{components};

        // Reloads now, if scripting is running; see GenerationLifecycle::Reload.
        Iris::CallResult<void> Reload();
        void PollForReload();

        // Stops the generation's systems before it is released: they say goodbye, and what they defined goes.
        void StopSystems();

        // Makes the generation's systems if the script assembly has any, and runs OnCreate. Nothing to do (and success)
        // when the script defines no systems. The phases themselves are scheduled once, at Install.
        Iris::CallResult<void> StartSystems(Detail::LoadedComponent& loaded)
        {
            using Result = Iris::CallResult<void>;
            const Iris::Component& component = IrisComponentLoader::Of(loaded);
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
        // and the native functions they call all assume it.
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
        scriptData.Forget();
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
        return {m_state->phaseCalls, m_state->context->queryCalls, m_state->changes.Size()};
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
        return loaded != nullptr ? &IrisComponentLoader::Of(*loaded) : nullptr;
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
        auto state = std::make_shared<State>();
        ILogger& logger = *app.Logger();

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

        // Why `function` cannot be listed by `extension`: it takes an extension state the extension does not carry, or
        // one of another type. Nothing for a stateless function.
        const auto checkState = [](const NativeFunction& function,
                                   const ScriptingExtension& extension) -> std::optional<StartupFailure> {
            const auto& wanted = function.StateType();
            const auto& carried = extension.m_stateType;
            if (!wanted.has_value() || wanted == carried)
            {
                return std::nullopt;
            }
            const std::string start = "the native function '" + function.Name() + "' of extension '" + extension.name +
                                      "' takes an extension state of type '" + ReadableName(*wanted) +
                                      "', but the extension ";
            return StartupFailure{carried.has_value() ? start + "carries one of type '" + ReadableName(*carried) + "'"
                                                      : start + "carries none",
                                  {}};
        };

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
                // Every extension's components, events, shared structs and resources, in order, by the same rules: the
                // registry refuses a name that is empty or that any of its tables already holds, whatever its kind.
                for (auto& extension : config.extensions)
                {
                    const std::string source = "extension '" + extension.name + "'";
                    auto addAll = [&](auto& registrations, auto& table,
                                      auto&& before) -> std::optional<StartupFailure> {
                        for (auto& registration : registrations)
                        {
                            if (auto failure = before(registration))
                            {
                                return failure;
                            }
                            if (table.Add(std::move(registration)) < 0)
                            {
                                const std::string noun(table.Noun());
                                const char* article =
                                    std::string_view("aeiou").find(noun[0]) == std::string_view::npos ? "a " : "an ";
                                return StartupFailure{
                                    article + noun + " in " + source + " has an empty or duplicate name", {}};
                            }
                        }
                        return std::nullopt;
                    };
                    auto nothing = [](const auto&) -> std::optional<StartupFailure> { return std::nullopt; };
                    // An event's queue is the App's: it is named before the event is registered.
                    auto bindQueue = [&](EventRegistration& registration) -> std::optional<StartupFailure> {
                        if (registration.bind && !registration.bind(app))
                        {
                            return StartupFailure{"the event '" + registration.name + "' in " + source +
                                                      " names a queue the app already has under another type",
                                                  {}};
                        }
                        return std::nullopt;
                    };
                    if (auto failure = addAll(extension.components, state->components.Components(), nothing))
                    {
                        return failure;
                    }
                    if (auto failure = addAll(extension.events, state->components.Events(), bindQueue))
                    {
                        return failure;
                    }
                    if (auto failure = addAll(extension.sharedStructs, state->components.SharedStructs(), nothing))
                    {
                        return failure;
                    }
                    if (auto failure = addAll(extension.resources, state->components.Resources(), nothing))
                    {
                        return failure;
                    }
                }
                return std::nullopt;
            },
            // Every extension's native functions are named Namespace.Type.Method, no name is declared twice, and each
            // one that takes an extension state is listed by an extension carrying a state of its type.
            [&]() -> std::optional<StartupFailure> {
                std::unordered_map<std::string, std::string> declaredBy;
                for (const auto& extension : config.extensions)
                {
                    for (const auto& function : extension.functions)
                    {
                        if (function.BindingName().empty())
                        {
                            return StartupFailure{"the native function '" + function.Name() + "' of extension '" +
                                                      extension.name + "' is not named Namespace.Type.Method",
                                                  {}};
                        }
                        if (auto failure = checkState(function, extension))
                        {
                            return failure;
                        }
                        const auto [first, added] = declaredBy.emplace(function.Name(), extension.name);
                        if (!added)
                        {
                            return StartupFailure{first->second == extension.name
                                                      ? "the native function '" + function.Name() +
                                                            "' is declared twice by extension '" + extension.name + "'"
                                                      : "the native function '" + function.Name() +
                                                            "' is declared by both extension '" + first->second +
                                                            "' and extension '" + extension.name + "'",
                                                  {}};
                        }
                    }
                }
                return std::nullopt;
            },
            // Only one Scripting is active in a process, and the native functions are registered before .NET starts.
            [&]() -> std::optional<StartupFailure> {
                if (!Detail::ActivateEngineContext(state->context.get()))
                {
                    return StartupFailure{"another Scripting is already active in this process", {}};
                }
                state->contextActive = true;
                // Before the host shares the extensions' contracts: sharing binds their fields to these.
                for (const auto& extension : config.extensions)
                {
                    if (extension.m_state != nullptr)
                    {
                        state->extensionStates.push_back(extension.m_state);
                    }
                    for (const auto& function : extension.functions)
                    {
                        // A stateless function ignores the state; checkState made sure a stateful one gets its type.
                        if (const auto registered = function.Register(extension.m_state.get()); !registered.Succeeded())
                        {
                            return StartupFailure{"the native function '" + function.Name() + "' of extension '" +
                                                      extension.name + "' could not be registered",
                                                  registered.diagnostic};
                        }
                    }
                }
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
                for (const auto& extension : config.extensions)
                {
                    for (const auto& assembly : extension.sharedAssemblies)
                    {
                        configuration.sharedAssemblies.push_back(bridgeDirectory / assembly);
                    }
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
                hooks.reconcile = [self](Detail::LoadedComponent& loaded) {
                    Detail::IrisScriptAssembly assembly(*self->context, IrisComponentLoader::Of(loaded));
                    return self->scriptData.Admit(*self->context->app, assembly);
                };
                hooks.start = [self](Detail::LoadedComponent& loaded) { return self->StartSystems(loaded); };
                hooks.stop = [self] { self->StopSystems(); };
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
                    app.AddSequentialSystems(priorities[phase], {[state, phase](App&) { state->RunPhase(phase); }});
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
