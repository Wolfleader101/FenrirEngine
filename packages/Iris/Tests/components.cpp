// Component ownership checks against the real bridge and the published example component.
#include "Iris/Host.hpp"
#include "TestHarness.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace
{
    using namespace Iris;
    using Iris::Tests::Check;

    constexpr std::string_view CalculationType = "Iris.Examples.Component.Calculations";
    constexpr std::string_view SharedCalculatorType = "Iris.Examples.Component.SharedCalculator";

    static_assert(!std::is_copy_constructible_v<Component> && std::is_nothrow_move_constructible_v<Component>);

    struct Arguments
    {
        HostConfiguration configuration;
        std::filesystem::path component;
    };

    std::unique_ptr<Host> StartHost(const Arguments& arguments)
    {
        auto host = std::make_unique<Host>();
        const auto initialized = host->Initialize(arguments.configuration);
        Check(initialized.success, "Initialization failed: " + initialized.diagnostic);
        return host;
    }

    Component Load(const Host& host, const std::filesystem::path& path)
    {
        auto loaded = host.LoadComponent(path);
        Check(loaded.Succeeded() && loaded.value->IsLoaded(), "Component load failed: " + loaded.diagnostic);
        return std::move(*loaded.value);
    }

    void CheckInfo(const Component& owner)
    {
        const auto info = owner.GetInfo();
        Check(info.Succeeded() && info.value->assemblyName == "Iris.Examples.Component",
              "Component identity incorrect");
    }

    void CheckCalculation(const Component& owner)
    {
        const auto result = owner.InvokeInt32(CalculationType, "Add", 20, 22);
        Check(result.Succeeded() && result.value == 42 && result.diagnostic.empty() && !result.diagnosticTruncated,
              "Component calculation failed: " + result.diagnostic);
    }

    void CheckUnload(const Component& owner, bool released)
    {
        const auto result = owner.GetUnloadStatus();
        Check(result.Succeeded() && result.diagnostic.empty() && !result.diagnosticTruncated &&
                  (released ? (*result.value == ComponentUnloadState::Pending ||
                               *result.value == ComponentUnloadState::Collected)
                            : *result.value == ComponentUnloadState::NotRequested),
              "Unload observation did not match the owner's lifecycle");
    }

    void CheckInvalidHandle(const Component& owner, const char* message)
    {
        Check(owner.GetInfo().status == CallStatus::InvalidHandle &&
                  owner.InvokeInt32(CalculationType, "LocalAdd", 20, 22).status == CallStatus::InvalidHandle,
              message);
    }

    int Run(const Arguments& arguments)
    {
        return Tests::RunCases({
            {"a loaded component reports its identity and NotRequested",
             [&] {
                 Host unready;
                 Check(unready.LoadComponent(arguments.component).status == CallStatus::NotInitialized,
                       "An uninitialized host loaded a component");
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 CheckInfo(component);
                 CheckUnload(component, false);
                 CheckCalculation(component);
             }},
            // Must run before the shared-assembly case: sharing is process-wide and permanent.
            {"without a shared declaration, a component uses its private contract copy",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto shared = component.InvokeInt32(SharedCalculatorType, "ContractIsShared", 0, 0);
                 Check(shared.value == 0, "The contract resolved to a shared copy without a declaration");
             }},
            {"lookup and component exceptions are contained, and the next call works",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 for (const auto method : {"Missing", "WrongReturn", "WrongParameters", "Generic", "UnmanagedOnly"})
                 {
                     const auto failed = component.InvokeInt32(CalculationType, method, 20, 22);
                     Check(failed.status == CallStatus::ManagedException && !failed.value &&
                               failed.diagnostic.find("MissingMethodException") != std::string::npos,
                           std::string("Lookup failure escaped or published a value: ") + method);
                 }
                 const auto thrown = component.InvokeInt32(CalculationType, "Throw", 20, 22);
                 Check(thrown.status == CallStatus::ManagedException && !thrown.value &&
                           thrown.diagnostic.find("InvalidOperationException") != std::string::npos &&
                           thrown.diagnostic.find("\xCF\x80 \xF0\x9F\x90\xBA") != std::string::npos,
                       "Component exception details or Unicode lost");
                 const auto argumentThrow = component.InvokeInt32(CalculationType, "ThrowArgument", 20, 22);
                 Check(argumentThrow.status == CallStatus::ManagedException && !argumentThrow.value,
                       "A component's ArgumentException was reported as an invalid bridge argument");
                 const auto overflow =
                     component.InvokeInt32(CalculationType, "Add", std::numeric_limits<std::int32_t>::max(), 1);
                 Check(overflow.status == CallStatus::Overflow && !overflow.value &&
                           overflow.diagnostic.find("OverflowException") != std::string::npos,
                       "Component overflow contract failed");
                 const auto bounded = component.InvokeInt32(CalculationType, "LongError", 0, 0);
                 Check(bounded.status == CallStatus::ManagedException && !bounded.value && bounded.diagnosticTruncated,
                       "Component diagnostic was not bounded");
                 CheckCalculation(component);
             }},
            {"malformed, empty and null-containing names are rejected",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto malformedType = component.InvokeInt32("\xC3(", "Add", 20, 22);
                 const auto malformedMethod = component.InvokeInt32(CalculationType, "\xFF", 20, 22);
                 Check(malformedType.status == CallStatus::InvalidArgument && !malformedType.value &&
                           malformedType.diagnostic.find("DecoderFallbackException") != std::string::npos &&
                           malformedMethod.status == CallStatus::InvalidArgument && !malformedMethod.value,
                       "Malformed UTF-8 names accepted");
                 Check(component.InvokeInt32({}, "Add", 20, 22).status == CallStatus::InvalidArgument &&
                           component.InvokeInt32(CalculationType, {}, 20, 22).status == CallStatus::InvalidArgument &&
                           component.InvokeInt32(CalculationType, std::string("Add\0suffix", 10), 20, 22).status ==
                               CallStatus::InvalidArgument,
                       "Empty or null-containing names accepted");
                 CheckCalculation(component);
             }},
            {"load failures return diagnostics and leave the host ready",
             [&] {
                 auto host = StartHost(arguments);
                 auto missingPath = arguments.component;
                 missingPath += ".missing";
                 const auto missing = host->LoadComponent(missingPath);
                 Check(missing.status == CallStatus::ManagedException && !missing.value &&
                           missing.diagnostic.find("FileNotFoundException") != std::string::npos,
                       "Missing assembly diagnostic lost");
                 Check(host->LoadComponent({}).status == CallStatus::InvalidArgument, "Empty path accepted");
                 // An existing JSON file, not a managed assembly.
                 const auto invalid = host->LoadComponent(arguments.configuration.runtimeConfigPath);
                 Check(invalid.status == CallStatus::ManagedException && !invalid.value &&
                           invalid.diagnostic.find("BadImageFormatException") != std::string::npos,
                       "Invalid assembly diagnostic lost");
#if defined(_WIN32)
                 // An unpaired surrogate cannot become UTF-8; it is an argument error, not a C++ exception.
                 const auto unpaired = host->LoadComponent(std::filesystem::path(L"component-\xD800.dll"));
                 Check(unpaired.status == CallStatus::InvalidArgument && !unpaired.value,
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
                 Check(noDependency.status == CallStatus::ManagedException && !noDependency.value &&
                           noDependency.diagnostic.find("FileNotFoundException") != std::string::npos,
                       "Missing dependency escaped or omitted diagnostics");
                 Check(component.InvokeInt32(CalculationType, "LocalAdd", 20, 22).value == 42,
                       "A call that needs no dependency failed");
             }},
            {"moves and release transfer ownership and unload observation",
             [&] {
                 auto host = StartHost(arguments);
                 auto original = Load(*host, arguments.component);
                 Component moved(std::move(original));
                 Check(!original.IsLoaded() && original.GetUnloadStatus().status == CallStatus::InvalidHandle,
                       "A move left the old owner usable");
                 CheckInvalidHandle(original, "A moved-from owner was callable");
                 CheckInfo(moved);
                 Check(moved.Release().Succeeded() && !moved.IsLoaded(), "Release failed");
                 CheckUnload(moved, true);
                 Check(moved.Release().Succeeded(), "A repeated release failed");
                 CheckInvalidHandle(moved, "A released owner was callable");

                 // A released owner keeps its weak observation; both forms of move transfer it, and
                 // move-assignment also releases the registration it replaces.
                 Component observation(std::move(moved));
                 CheckUnload(observation, true);
                 Check(moved.GetUnloadStatus().status == CallStatus::InvalidHandle,
                       "Move construction kept the old observation");
                 auto replacement = Load(*host, arguments.component);
                 replacement = std::move(observation);
                 CheckUnload(replacement, true);
                 Check(observation.GetUnloadStatus().status == CallStatus::InvalidHandle,
                       "Move assignment kept the old observation");
             }},
            {"owners and results outlive host reset and destruction",
             [&] {
                 std::optional<Component> retained;
                 CallResult<std::int32_t> failure;
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
                 Check(retained->InvokeInt32(CalculationType, "Add", 20, 22).status == CallStatus::InvalidHandle,
                       "A released owner remained callable");
                 Check(failure.diagnostic.find("InvalidOperationException") != std::string::npos,
                       "A diagnostic did not outlive its host");
                 Check(snapshot.assemblyName == "Iris.Examples.Component",
                       "An info snapshot did not outlive its owner");
             }},
            {"a declared shared assembly binds every component to one copy",
             [&] {
                 auto configuration = arguments.configuration;
                 configuration.sharedAssemblies = {arguments.component.parent_path() / "Iris.Examples.Contracts.dll"};
                 auto host = std::make_unique<Host>();
                 const auto initialized = host->Initialize(configuration);
                 Check(initialized.success, "Initialization with a shared assembly failed: " + initialized.diagnostic);
                 const auto first = Load(*host, arguments.component);
                 const auto second = Load(*host, arguments.component);
                 Check(first.InvokeInt32(SharedCalculatorType, "ContractIsShared", 0, 0).value == 1 &&
                           second.InvokeInt32(SharedCalculatorType, "ContractIsShared", 0, 0).value == 1,
                       "A component bound to its private contract copy");
                 // Sharing the same file again is a no-op; a contract cannot also be a component.
                 Check(host->Initialize(configuration).success, "Re-sharing the same file failed");
                 const auto asComponent = host->LoadComponent(configuration.sharedAssemblies.front());
                 Check(asComponent.status == CallStatus::InvalidArgument, "A shared assembly loaded as a component");
             }},
            {"sharing a missing assembly fails initialization",
             [&] {
                 auto configuration = arguments.configuration;
                 configuration.sharedAssemblies = {arguments.component.parent_path() / "Missing.Contracts.dll"};
                 Host host;
                 const auto initialized = host.Initialize(configuration);
                 Check(!initialized.success && !host.IsInitialized() &&
                           initialized.diagnostic.find("Sharing assembly failed") != std::string::npos,
                       "A missing shared assembly was accepted or undiagnosed");
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
        std::cerr << "Usage: IrisComponentChecks runtimeconfig.json bridge.dll component.dll\n";
        return EXIT_FAILURE;
    }
    return Run({{argv[1], argv[2], {}}, std::filesystem::absolute(argv[3])});
}
