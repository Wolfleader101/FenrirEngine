// Component ownership checks against the real bridge and the published example component.
#include "FenrirDotNetHost/DotNetHost.hpp"
#include "TestHarness.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
    using namespace Fenrir::Scripting;
    using Fenrir::Scripting::Tests::Check;

    constexpr std::string_view CalculationType = "Fenrir.HostingComponent.Calculations";

    static_assert(!std::is_copy_constructible_v<ManagedComponent> &&
                  std::is_nothrow_move_constructible_v<ManagedComponent>);

    struct Arguments
    {
        HostConfiguration configuration;
        std::filesystem::path component;
    };

    std::unique_ptr<DotNetHost> StartHost(const Arguments& arguments)
    {
        auto host = std::make_unique<DotNetHost>();
        const auto initialized = host->Initialize(arguments.configuration);
        Check(initialized.success, "Initialization failed: " + initialized.diagnostic);
        return host;
    }

    ManagedComponent Load(const DotNetHost& host, const std::filesystem::path& path)
    {
        auto loaded = host.LoadComponent(path);
        Check(loaded.Succeeded() && loaded.value->IsLoaded(), "Component load failed: " + loaded.diagnostic);
        return std::move(*loaded.value);
    }

    void CheckInfo(const ManagedComponent& owner)
    {
        const auto info = owner.GetInfo();
        Check(info.Succeeded() && info.value->assemblyName == "Fenrir.HostingComponent",
              "Component identity incorrect");
    }

    void CheckCalculation(const ManagedComponent& owner)
    {
        const auto result = owner.InvokeInt32(CalculationType, "Add", 20, 22);
        Check(result.Succeeded() && result.value == 42 && result.diagnostic.empty() && !result.diagnosticTruncated,
              "Component calculation failed: " + result.diagnostic);
    }

    void CheckUnload(const ManagedComponent& owner, bool released)
    {
        const auto result = owner.GetUnloadStatus();
        Check(result.Succeeded() && result.diagnostic.empty() && !result.diagnosticTruncated &&
                  (released ? (*result.value == ComponentUnloadState::Pending ||
                               *result.value == ComponentUnloadState::Collected)
                            : *result.value == ComponentUnloadState::NotRequested),
              "Unload observation did not match the owner's lifecycle");
    }

    void CheckInvalidHandle(const ManagedComponent& owner, const char* message)
    {
        Check(owner.GetInfo().status == ManagedCallStatus::InvalidHandle &&
                  owner.InvokeInt32(CalculationType, "LocalAdd", 20, 22).status == ManagedCallStatus::InvalidHandle,
              message);
    }

    int Run(const Arguments& arguments)
    {
        return Tests::RunCases({
            {"a loaded component reports its identity and NotRequested",
             [&] {
                 DotNetHost unready;
                 Check(unready.LoadComponent(arguments.component).status == ManagedCallStatus::NotInitialized,
                       "An uninitialized host loaded a component");
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 CheckInfo(component);
                 CheckUnload(component, false);
                 CheckCalculation(component);
             }},
            {"lookup and component exceptions are contained, and the next call works",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 for (const auto method : {"Missing", "WrongReturn", "WrongParameters", "Generic", "UnmanagedOnly"})
                 {
                     const auto failed = component.InvokeInt32(CalculationType, method, 20, 22);
                     Check(failed.status == ManagedCallStatus::ManagedException && !failed.value &&
                               failed.diagnostic.find("MissingMethodException") != std::string::npos,
                           std::string("Lookup failure escaped or published a value: ") + method);
                 }
                 const auto thrown = component.InvokeInt32(CalculationType, "Throw", 20, 22);
                 Check(thrown.status == ManagedCallStatus::ManagedException && !thrown.value &&
                           thrown.diagnostic.find("InvalidOperationException") != std::string::npos &&
                           thrown.diagnostic.find("\xCF\x80 \xF0\x9F\x90\xBA") != std::string::npos,
                       "Component exception details or Unicode lost");
                 const auto argumentThrow = component.InvokeInt32(CalculationType, "ThrowArgument", 20, 22);
                 Check(argumentThrow.status == ManagedCallStatus::ManagedException && !argumentThrow.value,
                       "A component's ArgumentException was reported as an invalid bridge argument");
                 const auto overflow =
                     component.InvokeInt32(CalculationType, "Add", std::numeric_limits<std::int32_t>::max(), 1);
                 Check(overflow.status == ManagedCallStatus::Overflow && !overflow.value &&
                           overflow.diagnostic.find("OverflowException") != std::string::npos,
                       "Component overflow contract failed");
                 const auto bounded = component.InvokeInt32(CalculationType, "LongError", 0, 0);
                 Check(bounded.status == ManagedCallStatus::ManagedException && !bounded.value &&
                           bounded.diagnosticTruncated,
                       "Component diagnostic was not bounded");
                 CheckCalculation(component);
             }},
            {"malformed, empty and null-containing names are rejected",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto malformedType = component.InvokeInt32("\xC3(", "Add", 20, 22);
                 const auto malformedMethod = component.InvokeInt32(CalculationType, "\xFF", 20, 22);
                 Check(malformedType.status == ManagedCallStatus::InvalidArgument && !malformedType.value &&
                           malformedType.diagnostic.find("DecoderFallbackException") != std::string::npos &&
                           malformedMethod.status == ManagedCallStatus::InvalidArgument && !malformedMethod.value,
                       "Malformed UTF-8 names accepted");
                 Check(component.InvokeInt32({}, "Add", 20, 22).status == ManagedCallStatus::InvalidArgument &&
                           component.InvokeInt32(CalculationType, {}, 20, 22).status ==
                               ManagedCallStatus::InvalidArgument &&
                           component.InvokeInt32(CalculationType, std::string("Add\0suffix", 10), 20, 22).status ==
                               ManagedCallStatus::InvalidArgument,
                       "Empty or null-containing names accepted");
                 CheckCalculation(component);
             }},
            {"load failures return diagnostics and leave the host ready",
             [&] {
                 auto host = StartHost(arguments);
                 auto missingPath = arguments.component;
                 missingPath += ".missing";
                 const auto missing = host->LoadComponent(missingPath);
                 Check(missing.status == ManagedCallStatus::ManagedException && !missing.value &&
                           missing.diagnostic.find("FileNotFoundException") != std::string::npos,
                       "Missing assembly diagnostic lost");
                 Check(host->LoadComponent({}).status == ManagedCallStatus::InvalidArgument, "Empty path accepted");
                 // An existing JSON file, not a managed assembly.
                 const auto invalid = host->LoadComponent(arguments.configuration.runtimeConfigPath);
                 Check(invalid.status == ManagedCallStatus::ManagedException && !invalid.value &&
                           invalid.diagnostic.find("BadImageFormatException") != std::string::npos,
                       "Invalid assembly diagnostic lost");
#if defined(_WIN32)
                 // An unpaired surrogate cannot become UTF-8; it is an argument error, not a C++ exception.
                 const auto unpaired = host->LoadComponent(std::filesystem::path(L"component-\xD800.dll"));
                 Check(unpaired.status == ManagedCallStatus::InvalidArgument && !unpaired.value,
                       "A path that is not valid Unicode was not rejected as an argument");
#endif
                 Check(host->IsInitialized(), "A load failure reset readiness");
                 CheckCalculation(Load(*host, arguments.component));
             }},
            {"a Unicode path loads, and a missing dependency fails only when used",
             [&] {
                 auto host = StartHost(arguments);
                 const auto directory =
                     arguments.component.parent_path() / std::filesystem::path(u8"component-π-\U0001F43A");
                 std::filesystem::create_directories(directory);
                 const auto path = directory / arguments.component.filename();
                 std::filesystem::copy_file(arguments.component, path,
                                            std::filesystem::copy_options::overwrite_existing);
                 const auto component = Load(*host, path);
                 CheckInfo(component);
                 // The copy lacks its private dependency, which loads lazily.
                 const auto noDependency = component.InvokeInt32(CalculationType, "Add", 20, 22);
                 Check(noDependency.status == ManagedCallStatus::ManagedException && !noDependency.value &&
                           noDependency.diagnostic.find("FileNotFoundException") != std::string::npos,
                       "Missing dependency escaped or omitted diagnostics");
                 Check(component.InvokeInt32(CalculationType, "LocalAdd", 20, 22).value == 42,
                       "A call that needs no dependency failed");
             }},
            {"moves and release transfer ownership and unload observation",
             [&] {
                 auto host = StartHost(arguments);
                 auto original = Load(*host, arguments.component);
                 ManagedComponent moved(std::move(original));
                 Check(!original.IsLoaded() && original.GetUnloadStatus().status == ManagedCallStatus::InvalidHandle,
                       "A move left the old owner usable");
                 CheckInvalidHandle(original, "A moved-from owner was callable");
                 CheckInfo(moved);
                 Check(moved.Release().Succeeded() && !moved.IsLoaded(), "Release failed");
                 CheckUnload(moved, true);
                 Check(moved.Release().Succeeded(), "A repeated release failed");
                 CheckInvalidHandle(moved, "A released owner was callable");

                 // A released owner keeps its weak observation; both forms of move transfer it, and
                 // move-assignment also releases the registration it replaces.
                 ManagedComponent observation(std::move(moved));
                 CheckUnload(observation, true);
                 Check(moved.GetUnloadStatus().status == ManagedCallStatus::InvalidHandle,
                       "Move construction kept the old observation");
                 auto replacement = Load(*host, arguments.component);
                 replacement = std::move(observation);
                 CheckUnload(replacement, true);
                 Check(observation.GetUnloadStatus().status == ManagedCallStatus::InvalidHandle,
                       "Move assignment kept the old observation");
             }},
            {"owners and results outlive host reset and destruction",
             [&] {
                 std::optional<ManagedComponent> retained;
                 ManagedCallResult<std::int32_t> failure;
                 ComponentInfo snapshot;
                 {
                     auto host = StartHost(arguments);
                     retained = Load(*host, arguments.component);
                     snapshot = *retained->GetInfo().value;
                     failure = retained->InvokeInt32(CalculationType, "Throw", 0, 0);
                     // A failed attempt resets readiness; the owner keeps its bridge table.
                     const auto failed = host->Initialize({arguments.configuration.runtimeConfigPath, {}, {}});
                     Check(!failed.success && !host->IsInitialized(), "A failed initialization kept readiness");
                     CheckCalculation(*retained);
                     Check(host->Initialize(arguments.configuration).success,
                           "The host did not recover after a failed initialization");
                 }
                 CheckInfo(*retained);
                 CheckCalculation(*retained);
                 CheckUnload(*retained, false);
                 Check(retained->Release().Succeeded() && !retained->IsLoaded(),
                       "Release after host destruction failed");
                 CheckUnload(*retained, true);
                 Check(retained->InvokeInt32(CalculationType, "Add", 20, 22).status == ManagedCallStatus::InvalidHandle,
                       "A released owner remained callable");
                 Check(failure.diagnostic.find("InvalidOperationException") != std::string::npos,
                       "A diagnostic did not outlive its host");
                 Check(snapshot.assemblyName == "Fenrir.HostingComponent",
                       "An info snapshot did not outlive its owner");
             }},
            {"repeated scoped loads release on destruction",
             [&] {
                 auto host = StartHost(arguments);
                 for (int i = 0; i < 16; ++i)
                 {
                     const auto scoped = Load(*host, arguments.component);
                     CheckInfo(scoped);
                     CheckCalculation(scoped);
                 }
             }},
        });
    }
} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
    if (argc != 4)
    {
        std::cerr << "Usage: FenrirComponentChecks runtimeconfig.json bridge.dll component.dll\n";
        return EXIT_FAILURE;
    }
    return Run({{argv[1], argv[2], {}}, std::filesystem::absolute(argv[3])});
}
