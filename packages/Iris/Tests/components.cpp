// Component ownership checks against the real bridge and the published example component.
#include "../Examples/Hosting/ExampleNatives.hpp"
#include "Iris/Array.hpp"
#include "Iris/Host.hpp"
#include "TestHarness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Fixture
{
    struct Vec3
    {
        float X, Y, Z;
    };
    struct Pose
    {
        Vec3 Position;
        Vec3 Scale;
        double Weight;
        std::uint8_t Flags;
    };
    struct Packed
    {
        std::int32_t Low, High;
    };
    // Wrong on purpose: the managed Vec3 is three floats.
    struct WideVec3
    {
        double X, Y, Z;
    };
    struct SwappedVec3
    {
        float Y, X, Z;
    };
    struct FlagValue
    {
        bool Flag;
        std::int32_t Value;
    };
} // namespace Fixture

template <>
struct Iris::StructMapping<Fixture::Vec3>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Vec3";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::Vec3, X), IRIS_FIELD(Fixture::Vec3, Y), IRIS_FIELD(Fixture::Vec3, Z));
};

template <>
struct Iris::StructMapping<Fixture::Pose>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Pose";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::Pose, Position), IRIS_FIELD(Fixture::Pose, Scale),
                           IRIS_FIELD(Fixture::Pose, Weight), IRIS_FIELD(Fixture::Pose, Flags));
};

template <>
struct Iris::StructMapping<Fixture::Packed>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Packed";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::Packed, Low), IRIS_FIELD(Fixture::Packed, High));
};

template <>
struct Iris::StructMapping<Fixture::WideVec3>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Vec3";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD(Fixture::WideVec3, X), IRIS_FIELD(Fixture::WideVec3, Y), IRIS_FIELD(Fixture::WideVec3, Z));
};

template <>
struct Iris::StructMapping<Fixture::SwappedVec3>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Vec3";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD(Fixture::SwappedVec3, Y), IRIS_FIELD(Fixture::SwappedVec3, X), IRIS_FIELD(Fixture::SwappedVec3, Z));
};

