// Component ownership checks against the real bridge and the published example component.
#include "Iris/Host.hpp"
#include "TestHarness.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    using namespace Iris;
    using Iris::Tests::Check;

    constexpr std::string_view CalculationType = "Iris.Examples.Component.Calculations";
    constexpr std::string_view CounterType = "Iris.Examples.Component.Counter";
    constexpr std::string_view StepCounterType = "Iris.Examples.Component.StepCounter";
    constexpr std::string_view ShapeType = "Iris.Examples.Component.Shape";
    constexpr std::string_view OtherType = "Iris.Examples.Component.Other";
    constexpr std::string_view FactoryType = "Iris.Examples.Component.Factory";
    constexpr std::string_view HidingCounterType = "Iris.Examples.Component.HidingCounter";
    constexpr std::string_view SettingsType = "Iris.Examples.Component.Settings";
    constexpr std::string_view HidingSettingsType = "Iris.Examples.Component.HidingSettings";
    constexpr std::string_view StringsType = "Iris.Examples.Component.Strings";
    constexpr std::string_view GreeterType = "Iris.Examples.Component.Greeter";
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

    template <typename T, typename U>
    CallResult<T> Rebind(const CallResult<U>& failed)
    {
        CallResult<T> result;
        result.status = failed.status;
        result.diagnostic = failed.diagnostic;
        result.diagnosticTruncated = failed.diagnosticTruncated;
        return result;
    }

    // Looks up and calls `static int method(int, int)`. The status is the first step that failed.
    CallResult<std::int32_t> Call(const Component& owner, std::string_view type, std::string_view method,
                                  std::int32_t left, std::int32_t right)
    {
        const auto found = owner.FindType(type);
        if (!found.Succeeded())
        {
            return Rebind<std::int32_t>(found);
        }
        const auto handle = found.value->FindMethod<std::int32_t(std::int32_t, std::int32_t)>(method);
        if (!handle.Succeeded())
        {
            return Rebind<std::int32_t>(handle);
        }
        return handle.value->Invoke(left, right);
    }

    // A logged message, copied: LogMessage::text is only valid during the sink call.
    struct Logged
    {
        LogLevel level;
        std::string text;
    };

    // Clears the process-wide sink when a case ends, however it ends, through a fresh host so a sink
    // set by a host that no longer exists is cleared too.
    struct SinkGuard
    {
        explicit SinkGuard(const Arguments& arguments) : configuration(arguments.configuration)
        {
        }
        ~SinkGuard()
        {
            Host host;
            if (host.Initialize(configuration).success)
            {
                (void)host.SetLogSink({});
            }
        }
        SinkGuard(const SinkGuard&) = delete;
        SinkGuard& operator=(const SinkGuard&) = delete;
        HostConfiguration configuration;
    };

    void CheckInfo(const Component& owner)
    {
        const auto info = owner.GetInfo();
        Check(info.Succeeded() && info.value->assemblyName == "Iris.Examples.Component",
              "Component identity incorrect");
    }

    void CheckCalculation(const Component& owner)
    {
        const auto result = Call(owner, CalculationType, "Add", 20, 22);
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
                  Call(owner, CalculationType, "LocalAdd", 20, 22).status == CallStatus::InvalidHandle,
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
                 const auto shared = Call(component, SharedCalculatorType, "ContractIsShared", 0, 0);
                 Check(shared.value == 0, "The contract resolved to a shared copy without a declaration");
             }},
            {"malformed, empty and null-containing names are rejected",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto malformedType = Call(component, "\xC3(", "Add", 20, 22);
                 const auto malformedMethod = Call(component, CalculationType, "\xFF", 20, 22);
                 Check(malformedType.status == CallStatus::InvalidArgument && !malformedType.value &&
                           malformedType.diagnostic.find("DecoderFallbackException") != std::string::npos &&
                           malformedMethod.status == CallStatus::InvalidArgument && !malformedMethod.value,
                       "Malformed UTF-8 names accepted");
                 Check(Call(component, {}, "Add", 20, 22).status == CallStatus::InvalidArgument &&
                           Call(component, CalculationType, {}, 20, 22).status == CallStatus::InvalidArgument &&
                           Call(component, CalculationType, std::string("Add\0suffix", 10), 20, 22).status ==
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
                 notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("WrongParameters"),
                          "parameters that would only match by widening");
                 notFound(type.FindMethod<std::int32_t(std::int16_t, std::int16_t)>("Add"), "narrower parameters");
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
            {"the log sink receives bridge events and exception reports with severity",
             [&] {
                 std::vector<Logged> messages;
                 auto host = StartHost(arguments);
                 const SinkGuard guard(arguments);
                 Check(host->SetLogSink([&](const LogMessage& message) {
                               messages.push_back(Logged{message.level, std::string(message.text)});
                           })
                           .Succeeded(),
                       "Registering a sink failed");
                 const auto has = [&messages](LogLevel level, std::string_view text) {
                     return std::any_of(messages.begin(), messages.end(), [&](const Logged& message) {
                         return message.level == level && message.text.find(text) != std::string::npos;
                     });
                 };
                 auto component = Load(*host, arguments.component);
                 Check(has(LogLevel::Info, "Iris.Examples.Component"), "Loading a component was not logged at Info");
                 messages.clear();

                 // A component exception reaches the sink at Error, and the call still returns its own details.
                 const auto thrown = Call(component, CalculationType, "Throw", 0, 0);
                 Check(thrown.status == CallStatus::ManagedException &&
                           thrown.diagnostic.find("InvalidOperationException") != std::string::npos,
                       "The call lost its own diagnostic");
                 Check(has(LogLevel::Error, "InvalidOperationException") &&
                           has(LogLevel::Error, "\xCF\x80 \xF0\x9F\x90\xBA"),
                       "A component exception was not reported at Error with its details");

                 // Bridge validation is a Warning; a NotFound is not an exception report.
                 messages.clear();
                 Check(component.FindType("\xFF").status == CallStatus::InvalidArgument,
                       "Malformed UTF-8 was accepted");
                 Check(has(LogLevel::Warning, "DecoderFallbackException"),
                       "A validation failure was not reported at Warning");
                 messages.clear();
                 Check(component.FindType("Iris.Examples.Component.Missing").status == CallStatus::NotFound,
                       "Expected NotFound");
                 Check(!std::any_of(messages.begin(), messages.end(),
                                    [](const Logged& message) { return message.level == LogLevel::Error; }),
                       "A NotFound was reported as an error");

                 // Releasing is logged too, and replacing the sink redirects messages.
                 messages.clear();
                 std::vector<Logged> replaced;
                 Check(host->SetLogSink([&](const LogMessage& message) {
                               replaced.push_back(Logged{message.level, std::string(message.text)});
                           })
                           .Succeeded(),
                       "Replacing the sink failed");
                 Check(component.Release().Succeeded(), "Release failed");
                 Check(messages.empty() && !replaced.empty() && replaced.front().level == LogLevel::Info,
                       "The replaced sink still received messages, or the new one received none");
             }},
            {"calls work without a sink and after it is cleared",
             [&] {
                 std::vector<Logged> messages;
                 auto host = StartHost(arguments);
                 const SinkGuard guard(arguments);
                 const auto component = Load(*host, arguments.component);
                 Check(Call(component, CalculationType, "Throw", 0, 0).status == CallStatus::ManagedException,
                       "A call failed without a sink");
                 Check(host->SetLogSink([&](const LogMessage& message) {
                               messages.push_back(Logged{message.level, std::string(message.text)});
                           })
                           .Succeeded(),
                       "Registering failed");
                 Check(host->SetLogSink({}).Succeeded(), "Clearing failed");
                 Check(Call(component, CalculationType, "Throw", 0, 0).status == CallStatus::ManagedException &&
                           messages.empty(),
                       "Messages arrived after the sink was cleared");
             }},
            {"a sink that triggers more failing calls does not recurse",
             [&] {
                 int deliveries = 0;
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const SinkGuard guard(arguments);
                 Check(host->SetLogSink([&](const LogMessage&) {
                               // Each failing call would log again; nested messages are dropped.
                               if (++deliveries == 1)
                               {
                                   (void)Call(component, CalculationType, "Throw", 0, 0);
                               }
                           })
                           .Succeeded(),
                       "Registering failed");
                 Check(Call(component, CalculationType, "Throw", 0, 0).status == CallStatus::ManagedException &&
                           deliveries == 1,
                       "A sink that made another failing call recursed or lost the original message");
                 Check(Call(component, CalculationType, "Throw", 0, 0).status == CallStatus::ManagedException &&
                           deliveries == 2,
                       "Delivery did not resume after a nested call");
             }},
            {"a sink that throws, or clears itself, cannot break a call",
             [&] {
                 int delivered = 0;
                 int clearing = 0;
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const SinkGuard guard(arguments);
                 Check(host->SetLogSink([&](const LogMessage&) {
                               ++delivered;
                               throw std::runtime_error("a sink failure");
                           })
                           .Succeeded(),
                       "Registering failed");
                 const auto thrown = Call(component, CalculationType, "Throw", 0, 0);
                 Check(thrown.status == CallStatus::ManagedException &&
                           thrown.diagnostic.find("InvalidOperationException") != std::string::npos && delivered > 0,
                       "A throwing sink changed the call's result");
                 Check(Call(component, CalculationType, "Add", 20, 22).value == 42,
                       "A call did not recover after a throwing sink");

                 Host* hostPointer = host.get();
                 Check(hostPointer
                           ->SetLogSink([&](const LogMessage&) {
                               ++clearing;
                               (void)hostPointer->SetLogSink({});
                           })
                           .Succeeded(),
                       "Registering failed");
                 (void)Call(component, CalculationType, "Throw", 0, 0);
                 (void)Call(component, CalculationType, "Throw", 0, 0);
                 Check(clearing >= 1 && Call(component, CalculationType, "Add", 20, 22).value == 42,
                       "A sink that cleared itself broke a call");
                 const int afterClear = clearing;
                 (void)Call(component, CalculationType, "Throw", 0, 0);
                 Check(clearing == afterClear, "A cleared sink was called again");
             }},
            {"the sink needs an initialized host and survives the host that set it",
             [&] {
                 Host unready;
                 Check(unready.SetLogSink([](const LogMessage&) {}).status == CallStatus::NotInitialized,
                       "An uninitialized host accepted a sink");
                 std::vector<Logged> messages;
                 const SinkGuard guard(arguments);
                 std::optional<Component> component;
                 {
                     auto host = StartHost(arguments);
                     component = Load(*host, arguments.component);
                     Check(host->SetLogSink([&](const LogMessage& message) {
                                   messages.push_back(Logged{message.level, std::string(message.text)});
                               })
                               .Succeeded(),
                           "Registering failed");
                 }
                 Check(component->Release().Succeeded() && !messages.empty(),
                       "A sink did not outlive the host that registered it");
             }},
            {"objects are built by constructor overload and keep their state",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto counter = *component.FindType(CounterType).value;
                 const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
                 const auto add = *counter.FindInstanceMethod<void(std::int32_t)>("Add").value;
                 const auto first = counter.CreateObject();
                 const auto started = counter.CreateObject(std::int32_t{5});
                 const auto scaled = counter.CreateObject(std::int32_t{4}, 2.5);
                 Check(first.Succeeded() && started.Succeeded() && scaled.Succeeded(),
                       "Construction failed: " + started.diagnostic);
                 Check(value.Invoke(*first.value).value == 0 && value.Invoke(*started.value).value == 5 &&
                           value.Invoke(*scaled.value).value == 10,
                       "A constructor overload was not selected");
                 Check(add.Invoke(*started.value, 3).Succeeded() && value.Invoke(*started.value).value == 8 &&
                           value.Invoke(*first.value).value == 0,
                       "Instance state was shared or lost between calls");
             }},
            {"virtual methods dispatch to the object's real type",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto counter = *component.FindType(CounterType).value;
                 const auto step = *component.FindType(StepCounterType).value;
                 const auto next = *counter.FindInstanceMethod<std::int32_t()>("Next").value;
                 const auto plain = counter.CreateObject(std::int32_t{1});
                 const auto stepper = step.CreateObject(std::int32_t{1});
                 Check(next.Invoke(*plain.value).value == 2 && next.Invoke(*stepper.value).value == 11,
                       "A call through the base method did not reach the override");
             }},
            {"construction failures and lookups are contained and leave nothing rooted",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 const auto counter = *component.FindType(CounterType).value;
                 const auto failed = counter.CreateObject(true);
                 Check(failed.status == CallStatus::ManagedException && !failed.value &&
                           failed.diagnostic.find("InvalidOperationException") != std::string::npos &&
                           failed.diagnostic.find("\xCF\x80 \xF0\x9F\x90\xBA") != std::string::npos,
                       "A constructor exception was not contained with its details");
                 Check(component.FindType(ShapeType).value->CreateObject().status == CallStatus::NotFound,
                       "An abstract type was constructed");
                 Check(component.FindType(CalculationType).value->CreateObject().status == CallStatus::NotFound,
                       "A static type was constructed");
                 Check(counter.CreateObject(std::int64_t{1}).status == CallStatus::NotFound,
                       "A constructor matched by widening");
                 auto recovered = counter.CreateObject(false);
                 Check(recovered.Succeeded(), "Construction did not recover after a failure");
                 Check(recovered.value->Release().Succeeded(), "Releasing the object failed");
                 Check(component.Release().Succeeded(), "Failed constructions left something rooted");
                 Check(!component.IsLoaded(), "The component stayed loaded");
             }},
            {"a component with live objects cannot be released until they are",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 const auto counter = *component.FindType(CounterType).value;
                 const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
                 auto object = counter.CreateObject();
                 Check(object.Succeeded() && object.value->IsValid(), "Construction failed");
                 const auto rejected = component.Release();
                 Check(rejected.status == CallStatus::InUse && component.IsLoaded() &&
                           value.Invoke(*object.value).Succeeded(),
                       "A rejected release damaged the component");
                 Check(object.value->Release().Succeeded() && !object.value->IsValid(), "Releasing the object failed");
                 Check(object.value->Release().Succeeded(), "Releasing an object twice was not idempotent");
                 Check(value.Invoke(*object.value).status == CallStatus::InvalidHandle,
                       "A released object still worked");
                 Check(component.Release().Succeeded() && !component.IsLoaded(),
                       "Releasing the component failed after its objects");
             }},
            {"moved objects transfer ownership and destruction releases them",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 const auto counter = *component.FindType(CounterType).value;
                 {
                     auto built = counter.CreateObject();
                     Object moved(std::move(*built.value));
                     Check(!built.value->IsValid() && moved.IsValid(), "A move did not transfer ownership");
                     Check(built.value->Release().Succeeded(), "A moved-from object was not a harmless release");
                     Check(component.Release().status == CallStatus::InUse, "A moved object was not still owned");
                 }
                 Check(component.Release().Succeeded(), "Destroying the last object did not release its root");
             }},
            {"objects and components can be destroyed in any order",
             [&] {
                 std::optional<Component> component;
                 std::optional<Object> object;
                 std::optional<InstanceMethod<std::int32_t()>> value;
                 {
                     auto host = StartHost(arguments);
                     component = Load(*host, arguments.component);
                     const auto counter = *component->FindType(CounterType).value;
                     object.emplace(std::move(*counter.CreateObject().value));
                     value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
                 }
                 Check(value->Invoke(*object).Succeeded(), "An object stopped working when its host was destroyed");
                 Check(component->FindType(CounterType).value->CreateObject().value->Release().Succeeded(),
                       "Releasing an object after its host was destroyed failed");
                 component.reset(); // Destroying a component force-releases it, dropping the object it still owned.
                 Check(value->Invoke(*object).status == CallStatus::InvalidHandle, "An object survived its component");
                 Check(object->Release().status == CallStatus::InvalidHandle && !object->IsValid(),
                       "Releasing an object after its component was destroyed was not InvalidHandle");
                 object.reset();
             }},
            {"an object only works with methods of its own component",
             [&] {
                 auto host = StartHost(arguments);
                 const auto first = Load(*host, arguments.component);
                 const auto second = Load(*host, arguments.component);
                 const auto firstCounter = *first.FindType(CounterType).value;
                 const auto secondCounter = *second.FindType(CounterType).value;
                 const auto object = firstCounter.CreateObject();
                 const auto foreign = *secondCounter.FindInstanceMethod<std::int32_t()>("Value").value;
                 const auto result = foreign.Invoke(*object.value);
                 Check(result.status == CallStatus::InvalidArgument && !result.value,
                       "An object from another component was accepted");
                 const auto other = *first.FindType(OtherType).value;
                 const auto ping = *other.FindInstanceMethod<std::int32_t()>("Ping").value;
                 Check(ping.Invoke(*object.value).status == CallStatus::InvalidArgument,
                       "An object of the wrong type was accepted");
             }},
            {"methods return objects, and null is an empty optional",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 const auto factory = *component.FindType(FactoryType).value;
                 const auto counter = *component.FindType(CounterType).value;
                 const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
                 const auto make = factory.FindMethod<std::optional<Object>(std::int32_t)>("Make");
                 Check(make.Succeeded(), "Lookup of a method returning an object failed: " + make.diagnostic);
                 auto made = make.value->Invoke(7);
                 Check(made.Succeeded() && made.value->has_value() && (*made.value)->IsValid(),
                       "A returned object was not delivered");
                 Check(value.Invoke(**made.value).value == 7, "A returned object lost its state");
                 const auto nothing = factory.FindMethod<std::optional<Object>()>("Nothing").value->Invoke();
                 Check(nothing.Succeeded() && nothing.value.has_value() && !nothing.value->has_value(),
                       "A null return was not an explicit empty optional");
                 // A returned object is owned, so it keeps its component from being released.
                 Check(component.Release().status == CallStatus::InUse,
                       "A component with a returned object was released");
                 Check((*made.value)->Release().Succeeded() && component.Release().Succeeded(),
                       "Releasing the returned object failed");
             }},
            {"returning one managed object twice gives independent handles",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 const auto counter = *component.FindType(CounterType).value;
                 const auto self = *counter.FindInstanceMethod<std::optional<Object>()>("Self").value;
                 const auto add = *counter.FindInstanceMethod<void(std::int32_t)>("Add").value;
                 const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
                 auto origin = counter.CreateObject(std::int32_t{3});
                 auto first = self.Invoke(*origin.value);
                 auto second = self.Invoke(*origin.value);
                 Check(first.Succeeded() && second.Succeeded() && first.value->has_value() && second.value->has_value(),
                       "Self did not return objects");
                 Check(add.Invoke(**first.value, 4).Succeeded() && value.Invoke(**second.value).value == 7 &&
                           value.Invoke(*origin.value).value == 7,
                       "The handles did not refer to the same managed object");
                 Check((*first.value)->Release().Succeeded(), "Releasing a returned handle failed");
                 Check(value.Invoke(**second.value).value == 7 && value.Invoke(*origin.value).value == 7,
                       "Releasing one handle affected another");
                 Check(component.Release().status == CallStatus::InUse,
                       "Handles still alive did not hold the component");
                 Check((*second.value)->Release().Succeeded() && origin.value->Release().Succeeded() &&
                           component.Release().Succeeded(),
                       "The component was not releasable after every handle");
             }},
            {"objects are passed as arguments and checked before the call",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 auto other = Load(*host, arguments.component);
                 const auto factory = *component.FindType(FactoryType).value;
                 const auto counter = *component.FindType(CounterType).value;
                 const auto total = factory.FindMethod<std::int32_t(const Object&, const Object&)>("Total");
                 const auto combine = counter.FindInstanceMethod<std::int32_t(const Object&)>("Combine");
                 Check(total.Succeeded() && combine.Succeeded(),
                       "Lookup of methods taking objects failed: " + total.diagnostic);
                 auto a = counter.CreateObject(std::int32_t{3});
                 auto b = counter.CreateObject(std::int32_t{4});
                 Check(total.value->Invoke(*a.value, *b.value).value == 7 &&
                           combine.value->Invoke(*a.value, *b.value).value == 7,
                       "Object arguments were not passed");

                 // The managed parameter type is checked before the call runs.
                 auto wrong = component.FindType(OtherType).value->CreateObject();
                 const auto mismatch = total.value->Invoke(*a.value, *wrong.value);
                 Check(mismatch.status == CallStatus::InvalidArgument && !mismatch.value &&
                           mismatch.diagnostic.find("instance of") != std::string::npos,
                       "An object of the wrong type was passed");

                 // Released, moved-from and foreign objects are rejected.
                 auto released = counter.CreateObject(std::int32_t{1});
                 Check(released.value->Release().Succeeded(), "Release failed");
                 Check(total.value->Invoke(*a.value, *released.value).status == CallStatus::InvalidHandle,
                       "A released object was passed");
                 auto foreign = other.FindType(CounterType).value->CreateObject();
                 Check(total.value->Invoke(*a.value, *foreign.value).status == CallStatus::InvalidArgument,
                       "An object from another component was passed");
                 Check(total.value->Invoke(*a.value, *b.value).value == 7,
                       "A call did not recover after rejected arguments");

                 // Constructors take objects too.
                 auto copy = counter.CreateObject(*a.value);
                 Check(copy.Succeeded() &&
                           counter.FindInstanceMethod<std::int32_t()>("Value").value->Invoke(*copy.value).value == 3,
                       "A constructor did not receive an object argument");
             }},
            {"a method hidden with new resolves to the most-derived declaration",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto hiding = *component.FindType(HidingCounterType).value;
                 const auto value = hiding.FindInstanceMethod<std::int32_t()>("Value");
                 Check(value.Succeeded(), "A hidden method was ambiguous: " + value.diagnostic);
                 const auto object = hiding.CreateObject();
                 Check(value.value->Invoke(*object.value).value == 42, "The hiding method was not the one called");
             }},
            {"fields round-trip, static and instance",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto settings = *component.FindType(SettingsType).value;
                 const auto object = settings.CreateObject();
                 const auto staticCount = settings.FindField<std::int32_t>("StaticCount");
                 Check(staticCount.Succeeded(), "Field lookup failed: " + staticCount.diagnostic);
                 Check(staticCount.value->Set(41).Succeeded() && staticCount.value->Get().value == 41,
                       "A static field did not round-trip");
                 const auto count = *settings.FindField<std::int32_t>("Count").value;
                 Check(count.Set(*object.value, -5).Succeeded() && count.Get(*object.value).value == -5,
                       "An instance field did not round-trip");
                 const auto ratio = *settings.FindField<double>("Ratio").value;
                 Check(ratio.Set(*object.value, 2.5).Succeeded() && ratio.Get(*object.value).value == 2.5,
                       "A double field did not round-trip");
                 const auto flag = *settings.FindField<bool>("Flag").value;
                 Check(flag.Set(*object.value, true).Succeeded() && flag.Get(*object.value).value == true,
                       "A bool field did not round-trip");
                 Check(count.Get(*object.value).value == -5, "Setting one field changed another");
             }},
            {"properties round-trip and accessor exceptions are contained",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto settings = *component.FindType(SettingsType).value;
                 const auto object = settings.CreateObject();
                 const auto enabled = *settings.FindProperty<bool>("Enabled").value;
                 Check(enabled.Set(*object.value, true).Succeeded() && enabled.Get(*object.value).value == true,
                       "A property did not round-trip");
                 const auto staticProperty = *settings.FindProperty<std::int32_t>("StaticProperty").value;
                 Check(staticProperty.Set(12).Succeeded() && staticProperty.Get().value == 12,
                       "A static property did not round-trip");
                 const auto checkedProperty = *settings.FindProperty<std::int32_t>("Checked").value;
                 const auto rejected = checkedProperty.Set(*object.value, -1);
                 Check(rejected.status == CallStatus::ManagedException &&
                           rejected.diagnostic.find("ArgumentOutOfRangeException") != std::string::npos &&
                           rejected.diagnostic.find("TargetInvocationException") == std::string::npos,
                       "A setter exception was not reported as itself");
                 Check(checkedProperty.Set(*object.value, 8).Succeeded() &&
                           checkedProperty.Get(*object.value).value == 8,
                       "An object was unusable after a setter failed");
                 const auto exploding = settings.FindProperty<std::int32_t>("Exploding").value->Get(*object.value);
                 Check(exploding.status == CallStatus::ManagedException && !exploding.value &&
                           exploding.diagnostic.find("InvalidOperationException") != std::string::npos,
                       "A getter exception was not reported as itself");
             }},
            {"read-only, write-only and wrong-form uses are diagnosed",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto settings = *component.FindType(SettingsType).value;
                 const auto object = settings.CreateObject();
                 const auto contains = [](const std::string& text, const char* word) {
                     return text.find(word) != std::string::npos;
                 };
                 const auto id = *settings.FindField<std::int32_t>("Id").value;
                 const auto readonlyWrite = id.Set(*object.value, 9);
                 Check(readonlyWrite.status == CallStatus::InvalidArgument &&
                           contains(readonlyWrite.diagnostic, "read-only") && id.Get(*object.value).value == 7,
                       "A readonly field was writable");
                 Check(settings.FindField<std::int32_t>("Limit").value->Set(1).status == CallStatus::InvalidArgument &&
                           settings.FindField<std::int32_t>("Constant").value->Set(1).status ==
                               CallStatus::InvalidArgument &&
                           settings.FindField<std::int32_t>("Constant").value->Get().value == 5,
                       "A static readonly field or constant was writable");
                 Check(settings.FindProperty<std::int32_t>("ReadOnly").value->Set(*object.value, 1).status ==
                           CallStatus::InvalidArgument,
                       "A property without a setter was writable");
                 const auto writeOnly = *settings.FindProperty<std::int32_t>("WriteOnly").value;
                 const auto unreadable = writeOnly.Get(*object.value);
                 Check(unreadable.status == CallStatus::InvalidArgument &&
                           contains(unreadable.diagnostic, "write-only") && writeOnly.Set(*object.value, 4).Succeeded(),
                       "A write-only property was readable");
                 const auto count = *settings.FindField<std::int32_t>("Count").value;
                 const auto staticCount = *settings.FindField<std::int32_t>("StaticCount").value;
                 Check(count.Get().status == CallStatus::InvalidArgument &&
                           count.Set(1).status == CallStatus::InvalidArgument,
                       "An instance field was used without an object");
                 Check(staticCount.Get(*object.value).status == CallStatus::InvalidArgument &&
                           staticCount.Set(*object.value, 1).status == CallStatus::InvalidArgument,
                       "A static field was used with an object");
                 const auto otherObject = component.FindType(OtherType).value->CreateObject();
                 Check(count.Get(*otherObject.value).status == CallStatus::InvalidArgument,
                       "An object of the wrong type was accepted");
                 // An init-only property is read-only once the object exists.
                 const auto initOnly = *settings.FindProperty<std::int32_t>("InitOnly").value;
                 Check(initOnly.Get(*object.value).value == 11 &&
                           initOnly.Set(*object.value, 1).status == CallStatus::InvalidArgument,
                       "An init-only property was writable");
                 // Released and foreign objects are rejected before the call.
                 auto released = settings.CreateObject();
                 Check(released.value->Release().Succeeded() &&
                           count.Get(*released.value).status == CallStatus::InvalidHandle,
                       "A released object was accepted by a member");
                 const auto second = Load(*host, arguments.component);
                 const auto foreign = second.FindType(SettingsType).value->CreateObject();
                 Check(count.Get(*foreign.value).status == CallStatus::InvalidArgument,
                       "An object from another component was accepted by a member");
             }},
            {"every primitive kind round-trips through a member",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto settings = *component.FindType(SettingsType).value;
                 const auto object = settings.CreateObject();
                 const auto big = *settings.FindField<std::uint64_t>("Big").value;
                 const auto single = *settings.FindField<float>("Single").value;
                 const auto small = *settings.FindField<std::int8_t>("Small").value;
                 Check(big.Set(*object.value, std::numeric_limits<std::uint64_t>::max()).Succeeded() &&
                           big.Get(*object.value).value == std::numeric_limits<std::uint64_t>::max(),
                       "ulong did not round-trip");
                 Check(single.Set(*object.value, 1.5f).Succeeded() && single.Get(*object.value).value == 1.5f,
                       "float did not round-trip");
                 Check(small.Set(*object.value, -7).Succeeded() && small.Get(*object.value).value == -7,
                       "sbyte did not round-trip");
             }},
            {"a field hidden with new resolves to the most-derived declaration",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto hiding = *component.FindType(HidingSettingsType).value;
                 const auto object = hiding.CreateObject();
                 const auto count = hiding.FindField<double>("Count");
                 Check(count.Succeeded(), "A hidden field was unreachable: " + count.diagnostic);
                 Check(count.value->Set(*object.value, 2.5).Succeeded() && count.value->Get(*object.value).value == 2.5,
                       "The hiding field was not the one used");
                 Check(hiding.FindField<std::int32_t>("Count").status == CallStatus::NotFound,
                       "The hidden base field was still reachable by name");
             }},
            {"member lookup failures are NotFound with a diagnostic",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto settings = *component.FindType(SettingsType).value;
                 const auto notFound = [](const auto& result, const char* what) {
                     Check(result.status == CallStatus::NotFound && !result.value && !result.diagnostic.empty(),
                           std::string("Not a NotFound with a diagnostic: ") + what);
                 };
                 notFound(settings.FindField<std::int32_t>("Missing"), "missing name");
                 notFound(settings.FindField<std::int64_t>("Count"), "a field matched by widening");
                 notFound(settings.FindProperty<std::int32_t>("Count"), "a field found as a property");
                 notFound(settings.FindField<bool>("Enabled"), "a property found as a field");
                 notFound(settings.FindField<std::int32_t>("secret"), "a private field");
                 notFound(settings.FindProperty<std::int32_t>("Item"), "an indexer");
                 Check(settings.FindField<std::int32_t>({}).status == CallStatus::InvalidArgument,
                       "An empty name was accepted");
             }},
            {"object-typed members hold, return and clear objects",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 const auto settings = *component.FindType(SettingsType).value;
                 const auto counter = *component.FindType(CounterType).value;
                 const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
                 const auto owner = settings.CreateObject();
                 const auto child = settings.FindField<std::optional<Object>>("Child");
                 const auto childProperty = settings.FindProperty<std::optional<Object>>("ChildProperty");
                 Check(child.Succeeded() && childProperty.Succeeded(),
                       "Object member lookup failed: " + child.diagnostic);
                 Check(child.value->Get(*owner.value).value && !child.value->Get(*owner.value).value->has_value(),
                       "An unset object member was not null");
                 const auto stored = counter.CreateObject(std::int32_t{6});
                 Check(child.value->Set(*owner.value, &*stored.value).Succeeded() &&
                           childProperty.value->Set(*owner.value, &*stored.value).Succeeded(),
                       "Storing an object failed");
                 const auto fetched = child.value->Get(*owner.value);
                 Check(fetched.Succeeded() && fetched.value->has_value() && value.Invoke(**fetched.value).value == 6,
                       "A stored object did not come back");
                 const auto otherObject = component.FindType(OtherType).value->CreateObject();
                 const auto wrong = child.value->Set(*owner.value, &*otherObject.value);
                 Check(wrong.status == CallStatus::InvalidArgument &&
                           wrong.diagnostic.find("instance of") != std::string::npos,
                       "An object of the wrong type was stored");
                 Check(child.value->Set(*owner.value, nullptr).Succeeded() &&
                           !child.value->Get(*owner.value).value->has_value(),
                       "Storing null did not clear the member");
                 Check(component.Release().status == CallStatus::InUse,
                       "Held objects did not keep the component loaded");
             }},
            {"strings round-trip as UTF-8 with multi-byte text and embedded nulls",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto strings = *component.FindType(StringsType).value;
                 const auto echo =
                     strings.FindMethod<std::optional<std::string>(std::optional<std::string_view>)>("EchoOrNull");
                 Check(echo.Succeeded(), "Lookup failed: " + echo.diagnostic);
                 const std::string wolf = "h\xC3\xA9llo \xCF\x80 \xF0\x9F\x90\xBA";
                 for (const std::string& sample : {std::string("plain"), wolf, std::string("A\0B", 3),
                                                   std::string("\0", 1), std::string("tail\0", 5), std::string()})
                 {
                     const auto echoed = echo.value->Invoke(std::string_view(sample));
                     Check(echoed.Succeeded() && echoed.value && *echoed.value && **echoed.value == sample,
                           "A string did not round-trip: " + sample);
                 }
                 const auto greeting = strings.FindMethod<std::optional<std::string>()>("Greeting");
                 const auto text = greeting.value->Invoke();
                 Check(text.Succeeded() && text.value && *text.value &&
                           **text.value == "h\xC3\xA9llo \xCF\x80 \xF0\x9F\x90\xBA \xE2\x80\x94 ok",
                       "A non-ASCII return was altered");
                 const auto withNull = strings.FindMethod<std::optional<std::string>()>("WithNull");
                 const auto embedded = withNull.value->Invoke();
                 Check(embedded.Succeeded() && **embedded.value == std::string("A\0B", 3),
                       "An embedded null in a return was lost");
                 const auto repeat =
                     strings.FindMethod<std::optional<std::string>(std::string_view, std::int32_t)>("Repeat");
                 const auto large = repeat.value->Invoke(std::string_view("\xCF\x80"), 1'000'000);
                 Check(
                     large.Succeeded() && **large.value ==
                                              [] {
                                                  std::string expected;
                                                  for (int i = 0; i < 1'000'000; ++i)
                                                      expected += "\xCF\x80";
                                                  return expected;
                                              }(),
                     "A large return was truncated");
             }},
            {"a null string is distinct from an empty one in both directions",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto strings = *component.FindType(StringsType).value;
                 const auto echo =
                     *strings.FindMethod<std::optional<std::string>(std::optional<std::string_view>)>("EchoOrNull")
                          .value;
                 const auto isNull = *strings.FindMethod<bool(std::optional<std::string_view>)>("IsNull").value;
                 const auto fromNull = echo.Invoke(std::nullopt);
                 const auto fromEmpty = echo.Invoke(std::string_view());
                 Check(fromNull.Succeeded() && fromNull.value && !*fromNull.value,
                       "A null argument did not come back null");
                 Check(fromEmpty.Succeeded() && fromEmpty.value && *fromEmpty.value && (*fromEmpty.value)->empty(),
                       "An empty argument did not come back empty");
                 Check(isNull.Invoke(std::nullopt).value == true && isNull.Invoke(std::string_view()).value == false,
                       "Managed code could not tell null from empty");
                 const auto nullReturn = strings.FindMethod<std::optional<std::string>()>("Null");
                 const auto emptyReturn = strings.FindMethod<std::optional<std::string>()>("Empty");
                 const auto fromNullReturn = nullReturn.value->Invoke();
                 const auto fromEmptyReturn = emptyReturn.value->Invoke();
                 Check(fromNullReturn.Succeeded() && fromNullReturn.value && !*fromNullReturn.value,
                       "A null return was not empty");
                 Check(fromEmptyReturn.Succeeded() && fromEmptyReturn.value && *fromEmptyReturn.value,
                       "An empty return was reported as null");
             }},
            {"malformed UTF-8 is rejected before managed code runs, and bad returns are diagnosed",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto strings = *component.FindType(StringsType).value;
                 const auto join =
                     *strings.FindMethod<std::optional<std::string>(std::string_view, std::string_view)>("Join").value;
                 for (const char* bad : {"\xFF", "\xC3", "\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80"})
                 {
                     const auto rejected = join.Invoke("x", bad);
                     Check(rejected.status == CallStatus::InvalidArgument && !rejected.value &&
                               rejected.diagnostic.find("UTF-8") != std::string::npos,
                           std::string("Malformed UTF-8 was accepted: ") + bad);
                 }
                 const auto unpaired = strings.FindMethod<std::optional<std::string>()>("UnpairedSurrogate");
                 Check(unpaired.value->Invoke().status == CallStatus::InvalidArgument,
                       "A return with no UTF-8 form was not diagnosed");
                 const auto throws = strings.FindMethod<std::optional<std::string>(std::string_view)>("Throws");
                 const auto thrown = throws.value->Invoke("\xCF\x80");
                 Check(thrown.status == CallStatus::ManagedException &&
                           thrown.diagnostic.find("Rejected: \xCF\x80") != std::string::npos,
                       "A managed exception with a string argument was not reported");
                 const auto again = join.Invoke("a", "b");
                 Check(again.Succeeded() && **again.value == "ab", "The method was unusable after a rejected call");
             }},
            {"strings are their own kind and work with constructors, instance methods and members",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto strings = *component.FindType(StringsType).value;
                 Check(strings.FindMethod<std::int32_t(const Object&)>("Length").status == CallStatus::NotFound &&
                           strings.FindMethod<std::int32_t(std::int32_t)>("Length").status == CallStatus::NotFound,
                       "Another kind matched a string parameter");
                 Check(strings.FindMethod<std::int32_t(std::string_view)>("Kind").value->Invoke("x").value == 1,
                       "A string did not pick the string overload");
                 const auto greeter = *component.FindType(GreeterType).value;
                 const auto object = greeter.CreateObject(std::string_view("W\xC3\xB6lfe"));
                 Check(object.Succeeded(), "A constructor with a string failed: " + object.diagnostic);
                 const auto greet = greeter.FindInstanceMethod<std::optional<std::string>(std::string_view)>("Greet");
                 const auto greeting = greet.value->Invoke(*object.value, "!");
                 Check(greeting.Succeeded() && **greeting.value == "Hello, W\xC3\xB6lfe!",
                       "An instance method with strings failed");

                 const auto settings = *component.FindType(SettingsType).value;
                 const auto label = *settings.FindField<std::optional<std::string>>("Label").value;
                 const auto caption = *settings.FindProperty<std::optional<std::string>>("Caption").value;
                 const auto staticLabel = *settings.FindField<std::optional<std::string>>("StaticLabel").value;
                 const auto instance = settings.CreateObject();
                 Check(!*label.Get(*instance.value).value, "An unset string field was not null");
                 for (const std::string& sample : {std::string("\xCF\x80"), std::string(), std::string("A\0B", 3)})
                 {
                     Check(label.Set(*instance.value, std::string_view(sample)).Succeeded() &&
                               **label.Get(*instance.value).value == sample,
                           "A string field did not round-trip");
                     Check(caption.Set(*instance.value, std::string_view(sample)).Succeeded() &&
                               **caption.Get(*instance.value).value == sample,
                           "A string property did not round-trip");
                     Check(staticLabel.Set(std::string_view(sample)).Succeeded() && **staticLabel.Get().value == sample,
                           "A static string field did not round-trip");
                 }
                 Check(label.Set(*instance.value, std::nullopt).Succeeded() && !*label.Get(*instance.value).value,
                       "Storing null did not clear a string field");
                 const auto readOnly = *settings.FindProperty<std::optional<std::string>>("ReadOnlyCaption").value;
                 Check(readOnly.Set(*instance.value, "x").status == CallStatus::InvalidArgument &&
                           **readOnly.Get(*instance.value).value == "fixed",
                       "A read-only string property was writable");
                 Check(label.Set(*instance.value, std::string_view("\xFF")).status == CallStatus::InvalidArgument,
                       "Malformed UTF-8 was stored in a field");
                 Check(settings.FindField<std::int32_t>("Label").status == CallStatus::NotFound &&
                           settings.FindField<std::optional<Object>>("Label").status == CallStatus::NotFound &&
                           settings.FindField<std::optional<std::string>>("Count").status == CallStatus::NotFound,
                       "A member matched a kind it is not");
             }},
            {"member handles stop working when their component is released",
             [&] {
                 auto host = StartHost(arguments);
                 auto released = Load(*host, arguments.component);
                 const auto other = Load(*host, arguments.component);
                 const auto stale =
                     *released.FindType(SettingsType).value->FindField<std::int32_t>("StaticCount").value;
                 const auto live = *other.FindType(SettingsType).value->FindField<std::int32_t>("StaticCount").value;
                 Check(released.Release().Succeeded(), "Release failed");
                 Check(stale.Get().status == CallStatus::InvalidHandle &&
                           stale.Set(1).status == CallStatus::InvalidHandle,
                       "A released component's member still worked");
                 Check(live.Get().Succeeded(), "Releasing one component invalidated another's member");
             }},
            {"overloads that differ only by object parameter types are ambiguous",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto factory = *component.FindType(FactoryType).value;
                 const auto describe = factory.FindMethod<std::int32_t(const Object&)>("Describe");
                 Check(describe.status == CallStatus::InvalidArgument &&
                           describe.diagnostic.find("ambiguous") != std::string::npos,
                       "An ambiguous object signature was not InvalidArgument");
                 Check(factory.FindMethod<std::int32_t(std::int32_t)>("Describe").status == CallStatus::NotFound,
                       "A by-value signature matched an object overload");
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
                 const auto noDependency = Call(component, CalculationType, "Add", 20, 22);
                 Check(noDependency.status == CallStatus::ManagedException && !noDependency.value &&
                           noDependency.diagnostic.find("FileNotFoundException") != std::string::npos,
                       "Missing dependency escaped or omitted diagnostics");
                 Check(Call(component, CalculationType, "LocalAdd", 20, 22).value == 42,
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
                     failure = Call(*retained, CalculationType, "Throw", 0, 0);
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
                 Check(Call(*retained, CalculationType, "Add", 20, 22).status == CallStatus::InvalidHandle,
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
                 Check(Call(first, SharedCalculatorType, "ContractIsShared", 0, 0).value == 1 &&
                           Call(second, SharedCalculatorType, "ContractIsShared", 0, 0).value == 1,
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
