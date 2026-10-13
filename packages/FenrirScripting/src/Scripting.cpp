#include "FenrirScripting/Scripting.hpp"

#include "ComponentRegistry.hpp"
#include "EngineContext.hpp"
#include "ScriptComponents.hpp"

#include <Iris/Iris.hpp>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
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
        // A directory holding one generation's private copy of the script assembly. It is deleted when the last owner
        // lets go: the generation loaded from it, a released generation that has not unloaded yet (Windows will not
        // delete a loaded assembly), or the "previous" slot that keeps it in case the next build fails to load.
        struct CopyDirectory
        {
            explicit CopyDirectory(std::filesystem::path path) : directory(std::move(path))
            {
            }
            ~CopyDirectory()
            {
                std::error_code ignored;
                std::filesystem::remove_all(directory, ignored);
            }
            CopyDirectory(const CopyDirectory&) = delete;
            CopyDirectory& operator=(const CopyDirectory&) = delete;

            std::filesystem::path directory;
        };

        // What a script component or event looked like when its data was last in use, so a reload can tell whether the
        // data still means the same thing: the size and alignment, and each field's name, type, kind and offset.
        struct OwnedLayout
        {
            std::string fingerprint;
            std::size_t size = 0;
            std::size_t alignment = 0;
        };
        using OwnedLayouts = std::map<std::string, OwnedLayout>;

        struct FileStamp
        {
            std::filesystem::file_time_type modified{};
            std::uintmax_t size = 0;
            bool operator==(const FileStamp&) const = default;
        };

        std::optional<FileStamp> StampOf(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto modified = std::filesystem::last_write_time(path, error);
            if (error)
            {
                return std::nullopt;
            }
            const auto size = std::filesystem::file_size(path, error);
            if (error)
            {
                return std::nullopt;
            }
            return FileStamp{modified, size};
        }

        // Whether the file can be opened for reading right now: a build still writing it, or a virus scanner holding
        // it, says no.
        bool CanBeRead(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            return stream.good();
        }

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
        std::unique_ptr<Iris::Host> host;
        std::optional<Iris::Component> component;

        // The script's systems: one managed object (the generated SystemHost) the engine calls once per phase. Released
        // before the component, which cannot be released while an object it made is alive.
        std::optional<Iris::Object> runner;
        std::optional<Iris::InstanceMethod<void(std::int32_t)>> runnerRun;
        std::optional<Iris::InstanceMethod<void()>> runnerDestroy;
        std::thread::id mainThread;
        std::size_t phaseCalls = 0;
        int phaseMask = 0; // Bit n is set when the loaded generation has a system in phase n.

        // Hot reload. The assembly named by the config is only ever copied; the copy is what loads.
        std::filesystem::path scriptAssembly;
        std::filesystem::path scratchRoot;
        bool hotReload = true;
        std::chrono::milliseconds reloadInterval{250};
        std::chrono::milliseconds reloadDebounce{400};
        std::chrono::milliseconds unloadTimeout{2000};
        int generation = 0;
        int copyCounter = 0;
        std::shared_ptr<CopyDirectory> activeCopy;
        std::shared_ptr<CopyDirectory> previousCopy; // Kept until the next generation loads, to restore if it does not.

        // A released generation waits here until the runtime confirms it was collected.
        struct Retired
        {
            Iris::Component component;
            std::shared_ptr<CopyDirectory> copy;
            int generation = 0;
            bool warned = false;
        };
        std::vector<Retired> retired;

        // The script components and events whose data is alive in the ECS and the App, by name.
        OwnedLayouts ownedComponents;
        OwnedLayouts ownedEvents;

        std::optional<FileStamp> loadedStamp;
        struct PendingChange
        {
            FileStamp stamp;
            std::chrono::steady_clock::time_point since;
        };
        std::optional<PendingChange> pendingChange;
        std::chrono::steady_clock::time_point lastPoll{};
        bool reloading = false;

        // Loads the script assembly as a new generation: a private copy of `assembly` (or the directory `existing` when
        // restoring a previous copy), then the all-or-nothing checks, then the systems. On failure nothing of the new
        // generation is left running.
        Iris::CallResult<void> LoadGeneration(const std::filesystem::path& assembly,
                                              std::shared_ptr<CopyDirectory> existing);

        // Says goodbye to the systems and releases the loaded generation. Its data (component storage, event queues)
        // stays.
        void UnloadGeneration();

        // Waits for released generations to unload, deleting their copies as they do.
        void SweepRetired(std::chrono::milliseconds timeout);

        // Removes the data of every script component and event: they exist only while a script assembly is loaded.
        void DropScriptData();

        Iris::CallResult<void> Reload();
        void PollForReload();

        void Retire(Iris::Component&& released, std::shared_ptr<CopyDirectory> copy)
        {
            (void)released.Release();
            retired.push_back({std::move(released), std::move(copy), generation, false});
        }

        // Makes the generation's systems if the script assembly has any, and runs OnCreate. Nothing to do (and success)
        // when the script defines no systems. The phases themselves are scheduled once, at Install.
        Iris::CallResult<void> StartSystems()
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
                if (component.has_value() || runner.has_value())
                {
                    UnloadGeneration();
                }
                if (contextActive && context.app != nullptr)
                {
                    DropScriptData();
                }
                if (host != nullptr && !retired.empty())
                {
                    SweepRetired(unloadTimeout);
                }
            }
            catch (...)
            {
            }
            retired.clear();
            previousCopy.reset();
            activeCopy.reset();
            if (!scratchRoot.empty())
            {
                std::error_code ignored;
                std::filesystem::remove(scratchRoot,
                                        ignored); // Only succeeds when it is empty: leaves other work alone.
            }
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
        Iris::CallResult<void> ValidateRegistration(const ComponentRegistration& registration,
                                                    const Iris::Component& component)
        {
            if (!registration.validate)
            {
                return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
            }
            auto validated = registration.validate(component);
            if (!validated.Succeeded())
            {
                validated.diagnostic =
                    "Component '" + registration.name + "' does not match its C# struct: " + validated.diagnostic;
            }
            return validated;
        }

        Iris::CallResult<void> ValidateAll(const std::vector<ComponentRegistration>& registrations,
                                           const Iris::Component& component)
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

        // Runs the table the SDK's generator wrote into the script assembly, which reports each component type the
        // script defines (full name, size, alignment) through an internal call. A script assembly without the table
        // defines none.
        Iris::CallResult<void> DiscoverScriptComponents(const Iris::Component& component,
                                                        Detail::EngineContext& context, std::vector<Description>& found)
        {
            const auto table = component.FindType("Fenrir.Scripting.Generated.ComponentTable");
            if (table.status == Iris::CallStatus::NotFound)
            {
                return Succeeded();
            }
            if (!table.Succeeded())
            {
                return Iris::CallResult<void>{table.status,
                                              "The component table could not be read: " + table.diagnostic, false};
            }
            const auto report = table.value->FindMethod<void()>("Register");
            if (!report.Succeeded())
            {
                return Iris::CallResult<void>{
                    report.status, "The component table has no Register method: " + report.diagnostic, false};
            }
            context.discovering = &found;
            const auto ran = report.value->Invoke();
            context.discovering = nullptr;
            if (!ran.Succeeded())
            {
                return Iris::CallResult<void>{ran.status,
                                              "The script assembly could not report its components: " + ran.diagnostic,
                                              ran.diagnosticTruncated};
            }
            return Succeeded();
        }

        // Whether what the script reported can be registered next to what the engine already has.
        Iris::CallResult<void> CheckDiscovered(const std::vector<Description>& found,
                                               const Detail::ComponentRegistry& registry)
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

        // Runs the table the generator wrote for the events the script defines (full name, size, alignment each).
        Iris::CallResult<void> DiscoverScriptEvents(const Iris::Component& component, Detail::EngineContext& context,
                                                    std::vector<Detail::ScriptEventDescription>& found)
        {
            const auto table = component.FindType("Fenrir.Scripting.Generated.EventTable");
            if (table.status == Iris::CallStatus::NotFound)
            {
                return Succeeded();
            }
            if (!table.Succeeded())
            {
                return Iris::CallResult<void>{table.status, "The event table could not be read: " + table.diagnostic,
                                              false};
            }
            const auto report = table.value->FindMethod<void()>("Register");
            if (!report.Succeeded())
            {
                return Iris::CallResult<void>{report.status,
                                              "The event table has no Register method: " + report.diagnostic, false};
            }
            context.discoveringEvents = &found;
            const auto ran = report.value->Invoke();
            context.discoveringEvents = nullptr;
            if (!ran.Succeeded())
            {
                return Iris::CallResult<void>{ran.status,
                                              "The script assembly could not report its events: " + ran.diagnostic,
                                              ran.diagnosticTruncated};
            }
            return Succeeded();
        }

        // Whether the events a script reported can live next to the engine's: usable layouts, and names nobody has.
        Iris::CallResult<void> CheckDiscoveredEvents(const std::vector<Detail::ScriptEventDescription>& found,
                                                     const Detail::ComponentRegistry& registry, App& app,
                                                     const OwnedLayouts& owned)
        {
            constexpr std::size_t maxAlignment = 16;
            std::unordered_set<std::string> names;
            for (const auto& description : found)
            {
                if (!names.insert(description.name).second)
                {
                    return Failed("The script reported the event '" + description.name + "' more than once.");
                }
                const bool usable = description.size > 0 && description.alignment > 0 &&
                                    description.alignment <= maxAlignment &&
                                    (description.alignment & (description.alignment - 1)) == 0 &&
                                    description.size % description.alignment == 0;
                if (!usable)
                {
                    return Failed("The script event '" + description.name + "' reported a size of " +
                                  std::to_string(description.size) + " and an alignment of " +
                                  std::to_string(description.alignment) + ", which the engine cannot store.");
                }
                if (registry.FindEvent(description.name) >= 0 ||
                    (app.FindEventQueue(description.name) != nullptr && !owned.contains(description.name)))
                {
                    return Failed("The script defines an event '" + description.name +
                                  "', but the engine already has an event of that name.");
                }
            }
            return Succeeded();
        }

        // Checks every registered native event against its C# struct.
        Iris::CallResult<void> ValidateAllEvents(const std::vector<EventRegistration>& registrations,
                                                 const Iris::Component& component)
        {
            for (const auto& registration : registrations)
            {
                if (!registration.validate)
                {
                    continue;
                }
                if (auto validated = registration.validate(component); !validated.Succeeded())
                {
                    validated.diagnostic =
                        "Event '" + registration.name + "' does not match its C# struct: " + validated.diagnostic;
                    return validated;
                }
            }
            return Succeeded();
        }

        // Size and alignment, and each field's name, type, kind and offset: two builds of a struct with the same
        // fingerprint lay the same bytes out the same way. A type the assembly does not expose (internal) can only be
        // told apart by its size.
        std::string Fingerprint(const Iris::Component& component, const std::string& name, std::size_t size,
                                std::size_t alignment)
        {
            std::string text = std::to_string(size) + "/" + std::to_string(alignment);
            const auto type = component.FindType(name);
            if (!type.Succeeded())
            {
                return text + ";opaque";
            }
            const auto fields = type.value->GetFields();
            if (!fields.Succeeded())
            {
                return text + ";opaque";
            }
            for (const auto& field : *fields.value)
            {
                if (!field.isStatic)
                {
                    text += ";" + field.name + ":" + field.type.fullName + ":" +
                            std::to_string(static_cast<int>(field.kind)) + "@" +
                            (field.offset.has_value() ? std::to_string(*field.offset) : std::string("?"));
                }
            }
            return text;
        }

        // Everything that must hold before a freshly loaded script assembly is kept, in the order the design gives.
        // Nothing is registered unless all of it passes, so a failure leaves the registry exactly as it was:
        //   1. discover the components the script defines, and check them;
        //   2. check every native component's mapping against its C# struct;
        //   3. only then register the script's components.
        Iris::CallResult<void> CompleteLoad(Detail::EngineContext& context, Detail::ComponentRegistry& registry,
                                            const Iris::Component& component, OwnedLayouts& ownedComponents,
                                            OwnedLayouts& ownedEvents)
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
            std::vector<Detail::ScriptEventDescription> foundEvents;
            if (auto discovered = DiscoverScriptEvents(component, context, foundEvents); !discovered.Succeeded())
            {
                return discovered;
            }
            if (auto checked = CheckDiscoveredEvents(foundEvents, registry, *context.app, ownedEvents);
                !checked.Succeeded())
            {
                return checked;
            }
            if (auto validated = ValidateAll(registry.All(), component); !validated.Succeeded())
            {
                return validated;
            }
            if (auto validated = ValidateAllEvents(registry.Events(), component); !validated.Succeeded())
            {
                return validated;
            }
            // Everything checks out, so from here the new generation is kept. What the previous generation left in the
            // ECS and the App is compared with what this one defines: data whose layout is unchanged stays, anything
            // else goes, with a warning, so no system ever reads old bytes as a new layout.
            App& app = *context.app;
            OwnedLayouts nextComponents;
            for (const auto& description : found)
            {
                nextComponents[description.name] = {
                    Fingerprint(component, description.name, description.size, description.alignment), description.size,
                    description.alignment};
            }
            OwnedLayouts nextEvents;
            for (const auto& description : foundEvents)
            {
                nextEvents[description.name] = {
                    Fingerprint(component, description.name, description.size, description.alignment), description.size,
                    description.alignment};
            }
            for (auto at = ownedComponents.begin(); at != ownedComponents.end();)
            {
                const auto next = nextComponents.find(at->first);
                if (next != nextComponents.end() && next->second.fingerprint == at->second.fingerprint)
                {
                    ++at;
                    continue;
                }
                const DynamicComponentType type(at->first, at->second.size, at->second.alignment);
                std::size_t removed = 0;
                app.ForEachScene([&](Scene& scene) { removed += scene.GetEntityList().ClearDynamic(type); });
                app.Logger()->Warn(
                    "Scripting: the script component '{}' {}; {} component(s) were removed so nothing reads the old "
                    "bytes as the new layout.",
                    at->first, next == nextComponents.end() ? "no longer exists" : "changed its layout", removed);
                at = ownedComponents.erase(at);
            }
            for (auto at = ownedEvents.begin(); at != ownedEvents.end();)
            {
                const auto next = nextEvents.find(at->first);
                if (next != nextEvents.end() && next->second.fingerprint == at->second.fingerprint)
                {
                    ++at;
                    continue;
                }
                (void)app.RemoveEventQueue(at->first);
                app.Logger()->Warn("Scripting: the script event '{}' {}; its queue and the events in it were dropped.",
                                   at->first, next == nextEvents.end() ? "no longer exists" : "changed its layout");
                at = ownedEvents.erase(at);
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
            // Likewise for events: the checks above make a refusal impossible.
            std::vector<EventRegistration> eventRegistrations;
            for (const auto& description : foundEvents)
            {
                EventRegistration registration;
                registration.name = description.name;
                registration.typeName = description.name;
                registration.size = description.size;
                registration.alignment = description.alignment;
                registration.scriptDefined = true;
                eventRegistrations.push_back(std::move(registration));
            }
            if (!registry.AddEvents(std::move(eventRegistrations)))
            {
                return Failed("The script's events could not be registered together.");
            }
            for (const auto& description : foundEvents)
            {
                // A queue whose layout is unchanged is kept, with the events in it.
                if (!ownedEvents.contains(description.name))
                {
                    if (app.CreateEventQueue(description.name, description.size, description.alignment) == nullptr)
                    {
                        return Failed("The queue for the script event '" + description.name + "' could not be made.");
                    }
                    // Recorded as soon as it exists, so a failure further on never leaves a queue nobody owns.
                    ownedEvents[description.name] = nextEvents[description.name];
                }
            }
            for (auto& [name, layout] : nextComponents)
            {
                ownedComponents[name] = std::move(layout);
            }
            for (auto& [name, layout] : nextEvents)
            {
                ownedEvents[name] = std::move(layout);
            }
            return Succeeded();
        }
    } // namespace

    namespace
    {
        // Copies the script assembly and what it needs into a new directory under `root`: the files named like it (its
        // pdb and deps.json), and every dll its deps.json lists that sits beside it. A build can then overwrite the
        // original while the copy is loaded. Returns null and says why in `error` if it could not.
        std::shared_ptr<CopyDirectory> MakeCopy(const std::filesystem::path& assembly,
                                                const std::filesystem::path& root, int number, std::string& error)
        {
            std::error_code code;
            const auto directory = root / ("gen-" + std::to_string(number));
            std::filesystem::remove_all(directory, code);
            std::filesystem::create_directories(directory, code);
            if (code)
            {
                error = "The directory '" + directory.string() + "' could not be made: " + code.message();
                return nullptr;
            }
            auto copy = std::make_shared<CopyDirectory>(directory);
            const auto source = assembly.parent_path();
            // Native strings throughout: a file beside the assembly may have a name the narrow code page cannot hold.
            const std::filesystem::path::string_type prefix =
                assembly.stem().native() + std::filesystem::path::string_type(1, '.');

            std::vector<std::filesystem::path> files{assembly};
            for (const auto& entry : std::filesystem::directory_iterator(source, code))
            {
                const auto& name = entry.path().filename().native();
                if (entry.is_regular_file(code) && entry.path() != assembly && name.rfind(prefix, 0) == 0)
                {
                    files.push_back(entry.path());
                }
            }
            auto dependencies = source / assembly.stem();
            dependencies += ".deps.json";
            if (std::ifstream stream(dependencies); stream.good())
            {
                const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
                static const std::regex dll(R"(([A-Za-z0-9_.+\-]+\.dll)\")");
                for (std::sregex_iterator found(text.begin(), text.end(), dll), end; found != end; ++found)
                {
                    const auto path = source / (*found)[1].str();
                    if (std::filesystem::is_regular_file(path, code) &&
                        std::find(files.begin(), files.end(), path) == files.end())
                    {
                        files.push_back(path);
                    }
                }
            }
            for (const auto& file : files)
            {
                std::filesystem::copy_file(file, directory / file.filename(),
                                           std::filesystem::copy_options::overwrite_existing, code);
                if (code)
                {
                    error = "The file '" + file.string() + "' could not be copied: " + code.message();
                    return nullptr;
                }
            }
            return copy;
        }
    } // namespace

    Iris::CallResult<void> Scripting::State::LoadGeneration(const std::filesystem::path& assembly,
                                                            std::shared_ptr<CopyDirectory> existing)
    {
        using Result = Iris::CallResult<void>;
        std::shared_ptr<CopyDirectory> copy = std::move(existing);
        if (copy == nullptr)
        {
            std::string error;
            copy = MakeCopy(assembly, scratchRoot, ++copyCounter, error);
            if (copy == nullptr)
            {
                return Result{
                    Iris::CallStatus::InvalidArgument,
                    "The script assembly '" + assembly.string() + "' could not be copied for loading. " + error, false};
            }
        }
        auto loaded = host->LoadComponent(copy->directory / assembly.filename());
        if (!loaded.Succeeded())
        {
            return Result{loaded.status,
                          "The script assembly '" + assembly.string() + "' could not be loaded: " + loaded.diagnostic,
                          loaded.diagnosticTruncated};
        }
        // All or nothing: the assembly stays loaded only if its components and events are discovered and every native
        // component and event matches its C# struct.
        if (auto validated = CompleteLoad(context, components, *loaded.value, ownedComponents, ownedEvents);
            !validated.Succeeded())
        {
            Retire(std::move(*loaded.value), copy);
            validated.diagnostic =
                "The script assembly '" + assembly.string() + "' was released. " + validated.diagnostic;
            return validated;
        }
        component.emplace(std::move(*loaded.value));
        activeCopy = copy;
        ++generation;
        if (auto started = StartSystems(); !started.Succeeded())
        {
            started.diagnostic = "The script assembly '" + assembly.string() + "' was released. " + started.diagnostic;
            UnloadGeneration();
            return started;
        }
        return Result{Iris::CallStatus::Success, {}, false};
    }

    void Scripting::State::UnloadGeneration()
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
        if (component.has_value())
        {
            Iris::Component released = std::move(*component);
            component.reset();
            Retire(std::move(released), activeCopy);
            previousCopy = activeCopy;
            activeCopy.reset();
        }
    }

    void Scripting::State::SweepRetired(std::chrono::milliseconds timeout)
    {
        for (auto at = retired.begin(); at != retired.end();)
        {
            const auto unloaded = at->component.WaitForUnload(timeout);
            if (unloaded.Succeeded() && *unloaded.value == Iris::ComponentUnloadState::Collected)
            {
                at = retired.erase(at);
                continue;
            }
            if (!at->warned && logger != nullptr)
            {
                logger->Warn(
                    "Scripting: generation {} of the script assembly has not unloaded; something still holds a type "
                    "from "
                    "it (a thread it started, or a static that keeps one). Its copy stays on disk until it does.",
                    at->generation);
            }
            at->warned = true;
            ++at;
        }
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
        using Result = Iris::CallResult<void>;
        if (reloading || host == nullptr || !contextActive || context.app == nullptr || scriptAssembly.empty())
        {
            return Result{Iris::CallStatus::NotInitialized,
                          "Scripting is not running a script assembly that can be reloaded.", false};
        }
        reloading = true;
        struct Done
        {
            bool& flag;
            ~Done()
            {
                flag = false;
            }
        } done{reloading};

        App& app = *context.app;
        logger->Info("Scripting: reloading '{}'.", scriptAssembly.filename().string());
        // Anything a script left recorded is played back first, so no entity is left half made.
        (void)changes.Apply(app, components);
        loadedStamp = StampOf(scriptAssembly);
        pendingChange.reset();

        UnloadGeneration();
        // What to go back to if the new build does not load. Taken now: a generation that loads and then fails to start
        // unloads itself, and would otherwise replace it with its own copy.
        const std::shared_ptr<CopyDirectory> restoreCopy = previousCopy;
        SweepRetired(unloadTimeout);
        const auto fresh = LoadGeneration(scriptAssembly, nullptr);
        if (fresh.Succeeded())
        {
            previousCopy.reset();
            logger->Info("Scripting: reloaded '{}' (generation {}).", scriptAssembly.filename().string(), generation);
            return fresh;
        }
        logger->Error("Scripting: the new build of '{}' did not load. {}", scriptAssembly.filename().string(),
                      fresh.diagnostic);
        if (restoreCopy == nullptr)
        {
            return fresh;
        }
        auto restored = LoadGeneration(scriptAssembly, restoreCopy);
        if (restored.Succeeded())
        {
            logger->Warn("Scripting: the previous build of '{}' is running again.", scriptAssembly.filename().string());
        }
        else
        {
            logger->Error("Scripting: the previous build could not be restored either; no scripts are running. {}",
                          restored.diagnostic);
        }
        return fresh;
    }

    void Scripting::State::PollForReload()
    {
        if (!hotReload || reloading || host == nullptr || !contextActive)
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - lastPoll < reloadInterval)
        {
            return;
        }
        lastPoll = now;
        const auto stamp = StampOf(scriptAssembly);
        if (!stamp.has_value() || stamp == loadedStamp)
        {
            pendingChange.reset();
            return;
        }
        // A build is still going if the file changes between looks, so only a stable one is loaded.
        if (!pendingChange.has_value() || !(pendingChange->stamp == *stamp))
        {
            pendingChange = PendingChange{*stamp, now};
            return;
        }
        if (now - pendingChange->since < reloadDebounce || !CanBeRead(scriptAssembly))
        {
            return;
        }
        try
        {
            (void)Reload();
        }
        catch (const std::exception& exception)
        {
            reloading = false;
            if (logger != nullptr)
            {
                logger->Error("Scripting: reloading failed: {}", exception.what());
            }
        }
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
        if (!m_state->component.has_value() || !validate)
        {
            return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
        }
        auto validated = validate(*m_state->component);
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
        return m_state == nullptr ? 0 : m_state->generation;
    }

    std::size_t Scripting::PendingUnloadCount() const noexcept
    {
        return m_state == nullptr ? 0 : m_state->retired.size();
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
        state->hotReload = config.hotReload;
        state->reloadInterval = config.reloadInterval;
        state->reloadDebounce = config.reloadDebounce;
        state->unloadTimeout = config.unloadTimeout;
        // Each Install has a directory of its own, so one that is still waiting for a generation to unload never gets
        // in the way of the next.
        static std::atomic<int> installs{0};
        state->scratchRoot =
            (config.reloadDirectory.empty()
                 ? std::filesystem::temp_directory_path(error) / ("FenrirScripting-" + std::to_string(ProcessId()))
                 : std::filesystem::absolute(config.reloadDirectory, error)) /
            ("install-" + std::to_string(++installs));

        // The watcher looks before anything else runs in a frame, so a reload happens between frames. The phases are
        // scheduled once; each runs the current generation's systems, if it has any in that phase.
        if (state->hotReload)
        {
            app.AddSequentialSystems(SchedulePriority::PreUpdate, {[state](App&) { state->PollForReload(); }});
        }
        constexpr SchedulePriority priorities[] = {SchedulePriority::PreUpdate, SchedulePriority::Tick,
                                                   SchedulePriority::Update, SchedulePriority::PostUpdate};
        for (int phase = 0; phase < 4; ++phase)
        {
            app.AddSequentialSystems(priorities[phase], {[state, phase](App&) { state->RunPhase(phase); }});
        }

        if (error || state->scratchRoot.empty())
        {
            logger.Error("Scripting: the folder for the script copies could not be chosen; running without scripts.");
            state->Shutdown();
            return Scripting(std::move(state));
        }
        if (const auto loaded = state->LoadGeneration(assembly, nullptr); !loaded.Succeeded())
        {
            logger.Error("Scripting: running without scripts. {}", loaded.diagnostic);
            // Nothing runs, so give back the runtime and the process-wide slot.
            state->Shutdown();
            return Scripting(std::move(state));
        }
        state->loadedStamp = StampOf(assembly);
        logger.Info("Scripting: loaded '{}'.", assembly.filename().string());
        return Scripting(std::move(state));
    }
} // namespace Fenrir