template <>
struct Iris::StructMapping<Fixture::FlagValue>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.WithBool";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::FlagValue, Flag), IRIS_FIELD(Fixture::FlagValue, Value));
};

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
    constexpr std::string_view StructsType = "Iris.Examples.Component.Structs";
    constexpr std::string_view BodyType = "Iris.Examples.Component.Body";
    constexpr std::string_view NativesType = "Iris.Examples.Component.Natives";
    constexpr std::string_view ArraysType = "Iris.Examples.Component.Arrays";
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

    // The second build of the component (version B), named by the test runner.
    std::filesystem::path VersionBPath()
    {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* path = std::getenv("IRIS_TEST_COMPONENT_B");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        Check(path != nullptr && std::filesystem::exists(path),
              "IRIS_TEST_COMPONENT_B does not name version B of the component");
        return std::filesystem::absolute(path);
    }

    int VersionNumber(const Component& component)
    {
        const auto version = component.FindType("Iris.Examples.Component.ComponentVersion");
        Check(version.Succeeded(), "The Version type was missing");
        const auto number = version.value->FindMethod<std::int32_t()>("Number");
        Check(number.Succeeded(), "Version.Number was missing");
        const auto result = number.value->Invoke();
        Check(result.Succeeded(), "Version.Number failed");
        return *result.value;
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
                 // A rejected call never reached managed code; an accepted one runs exactly once.
                 const auto counted =
                     *strings.FindMethod<std::int32_t(std::string_view, std::string_view)>("Counted").value;
                 const auto callCount = *strings.FindField<std::int32_t>("Calls").value;
                 Check(counted.Invoke("x", "\xFF").status == CallStatus::InvalidArgument && callCount.Get().value == 0,
                       "Managed code ran for a malformed argument");
                 Check(counted.Invoke("x", "y").value == 1 && callCount.Get().value == 1,
                       "An accepted call did not run once");
                 const auto repeatOnce =
                     *strings.FindMethod<std::optional<std::string>(std::string_view, std::int32_t)>("Repeat").value;
                 Check(repeatOnce.Invoke("ab", 500'000).Succeeded() && callCount.Get().value == 2,
                       "A large return ran managed code more than once");
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
            {"blittable structs pass by value, return by value, and nest",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto structs = *component.FindType(StructsType).value;
                 using Fixture::Vec3;
                 const auto add = structs.FindMethod<Vec3(Vec3, Vec3)>("Add");
                 Check(add.Succeeded(), "A struct signature was not found: " + add.diagnostic);
                 const auto sum = add.value->Invoke(Vec3{1, 2, 3}, Vec3{10, 20, 30});
                 Check(sum.Succeeded() && sum.value->X == 11 && sum.value->Y == 22 && sum.value->Z == 33,
                       "A struct did not round-trip");
                 const auto dot = structs.FindMethod<float(Vec3, Vec3)>("Dot");
                 Check(dot.value->Invoke(Vec3{1, 2, 3}, Vec3{4, 5, 6}).value == 32.0f, "A struct argument was misread");
                 const auto scale = structs.FindMethod<Vec3(Vec3, float)>("Scale");
                 Check(scale.value->Invoke(Vec3{1, 2, 3}, 2.0f).value->Z == 6, "A struct and a primitive did not mix");
                 const auto mixed = structs.FindMethod<std::int32_t(std::int32_t, Vec3, double)>("Mixed");
                 Check(mixed.value->Invoke(1, Vec3{40, 0, 0}, 1.9).value == 42,
                       "Struct arguments shifted their neighbours");

                 // A struct containing structs, with padding before the double and after the byte.
                 using Fixture::Pose;
                 const auto move = structs.FindMethod<Pose(Pose, Vec3)>("Move");
                 Check(move.Succeeded(), "A nested struct signature was not found: " + move.diagnostic);
                 const auto moved = move.value->Invoke(Pose{{1, 1, 1}, {2, 2, 2}, 0.5, 7}, Vec3{1, 2, 3});
                 Check(moved.Succeeded() && moved.value->Position.Y == 3 && moved.value->Scale.Z == 2 &&
                           moved.value->Weight == 0.5 && moved.value->Flags == 8,
                       "A nested struct did not round-trip");

                 using Fixture::Packed;
                 const auto swap = structs.FindMethod<Packed(Packed)>("Swap");
                 Check(swap.value->Invoke(Packed{1, 2}).value->Low == 2,
                       "An explicit-layout struct did not round-trip");

                 // Struct parameters are matched exactly.
                 Check(structs.FindMethod<Vec3(Vec3)>("Add").status == CallStatus::NotFound &&
                           structs.FindMethod<float(Vec3, Vec3)>("Add").status == CallStatus::NotFound &&
                           structs.FindMethod<float(float, float)>("Dot").status == CallStatus::NotFound,
                       "A struct signature matched something else");
                 Check(structs.FindMethod<Packed(Vec3, Vec3)>("Add").status == CallStatus::NotFound,
                       "A different struct return type matched");
             }},
            {"struct mappings that do not match the managed type fail the lookup and never call",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto structs = *component.FindType(StructsType).value;
                 const auto wide = structs.FindMethod<Fixture::WideVec3(Fixture::WideVec3, Fixture::WideVec3)>("Add");
                 Check(wide.status == CallStatus::InvalidArgument && !wide.value &&
                           wide.diagnostic.find("24") != std::string::npos &&
                           wide.diagnostic.find("12") != std::string::npos,
                       "A size mismatch was not diagnosed: " + wide.diagnostic);
                 const auto swapped =
                     structs.FindMethod<Fixture::SwappedVec3(Fixture::SwappedVec3, Fixture::SwappedVec3)>("Add");
                 Check(swapped.status == CallStatus::InvalidArgument &&
                           swapped.diagnostic.find("order") != std::string::npos,
                       "A field-order mismatch was not diagnosed: " + swapped.diagnostic);
                 const auto body = *component.FindType(BodyType).value;
                 Check(body.FindField<Fixture::WideVec3>("Position").status == CallStatus::InvalidArgument &&
                           body.CreateObject(Fixture::SwappedVec3{1, 2, 3}).status == CallStatus::InvalidArgument,
                       "A bad mapping was accepted by a member or constructor");

                 const auto flip = structs.FindMethod<Fixture::FlagValue(Fixture::FlagValue)>("Flip");
                 Check(flip.status == CallStatus::InvalidArgument &&
                           flip.diagnostic.find("blittable") != std::string::npos,
                       "A non-blittable struct was accepted: " + flip.diagnostic);

                 // The right mapping still works afterwards.
                 Check(structs.FindMethod<Fixture::Vec3(Fixture::Vec3, Fixture::Vec3)>("Add").Succeeded(),
                       "A good mapping failed after bad ones");
             }},
            {"structs work as constructor arguments, instance method arguments, fields and properties",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto body = *component.FindType(BodyType).value;
                 using Fixture::Vec3;
                 const auto object = body.CreateObject(Vec3{1, 2, 3});
                 Check(object.Succeeded(), "A constructor with a struct failed: " + object.diagnostic);
                 const auto position = *body.FindField<Vec3>("Position").value;
                 Check(position.Get(*object.value).value->Y == 2, "A constructor stored the wrong struct");
                 const auto moved = body.FindInstanceMethod<Vec3(Vec3)>("Moved");
                 Check(moved.value->Invoke(*object.value, Vec3{10, 10, 10}).value->X == 11,
                       "An instance method with structs failed");
                 const auto velocity = *body.FindProperty<Vec3>("Velocity").value;
                 Check(velocity.Set(*object.value, Vec3{4, 5, 6}).Succeeded() &&
                           velocity.Get(*object.value).value->Z == 6,
                       "A struct property did not round-trip");
                 const auto origin = *body.FindField<Vec3>("Origin").value;
                 Check(origin.Set(Vec3{7, 8, 9}).Succeeded() && origin.Get().value->Y == 8,
                       "A static struct field did not round-trip");
                 const auto pose = *body.FindField<Fixture::Pose>("Pose").value;
                 Check(pose.Set(*object.value, Fixture::Pose{{1, 2, 3}, {4, 5, 6}, 7.5, 9}).Succeeded() &&
                           pose.Get(*object.value).value->Weight == 7.5,
                       "A nested struct field did not round-trip");
                 Check(body.FindField<float>("Position").status == CallStatus::NotFound &&
                           body.FindField<std::optional<Object>>("Position").status == CallStatus::NotFound,
                       "A struct field matched another kind");
             }},
            {"arrays of primitives, strings and structs: create, access, bulk copy and ranges",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 auto ints = Array<std::int32_t>::Create(component, 4);
                 Check(ints.Succeeded(), "Array creation failed: " + ints.diagnostic);
                 Check(ints.value->Length().value == 4U && ints.value->Get(2).value == 0, "A new array was wrong");
                 Check(ints.value->Set(2, -7).Succeeded() && ints.value->Get(2).value == -7,
                       "An element did not round-trip");
                 const std::int32_t source[] = {10, 20, 30};
                 std::int32_t back[4] = {};
                 Check(ints.value->CopyIn(1, source).Succeeded() && ints.value->CopyOut(0, back).Succeeded() &&
                           back[0] == 0 && back[1] == 10 && back[3] == 30,
                       "A bulk copy was wrong");
                 const auto past = ints.value->Get(4);
                 Check(past.status == CallStatus::InvalidArgument && !past.value &&
                           past.diagnostic.find("outside") != std::string::npos,
                       "An out-of-range read was not diagnosed: " + past.diagnostic);
                 Check(ints.value->Set(99, 1).status == CallStatus::InvalidArgument &&
                           ints.value->CopyIn(2, source).status == CallStatus::InvalidArgument &&
                           ints.value->CopyOut(5, std::span<std::int32_t>(back, 0)).status ==
                               CallStatus::InvalidArgument,
                       "An out-of-range write or copy was accepted");
                 Check(Array<std::int32_t>::Create(component, 0).value->Length().value == 0U, "An empty array failed");
                 Check(Array<std::int32_t>::Create(component, std::numeric_limits<std::size_t>::max()).status ==
                           CallStatus::InvalidArgument,
                       "An impossible length was accepted");
                 auto emptyArray = Array<std::int32_t>::Create(component, 0);
                 Check(emptyArray.value->CopyIn(0, std::span<const std::int32_t>()).Succeeded() &&
                           emptyArray.value->CopyOut(0, std::span<std::int32_t>()).Succeeded(),
                       "An empty bulk copy failed");
                 auto flagArray = Array<bool>::Create(component, 3);
                 const bool flagSource[] = {true, false, true};
                 bool flagBack[3] = {};
                 Check(flagArray.value->CopyIn(0, flagSource).Succeeded() &&
                           flagArray.value->CopyOut(0, flagBack).Succeeded() && flagBack[0] && !flagBack[1] &&
                           flagBack[2],
                       "A bool bulk copy was wrong");
                 // An object that is not an array is diagnosed, not misread.
                 auto counter = component.FindType(CounterType).value->CreateObject();
                 auto adopted = Array<std::int32_t>::FromObject(std::move(*counter.value));
                 Check(adopted.Length().status == CallStatus::InvalidArgument &&
                           adopted.Get(0).status == CallStatus::InvalidArgument,
                       "A non-array object was used as an array");

                 auto doubles = Array<double>::Create(component, 2);
                 Check(doubles.value->Set(1, 2.5).Succeeded() && doubles.value->Get(1).value == 2.5,
                       "A double element failed");
                 auto flags = Array<bool>::Create(component, 2);
                 Check(flags.value->Set(1, true).Succeeded() && flags.value->Get(1).value == true &&
                           flags.value->Get(0).value == false,
                       "A bool element failed");

                 auto words = Array<std::string>::Create(component, 3);
                 Check(words.value->Get(0).Succeeded() && !*words.value->Get(0).value,
                       "A new string element was not null");
                 const std::string wolf = "\xCF\x80\xF0\x9F\x90\xBA";
                 Check(words.value->Set(1, std::string_view(wolf)).Succeeded() && **words.value->Get(1).value == wolf,
                       "A string element did not round-trip");
                 Check(words.value->Set(2, std::string_view()).Succeeded() && **words.value->Get(2).value == "",
                       "An empty string element came back wrong");
                 Check(words.value->Set(1, std::nullopt).Succeeded() && !*words.value->Get(1).value,
                       "A null string element was not null");
                 Check(words.value->Set(0, std::string_view("\xFF")).status == CallStatus::InvalidArgument,
                       "Malformed UTF-8 was stored in an array");

                 using Fixture::Vec3;
                 auto points = Array<Vec3>::Create(component, 3);
                 Check(points.Succeeded(), "A struct array was not created: " + points.diagnostic);
                 const Vec3 line[] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
                 Vec3 readBack[3] = {};
                 Check(points.value->CopyIn(0, line).Succeeded() && points.value->CopyOut(0, readBack).Succeeded() &&
                           readBack[2].Z == 9 && points.value->Get(1).value->Y == 5,
                       "A struct array did not round-trip");
                 Check(points.value->Set(0, Vec3{9, 9, 9}).Succeeded() && points.value->Get(0).value->X == 9,
                       "A struct element did not store");
                 Check(Array<Fixture::WideVec3>::Create(component, 1).status == CallStatus::InvalidArgument,
                       "A bad struct mapping created an array");
             }},
            {"arrays pass to and return from managed methods, and null differs from empty",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto arrays = *component.FindType(ArraysType).value;
                 using Ints = Array<std::int32_t>;
                 const auto range = *arrays.FindMethod<std::optional<Ints>(std::int32_t)>("Range").value;
                 auto made = range.Invoke(5);
                 Check(made.Succeeded() && made.value && *made.value, "A returned array was missing");
                 Check((*made.value)->Length().value == 5U && (*made.value)->Get(4).value == 4,
                       "A returned array was wrong");
                 const auto sum = *arrays.FindMethod<std::int32_t(const Ints&)>("Sum").value;
                 Check(sum.Invoke(**made.value).value == 10, "An array argument was not passed");

                 // Null versus empty, in both directions.
                 const auto isNull = *arrays.FindMethod<bool(const Ints*)>("IsNull").value;
                 const Ints* none = nullptr;
                 Check(isNull.Invoke(none).value == true && isNull.Invoke(&**made.value).value == false,
                       "Managed code could not tell a null array from an array");
                 const auto empty = *arrays.FindMethod<std::optional<Ints>()>("Empty").value;
                 const auto nothing = *arrays.FindMethod<std::optional<Ints>()>("Null").value;
                 const auto emptyResult = empty.Invoke();
                 const auto nullResult = nothing.Invoke();
                 Check(emptyResult.Succeeded() && emptyResult.value && *emptyResult.value &&
                           (*emptyResult.value)->Length().value == 0U,
                       "An empty array return was null or wrong");
                 Check(nullResult.Succeeded() && nullResult.value && !*nullResult.value,
                       "A null array return was not empty");
                 const auto echo = *arrays.FindMethod<std::optional<Ints>(const Ints*)>("EchoOrNull").value;
                 Check(!*echo.Invoke(none).value && *echo.Invoke(&**made.value).value,
                       "A null array did not round-trip");

                 // The managed array is shared: a callee's writes are visible through the handle.
                 const auto fill = *arrays.FindMethod<std::optional<Ints>(const Ints&, std::int32_t)>("Fill").value;
                 const auto filled = fill.Invoke(**made.value, 7);
                 Check((*made.value)->Get(0).value == 7 && (*filled.value)->Get(3).value == 7,
                       "An array was copied rather than shared");

                 const auto scale =
                     *arrays.FindMethod<std::optional<Array<double>>(const Array<double>&, double)>("Scale").value;
                 auto doubles = Array<double>::Create(component, 2);
                 (void)doubles.value->Set(0, 1.5);
                 Check((*scale.Invoke(*doubles.value, 2.0).value)->Get(0).value == 3.0,
                       "A double array did not round-trip");

                 // String arrays, including a null element, and struct arrays.
                 const auto words = *arrays.FindMethod<std::optional<Array<std::string>>()>("Words").value;
                 const auto wordResult = words.Invoke();
                 const auto& list = **wordResult.value;
                 Check(list.Length().value == 4U && **list.Get(1).value == "\xCF\x80\xF0\x9F\x90\xBA" &&
                           **list.Get(2).value == "" && **list.Get(3).value == std::string("A\0B", 3),
                       "A string array return was wrong");
                 const auto withNull = *arrays.FindMethod<std::optional<Array<std::string>>()>("WordsWithNull").value;
                 const auto nullable = withNull.Invoke();
                 Check(!*(*nullable.value)->Get(1).value && **(*nullable.value)->Get(0).value == "first",
                       "A null string element was lost");
                 const auto join =
                     *arrays.FindMethod<std::optional<std::string>(const Array<std::string>&)>("Join").value;
                 Check(**join.Invoke(**nullable.value).value == "first+<null>+third",
                       "A string array argument was wrong");

                 using Fixture::Vec3;
                 const auto lineMethod = *arrays.FindMethod<std::optional<Array<Vec3>>(std::int32_t)>("Line").value;
                 const auto sumX = *arrays.FindMethod<float(const Array<Vec3>&)>("SumX").value;
                 const auto lineResult = lineMethod.Invoke(4);
                 Check((*lineResult.value)->Get(3).value->Z == 9 && sumX.Invoke(**lineResult.value).value == 6.0f,
                       "A struct array return or argument was wrong");

                 // Signatures are exact.
                 Check(arrays.FindMethod<std::int32_t(const Array<double>&)>("Sum").status == CallStatus::NotFound &&
                           arrays.FindMethod<std::int32_t(std::int32_t)>("Sum").status == CallStatus::NotFound &&
                           arrays.FindMethod<std::int32_t(const Object&)>("Count").status == CallStatus::NotFound &&
                           arrays.FindMethod<std::int32_t(const Ints&)>("Jagged").status == CallStatus::NotFound,
                       "An array signature matched something else");
             }},
            {"array handles are owned, validated and block release until freed",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 const auto other = Load(*host, arguments.component);
                 using Ints = Array<std::int32_t>;
                 auto owned = Ints::Create(component, 3);
                 Check(component.Release().status == CallStatus::InUse && component.IsLoaded(),
                       "A component with a live array was released");

                 // Moves transfer ownership; the source stops working.
                 Ints moved = std::move(*owned.value);
                 Check(moved.IsValid() && moved.Length().value == 3U && !owned.value->IsValid() &&
                           owned.value->Length().status == CallStatus::InvalidHandle,
                       "A moved array did not transfer ownership");

                 // Two returns of one managed array are independent handles over shared elements.
                 const auto arrays = *component.FindType(ArraysType).value;
                 const auto same = *arrays.FindMethod<std::optional<Ints>()>("SameArray").value;
                 auto first = std::move(*same.Invoke().value);
                 auto second = std::move(*same.Invoke().value);
                 Check(first->Set(0, 99).Succeeded() && second->Get(0).value == 99, "Handles did not share the array");
                 Check(first->Release().Succeeded() && !first->IsValid() && second->Get(0).value == 99,
                       "Releasing one handle affected the other");

                 // An array from another component is rejected before the call.
                 const auto sum = *arrays.FindMethod<std::int32_t(const Ints&)>("Sum").value;
                 auto foreign = Ints::Create(other, 1);
                 Check(sum.Invoke(*foreign.value).status == CallStatus::InvalidArgument,
                       "A foreign array was accepted");
                 Check(moved.Release().Succeeded() && sum.Invoke(moved).status == CallStatus::InvalidHandle,
                       "A released array was accepted as an argument");

                 // An array outlives nothing: once its component is gone it reports InvalidHandle.
                 std::optional<Ints> orphan;
                 {
                     Component dying = Load(*host, arguments.component);
                     orphan.emplace(std::move(*Ints::Create(dying, 2).value));
                     Check(orphan->Length().value == 2U, "An array was unusable in its own component");
                 }
                 Check(orphan->Length().status == CallStatus::InvalidHandle &&
                           orphan->Get(0).status == CallStatus::InvalidHandle,
                       "An array still worked after its component was destroyed");

                 // With everything freed the component releases; destroying a component force-releases arrays.
                 (void)second->Release();
                 Check(component.Release().Succeeded(), "A component with no live arrays could not be released");
                 auto survivor = Ints::Create(other, 1);
                 Ints stale = std::move(*survivor.value);
                 {
                     Component dying = Load(*host, arguments.component);
                     auto inner = Ints::Create(dying, 1);
                     Check(inner.Succeeded(), "Creating an array in a second component failed");
                 }
                 Check(stale.Length().Succeeded(), "Destroying another component affected this array");
             }},
            {"bound internal calls reach native code with arguments and results",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto natives = *component.FindType(NativesType).value;
                 const auto add = *natives.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("CallAdd").value;
                 Check(add.Invoke(20, 22).value == 42, "A bound function returned the wrong value");
                 Check(natives.FindMethod<double(double)>("CallHalf").value->Invoke(5.0).value == 2.5,
                       "A double function was wrong");
                 Check(natives.FindMethod<std::int64_t(std::int64_t, std::uint8_t)>("CallCombine")
                               .value->Invoke(7, 255)
                               .value == 7255,
                       "Mixed argument widths were wrong");
                 Check(natives.FindMethod<std::int32_t(std::string_view)>("CallMeasure")
                               .value->Invoke("\xCF\x80\xF0\x9F\x90\xBA")
                               .value == 6,
                       "A UTF-8 string did not arrive as bytes and a length");
                 using Fixture::Vec3;
                 const auto normalized = natives.FindMethod<Vec3(Vec3)>("CallNormalize").value->Invoke(Vec3{3, 0, 4});
                 Check(normalized.Succeeded() && std::abs(normalized.value->X - 0.6f) < 1e-6f &&
                           std::abs(normalized.value->Z - 0.8f) < 1e-6f,
                       "A struct passed by pointer was not updated");
                 Examples::recordedValue = 0;
                 Check(natives.FindMethod<void(std::int32_t)>("CallRecord").value->Invoke(9).Succeeded() &&
                           Examples::recordedValue == 9,
                       "A void function was not called");
             }},
            {"a C++ exception in an internal call is stopped at the boundary",
             [&] {
                 SinkGuard guard(arguments);
                 auto host = StartHost(arguments);
                 std::vector<Logged> logged;
                 std::mutex mutex;
                 Check(host->SetLogSink([&](const LogMessage& message) {
                               const std::lock_guard lock(mutex);
                               logged.push_back({message.level, std::string(message.text)});
                           })
                           .Succeeded(),
                       "The log sink was not set");
                 const auto component = Load(*host, arguments.component);
                 const auto natives = *component.FindType(NativesType).value;
                 const auto thrown = natives.FindMethod<std::int32_t()>("CallThrows").value->Invoke();
                 Check(thrown.Succeeded() && thrown.value == 0,
                       "The managed caller did not get a value-initialized result");
                 const std::lock_guard lock(mutex);
                 const bool reported = std::any_of(logged.begin(), logged.end(), [](const Logged& entry) {
                     return entry.level == LogLevel::Error && entry.text.find("native failure") != std::string::npos;
                 });
                 Check(reported, "The escaped exception was not reported through the log sink");
                 // The runtime is intact and later calls still work.
                 Check(natives.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("CallAdd")
                               .value->Invoke(1, 2)
                               .value == 3,
                       "Internal calls stopped working after an exception");
             }},
            {"internal call registration is idempotent and rejects conflicts",
             [&] {
                 const std::string name = "Iris.Examples.Component.Natives.Add";
                 Check(Iris::RegisterInternalCall<&Examples::Add>(name).Succeeded(),
                       "Registering the same function again failed");
                 Check(Iris::RegisterInternalCall<&Examples::Combine>(name).status == CallStatus::InvalidArgument,
                       "A different function under a used name was accepted");
                 Check(Iris::RegisterInternalCall<&Examples::Add>("").status == CallStatus::InvalidArgument,
                       "An empty name was accepted");
                 auto host = StartHost(arguments);
                 // A registration made after the host is up is accepted, and harmless without a matching field.
                 Check(
                     Iris::RegisterInternalCall<&Examples::Half>("Iris.Examples.Component.Natives.Unused").Succeeded(),
                     "Registering after start-up failed");
                 Check(
                     Iris::RegisterInternalCall<&Examples::Half>("Iris.Examples.Component.Natives.Unused").Succeeded(),
                     "Registering twice after start-up failed");
                 CheckCalculation(Load(*host, arguments.component));
             }},
            {"a component lists its public types in declaration order",
             [&] {
                 auto host = StartHost(arguments);
                 auto component = Load(*host, arguments.component);
                 const auto listed = component.GetTypes();
                 Check(listed.Succeeded() && listed.value->size() > 10,
                       "Listing returned too few types: " + listed.diagnostic);
                 std::vector<std::string> names;
                 for (const auto& type : *listed.value)
                 {
                     const auto info = type.GetInfo();
                     Check(info.Succeeded() && info.value->assemblyName == "Iris.Examples.Component",
                           "A listed type did not belong to the component");
                     names.push_back(info.value->fullName);
                 }
                 const auto index = [&](std::string_view name) {
                     return std::find(names.begin(), names.end(), std::string(name)) - names.begin();
                 };
                 const auto count = static_cast<std::ptrdiff_t>(names.size());
                 Check(index("Iris.Examples.Component.Actor") < count &&
                           index("Iris.Examples.Component.Boss") < count &&
                           index("Iris.Examples.Component.Actor") < index("Iris.Examples.Component.Player") &&
                           index("Iris.Examples.Component.Player") < index("Iris.Examples.Component.Enemy") &&
                           index("Iris.Examples.Component.Enemy") < index("Iris.Examples.Component.Boss"),
                       "Types were missing or out of declaration order");
                 Check(index("Iris.Examples.Component.HiddenActor") == count &&
                           std::none_of(names.begin(), names.end(),
                                        [](const std::string& name) { return name.find("Box") != std::string::npos; }),
                       "An internal or generic type was listed");
                 // Every listed type can also be found by name, and listing again gives the same set.
                 for (const auto& name : names)
                 {
                     Check(component.FindType(name).Succeeded(), "A listed type could not be found: " + name);
                 }
                 Check(component.GetTypes().value->size() == names.size(), "Listing twice differed");
                 Check(index("Iris.Examples.Component.Plain+Inner") < count &&
                           index("Iris.Examples.Component.Plain") < index("Iris.Examples.Component.Plain+Inner"),
                       "A nested public type was not listed after its enclosing type");

                 // The list is a set of handles: it stops working with the component, and holds nothing of its own.
                 auto types = std::move(*listed.value);
                 Check(component.Release().Succeeded(), "Listed types kept the component from being released");
                 Check(types.front().GetInfo().status == CallStatus::InvalidHandle &&
                           component.GetTypes().status == CallStatus::InvalidHandle,
                       "Listed types worked after their component was released");
             }},
            {"base types, interfaces and assignability across the component, a contract and the framework",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto player = *component.FindType("Iris.Examples.Component.Player").value;
                 const auto actor = *component.FindType("Iris.Examples.Component.Actor").value;
                 const auto boss = *component.FindType("Iris.Examples.Component.Boss").value;
                 const auto base = player.GetBaseType();
                 Check(base.Succeeded() && base.value->has_value() &&
                           (*base.value)->fullName == "Iris.Examples.Component.Actor" &&
                           (*base.value)->assemblyName == "Iris.Examples.Component",
                       "The base type was wrong");
                 const auto objectBase = actor.GetBaseType();
                 Check(objectBase.Succeeded() && objectBase.value->has_value() &&
                           (*objectBase.value)->fullName == "System.Object",
                       "A class without a base declared the wrong base");
                 const auto interfaces = player.GetInterfaces();
                 Check(interfaces.Succeeded() && interfaces.value->size() == 2 &&
                           (*interfaces.value)[0].fullName == "Iris.Examples.Contracts.IScript" &&
                           (*interfaces.value)[0].assemblyName == "Iris.Examples.Contracts" &&
                           (*interfaces.value)[1].fullName == "System.IDisposable",
                       "The interfaces were wrong or unsorted");
                 Check(boss.GetInterfaces().value->size() == 1, "An inherited interface was missed");

                 Check(player.IsAssignableTo(actor).value == true && boss.IsAssignableTo(actor).value == true &&
                           actor.IsAssignableTo(player).value == false && player.IsAssignableTo(player).value == true,
                       "Assignability between component types was wrong");
                 Check(player.IsAssignableTo("System.IDisposable").value == true &&
                           player.IsAssignableTo("Iris.Examples.Contracts.IScript").value == true &&
                           boss.IsAssignableTo("Iris.Examples.Contracts.IScript").value == true &&
                           player.IsAssignableTo("System.Object").value == true &&
                           component.FindType("Iris.Examples.Component.Plain")
                                   .value->IsAssignableTo("Iris.Examples.Contracts.IScript")
                                   .value == false,
                       "Assignability to framework and contract types was wrong");
                 const auto unknown = player.IsAssignableTo("No.Such.Type");
                 Check(unknown.status == CallStatus::NotFound && !unknown.value, "An unknown target was not NotFound");
                 Check(player.IsAssignableTo("").status == CallStatus::InvalidArgument,
                       "An empty target name was accepted");
                 const auto second = Load(*host, arguments.component);
                 Check(player.IsAssignableTo(*second.FindType("Iris.Examples.Component.Actor").value).status ==
                           CallStatus::InvalidArgument,
                       "A type of another component was accepted");
             }},
            {"custom attributes list their constructor and named arguments without running component code",
             [&] {
                 auto host = StartHost(arguments);
                 const auto component = Load(*host, arguments.component);
                 const auto attributes = component.FindType("Iris.Examples.Component.Player").value->GetAttributes();
                 Check(attributes.Succeeded(), "Attributes failed: " + attributes.diagnostic);
                 std::vector<AttributeInfo> scripts, others;
                 for (const auto& attribute : *attributes.value)
                 {
                     (attribute.type.fullName == "Iris.Examples.Component.ScriptAttribute" ? scripts : others)
                         .push_back(attribute);
                 }
                 Check(scripts.size() == 2 && others.size() == 2, "The attributes of Player were not listed");
                 // [Script("player", 3, Enabled = true, Weight = 0.5)]
                 const auto& player = scripts[0];
                 Check(player.arguments.size() == 4 && player.arguments[0].name.empty() &&
                           std::get<std::string>(player.arguments[0].value) == "player" &&
                           std::get<std::int64_t>(player.arguments[1].value) == 3 &&
                           player.arguments[1].typeName == "System.Int32" && player.arguments[2].name == "Enabled" &&
                           std::get<bool>(player.arguments[2].value) && player.arguments[3].name == "Weight" &&
                           std::get<double>(player.arguments[3].value) == 0.5,
                       "A constructor or named argument was wrong");
                 Check(scripts[1].arguments.size() == 1 &&
                           std::get<std::string>(scripts[1].arguments[0].value) == "hero",
                       "A second attribute of the same type was wrong");
                 Check(player.type.assemblyName == "Iris.Examples.Component", "An attribute's assembly was wrong");

                 // The framework's attribute, with a string argument.
                 const auto obsolete = std::find_if(others.begin(), others.end(), [](const AttributeInfo& attribute) {
                     return attribute.type.fullName == "System.ObsoleteAttribute";
                 });
                 Check(obsolete != others.end() && std::get<std::string>(obsolete->arguments.at(0).value) == "replaced",
                       "A framework attribute was wrong");

                 // Every shape of constructor argument.
                 const auto details = std::find_if(others.begin(), others.end(), [](const AttributeInfo& attribute) {
                     return attribute.type.fullName == "Iris.Examples.Component.DetailsAttribute";
                 });
                 Check(details != others.end() && details->arguments.size() == 8, "The Details attribute was wrong");
                 const auto& a = details->arguments;
                 Check(std::get<std::int64_t>(a[0].value) == 2 && a[0].typeName == "Iris.Examples.Component.Mode",
                       "An enum argument was wrong");
                 Check(std::get<std::string>(a[1].value) == "Iris.Examples.Component.Actor" &&
                           a[1].typeName == "System.Type",
                       "A Type argument was wrong");
                 Check(std::get<std::uint64_t>(a[2].value) == 200 &&
                           std::get<std::uint64_t>(a[3].value) == std::numeric_limits<std::uint64_t>::max(),
                       "An unsigned argument was wrong");
                 Check(std::get<double>(a[4].value) == 1.5 && std::get<std::uint64_t>(a[5].value) == 'x',
                       "A float or char argument was wrong");
                 Check(std::holds_alternative<std::monostate>(a[6].value) && a[6].typeName == "System.String",
                       "A null argument was wrong");
                 Check(std::holds_alternative<std::monostate>(a[7].value) && a[7].typeName == "System.Int32[]",
                       "An array argument was not reported as unrepresentable");

                 // A type with none.
                 Check(component.FindType("Iris.Examples.Component.Plain").value->GetAttributes().value->empty(),
                       "A type without attributes listed some");
             }},
            {"replacing a component with version B: new behaviour, and no handle from A ever works again",
             [&] {
                 const auto pathB = VersionBPath();
                 auto host = StartHost(arguments);
                 for (int cycle = 0; cycle < 4; ++cycle)
                 {
                     std::optional<Component> a;
                     a.emplace(Load(*host, arguments.component));
                     Check(VersionNumber(*a) == 1, "Version A did not behave as A");
                     const auto aVersion = *a->FindType("Iris.Examples.Component.ComponentVersion").value;
                     const auto aNumber = *aVersion.FindMethod<std::int32_t()>("Number").value;
                     const auto aMember =
                         *a->FindType(SettingsType).value->FindField<std::int32_t>("StaticCount").value;
                     const auto aTypes = *a->GetTypes().value;
                     auto aObject = a->FindType(CounterType).value->CreateObject();
                     auto aArray = Array<std::int32_t>::Create(*a, 3);
                     Check(aObject.Succeeded() && aArray.Succeeded(), "Version A's object or array was not created");
                     Check(a->Release().status == CallStatus::InUse,
                           "Live objects did not block replacing the component");
                     Check(aObject.value->Release().Succeeded() && aArray.value->Release().Succeeded() &&
                               a->Release().Succeeded(),
                           "Version A could not be released");

                     // Version B loads from its own path and behaves differently.
                     const auto b = Load(*host, pathB);
                     Check(VersionNumber(b) == 2, "Version B did not behave as B");
                     Check(b.FindType("Iris.Examples.Component.OnlyInB").Succeeded() &&
                               b.FindType("Iris.Examples.Component.OnlyInA").status == CallStatus::NotFound,
                           "Version B's types were wrong");

                     // Nothing from A works, and A's handles are not reinterpreted against B.
                     Check(aVersion.GetInfo().status == CallStatus::InvalidHandle &&
                               aNumber.Invoke().status == CallStatus::InvalidHandle &&
                               aMember.Get().status == CallStatus::InvalidHandle &&
                               aTypes.front().GetInfo().status == CallStatus::InvalidHandle &&
                               a->GetInfo().status == CallStatus::InvalidHandle &&
                               a->FindType(CounterType).status == CallStatus::InvalidHandle &&
                               aObject.value->Release().Succeeded() && !aObject.value->IsValid() &&
                               aArray.value->Length().status == CallStatus::InvalidHandle,
                           "A handle from version A survived the replacement");
                     Check(VersionNumber(b) == 2, "Using A's stale handles disturbed version B");
                 }
             }},
            {"a failed replacement leaves the host usable and the next version loads",
             [&] {
                 const auto pathB = VersionBPath();
                 auto host = StartHost(arguments);
                 {
                     const auto a = Load(*host, arguments.component);
                     Check(VersionNumber(a) == 1, "Version A did not behave as A");
                 }
                 const auto missing = host->LoadComponent(pathB.parent_path() / "Missing.Component.dll");
                 Check(!missing.Succeeded() && !missing.value && !missing.diagnostic.empty(),
                       "Loading a missing component did not fail with a diagnostic");
                 const auto b = Load(*host, pathB);
                 Check(VersionNumber(b) == 2, "Version B did not load after a failed replacement");
                 Check(VersionNumber(Load(*host, arguments.component)) == 1, "Version A could not be loaded again");
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
            {"type queries give the same answers when the contract assembly is shared",
             [&] {
                 auto configuration = arguments.configuration;
                 configuration.sharedAssemblies = {arguments.component.parent_path() / "Iris.Examples.Contracts.dll"};
                 auto host = std::make_unique<Host>();
                 Check(host->Initialize(configuration).success, "Initialization with a shared assembly failed");
                 const auto component = Load(*host, arguments.component);
                 Check(Call(component, SharedCalculatorType, "ContractIsShared", 0, 0).value == 1,
                       "The contract was not shared");
                 const auto player = *component.FindType("Iris.Examples.Component.Player").value;
                 const auto interfaces = player.GetInterfaces();
                 Check(interfaces.Succeeded() && interfaces.value->size() == 2 &&
                           (*interfaces.value)[0].fullName == "Iris.Examples.Contracts.IScript",
                       "The shared contract interface was not reported");
                 Check(player.IsAssignableTo("Iris.Examples.Contracts.IScript").value == true &&
                           component.FindType(SharedCalculatorType)
                                   .value->IsAssignableTo("Iris.Examples.Contracts.CalculatorBase")
                                   .value == true &&
                           player.IsAssignableTo("Iris.Examples.Contracts.CalculatorBase").value == false,
                       "Assignability to a shared contract type was wrong");
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

    // Nothing is registered in this process: the component cannot load until its internal calls are.
    int RunUnbound(const Arguments& arguments)
    {
        return Tests::RunCases({
            {"a component with unbound internal calls fails to load, then loads once they are registered",
             [&] {
                 auto host = StartHost(arguments);
                 const auto missing = host->LoadComponent(arguments.component);
                 Check(missing.status == CallStatus::InvalidArgument && !missing.value &&
                           missing.diagnostic.find("7 internal call(s)") != std::string::npos &&
                           missing.diagnostic.find("Iris.Examples.Component.Natives.Add") != std::string::npos &&
                           missing.diagnostic.find("no internal call is registered") != std::string::npos,
                       "Unbound internal calls were not diagnosed: " + missing.diagnostic);
                 // Registering after start-up, then loading, works; the failed load left nothing behind.
                 Check(Iris::Examples::RegisterNatives().Succeeded(), "Registering after a failed load failed");
                 const auto component = Load(*host, arguments.component);
                 const auto natives = *component.FindType(NativesType).value;
                 Check(natives.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("CallAdd")
                               .value->Invoke(2, 3)
                               .value == 5,
                       "A function registered after start-up did not bind");
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
    if (argc != 4 && argc != 5)
    {
        std::cerr << "Usage: IrisComponentChecks runtimeconfig.json bridge.dll component.dll\n";
        return EXIT_FAILURE;
    }
    if (argc == 5)
    {
        return RunUnbound({{argv[1], argv[2], {}}, std::filesystem::absolute(argv[3])});
    }
    // Registered before any host exists: registrations apply to components loaded afterwards.
    if (const auto natives = Iris::Examples::RegisterNatives(); !natives.Succeeded())
    {
        std::cerr << "Registering native functions failed: " << natives.diagnostic << '\n';
        return EXIT_FAILURE;
    }
    return Run({{argv[1], argv[2], {}}, std::filesystem::absolute(argv[3])});
}
