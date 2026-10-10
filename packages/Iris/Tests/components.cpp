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
            {"a type is found by name and reports its names",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto found = component.FindType(CalculationType);
                 Check(found.Succeeded(), "A public type was not found: " + found.diagnostic);
                 const auto info = found.value->GetInfo();
                 Check(info.Succeeded() && info.value->fullName == CalculationType &&
                           info.value->assemblyName == "Iris.Examples.Component",
                       "Type names incorrect");
                 // Handles are cheap values: a copy works the same.
                 const Type copy = *found.value;
                 Check(copy.GetInfo().value->fullName == CalculationType, "A copied type handle stopped working");
             }},
            {"missing and invalid type names return diagnostics",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 for (const char* name :
                      {"Iris.Examples.Component.Missing", "Iris.Examples.Component.HiddenCalculation",
                       "Iris.Examples.Component.OpenCalculation`1", "Iris.Examples.Dependency.DependencyMath"})
                 {
                     const auto missing = component.FindType(name);
                     Check(missing.status == CallStatus::NotFound && !missing.value &&
                               missing.diagnostic.find(name) != std::string::npos,
                           std::string("Not a NotFound naming the type: ") + name);
                 }
                 Check(component.FindType({}).status == CallStatus::InvalidArgument &&
                           component.FindType(std::string("A\0B", 3)).status == CallStatus::InvalidArgument &&
                           component.FindType("\xFF").status == CallStatus::InvalidArgument,
                       "An empty, null-containing or malformed name was accepted");
                 Check(component.FindType(CalculationType).Succeeded(), "Lookup did not recover after failures");
             }},
            {"type handles become invalid when their component is released",
             [&] {
                 auto host = StartHost(arguments);
                 auto released = Load(*host, arguments.component);
                 const auto other = Load(*host, arguments.component);
                 const auto stale = *released.FindType(CalculationType).value;
                 const auto live = *other.FindType(CalculationType).value;
                 Check(released.Release().Succeeded(), "Release failed");
                 const auto info = stale.GetInfo();
                 Check(info.status == CallStatus::InvalidHandle && !info.value,
                       "A released component's type handle worked");
                 Check(released.FindType(CalculationType).status == CallStatus::InvalidHandle,
                       "A released owner still resolved types");
                 Check(live.GetInfo().Succeeded(), "Releasing one component invalidated another's type handle");
                 host.reset(); // A type handle outlives its host while its component is alive.
                 Check(live.GetInfo().Succeeded(), "A type handle did not outlive the host");
             }},
            {"typed methods round-trip every supported value kind",
             [&] {
                 static_assert(PrimitiveValue<std::int32_t> && PrimitiveValue<bool> && PrimitiveValue<double> &&
                               !PrimitiveValue<std::string> && !PrimitiveValue<char> && !PrimitiveValue<void*>);
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto type = *component.FindType(CalculationType).value;
                 const auto find = [&type](auto signature, std::string_view name) {
                     using Signature = typename decltype(signature)::type;
                     auto found = type.FindMethod<Signature>(name);
                     Check(found.Succeeded(),
                           "Method lookup failed for " + std::string(name) + ": " + found.diagnostic);
                     return *found.value;
                 };
                 Check(
                     find(std::type_identity<std::int32_t(std::int32_t, std::int32_t)>{}, "Add").Invoke(20, 22).value ==
                         42,
                     "Add(int, int) returned the wrong value");
                 Check(
                     find(std::type_identity<std::int32_t(std::int32_t, std::int32_t)>{}, "Add").Invoke(-5, 2).value ==
                         -3,
                     "A negative int did not round-trip");
                 Check(find(std::type_identity<std::int64_t(std::int64_t, std::int64_t)>{}, "Add")
                               .Invoke(5'000'000'000LL, 7)
                               .value == 5'000'000'007LL,
                       "The long overload was not selected");
                 Check(find(std::type_identity<double(double, float)>{}, "Scale").Invoke(2.5, 4.0f).value == 10.0,
                       "double and float did not round-trip");
                 Check(find(std::type_identity<bool(bool)>{}, "Not").Invoke(true).value == false &&
                           find(std::type_identity<bool(bool)>{}, "Not").Invoke(false).value == true,
                       "bool did not round-trip");
                 Check(find(std::type_identity<std::uint8_t(std::uint8_t)>{}, "WrapByte").Invoke(255).value == 0,
                       "byte did not round-trip");
                 Check(find(std::type_identity<std::int8_t(std::int8_t)>{}, "NegateSByte").Invoke(-7).value == 7 &&
                           find(std::type_identity<std::int16_t(std::int16_t)>{}, "NegateShort").Invoke(300).value ==
                               -300,
                       "Signed small integers did not round-trip");
                 Check(
                     find(std::type_identity<std::uint16_t(std::uint16_t)>{}, "IncrementUShort").Invoke(65535).value ==
                             0 &&
                         find(std::type_identity<std::uint32_t(std::uint32_t)>{}, "IncrementUInt")
                                 .Invoke(std::numeric_limits<std::uint32_t>::max())
                                 .value == 0,
                     "Unsigned integers did not round-trip");
                 Check(find(std::type_identity<std::uint64_t(std::uint64_t, std::uint64_t)>{}, "Sum64")
                               .Invoke(std::numeric_limits<std::uint64_t>::max(), 2)
                               .value == 1,
                       "ulong did not round-trip");
                 Check(find(std::type_identity<float(float)>{}, "Halve").Invoke(5.0f).value == 2.5f,
                       "float did not round-trip");

                 // A void method really runs, and the same lookup returns an equivalent handle.
                 const auto touch = find(std::type_identity<void()>{}, "Touch");
                 const auto touches = find(std::type_identity<std::int32_t()>{}, "Touches");
                 const auto before = *touches.Invoke().value;
                 Check(touch.Invoke().Succeeded() && touch.Invoke().Succeeded() && touches.Invoke().value == before + 2,
                       "A void method did not run once per call");
             }},
            {"method lookup failures are NotFound or InvalidArgument with diagnostics",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto type = *component.FindType(CalculationType).value;
                 const auto notFound = [](const auto& result, const char* what) {
                     Check(result.status == CallStatus::NotFound && !result.value && !result.diagnostic.empty(),
                           std::string("Not a NotFound with a diagnostic: ") + what);
                 };
                 notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Missing"), "missing name");
                 notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("WrongReturn"),
                          "wrong return type");
                 notFound(type.FindMethod<std::int32_t(std::int32_t, std::int64_t)>("Add"), "wrong parameter types");
                 notFound(type.FindMethod<std::int32_t(std::int32_t)>("Add"), "wrong parameter count");
                 notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Generic"), "generic method");
                 notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("UnmanagedOnly"),
                          "unmanaged-only method");
                 notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Hidden"), "non-public method");
                 notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("add"), "wrong case");
                 Check(type.FindMethod<void()>({}).status == CallStatus::InvalidArgument &&
                           type.FindMethod<void()>(std::string("A\0B", 3)).status == CallStatus::InvalidArgument &&
                           type.FindMethod<void()>("\xFF").status == CallStatus::InvalidArgument,
                       "An empty, null-containing or malformed name was accepted");
                 const auto add = type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add");
                 Check(add.Succeeded() && add.value->Invoke(20, 22).value == 42,
                       "Lookup did not recover after failures");
             }},
            {"component exceptions in typed calls are ManagedException and the next call works",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto type = *component.FindType(CalculationType).value;
                 const auto add = *type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add").value;
                 const auto thrown =
                     type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Throw").value->Invoke(0, 0);
                 Check(thrown.status == CallStatus::ManagedException && !thrown.value &&
                           thrown.diagnostic.find("InvalidOperationException") != std::string::npos &&
                           thrown.diagnostic.find("\xCF\x80 \xF0\x9F\x90\xBA") != std::string::npos,
                       "Component exception details or Unicode lost");
                 const auto argument =
                     type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("ThrowArgument").value->Invoke(0, 0);
                 Check(argument.status == CallStatus::ManagedException && !argument.value,
                       "A component's ArgumentException was reported as an invalid bridge argument");
                 const auto overflow = add.Invoke(std::numeric_limits<std::int32_t>::max(), 1);
                 Check(overflow.status == CallStatus::ManagedException && !overflow.value &&
                           overflow.diagnostic.find("OverflowException") != std::string::npos,
                       "A component's OverflowException was not a ManagedException");
                 const auto bounded =
                     type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("LongError").value->Invoke(0, 0);
                 Check(bounded.status == CallStatus::ManagedException && bounded.diagnosticTruncated,
                       "Component diagnostic was not bounded");
                 Check(add.Invoke(20, 22).value == 42, "A call did not recover after exceptions");
             }},
            {"method handles stop working when their component is released",
             [&] {
                 auto host = StartHost(arguments);
                 auto released = Load(*host, arguments.component);
                 const auto other = Load(*host, arguments.component);
                 const auto stale = *(*released.FindType(CalculationType).value)
                                         .FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add")
                                         .value;
                 const auto live = *(*other.FindType(CalculationType).value)
                                        .FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add")
                                        .value;
                 const auto staleType = *released.FindType(CalculationType).value;
                 Check(released.Release().Succeeded(), "Release failed");
                 const auto result = stale.Invoke(20, 22);
                 Check(result.status == CallStatus::InvalidHandle && !result.value,
                       "A released component's method still ran");
                 Check(staleType.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add").status ==
                           CallStatus::InvalidHandle,
                       "A released component's type still resolved methods");
                 Check(live.Invoke(20, 22).value == 42, "Releasing one component invalidated another's method");
                 host.reset();
                 Check(live.Invoke(20, 22).value == 42, "A method handle did not outlive the host");
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
