// Component ownership checks against the real bridge and the published example component.
#include "../Examples/Hosting/ExampleNatives.hpp"
#include "Iris/Iris.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
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
    struct Glyph
    {
        char16_t Letter;
        bool Bold;
        std::uint16_t Code;
    };
    enum class Gear : std::int32_t
    {
        Off = 0,
        On = 1,
        Auto = 7
    };
    enum class Shade : std::uint8_t
    {
        Dark = 1,
        Light = 2
    };
    enum class Offset : std::int16_t
    {
        Negative = -5,
        Positive = 5
    };
    // Wrong on purpose: the managed Gear is an int.
    enum class WideGear : std::int64_t
    {
        Off = 0
    };
    struct Setting
    {
        Gear Current;
        Shade Tint;
        bool Locked;
        std::int32_t Level;
    };
    struct WideSetting
    {
        WideGear Current;
        Shade Tint;
        bool Locked;
        std::int32_t Level;
    };
    struct Label
    {
        std::int32_t Id;
        char Text[16];
        char16_t Wide[4];
    };
    // Wrong on purpose: the same size and offsets as Label, but the text is 15 bytes long.
    struct ShortLabel
    {
        std::int32_t Id;
        std::uint8_t Text[15];
        char16_t Wide[4];
    };
    // Wrong on purpose: the text elements are signed bytes.
    struct SignedLabel
    {
        std::int32_t Id;
        std::int8_t Text[16];
        char16_t Wide[4];
    };
    struct Bundle
    {
        float Weights[4];
        Vec3 Points[2];
        std::array<Gear, 3> Gears;
        bool Ready[3];
    };
    // Wrong on purpose: the managed WithText holds a string.
    struct TextHolder
    {
        std::int32_t Name;
    };
    // Wrong on purpose: no such managed type.
    struct Ghost
    {
        std::int32_t Value;
    };
    // A nested struct whose inner mapping is wrong.
    struct WidePose
    {
        WideVec3 Position;
        Vec3 Scale;
        double Weight;
        std::uint8_t Flags;
    };
    // A struct from the shared contract, with C++ member names that differ from the managed field names.
    struct SharedPoint
    {
        std::int32_t x;
        std::int32_t y;
    };
} // namespace Fixture

template <>
struct Iris::StructMapping<Fixture::SharedPoint>
{
    static constexpr std::string_view typeName = "Iris.Examples.Contracts.SharedPoint";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fixture::SharedPoint, x, "X"),
                                                      IRIS_FIELD_AS(Fixture::SharedPoint, y, "Renamed"));
};

template <>
struct Iris::EnumMapping<Fixture::Gear>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Gear";
};
template <>
struct Iris::EnumMapping<Fixture::Shade>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Shade";
};
template <>
struct Iris::EnumMapping<Fixture::Offset>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Offset";
};
template <>
struct Iris::EnumMapping<Fixture::WideGear>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Gear";
};
template <>
struct Iris::StructMapping<Fixture::Setting>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Setting";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::Setting, Current), IRIS_FIELD(Fixture::Setting, Tint),
                           IRIS_FIELD(Fixture::Setting, Locked), IRIS_FIELD(Fixture::Setting, Level));
};
template <>
struct Iris::StructMapping<Fixture::WideSetting>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Setting";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::WideSetting, Current), IRIS_FIELD(Fixture::WideSetting, Tint),
                           IRIS_FIELD(Fixture::WideSetting, Locked), IRIS_FIELD(Fixture::WideSetting, Level));
};

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

template <>
struct Iris::StructMapping<Fixture::Glyph>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Glyph";
    static constexpr auto fields = Iris::StructFields(
        IRIS_FIELD(Fixture::Glyph, Letter), IRIS_FIELD(Fixture::Glyph, Bold), IRIS_FIELD(Fixture::Glyph, Code));
};

template <>
struct Iris::StructMapping<Fixture::Label>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Label";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fixture::Label, Id), IRIS_FIELD(Fixture::Label, Text),
                                                      IRIS_FIELD(Fixture::Label, Wide));
};
template <>
struct Iris::StructMapping<Fixture::ShortLabel>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Label";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::ShortLabel, Id), IRIS_FIELD(Fixture::ShortLabel, Text),
                           IRIS_FIELD(Fixture::ShortLabel, Wide));
};
template <>
struct Iris::StructMapping<Fixture::SignedLabel>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Label";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::SignedLabel, Id), IRIS_FIELD(Fixture::SignedLabel, Text),
                           IRIS_FIELD(Fixture::SignedLabel, Wide));
};
template <>
struct Iris::StructMapping<Fixture::Bundle>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Bundle";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::Bundle, Weights), IRIS_FIELD(Fixture::Bundle, Points),
                           IRIS_FIELD(Fixture::Bundle, Gears), IRIS_FIELD(Fixture::Bundle, Ready));
};

template <>
struct Iris::StructMapping<Fixture::TextHolder>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.WithText";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fixture::TextHolder, Name));
};
template <>
struct Iris::StructMapping<Fixture::Ghost>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Missing";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fixture::Ghost, Value));
};
template <>
struct Iris::StructMapping<Fixture::WidePose>
{
    static constexpr std::string_view typeName = "Iris.Examples.Component.Pose";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::WidePose, Position), IRIS_FIELD(Fixture::WidePose, Scale),
                           IRIS_FIELD(Fixture::WidePose, Weight), IRIS_FIELD(Fixture::WidePose, Flags));
};

namespace
{
    using namespace Iris;

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
    constexpr std::string_view ValuesType = "Iris.Examples.Component.Values";
    constexpr std::string_view ArraysType = "Iris.Examples.Component.Arrays";
    constexpr std::string_view GreeterType = "Iris.Examples.Component.Greeter";
    constexpr std::string_view SharedCalculatorType = "Iris.Examples.Component.SharedCalculator";

    static_assert(!std::is_copy_constructible_v<Component> && std::is_nothrow_move_constructible_v<Component>);

    struct Arguments
    {
        HostConfiguration configuration;
        std::filesystem::path component;
    };

    std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the component checks need the files it built.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    // CTest names the files in the environment: IRIS_TEST_RUNTIMECONFIG, IRIS_TEST_BRIDGE and IRIS_TEST_COMPONENT.
    const Arguments checkArguments{
        {FromEnvironment("IRIS_TEST_RUNTIMECONFIG"), FromEnvironment("IRIS_TEST_BRIDGE"), {}},
        FromEnvironment("IRIS_TEST_COMPONENT")};

    std::unique_ptr<Host> StartHost(const Arguments& arguments)
    {
        auto host = std::make_unique<Host>();
        const auto initialized = host->Initialize(arguments.configuration);
        {
            INFO("Initialization failed: " + initialized.diagnostic);
            REQUIRE(initialized.success);
        }
        return host;
    }

    Component Load(const Host& host, const std::filesystem::path& path)
    {
        auto loaded = host.LoadComponent(path);
        {
            INFO("Component load failed: " + loaded.diagnostic);
            REQUIRE((loaded.Succeeded() && loaded.value->IsLoaded()));
        }
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
        {
            INFO("Component identity incorrect");
            REQUIRE((info.Succeeded() && info.value->assemblyName == "Iris.Examples.Component"));
        }
    }

    void CheckCalculation(const Component& owner)
    {
        const auto result = Call(owner, CalculationType, "Add", 20, 22);
        {
            INFO("Component calculation failed: " + result.diagnostic);
            REQUIRE(
                (result.Succeeded() && result.value == 42 && result.diagnostic.empty() && !result.diagnosticTruncated));
        }
    }

    void CheckUnload(const Component& owner, bool released)
    {
        const auto result = owner.GetUnloadState();
        {
            INFO("Unload observation did not match the owner's lifecycle");
            REQUIRE((result.Succeeded() && result.diagnostic.empty() && !result.diagnosticTruncated &&
                     (released ? (*result.value == ComponentUnloadState::Pending ||
                                  *result.value == ComponentUnloadState::Collected)
                               : *result.value == ComponentUnloadState::NotRequested)));
        }
    }

    void CheckInvalidHandle(const Component& owner, const char* message)
    {
        {
            INFO(message);
            REQUIRE((owner.GetInfo().status == CallStatus::InvalidHandle &&
                     Call(owner, CalculationType, "LocalAdd", 20, 22).status == CallStatus::InvalidHandle));
        }
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
        {
            INFO("IRIS_TEST_COMPONENT_B does not name version B of the component");
            REQUIRE((path != nullptr && std::filesystem::exists(path)));
        }
        return std::filesystem::absolute(path);
    }

    int VersionNumber(const Component& component)
    {
        const auto version = component.FindType("Iris.Examples.Component.ComponentVersion");
        {
            INFO("The Version type was missing");
            REQUIRE(version.Succeeded());
        }
        const auto number = version.value->FindMethod<std::int32_t()>("Number");
        {
            INFO("Version.Number was missing");
            REQUIRE(number.Succeeded());
        }
        const auto result = number.value->Invoke();
        {
            INFO("Version.Number failed");
            REQUIRE(result.Succeeded());
        }
        return *result.value;
    }

    TEST_CASE("a loaded component reports its identity and NotRequested", "[.components]")
    {
        Host unready;
        {
            INFO("An uninitialized host loaded a component");
            REQUIRE(unready.LoadComponent(checkArguments.component).status == CallStatus::NotInitialized);
        }
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        CheckInfo(component);
        CheckUnload(component, false);
        CheckCalculation(component);
    }

    // Must run before the shared-assembly case: sharing is process-wide and permanent.
    TEST_CASE("without a shared declaration, a component uses its private contract copy", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto shared = Call(component, SharedCalculatorType, "ContractIsShared", 0, 0);
        {
            INFO("The contract resolved to a shared copy without a declaration");
            REQUIRE(shared.value == 0);
        }
    }

    TEST_CASE("results reach their value through * and ->, and move it out of an rvalue", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto initialized = host->Initialize(checkArguments.configuration);
        const auto discovered = Host::DiscoverHostFxr(checkArguments.configuration.dotnetRoot);
        {
            INFO("Succeeded() disagreed with success");
            REQUIRE((initialized.Succeeded() && initialized.Succeeded() == initialized.success &&
                     discovered.Succeeded() == discovered.success));
        }
        auto loaded = host->LoadComponent(checkArguments.component);
        {
            INFO("operator-> did not reach the component");
            REQUIRE((loaded.Succeeded() && loaded->IsLoaded()));
        }
        Component component = *std::move(loaded);
        {
            INFO("operator* && did not move the owner out");
            REQUIRE((component.IsLoaded() && !loaded.value->IsLoaded()));
        }
        const auto calculations = component.FindType(CalculationType);
        {
            INFO("Calculations was missing");
            REQUIRE(calculations.Succeeded());
        }
        const auto add = calculations->FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add");
        {
            INFO("Add was missing");
            REQUIRE(add.Succeeded());
        }
        const auto sum = add->Invoke(20, 22);
        const auto& constSum = sum;
        {
            INFO("operator* did not read the value");
            REQUIRE((sum.Succeeded() && *sum == 42 && *constSum == 42));
        }
        // Dereferencing a temporary result gives a value, never a reference into the dead result, so
        // iterating one is safe.
        static_assert(std::is_same_v<decltype(*component.FindType(SettingsType)), Type>);
        int listed = 0;
        for (const Type& type : *component.GetTypes())
        {
            listed += type.GetInfo().Succeeded() ? 1 : 0;
        }
        {
            INFO("Iterating a dereferenced temporary result failed");
            REQUIRE(listed > 10);
        }
        const auto settingsType = component.FindType(SettingsType);
        {
            INFO("Settings was missing");
            REQUIRE(settingsType.Succeeded());
        }
        const Type settings = *settingsType;
        auto created = settings.CreateObject();
        {
            INFO("Creating Settings failed");
            REQUIRE(created.Succeeded());
        }
        Object object = *std::move(created);
        {
            INFO("Moving an Object out of a result failed");
            REQUIRE(object.IsValid());
        }
        // A CallResult<bool> is not a bool: the answer and the call's success stay separate.
        static_assert(!std::is_constructible_v<bool, CallResult<bool>>);
        const auto assignable = settings.IsAssignableTo("System.IDisposable");
        {
            INFO("IsAssignableTo answered wrongly");
            REQUIRE((assignable.Succeeded() && !*assignable));
        }
    }

    TEST_CASE("malformed, empty and null-containing names are rejected", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto malformedType = Call(component, "\xC3(", "Add", 20, 22);
        const auto malformedMethod = Call(component, CalculationType, "\xFF", 20, 22);
        {
            INFO("Malformed UTF-8 names accepted");
            REQUIRE((malformedType.status == CallStatus::InvalidArgument && !malformedType.value &&
                     malformedType.diagnostic.find("DecoderFallbackException") != std::string::npos &&
                     malformedMethod.status == CallStatus::InvalidArgument && !malformedMethod.value));
        }
        {
            INFO("Empty or null-containing names accepted");
            REQUIRE((Call(component, {}, "Add", 20, 22).status == CallStatus::InvalidArgument &&
                     Call(component, CalculationType, {}, 20, 22).status == CallStatus::InvalidArgument &&
                     Call(component, CalculationType, std::string("Add\0suffix", 10), 20, 22).status ==
                         CallStatus::InvalidArgument));
        }
        CheckCalculation(component);
    }

    TEST_CASE("load failures return diagnostics and leave the host ready", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto missingPath = checkArguments.component;
        missingPath += ".missing";
        const auto missing = host->LoadComponent(missingPath);
        {
            INFO("Missing assembly diagnostic lost");
            REQUIRE((missing.status == CallStatus::ManagedException && !missing.value &&
                     missing.diagnostic.find("FileNotFoundException") != std::string::npos));
        }
        {
            INFO("Empty path accepted");
            REQUIRE(host->LoadComponent({}).status == CallStatus::InvalidArgument);
        }
        // An existing JSON file, not a managed assembly.
        const auto invalid = host->LoadComponent(checkArguments.configuration.runtimeConfigPath);
        {
            INFO("Invalid assembly diagnostic lost");
            REQUIRE((invalid.status == CallStatus::ManagedException && !invalid.value &&
                     invalid.diagnostic.find("BadImageFormatException") != std::string::npos));
        }
#if defined(_WIN32)
        // An unpaired surrogate cannot become UTF-8; it is an argument error, not a C++ exception.
        const auto unpaired = host->LoadComponent(std::filesystem::path(L"component-\xD800.dll"));
        {
            INFO("A path that is not valid Unicode was not rejected as an argument");
            REQUIRE((unpaired.status == CallStatus::InvalidArgument && !unpaired.value));
        }
#endif
        {
            INFO("A load failure reset readiness");
            REQUIRE(host->IsInitialized());
        }
        CheckCalculation(Load(*host, checkArguments.component));
    }

    TEST_CASE("a type is found by name and reports its names", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto found = component.FindType(CalculationType);
        {
            INFO("A public type was not found: " + found.diagnostic);
            REQUIRE(found.Succeeded());
        }
        const auto info = found.value->GetInfo();
        {
            INFO("Type names incorrect");
            REQUIRE((info.Succeeded() && info.value->fullName == CalculationType &&
                     info.value->assemblyName == "Iris.Examples.Component"));
        }
        // Handles are cheap values: a copy works the same.
        const Type copy = *found.value;
        {
            INFO("A copied type handle stopped working");
            REQUIRE(copy.GetInfo().value->fullName == CalculationType);
        }
    }

    TEST_CASE("missing and invalid type names return diagnostics", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        for (const char* name :
             {"Iris.Examples.Component.Missing", "Iris.Examples.Component.HiddenCalculation",
              "Iris.Examples.Component.OpenCalculation`1", "Iris.Examples.Dependency.DependencyMath"})
        {
            const auto missing = component.FindType(name);
            {
                INFO(std::string("Not a NotFound naming the type: ") + name);
                REQUIRE((missing.status == CallStatus::NotFound && !missing.value &&
                         missing.diagnostic.find(name) != std::string::npos));
            }
        }
        {
            INFO("An empty, null-containing or malformed name was accepted");
            REQUIRE((component.FindType({}).status == CallStatus::InvalidArgument &&
                     component.FindType(std::string("A\0B", 3)).status == CallStatus::InvalidArgument &&
                     component.FindType("\xFF").status == CallStatus::InvalidArgument));
        }
        {
            INFO("Lookup did not recover after failures");
            REQUIRE(component.FindType(CalculationType).Succeeded());
        }
    }

    TEST_CASE("type handles become invalid when their component is released", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto released = Load(*host, checkArguments.component);
        const auto other = Load(*host, checkArguments.component);
        const auto stale = *released.FindType(CalculationType).value;
        const auto live = *other.FindType(CalculationType).value;
        {
            INFO("Release failed");
            REQUIRE(released.Release().Succeeded());
        }
        const auto info = stale.GetInfo();
        {
            INFO("A released component's type handle worked");
            REQUIRE((info.status == CallStatus::InvalidHandle && !info.value));
        }
        {
            INFO("A released owner still resolved types");
            REQUIRE(released.FindType(CalculationType).status == CallStatus::InvalidHandle);
        }
        {
            INFO("Releasing one component invalidated another's type handle");
            REQUIRE(live.GetInfo().Succeeded());
        }
        host.reset(); // A type handle outlives its host while its component is alive.
        {
            INFO("A type handle did not outlive the host");
            REQUIRE(live.GetInfo().Succeeded());
        }
    }

    TEST_CASE("typed methods round-trip every supported value kind", "[.components]")
    {
        static_assert(PrimitiveValue<std::int32_t> && PrimitiveValue<bool> && PrimitiveValue<double> &&
                      !PrimitiveValue<std::string> && !PrimitiveValue<char> && !PrimitiveValue<void*>);
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto type = *component.FindType(CalculationType).value;
        const auto find = [&type](auto signature, std::string_view name) {
            using Signature = typename decltype(signature)::type;
            auto found = type.FindMethod<Signature>(name);
            {
                INFO("Method lookup failed for " + std::string(name) + ": " + found.diagnostic);
                REQUIRE(found.Succeeded());
            }
            return *found.value;
        };
        {
            INFO("Add(int, int) returned the wrong value");
            REQUIRE(find(std::type_identity<std::int32_t(std::int32_t, std::int32_t)>{}, "Add").Invoke(20, 22).value ==
                    42);
        }
        {
            INFO("A negative int did not round-trip");
            REQUIRE(find(std::type_identity<std::int32_t(std::int32_t, std::int32_t)>{}, "Add").Invoke(-5, 2).value ==
                    -3);
        }
        {
            INFO("The long overload was not selected");
            REQUIRE(find(std::type_identity<std::int64_t(std::int64_t, std::int64_t)>{}, "Add")
                        .Invoke(5'000'000'000LL, 7)
                        .value == 5'000'000'007LL);
        }
        {
            INFO("double and float did not round-trip");
            REQUIRE(find(std::type_identity<double(double, float)>{}, "Scale").Invoke(2.5, 4.0f).value == 10.0);
        }
        {
            INFO("bool did not round-trip");
            REQUIRE((find(std::type_identity<bool(bool)>{}, "Not").Invoke(true).value == false &&
                     find(std::type_identity<bool(bool)>{}, "Not").Invoke(false).value == true));
        }
        {
            INFO("byte did not round-trip");
            REQUIRE(find(std::type_identity<std::uint8_t(std::uint8_t)>{}, "WrapByte").Invoke(255).value == 0);
        }
        {
            INFO("Signed small integers did not round-trip");
            REQUIRE((find(std::type_identity<std::int8_t(std::int8_t)>{}, "NegateSByte").Invoke(-7).value == 7 &&
                     find(std::type_identity<std::int16_t(std::int16_t)>{}, "NegateShort").Invoke(300).value == -300));
        }
        {
            INFO("Unsigned integers did not round-trip");
            REQUIRE(
                (find(std::type_identity<std::uint16_t(std::uint16_t)>{}, "IncrementUShort").Invoke(65535).value == 0 &&
                 find(std::type_identity<std::uint32_t(std::uint32_t)>{}, "IncrementUInt")
                         .Invoke(std::numeric_limits<std::uint32_t>::max())
                         .value == 0));
        }
        {
            INFO("ulong did not round-trip");
            REQUIRE(find(std::type_identity<std::uint64_t(std::uint64_t, std::uint64_t)>{}, "Sum64")
                        .Invoke(std::numeric_limits<std::uint64_t>::max(), 2)
                        .value == 1);
        }
        {
            INFO("float did not round-trip");
            REQUIRE(find(std::type_identity<float(float)>{}, "Halve").Invoke(5.0f).value == 2.5f);
        }

        // A void method really runs, and the same lookup returns an equivalent handle.
        const auto touch = find(std::type_identity<void()>{}, "Touch");
        const auto touches = find(std::type_identity<std::int32_t()>{}, "Touches");
        const auto before = *touches.Invoke().value;
        {
            INFO("A void method did not run once per call");
            REQUIRE((touch.Invoke().Succeeded() && touch.Invoke().Succeeded() && touches.Invoke().value == before + 2));
        }
    }

    TEST_CASE("method lookup failures are NotFound or InvalidArgument with diagnostics", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto type = *component.FindType(CalculationType).value;
        const auto notFound = [](const auto& result, const char* what) {
            {
                INFO(std::string("Not a NotFound with a diagnostic: ") + what);
                REQUIRE((result.status == CallStatus::NotFound && !result.value && !result.diagnostic.empty()));
            }
        };
        notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Missing"), "missing name");
        notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("WrongReturn"), "wrong return type");
        notFound(type.FindMethod<std::int32_t(std::int32_t, std::int64_t)>("Add"), "wrong parameter types");
        notFound(type.FindMethod<std::int32_t(std::int32_t)>("Add"), "wrong parameter count");
        notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("WrongParameters"),
                 "parameters that would only match by widening");
        notFound(type.FindMethod<std::int32_t(std::int16_t, std::int16_t)>("Add"), "narrower parameters");
        notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Generic"), "generic method");
        notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("UnmanagedOnly"), "unmanaged-only method");
        notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Hidden"), "non-public method");
        notFound(type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("add"), "wrong case");
        {
            INFO("An empty, null-containing or malformed name was accepted");
            REQUIRE((type.FindMethod<void()>({}).status == CallStatus::InvalidArgument &&
                     type.FindMethod<void()>(std::string("A\0B", 3)).status == CallStatus::InvalidArgument &&
                     type.FindMethod<void()>("\xFF").status == CallStatus::InvalidArgument));
        }
        const auto add = type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add");
        {
            INFO("Lookup did not recover after failures");
            REQUIRE((add.Succeeded() && add.value->Invoke(20, 22).value == 42));
        }
    }

    TEST_CASE("component exceptions in typed calls are ManagedException and the next call works", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto type = *component.FindType(CalculationType).value;
        const auto add = *type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add").value;
        const auto thrown = type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Throw").value->Invoke(0, 0);
        {
            INFO("Component exception details or Unicode lost");
            REQUIRE((thrown.status == CallStatus::ManagedException && !thrown.value &&
                     thrown.diagnostic.find("InvalidOperationException") != std::string::npos &&
                     thrown.diagnostic.find("\xCF\x80 \xF0\x9F\x90\xBA") != std::string::npos));
        }
        const auto argument =
            type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("ThrowArgument").value->Invoke(0, 0);
        {
            INFO("A component's ArgumentException was reported as an invalid bridge argument");
            REQUIRE((argument.status == CallStatus::ManagedException && !argument.value));
        }
        const auto overflow = add.Invoke(std::numeric_limits<std::int32_t>::max(), 1);
        {
            INFO("A component's OverflowException was not a ManagedException");
            REQUIRE((overflow.status == CallStatus::ManagedException && !overflow.value &&
                     overflow.diagnostic.find("OverflowException") != std::string::npos));
        }
        const auto bounded = type.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("LongError").value->Invoke(0, 0);
        {
            INFO("Component diagnostic was not bounded");
            REQUIRE((bounded.status == CallStatus::ManagedException && bounded.diagnosticTruncated));
        }
        {
            INFO("A call did not recover after exceptions");
            REQUIRE(add.Invoke(20, 22).value == 42);
        }
    }

    TEST_CASE("method handles stop working when their component is released", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto released = Load(*host, checkArguments.component);
        const auto other = Load(*host, checkArguments.component);
        const auto stale = *(*released.FindType(CalculationType).value)
                                .FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add")
                                .value;
        const auto live =
            *(*other.FindType(CalculationType).value).FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add").value;
        const auto staleType = *released.FindType(CalculationType).value;
        {
            INFO("Release failed");
            REQUIRE(released.Release().Succeeded());
        }
        const auto result = stale.Invoke(20, 22);
        {
            INFO("A released component's method still ran");
            REQUIRE((result.status == CallStatus::InvalidHandle && !result.value));
        }
        {
            INFO("A released component's type still resolved methods");
            REQUIRE(staleType.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("Add").status ==
                    CallStatus::InvalidHandle);
        }
        {
            INFO("Releasing one component invalidated another's method");
            REQUIRE(live.Invoke(20, 22).value == 42);
        }
        host.reset();
        {
            INFO("A method handle did not outlive the host");
            REQUIRE(live.Invoke(20, 22).value == 42);
        }
    }

    TEST_CASE("the log sink receives bridge events and exception reports with severity", "[.components]")
    {
        std::vector<Logged> messages;
        auto host = StartHost(checkArguments);
        const SinkGuard guard(checkArguments);
        {
            INFO("Registering a sink failed");
            REQUIRE(host->SetLogSink([&](const LogMessage& message) {
                            messages.push_back(Logged{message.level, std::string(message.text)});
                        })
                        .Succeeded());
        }
        const auto has = [&messages](LogLevel level, std::string_view text) {
            return std::any_of(messages.begin(), messages.end(), [&](const Logged& message) {
                return message.level == level && message.text.find(text) != std::string::npos;
            });
        };
        auto component = Load(*host, checkArguments.component);
        {
            INFO("Loading a component was not logged at Info");
            REQUIRE(has(LogLevel::Info, "Iris.Examples.Component"));
        }
        messages.clear();

        // A component exception reaches the sink at Error, and the call still returns its own details.
        const auto thrown = Call(component, CalculationType, "Throw", 0, 0);
        {
            INFO("The call lost its own diagnostic");
            REQUIRE((thrown.status == CallStatus::ManagedException &&
                     thrown.diagnostic.find("InvalidOperationException") != std::string::npos));
        }
        {
            INFO("A component exception was not reported at Error with its details");
            REQUIRE((has(LogLevel::Error, "InvalidOperationException") &&
                     has(LogLevel::Error, "\xCF\x80 \xF0\x9F\x90\xBA")));
        }

        // Bridge validation is a Warning; a NotFound is not an exception report.
        messages.clear();
        {
            INFO("Malformed UTF-8 was accepted");
            REQUIRE(component.FindType("\xFF").status == CallStatus::InvalidArgument);
        }
        {
            INFO("A validation failure was not reported at Warning");
            REQUIRE(has(LogLevel::Warning, "DecoderFallbackException"));
        }
        messages.clear();
        {
            INFO("Expected NotFound");
            REQUIRE(component.FindType("Iris.Examples.Component.Missing").status == CallStatus::NotFound);
        }
        {
            INFO("A NotFound was reported as an error");
            REQUIRE(!std::any_of(messages.begin(), messages.end(),
                                 [](const Logged& message) { return message.level == LogLevel::Error; }));
        }

        // Releasing is logged too, and replacing the sink redirects messages.
        messages.clear();
        std::vector<Logged> replaced;
        {
            INFO("Replacing the sink failed");
            REQUIRE(host->SetLogSink([&](const LogMessage& message) {
                            replaced.push_back(Logged{message.level, std::string(message.text)});
                        })
                        .Succeeded());
        }
        {
            INFO("Release failed");
            REQUIRE(component.Release().Succeeded());
        }
        {
            INFO("The replaced sink still received messages, or the new one received none");
            REQUIRE((messages.empty() && !replaced.empty() && replaced.front().level == LogLevel::Info));
        }
    }

    TEST_CASE("calls work without a sink and after it is cleared", "[.components]")
    {
        std::vector<Logged> messages;
        auto host = StartHost(checkArguments);
        const SinkGuard guard(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        {
            INFO("A call failed without a sink");
            REQUIRE(Call(component, CalculationType, "Throw", 0, 0).status == CallStatus::ManagedException);
        }
        {
            INFO("Registering failed");
            REQUIRE(host->SetLogSink([&](const LogMessage& message) {
                            messages.push_back(Logged{message.level, std::string(message.text)});
                        })
                        .Succeeded());
        }
        {
            INFO("Clearing failed");
            REQUIRE(host->SetLogSink({}).Succeeded());
        }
        {
            INFO("Messages arrived after the sink was cleared");
            REQUIRE((Call(component, CalculationType, "Throw", 0, 0).status == CallStatus::ManagedException &&
                     messages.empty()));
        }
    }

    TEST_CASE("a sink that triggers more failing calls does not recurse", "[.components]")
    {
        int deliveries = 0;
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const SinkGuard guard(checkArguments);
        {
            INFO("Registering failed");
            REQUIRE(host->SetLogSink([&](const LogMessage&) {
                            // Each failing call would log again; nested messages are dropped.
                            if (++deliveries == 1)
                            {
                                (void)Call(component, CalculationType, "Throw", 0, 0);
                            }
                        })
                        .Succeeded());
        }
        {
            INFO("A sink that made another failing call recursed or lost the original message");
            REQUIRE((Call(component, CalculationType, "Throw", 0, 0).status == CallStatus::ManagedException &&
                     deliveries == 1));
        }
        {
            INFO("Delivery did not resume after a nested call");
            REQUIRE((Call(component, CalculationType, "Throw", 0, 0).status == CallStatus::ManagedException &&
                     deliveries == 2));
        }
    }

    TEST_CASE("a sink that throws, or clears itself, cannot break a call", "[.components]")
    {
        int delivered = 0;
        int clearing = 0;
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const SinkGuard guard(checkArguments);
        {
            INFO("Registering failed");
            REQUIRE(host->SetLogSink([&](const LogMessage&) {
                            ++delivered;
                            throw std::runtime_error("a sink failure");
                        })
                        .Succeeded());
        }
        const auto thrown = Call(component, CalculationType, "Throw", 0, 0);
        {
            INFO("A throwing sink changed the call's result");
            REQUIRE((thrown.status == CallStatus::ManagedException &&
                     thrown.diagnostic.find("InvalidOperationException") != std::string::npos && delivered > 0));
        }
        {
            INFO("A call did not recover after a throwing sink");
            REQUIRE(Call(component, CalculationType, "Add", 20, 22).value == 42);
        }

        Host* hostPointer = host.get();
        {
            INFO("Registering failed");
            REQUIRE(hostPointer
                        ->SetLogSink([&](const LogMessage&) {
                            ++clearing;
                            (void)hostPointer->SetLogSink({});
                        })
                        .Succeeded());
        }
        (void)Call(component, CalculationType, "Throw", 0, 0);
        (void)Call(component, CalculationType, "Throw", 0, 0);
        {
            INFO("A sink that cleared itself broke a call");
            REQUIRE((clearing >= 1 && Call(component, CalculationType, "Add", 20, 22).value == 42));
        }
        const int afterClear = clearing;
        (void)Call(component, CalculationType, "Throw", 0, 0);
        {
            INFO("A cleared sink was called again");
            REQUIRE(clearing == afterClear);
        }
    }

    TEST_CASE("the sink needs an initialized host and survives the host that set it", "[.components]")
    {
        Host unready;
        {
            INFO("An uninitialized host accepted a sink");
            REQUIRE(unready.SetLogSink([](const LogMessage&) {}).status == CallStatus::NotInitialized);
        }
        std::vector<Logged> messages;
        const SinkGuard guard(checkArguments);
        std::optional<Component> component;
        {
            auto host = StartHost(checkArguments);
            component = Load(*host, checkArguments.component);
            {
                INFO("Registering failed");
                REQUIRE(host->SetLogSink([&](const LogMessage& message) {
                                messages.push_back(Logged{message.level, std::string(message.text)});
                            })
                            .Succeeded());
            }
        }
        {
            INFO("A sink did not outlive the host that registered it");
            REQUIRE((component->Release().Succeeded() && !messages.empty()));
        }
    }

    TEST_CASE("objects are built by constructor overload and keep their state", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto counter = *component.FindType(CounterType).value;
        const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
        const auto add = *counter.FindInstanceMethod<void(std::int32_t)>("Add").value;
        const auto first = counter.CreateObject();
        const auto started = counter.CreateObject(std::int32_t{5});
        const auto scaled = counter.CreateObject(std::int32_t{4}, 2.5);
        {
            INFO("Construction failed: " + started.diagnostic);
            REQUIRE((first.Succeeded() && started.Succeeded() && scaled.Succeeded()));
        }
        {
            INFO("A constructor overload was not selected");
            REQUIRE((value.Invoke(*first.value).value == 0 && value.Invoke(*started.value).value == 5 &&
                     value.Invoke(*scaled.value).value == 10));
        }
        {
            INFO("Instance state was shared or lost between calls");
            REQUIRE((add.Invoke(*started.value, 3).Succeeded() && value.Invoke(*started.value).value == 8 &&
                     value.Invoke(*first.value).value == 0));
        }
    }

    TEST_CASE("virtual methods dispatch to the object's real type", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto counter = *component.FindType(CounterType).value;
        const auto step = *component.FindType(StepCounterType).value;
        const auto next = *counter.FindInstanceMethod<std::int32_t()>("Next").value;
        const auto plain = counter.CreateObject(std::int32_t{1});
        const auto stepper = step.CreateObject(std::int32_t{1});
        {
            INFO("A call through the base method did not reach the override");
            REQUIRE((next.Invoke(*plain.value).value == 2 && next.Invoke(*stepper.value).value == 11));
        }
    }

    TEST_CASE("construction failures and lookups are contained and leave nothing rooted", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto counter = *component.FindType(CounterType).value;
        const auto failed = counter.CreateObject(true);
        {
            INFO("A constructor exception was not contained with its details");
            REQUIRE((failed.status == CallStatus::ManagedException && !failed.value &&
                     failed.diagnostic.find("InvalidOperationException") != std::string::npos &&
                     failed.diagnostic.find("\xCF\x80 \xF0\x9F\x90\xBA") != std::string::npos));
        }
        {
            INFO("An abstract type was constructed");
            REQUIRE(component.FindType(ShapeType).value->CreateObject().status == CallStatus::NotFound);
        }
        {
            INFO("A static type was constructed");
            REQUIRE(component.FindType(CalculationType).value->CreateObject().status == CallStatus::NotFound);
        }
        {
            INFO("A constructor matched by widening");
            REQUIRE(counter.CreateObject(std::int64_t{1}).status == CallStatus::NotFound);
        }
        auto recovered = counter.CreateObject(false);
        {
            INFO("Construction did not recover after a failure");
            REQUIRE(recovered.Succeeded());
        }
        {
            INFO("Releasing the object failed");
            REQUIRE(recovered.value->Release().Succeeded());
        }
        {
            INFO("Failed constructions left something rooted");
            REQUIRE(component.Release().Succeeded());
        }
        {
            INFO("The component stayed loaded");
            REQUIRE(!component.IsLoaded());
        }
    }

    TEST_CASE("a component with live objects cannot be released until they are", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto counter = *component.FindType(CounterType).value;
        const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
        auto object = counter.CreateObject();
        {
            INFO("Construction failed");
            REQUIRE((object.Succeeded() && object.value->IsValid()));
        }
        const auto rejected = component.Release();
        {
            INFO("A rejected release damaged the component");
            REQUIRE((rejected.status == CallStatus::InUse && component.IsLoaded() &&
                     value.Invoke(*object.value).Succeeded()));
        }
        {
            INFO("Releasing the object failed");
            REQUIRE((object.value->Release().Succeeded() && !object.value->IsValid()));
        }
        {
            INFO("Releasing an object twice was not idempotent");
            REQUIRE(object.value->Release().Succeeded());
        }
        {
            INFO("A released object still worked");
            REQUIRE(value.Invoke(*object.value).status == CallStatus::InvalidHandle);
        }
        {
            INFO("Releasing the component failed after its objects");
            REQUIRE((component.Release().Succeeded() && !component.IsLoaded()));
        }
    }

    TEST_CASE("moved objects transfer ownership and destruction releases them", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto counter = *component.FindType(CounterType).value;
        {
            auto built = counter.CreateObject();
            Object moved(std::move(*built.value));
            {
                INFO("A move did not transfer ownership");
                REQUIRE((!built.value->IsValid() && moved.IsValid()));
            }
            {
                INFO("A moved-from object was not a harmless release");
                REQUIRE(built.value->Release().Succeeded());
            }
            {
                INFO("A moved object was not still owned");
                REQUIRE(component.Release().status == CallStatus::InUse);
            }
        }
        {
            INFO("Destroying the last object did not release its root");
            REQUIRE(component.Release().Succeeded());
        }
    }

    TEST_CASE("objects and components can be destroyed in any order", "[.components]")
    {
        std::optional<Component> component;
        std::optional<Object> object;
        std::optional<InstanceMethod<std::int32_t()>> value;
        {
            auto host = StartHost(checkArguments);
            component = Load(*host, checkArguments.component);
            const auto counter = *component->FindType(CounterType).value;
            object.emplace(std::move(*counter.CreateObject().value));
            value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
        }
        {
            INFO("An object stopped working when its host was destroyed");
            REQUIRE(value->Invoke(*object).Succeeded());
        }
        {
            INFO("Releasing an object after its host was destroyed failed");
            REQUIRE(component->FindType(CounterType).value->CreateObject().value->Release().Succeeded());
        }
        component.reset(); // Destroying a component force-releases it, dropping the object it still owned.
        {
            INFO("An object survived its component");
            REQUIRE(value->Invoke(*object).status == CallStatus::InvalidHandle);
        }
        {
            INFO("Releasing an object after its component was destroyed was not InvalidHandle");
            REQUIRE((object->Release().status == CallStatus::InvalidHandle && !object->IsValid()));
        }
        object.reset();
    }

    TEST_CASE("an object only works with methods of its own component", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto first = Load(*host, checkArguments.component);
        const auto second = Load(*host, checkArguments.component);
        const auto firstCounter = *first.FindType(CounterType).value;
        const auto secondCounter = *second.FindType(CounterType).value;
        const auto object = firstCounter.CreateObject();
        const auto foreign = *secondCounter.FindInstanceMethod<std::int32_t()>("Value").value;
        const auto result = foreign.Invoke(*object.value);
        {
            INFO("An object from another component was accepted");
            REQUIRE((result.status == CallStatus::InvalidArgument && !result.value));
        }
        const auto other = *first.FindType(OtherType).value;
        const auto ping = *other.FindInstanceMethod<std::int32_t()>("Ping").value;
        {
            INFO("An object of the wrong type was accepted");
            REQUIRE(ping.Invoke(*object.value).status == CallStatus::InvalidArgument);
        }
    }

    TEST_CASE("methods return objects, and null is an empty optional", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto factory = *component.FindType(FactoryType).value;
        const auto counter = *component.FindType(CounterType).value;
        const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
        const auto make = factory.FindMethod<std::optional<Object>(std::int32_t)>("Make");
        {
            INFO("Lookup of a method returning an object failed: " + make.diagnostic);
            REQUIRE(make.Succeeded());
        }
        auto made = make.value->Invoke(7);
        {
            INFO("A returned object was not delivered");
            REQUIRE((made.Succeeded() && made.value->has_value() && (*made.value)->IsValid()));
        }
        {
            INFO("A returned object lost its state");
            REQUIRE(value.Invoke(**made.value).value == 7);
        }
        const auto nothing = factory.FindMethod<std::optional<Object>()>("Nothing").value->Invoke();
        {
            INFO("A null return was not an explicit empty optional");
            REQUIRE((nothing.Succeeded() && nothing.value.has_value() && !nothing.value->has_value()));
        }
        // A returned object is owned, so it keeps its component from being released.
        {
            INFO("A component with a returned object was released");
            REQUIRE(component.Release().status == CallStatus::InUse);
        }
        {
            INFO("Releasing the returned object failed");
            REQUIRE(((*made.value)->Release().Succeeded() && component.Release().Succeeded()));
        }
    }

    TEST_CASE("returning one managed object twice gives independent handles", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto counter = *component.FindType(CounterType).value;
        const auto self = *counter.FindInstanceMethod<std::optional<Object>()>("Self").value;
        const auto add = *counter.FindInstanceMethod<void(std::int32_t)>("Add").value;
        const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
        auto origin = counter.CreateObject(std::int32_t{3});
        auto first = self.Invoke(*origin.value);
        auto second = self.Invoke(*origin.value);
        {
            INFO("Self did not return objects");
            REQUIRE((first.Succeeded() && second.Succeeded() && first.value->has_value() && second.value->has_value()));
        }
        {
            INFO("The handles did not refer to the same managed object");
            REQUIRE((add.Invoke(**first.value, 4).Succeeded() && value.Invoke(**second.value).value == 7 &&
                     value.Invoke(*origin.value).value == 7));
        }
        {
            INFO("Releasing a returned handle failed");
            REQUIRE((*first.value)->Release().Succeeded());
        }
        {
            INFO("Releasing one handle affected another");
            REQUIRE((value.Invoke(**second.value).value == 7 && value.Invoke(*origin.value).value == 7));
        }
        {
            INFO("Handles still alive did not hold the component");
            REQUIRE(component.Release().status == CallStatus::InUse);
        }
        {
            INFO("The component was not releasable after every handle");
            REQUIRE(((*second.value)->Release().Succeeded() && origin.value->Release().Succeeded() &&
                     component.Release().Succeeded()));
        }
    }

    TEST_CASE("objects are passed as arguments and checked before the call", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        auto other = Load(*host, checkArguments.component);
        const auto factory = *component.FindType(FactoryType).value;
        const auto counter = *component.FindType(CounterType).value;
        const auto total = factory.FindMethod<std::int32_t(const Object&, const Object&)>("Total");
        const auto combine = counter.FindInstanceMethod<std::int32_t(const Object&)>("Combine");
        {
            INFO("Lookup of methods taking objects failed: " + total.diagnostic);
            REQUIRE((total.Succeeded() && combine.Succeeded()));
        }
        auto a = counter.CreateObject(std::int32_t{3});
        auto b = counter.CreateObject(std::int32_t{4});
        {
            INFO("Object arguments were not passed");
            REQUIRE((total.value->Invoke(*a.value, *b.value).value == 7 &&
                     combine.value->Invoke(*a.value, *b.value).value == 7));
        }

        // The managed parameter type is checked before the call runs.
        auto wrong = component.FindType(OtherType).value->CreateObject();
        const auto mismatch = total.value->Invoke(*a.value, *wrong.value);
        {
            INFO("An object of the wrong type was passed");
            REQUIRE((mismatch.status == CallStatus::InvalidArgument && !mismatch.value &&
                     mismatch.diagnostic.find("instance of") != std::string::npos));
        }

        // Released, moved-from and foreign objects are rejected.
        auto released = counter.CreateObject(std::int32_t{1});
        {
            INFO("Release failed");
            REQUIRE(released.value->Release().Succeeded());
        }
        {
            INFO("A released object was passed");
            REQUIRE(total.value->Invoke(*a.value, *released.value).status == CallStatus::InvalidHandle);
        }
        auto foreign = other.FindType(CounterType).value->CreateObject();
        {
            INFO("An object from another component was passed");
            REQUIRE(total.value->Invoke(*a.value, *foreign.value).status == CallStatus::InvalidArgument);
        }
        {
            INFO("A call did not recover after rejected arguments");
            REQUIRE(total.value->Invoke(*a.value, *b.value).value == 7);
        }

        // Constructors take objects too.
        auto copy = counter.CreateObject(*a.value);
        {
            INFO("A constructor did not receive an object argument");
            REQUIRE((copy.Succeeded() &&
                     counter.FindInstanceMethod<std::int32_t()>("Value").value->Invoke(*copy.value).value == 3));
        }
    }

    TEST_CASE("a method hidden with new resolves to the most-derived declaration", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto hiding = *component.FindType(HidingCounterType).value;
        const auto value = hiding.FindInstanceMethod<std::int32_t()>("Value");
        {
            INFO("A hidden method was ambiguous: " + value.diagnostic);
            REQUIRE(value.Succeeded());
        }
        const auto object = hiding.CreateObject();
        {
            INFO("The hiding method was not the one called");
            REQUIRE(value.value->Invoke(*object.value).value == 42);
        }
    }

    TEST_CASE("fields round-trip, static and instance", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto settings = *component.FindType(SettingsType).value;
        const auto object = settings.CreateObject();
        const auto staticCount = settings.FindField<std::int32_t>("StaticCount");
        {
            INFO("Field lookup failed: " + staticCount.diagnostic);
            REQUIRE(staticCount.Succeeded());
        }
        {
            INFO("A static field did not round-trip");
            REQUIRE((staticCount.value->Set(41).Succeeded() && staticCount.value->Get().value == 41));
        }
        const auto count = *settings.FindField<std::int32_t>("Count").value;
        {
            INFO("An instance field did not round-trip");
            REQUIRE((count.Set(*object.value, -5).Succeeded() && count.Get(*object.value).value == -5));
        }
        const auto ratio = *settings.FindField<double>("Ratio").value;
        {
            INFO("A double field did not round-trip");
            REQUIRE((ratio.Set(*object.value, 2.5).Succeeded() && ratio.Get(*object.value).value == 2.5));
        }
        const auto flag = *settings.FindField<bool>("Flag").value;
        {
            INFO("A bool field did not round-trip");
            REQUIRE((flag.Set(*object.value, true).Succeeded() && flag.Get(*object.value).value == true));
        }
        {
            INFO("Setting one field changed another");
            REQUIRE(count.Get(*object.value).value == -5);
        }
    }

    TEST_CASE("properties round-trip and accessor exceptions are contained", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto settings = *component.FindType(SettingsType).value;
        const auto object = settings.CreateObject();
        const auto enabled = *settings.FindProperty<bool>("Enabled").value;
        {
            INFO("A property did not round-trip");
            REQUIRE((enabled.Set(*object.value, true).Succeeded() && enabled.Get(*object.value).value == true));
        }
        const auto staticProperty = *settings.FindProperty<std::int32_t>("StaticProperty").value;
        {
            INFO("A static property did not round-trip");
            REQUIRE((staticProperty.Set(12).Succeeded() && staticProperty.Get().value == 12));
        }
        const auto checkedProperty = *settings.FindProperty<std::int32_t>("Checked").value;
        const auto rejected = checkedProperty.Set(*object.value, -1);
        {
            INFO("A setter exception was not reported as itself");
            REQUIRE((rejected.status == CallStatus::ManagedException &&
                     rejected.diagnostic.find("ArgumentOutOfRangeException") != std::string::npos &&
                     rejected.diagnostic.find("TargetInvocationException") == std::string::npos));
        }
        {
            INFO("An object was unusable after a setter failed");
            REQUIRE(
                (checkedProperty.Set(*object.value, 8).Succeeded() && checkedProperty.Get(*object.value).value == 8));
        }
        const auto exploding = settings.FindProperty<std::int32_t>("Exploding").value->Get(*object.value);
        {
            INFO("A getter exception was not reported as itself");
            REQUIRE((exploding.status == CallStatus::ManagedException && !exploding.value &&
                     exploding.diagnostic.find("InvalidOperationException") != std::string::npos));
        }
    }

    TEST_CASE("read-only, write-only and wrong-form uses are diagnosed", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto settings = *component.FindType(SettingsType).value;
        const auto object = settings.CreateObject();
        const auto contains = [](const std::string& text, const char* word) {
            return text.find(word) != std::string::npos;
        };
        const auto id = *settings.FindField<std::int32_t>("Id").value;
        const auto readonlyWrite = id.Set(*object.value, 9);
        {
            INFO("A readonly field was writable");
            REQUIRE((readonlyWrite.status == CallStatus::InvalidArgument &&
                     contains(readonlyWrite.diagnostic, "read-only") && id.Get(*object.value).value == 7));
        }
        {
            INFO("A static readonly field or constant was writable");
            REQUIRE((settings.FindField<std::int32_t>("Limit").value->Set(1).status == CallStatus::InvalidArgument &&
                     settings.FindField<std::int32_t>("Constant").value->Set(1).status == CallStatus::InvalidArgument &&
                     settings.FindField<std::int32_t>("Constant").value->Get().value == 5));
        }
        {
            INFO("A property without a setter was writable");
            REQUIRE(settings.FindProperty<std::int32_t>("ReadOnly").value->Set(*object.value, 1).status ==
                    CallStatus::InvalidArgument);
        }
        const auto writeOnly = *settings.FindProperty<std::int32_t>("WriteOnly").value;
        const auto unreadable = writeOnly.Get(*object.value);
        {
            INFO("A write-only property was readable");
            REQUIRE((unreadable.status == CallStatus::InvalidArgument &&
                     contains(unreadable.diagnostic, "write-only") && writeOnly.Set(*object.value, 4).Succeeded()));
        }
        const auto count = *settings.FindField<std::int32_t>("Count").value;
        const auto staticCount = *settings.FindField<std::int32_t>("StaticCount").value;
        {
            INFO("An instance field was used without an object");
            REQUIRE((count.Get().status == CallStatus::InvalidArgument &&
                     count.Set(1).status == CallStatus::InvalidArgument));
        }
        {
            INFO("A static field was used with an object");
            REQUIRE((staticCount.Get(*object.value).status == CallStatus::InvalidArgument &&
                     staticCount.Set(*object.value, 1).status == CallStatus::InvalidArgument));
        }
        const auto otherObject = component.FindType(OtherType).value->CreateObject();
        {
            INFO("An object of the wrong type was accepted");
            REQUIRE(count.Get(*otherObject.value).status == CallStatus::InvalidArgument);
        }
        // An init-only property is read-only once the object exists.
        const auto initOnly = *settings.FindProperty<std::int32_t>("InitOnly").value;
        {
            INFO("An init-only property was writable");
            REQUIRE((initOnly.Get(*object.value).value == 11 &&
                     initOnly.Set(*object.value, 1).status == CallStatus::InvalidArgument));
        }
        // Released and foreign objects are rejected before the call.
        auto released = settings.CreateObject();
        {
            INFO("A released object was accepted by a member");
            REQUIRE((released.value->Release().Succeeded() &&
                     count.Get(*released.value).status == CallStatus::InvalidHandle));
        }
        const auto second = Load(*host, checkArguments.component);
        const auto foreign = second.FindType(SettingsType).value->CreateObject();
        {
            INFO("An object from another component was accepted by a member");
            REQUIRE(count.Get(*foreign.value).status == CallStatus::InvalidArgument);
        }
    }

    TEST_CASE("every primitive kind round-trips through a member", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto settings = *component.FindType(SettingsType).value;
        const auto object = settings.CreateObject();
        const auto big = *settings.FindField<std::uint64_t>("Big").value;
        const auto single = *settings.FindField<float>("Single").value;
        const auto small = *settings.FindField<std::int8_t>("Small").value;
        {
            INFO("ulong did not round-trip");
            REQUIRE((big.Set(*object.value, std::numeric_limits<std::uint64_t>::max()).Succeeded() &&
                     big.Get(*object.value).value == std::numeric_limits<std::uint64_t>::max()));
        }
        {
            INFO("float did not round-trip");
            REQUIRE((single.Set(*object.value, 1.5f).Succeeded() && single.Get(*object.value).value == 1.5f));
        }
        {
            INFO("sbyte did not round-trip");
            REQUIRE((small.Set(*object.value, -7).Succeeded() && small.Get(*object.value).value == -7));
        }
    }

    TEST_CASE("a field hidden with new resolves to the most-derived declaration", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto hiding = *component.FindType(HidingSettingsType).value;
        const auto object = hiding.CreateObject();
        const auto count = hiding.FindField<double>("Count");
        {
            INFO("A hidden field was unreachable: " + count.diagnostic);
            REQUIRE(count.Succeeded());
        }
        {
            INFO("The hiding field was not the one used");
            REQUIRE((count.value->Set(*object.value, 2.5).Succeeded() && count.value->Get(*object.value).value == 2.5));
        }
        {
            INFO("The hidden base field was still reachable by name");
            REQUIRE(hiding.FindField<std::int32_t>("Count").status == CallStatus::NotFound);
        }
    }

    TEST_CASE("member lookup failures are NotFound with a diagnostic", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto settings = *component.FindType(SettingsType).value;
        const auto notFound = [](const auto& result, const char* what) {
            {
                INFO(std::string("Not a NotFound with a diagnostic: ") + what);
                REQUIRE((result.status == CallStatus::NotFound && !result.value && !result.diagnostic.empty()));
            }
        };
        notFound(settings.FindField<std::int32_t>("Missing"), "missing name");
        notFound(settings.FindField<std::int64_t>("Count"), "a field matched by widening");
        notFound(settings.FindProperty<std::int32_t>("Count"), "a field found as a property");
        notFound(settings.FindField<bool>("Enabled"), "a property found as a field");
        notFound(settings.FindField<std::int32_t>("secret"), "a private field");
        notFound(settings.FindProperty<std::int32_t>("Item"), "an indexer");
        {
            INFO("An empty name was accepted");
            REQUIRE(settings.FindField<std::int32_t>({}).status == CallStatus::InvalidArgument);
        }
    }

    TEST_CASE("object-typed members hold, return and clear objects", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto settings = *component.FindType(SettingsType).value;
        const auto counter = *component.FindType(CounterType).value;
        const auto value = *counter.FindInstanceMethod<std::int32_t()>("Value").value;
        const auto owner = settings.CreateObject();
        const auto child = settings.FindField<std::optional<Object>>("Child");
        const auto childProperty = settings.FindProperty<std::optional<Object>>("ChildProperty");
        {
            INFO("Object member lookup failed: " + child.diagnostic);
            REQUIRE((child.Succeeded() && childProperty.Succeeded()));
        }
        {
            INFO("An unset object member was not null");
            REQUIRE((child.value->Get(*owner.value).value && !child.value->Get(*owner.value).value->has_value()));
        }
        const auto stored = counter.CreateObject(std::int32_t{6});
        {
            INFO("Storing an object failed");
            REQUIRE((child.value->Set(*owner.value, &*stored.value).Succeeded() &&
                     childProperty.value->Set(*owner.value, &*stored.value).Succeeded()));
        }
        const auto fetched = child.value->Get(*owner.value);
        {
            INFO("A stored object did not come back");
            REQUIRE((fetched.Succeeded() && fetched.value->has_value() && value.Invoke(**fetched.value).value == 6));
        }
        const auto otherObject = component.FindType(OtherType).value->CreateObject();
        const auto wrong = child.value->Set(*owner.value, &*otherObject.value);
        {
            INFO("An object of the wrong type was stored");
            REQUIRE((wrong.status == CallStatus::InvalidArgument &&
                     wrong.diagnostic.find("instance of") != std::string::npos));
        }
        {
            INFO("Storing null did not clear the member");
            REQUIRE((child.value->Set(*owner.value, nullptr).Succeeded() &&
                     !child.value->Get(*owner.value).value->has_value()));
        }
        {
            INFO("Held objects did not keep the component loaded");
            REQUIRE(component.Release().status == CallStatus::InUse);
        }
    }

    TEST_CASE("strings round-trip as UTF-8 with multi-byte text and embedded nulls", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto strings = *component.FindType(StringsType).value;
        const auto echo = strings.FindMethod<std::optional<std::string>(std::optional<std::string_view>)>("EchoOrNull");
        {
            INFO("Lookup failed: " + echo.diagnostic);
            REQUIRE(echo.Succeeded());
        }
        const std::string wolf = "h\xC3\xA9llo \xCF\x80 \xF0\x9F\x90\xBA";
        for (const std::string& sample : {std::string("plain"), wolf, std::string("A\0B", 3), std::string("\0", 1),
                                          std::string("tail\0", 5), std::string()})
        {
            const auto echoed = echo.value->Invoke(std::string_view(sample));
            {
                INFO("A string did not round-trip: " + sample);
                REQUIRE((echoed.Succeeded() && echoed.value && *echoed.value && **echoed.value == sample));
            }
        }
        const auto greeting = strings.FindMethod<std::optional<std::string>()>("Greeting");
        const auto text = greeting.value->Invoke();
        {
            INFO("A non-ASCII return was altered");
            REQUIRE((text.Succeeded() && text.value && *text.value &&
                     **text.value == "h\xC3\xA9llo \xCF\x80 \xF0\x9F\x90\xBA \xE2\x80\x94 ok"));
        }
        const auto withNull = strings.FindMethod<std::optional<std::string>()>("WithNull");
        const auto embedded = withNull.value->Invoke();
        {
            INFO("An embedded null in a return was lost");
            REQUIRE((embedded.Succeeded() && **embedded.value == std::string("A\0B", 3)));
        }
        const auto repeat = strings.FindMethod<std::optional<std::string>(std::string_view, std::int32_t)>("Repeat");
        const auto large = repeat.value->Invoke(std::string_view("\xCF\x80"), 1'000'000);
        {
            INFO("A large return was truncated");
            REQUIRE((large.Succeeded() && **large.value == [] {
                std::string expected;
                for (int i = 0; i < 1'000'000; ++i)
                    expected += "\xCF\x80";
                return expected;
            }()));
        }
    }

    TEST_CASE("a null string is distinct from an empty one in both directions", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto strings = *component.FindType(StringsType).value;
        const auto echo =
            *strings.FindMethod<std::optional<std::string>(std::optional<std::string_view>)>("EchoOrNull").value;
        const auto isNull = *strings.FindMethod<bool(std::optional<std::string_view>)>("IsNull").value;
        const auto fromNull = echo.Invoke(std::nullopt);
        const auto fromEmpty = echo.Invoke(std::string_view());
        {
            INFO("A null argument did not come back null");
            REQUIRE((fromNull.Succeeded() && fromNull.value && !*fromNull.value));
        }
        {
            INFO("An empty argument did not come back empty");
            REQUIRE((fromEmpty.Succeeded() && fromEmpty.value && *fromEmpty.value && (*fromEmpty.value)->empty()));
        }
        {
            INFO("Managed code could not tell null from empty");
            REQUIRE((isNull.Invoke(std::nullopt).value == true && isNull.Invoke(std::string_view()).value == false));
        }
        const auto nullReturn = strings.FindMethod<std::optional<std::string>()>("Null");
        const auto emptyReturn = strings.FindMethod<std::optional<std::string>()>("Empty");
        const auto fromNullReturn = nullReturn.value->Invoke();
        const auto fromEmptyReturn = emptyReturn.value->Invoke();
        {
            INFO("A null return was not empty");
            REQUIRE((fromNullReturn.Succeeded() && fromNullReturn.value && !*fromNullReturn.value));
        }
        {
            INFO("An empty return was reported as null");
            REQUIRE((fromEmptyReturn.Succeeded() && fromEmptyReturn.value && *fromEmptyReturn.value));
        }
    }

    TEST_CASE("malformed UTF-8 is rejected before managed code runs, and bad returns are diagnosed", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto strings = *component.FindType(StringsType).value;
        const auto join =
            *strings.FindMethod<std::optional<std::string>(std::string_view, std::string_view)>("Join").value;
        for (const char* bad : {"\xFF", "\xC3", "\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80"})
        {
            const auto rejected = join.Invoke("x", bad);
            {
                INFO(std::string("Malformed UTF-8 was accepted: ") + bad);
                REQUIRE((rejected.status == CallStatus::InvalidArgument && !rejected.value &&
                         rejected.diagnostic.find("UTF-8") != std::string::npos));
            }
        }
        // A rejected call never reached managed code; an accepted one runs exactly once.
        const auto counted = *strings.FindMethod<std::int32_t(std::string_view, std::string_view)>("Counted").value;
        const auto callCount = *strings.FindField<std::int32_t>("Calls").value;
        {
            INFO("Managed code ran for a malformed argument");
            REQUIRE((counted.Invoke("x", "\xFF").status == CallStatus::InvalidArgument && callCount.Get().value == 0));
        }
        {
            INFO("An accepted call did not run once");
            REQUIRE((counted.Invoke("x", "y").value == 1 && callCount.Get().value == 1));
        }
        const auto repeatOnce =
            *strings.FindMethod<std::optional<std::string>(std::string_view, std::int32_t)>("Repeat").value;
        {
            INFO("A large return ran managed code more than once");
            REQUIRE((repeatOnce.Invoke("ab", 500'000).Succeeded() && callCount.Get().value == 2));
        }
        const auto unpaired = strings.FindMethod<std::optional<std::string>()>("UnpairedSurrogate");
        {
            INFO("A return with no UTF-8 form was not diagnosed");
            REQUIRE(unpaired.value->Invoke().status == CallStatus::InvalidArgument);
        }
        const auto throws = strings.FindMethod<std::optional<std::string>(std::string_view)>("Throws");
        const auto thrown = throws.value->Invoke("\xCF\x80");
        {
            INFO("A managed exception with a string argument was not reported");
            REQUIRE((thrown.status == CallStatus::ManagedException &&
                     thrown.diagnostic.find("Rejected: \xCF\x80") != std::string::npos));
        }
        const auto again = join.Invoke("a", "b");
        {
            INFO("The method was unusable after a rejected call");
            REQUIRE((again.Succeeded() && **again.value == "ab"));
        }
    }

    TEST_CASE("strings are their own kind and work with constructors, instance methods and members", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto strings = *component.FindType(StringsType).value;
        {
            INFO("Another kind matched a string parameter");
            REQUIRE((strings.FindMethod<std::int32_t(const Object&)>("Length").status == CallStatus::NotFound &&
                     strings.FindMethod<std::int32_t(std::int32_t)>("Length").status == CallStatus::NotFound));
        }
        {
            INFO("A string did not pick the string overload");
            REQUIRE(strings.FindMethod<std::int32_t(std::string_view)>("Kind").value->Invoke("x").value == 1);
        }
        const auto greeter = *component.FindType(GreeterType).value;
        const auto object = greeter.CreateObject(std::string_view("W\xC3\xB6lfe"));
        {
            INFO("A constructor with a string failed: " + object.diagnostic);
            REQUIRE(object.Succeeded());
        }
        const auto greet = greeter.FindInstanceMethod<std::optional<std::string>(std::string_view)>("Greet");
        const auto greeting = greet.value->Invoke(*object.value, "!");
        {
            INFO("An instance method with strings failed");
            REQUIRE((greeting.Succeeded() && **greeting.value == "Hello, W\xC3\xB6lfe!"));
        }

        const auto settings = *component.FindType(SettingsType).value;
        const auto label = *settings.FindField<std::optional<std::string>>("Label").value;
        const auto caption = *settings.FindProperty<std::optional<std::string>>("Caption").value;
        const auto staticLabel = *settings.FindField<std::optional<std::string>>("StaticLabel").value;
        const auto instance = settings.CreateObject();
        {
            INFO("An unset string field was not null");
            REQUIRE(!*label.Get(*instance.value).value);
        }
        for (const std::string& sample : {std::string("\xCF\x80"), std::string(), std::string("A\0B", 3)})
        {
            {
                INFO("A string field did not round-trip");
                REQUIRE((label.Set(*instance.value, std::string_view(sample)).Succeeded() &&
                         **label.Get(*instance.value).value == sample));
            }
            {
                INFO("A string property did not round-trip");
                REQUIRE((caption.Set(*instance.value, std::string_view(sample)).Succeeded() &&
                         **caption.Get(*instance.value).value == sample));
            }
            {
                INFO("A static string field did not round-trip");
                REQUIRE((staticLabel.Set(std::string_view(sample)).Succeeded() && **staticLabel.Get().value == sample));
            }
        }
        {
            INFO("Storing null did not clear a string field");
            REQUIRE((label.Set(*instance.value, std::nullopt).Succeeded() && !*label.Get(*instance.value).value));
        }
        const auto readOnly = *settings.FindProperty<std::optional<std::string>>("ReadOnlyCaption").value;
        {
            INFO("A read-only string property was writable");
            REQUIRE((readOnly.Set(*instance.value, "x").status == CallStatus::InvalidArgument &&
                     **readOnly.Get(*instance.value).value == "fixed"));
        }
        {
            INFO("Malformed UTF-8 was stored in a field");
            REQUIRE(label.Set(*instance.value, std::string_view("\xFF")).status == CallStatus::InvalidArgument);
        }
        {
            INFO("A member matched a kind it is not");
            REQUIRE((settings.FindField<std::int32_t>("Label").status == CallStatus::NotFound &&
                     settings.FindField<std::optional<Object>>("Label").status == CallStatus::NotFound &&
                     settings.FindField<std::optional<std::string>>("Count").status == CallStatus::NotFound));
        }
    }

    TEST_CASE("blittable structs pass by value, return by value, and nest", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto structs = *component.FindType(StructsType).value;
        using Fixture::Vec3;
        const auto add = structs.FindMethod<Vec3(Vec3, Vec3)>("Add");
        {
            INFO("A struct signature was not found: " + add.diagnostic);
            REQUIRE(add.Succeeded());
        }
        const auto sum = add.value->Invoke(Vec3{1, 2, 3}, Vec3{10, 20, 30});
        {
            INFO("A struct did not round-trip");
            REQUIRE((sum.Succeeded() && sum.value->X == 11 && sum.value->Y == 22 && sum.value->Z == 33));
        }
        const auto dot = structs.FindMethod<float(Vec3, Vec3)>("Dot");
        {
            INFO("A struct argument was misread");
            REQUIRE(dot.value->Invoke(Vec3{1, 2, 3}, Vec3{4, 5, 6}).value == 32.0f);
        }
        const auto scale = structs.FindMethod<Vec3(Vec3, float)>("Scale");
        {
            INFO("A struct and a primitive did not mix");
            REQUIRE(scale.value->Invoke(Vec3{1, 2, 3}, 2.0f).value->Z == 6);
        }
        const auto mixed = structs.FindMethod<std::int32_t(std::int32_t, Vec3, double)>("Mixed");
        {
            INFO("Struct arguments shifted their neighbours");
            REQUIRE(mixed.value->Invoke(1, Vec3{40, 0, 0}, 1.9).value == 42);
        }

        // A struct containing structs, with padding before the double and after the byte.
        using Fixture::Pose;
        const auto move = structs.FindMethod<Pose(Pose, Vec3)>("Move");
        {
            INFO("A nested struct signature was not found: " + move.diagnostic);
            REQUIRE(move.Succeeded());
        }
        const auto moved = move.value->Invoke(Pose{{1, 1, 1}, {2, 2, 2}, 0.5, 7}, Vec3{1, 2, 3});
        {
            INFO("A nested struct did not round-trip");
            REQUIRE((moved.Succeeded() && moved.value->Position.Y == 3 && moved.value->Scale.Z == 2 &&
                     moved.value->Weight == 0.5 && moved.value->Flags == 8));
        }

        using Fixture::Packed;
        const auto swap = structs.FindMethod<Packed(Packed)>("Swap");
        {
            INFO("An explicit-layout struct did not round-trip");
            REQUIRE(swap.value->Invoke(Packed{1, 2}).value->Low == 2);
        }

        // Struct parameters are matched exactly.
        {
            INFO("A struct signature matched something else");
            REQUIRE((structs.FindMethod<Vec3(Vec3)>("Add").status == CallStatus::NotFound &&
                     structs.FindMethod<float(Vec3, Vec3)>("Add").status == CallStatus::NotFound &&
                     structs.FindMethod<float(float, float)>("Dot").status == CallStatus::NotFound));
        }
        {
            INFO("A different struct return type matched");
            REQUIRE(structs.FindMethod<Packed(Vec3, Vec3)>("Add").status == CallStatus::NotFound);
        }
    }

    TEST_CASE("struct mappings that do not match the managed type fail the lookup and never call", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto structs = *component.FindType(StructsType).value;
        const auto wide = structs.FindMethod<Fixture::WideVec3(Fixture::WideVec3, Fixture::WideVec3)>("Add");
        {
            INFO("A size mismatch was not diagnosed: " + wide.diagnostic);
            REQUIRE((wide.status == CallStatus::InvalidArgument && !wide.value &&
                     wide.diagnostic.find("24") != std::string::npos &&
                     wide.diagnostic.find("12") != std::string::npos));
        }
        const auto swapped =
            structs.FindMethod<Fixture::SwappedVec3(Fixture::SwappedVec3, Fixture::SwappedVec3)>("Add");
        {
            INFO("A field-order mismatch was not diagnosed: " + swapped.diagnostic);
            REQUIRE((swapped.status == CallStatus::InvalidArgument &&
                     swapped.diagnostic.find("order") != std::string::npos));
        }
        const auto body = *component.FindType(BodyType).value;
        {
            INFO("A bad mapping was accepted by a member or constructor");
            REQUIRE((body.FindField<Fixture::WideVec3>("Position").status == CallStatus::InvalidArgument &&
                     body.CreateObject(Fixture::SwappedVec3{1, 2, 3}).status == CallStatus::InvalidArgument));
        }

        // The right mapping still works afterwards.
        {
            INFO("A good mapping failed after bad ones");
            REQUIRE(structs.FindMethod<Fixture::Vec3(Fixture::Vec3, Fixture::Vec3)>("Add").Succeeded());
        }
    }

    TEST_CASE("structs work as constructor arguments, instance method arguments, fields and properties",
              "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto body = *component.FindType(BodyType).value;
        using Fixture::Vec3;
        const auto object = body.CreateObject(Vec3{1, 2, 3});
        {
            INFO("A constructor with a struct failed: " + object.diagnostic);
            REQUIRE(object.Succeeded());
        }
        const auto position = *body.FindField<Vec3>("Position").value;
        {
            INFO("A constructor stored the wrong struct");
            REQUIRE(position.Get(*object.value).value->Y == 2);
        }
        const auto moved = body.FindInstanceMethod<Vec3(Vec3)>("Moved");
        {
            INFO("An instance method with structs failed");
            REQUIRE(moved.value->Invoke(*object.value, Vec3{10, 10, 10}).value->X == 11);
        }
        const auto velocity = *body.FindProperty<Vec3>("Velocity").value;
        {
            INFO("A struct property did not round-trip");
            REQUIRE(
                (velocity.Set(*object.value, Vec3{4, 5, 6}).Succeeded() && velocity.Get(*object.value).value->Z == 6));
        }
        const auto origin = *body.FindField<Vec3>("Origin").value;
        {
            INFO("A static struct field did not round-trip");
            REQUIRE((origin.Set(Vec3{7, 8, 9}).Succeeded() && origin.Get().value->Y == 8));
        }
        const auto pose = *body.FindField<Fixture::Pose>("Pose").value;
        {
            INFO("A nested struct field did not round-trip");
            REQUIRE((pose.Set(*object.value, Fixture::Pose{{1, 2, 3}, {4, 5, 6}, 7.5, 9}).Succeeded() &&
                     pose.Get(*object.value).value->Weight == 7.5));
        }
        {
            INFO("A struct field matched another kind");
            REQUIRE((body.FindField<float>("Position").status == CallStatus::NotFound &&
                     body.FindField<std::optional<Object>>("Position").status == CallStatus::NotFound));
        }
    }

    TEST_CASE("bool is one byte: it works in mapped structs, and any nonzero byte reads as true", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto structs = *component.FindType(StructsType).value;
        const auto flip = structs.FindMethod<Fixture::FlagValue(Fixture::FlagValue)>("Flip");
        {
            INFO("A struct with a bool was not found: " + flip.diagnostic);
            REQUIRE(flip.Succeeded());
        }
        const auto flipped = flip.value->Invoke(Fixture::FlagValue{true, 5});
        {
            INFO("A bool field did not cross: " + flipped.diagnostic);
            REQUIRE((flipped.Succeeded() && !flipped.value->Flag && flipped.value->Value == 5));
        }
        {
            INFO("A false bool did not flip");
            REQUIRE(flip.value->Invoke(Fixture::FlagValue{false, -3}).value->Flag);
        }

        // Managed code is only ever given 0 or 1: a stray nonzero byte is true, and comes back as 1.
        const auto same =
            (*component.FindType(ValuesType).value).FindMethod<Fixture::FlagValue(Fixture::FlagValue)>("Same");
        {
            INFO("Same was not found: " + same.diagnostic);
            REQUIRE(same.Succeeded());
        }
        Fixture::FlagValue odd{false, 9};
        std::memset(&odd, 2, 1);
        const auto back = same.value->Invoke(odd);
        {
            INFO("A struct with an odd bool did not cross");
            REQUIRE((back.Succeeded() && back.value->Value == 9));
        }
        std::uint8_t raw = 0;
        std::memcpy(&raw, &*back.value, 1);
        {
            INFO("A nonzero bool byte was not normalised to 1 (it was " + std::to_string(raw) + ")");
            REQUIRE(raw == 1);
        }

        // A bulk copy into a managed bool[] normalises too.
        auto flags = Array<bool>::Create(component, 3);
        {
            INFO("A bool array was not created");
            REQUIRE(flags.Succeeded());
        }
        std::uint8_t stray[3] = {0, 2, 255};
        bool strayBools[3];
        std::memcpy(strayBools, stray, 3);
        {
            INFO("A bool copy failed");
            REQUIRE(flags.value->CopyIn(0, std::span<const bool>(strayBools)).Succeeded());
        }
        {
            INFO("A stray nonzero byte in a bool array was not read as true");
            REQUIRE((flags.value->Get(0).value == false && flags.value->Get(1).value == true &&
                     flags.value->Get(2).value == true));
        }
        {
            INFO("A bool array was not released");
            REQUIRE(flags.value->Release().Succeeded());
        }

        // The same struct as a property of an object, and as an array element.
        const auto values = *component.FindType(ValuesType).value;
        {
            INFO("bool arguments and returns regressed");
            REQUIRE((values.FindMethod<bool(bool, bool)>("Both").value->Invoke(true, true).value == true &&
                     values.FindMethod<bool(bool, bool)>("Both").value->Invoke(true, false).value == false));
        }
    }

    TEST_CASE("char is a UTF-16 code unit: arguments, returns, struct fields, arrays and internal calls",
              "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto values = *component.FindType(ValuesType).value;
        const auto next = values.FindMethod<char16_t(char16_t)>("Next");
        {
            INFO("A char method was not found: " + next.diagnostic);
            REQUIRE(next.Succeeded());
        }
        {
            INFO("A char did not cross as a UTF-16 code unit");
            REQUIRE((next.value->Invoke(u'a').value == u'b' && next.value->Invoke(u'\u03c0').value == u'\u03c1'));
        }
        // A char is not an int, and is matched exactly.
        {
            INFO("A ushort signature matched a char method");
            REQUIRE(values.FindMethod<std::uint16_t(std::uint16_t)>("Next").status == CallStatus::NotFound);
        }

        const auto bump = values.FindMethod<Fixture::Glyph(Fixture::Glyph)>("Bump");
        {
            INFO("A struct with a char was not found: " + bump.diagnostic);
            REQUIRE(bump.Succeeded());
        }
        const auto bumped = bump.value->Invoke(Fixture::Glyph{u'x', true, 7});
        {
            INFO("A struct with a char, a bool and padding did not round-trip");
            REQUIRE(
                (bumped.Succeeded() && bumped.value->Letter == u'y' && !bumped.value->Bold && bumped.value->Code == 8));
        }

        const auto word = values.FindMethod<std::optional<Array<char16_t>>()>("Word");
        {
            INFO("A char array was not found: " + word.diagnostic);
            REQUIRE(word.Succeeded());
        }
        const auto letters = word.value->Invoke();
        {
            INFO("A char array was not returned");
            REQUIRE((letters.Succeeded() && letters.value->has_value()));
        }
        std::vector<char16_t> text(*(*letters.value)->Length().value);
        {
            INFO("A char array did not copy");
            REQUIRE((text.size() == 3 && (*letters.value)->CopyOut(0, text).Succeeded() && text[0] == u'h' &&
                     text[2] == u'\u03c0'));
        }

        const auto natives = *component.FindType(NativesType).value;
        {
            INFO("A char crossed an internal call as something other than two bytes");
            REQUIRE(natives.FindMethod<char16_t(char16_t)>("CallNextChar").value->Invoke(u'q').value == u'r');
        }
    }

    TEST_CASE("enums cross as their underlying integer: arguments, returns, struct fields, fields and properties",
              "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto values = *component.FindType(ValuesType).value;
        using Fixture::Gear;
        const auto cycle = values.FindMethod<Gear(Gear)>("Cycle");
        {
            INFO("An enum method was not found: " + cycle.diagnostic);
            REQUIRE(cycle.Succeeded());
        }
        {
            INFO("An enum did not round-trip");
            REQUIRE((cycle.value->Invoke(Gear::Off).value == Gear::On &&
                     cycle.value->Invoke(Gear::On).value == Gear::Auto &&
                     cycle.value->Invoke(Gear::Auto).value == Gear::Off));
        }
        // A value the enum does not name is still just an integer.
        {
            INFO("An undefined enum value failed");
            REQUIRE(cycle.value->Invoke(static_cast<Gear>(42)).value == Gear::Off);
        }

        // The underlying type is part of the signature: byte and short enums, and sign extension.
        const auto invert = values.FindMethod<Fixture::Shade(Fixture::Shade)>("Invert");
        {
            INFO("A byte enum did not round-trip");
            REQUIRE((invert.Succeeded() && invert.value->Invoke(Fixture::Shade::Dark).value == Fixture::Shade::Light));
        }
        const auto mirror = values.FindMethod<Fixture::Offset(Fixture::Offset)>("Mirror");
        {
            INFO("A signed short enum lost its sign");
            REQUIRE((mirror.Succeeded() &&
                     mirror.value->Invoke(Fixture::Offset::Negative).value == Fixture::Offset::Positive &&
                     mirror.value->Invoke(Fixture::Offset::Positive).value == Fixture::Offset::Negative));
        }

        // An enum is not its integer, and is matched by type.
        {
            INFO("An int signature matched an enum method");
            REQUIRE(values.FindMethod<std::int32_t(std::int32_t)>("Cycle").status == CallStatus::NotFound);
        }
        {
            INFO("A different enum return type matched");
            REQUIRE(values.FindMethod<Fixture::Shade(Gear)>("Cycle").status == CallStatus::NotFound);
        }

        // A native enum with the wrong underlying type is diagnosed and never called.
        const auto wide = values.FindMethod<Fixture::WideGear(Fixture::WideGear)>("Cycle");
        {
            INFO("An enum with the wrong underlying type was accepted: " + wide.diagnostic);
            REQUIRE((wide.status == CallStatus::InvalidArgument && !wide.value &&
                     wide.diagnostic.find("Gear") != std::string::npos));
        }

        // As a struct field, next to a bool and a byte enum.
        const auto tune = values.FindMethod<Fixture::Setting(Fixture::Setting)>("Tune");
        {
            INFO("A struct with enums was not found: " + tune.diagnostic);
            REQUIRE(tune.Succeeded());
        }
        const auto tuned = tune.value->Invoke(Fixture::Setting{Gear::On, Fixture::Shade::Dark, false, 4});
        {
            INFO("A struct with enum fields did not round-trip");
            REQUIRE((tuned.Succeeded() && tuned.value->Current == Gear::Auto &&
                     tuned.value->Tint == Fixture::Shade::Light && tuned.value->Locked && tuned.value->Level == 5));
        }
        const auto badStruct = values.FindMethod<Fixture::WideSetting(Fixture::WideSetting)>("Tune");
        {
            INFO("A struct with a wrong enum field was accepted: " + badStruct.diagnostic);
            REQUIRE(badStruct.status == CallStatus::InvalidArgument);
        }

        // Fields and properties, static and instance.
        const auto dial = *component.FindType("Iris.Examples.Component.Dial").value;
        const auto object = dial.CreateObject();
        {
            INFO("An object could not be created: " + object.diagnostic);
            REQUIRE(object.Succeeded());
        }
        const auto selected = *dial.FindField<Gear>("Selected").value;
        {
            INFO("An enum field did not round-trip");
            REQUIRE(
                (selected.Set(*object.value, Gear::On).Succeeded() && selected.Get(*object.value).value == Gear::On));
        }
        const auto tone = *dial.FindProperty<Fixture::Shade>("Tone").value;
        {
            INFO("An enum property did not round-trip");
            REQUIRE((tone.Get(*object.value).value == Fixture::Shade::Dark &&
                     tone.Set(*object.value, Fixture::Shade::Light).Succeeded() &&
                     tone.Get(*object.value).value == Fixture::Shade::Light));
        }
        const auto fallback = *dial.FindField<Gear>("Default").value;
        {
            INFO("A static enum field was wrong");
            REQUIRE(fallback.Get().value == Gear::Auto);
        }
        {
            INFO("An enum field matched its integer");
            REQUIRE(dial.FindField<std::int32_t>("Selected").status == CallStatus::NotFound);
        }
    }

    TEST_CASE("fixed-size arrays inside structs: bytes, UTF-16 units, floats, structs, enums and bools",
              "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto values = *component.FindType(ValuesType).value;

        // A fixed buffer of bytes ("char Text[16]") and one of UTF-16 units.
        const auto rename = values.FindMethod<Fixture::Label(Fixture::Label)>("Rename");
        {
            INFO("A struct with fixed buffers was not found: " + rename.diagnostic);
            REQUIRE(rename.Succeeded());
        }
        Fixture::Label label{};
        label.Id = 7;
        std::memcpy(label.Text, "original text", 13);
        label.Wide[0] = u'a';
        label.Wide[3] = u'\u03c0';
        const auto renamed = rename.value->Invoke(label);
        {
            INFO("A struct with fixed buffers did not cross: " + renamed.diagnostic);
            REQUIRE(renamed.Succeeded());
        }
        {
            INFO("A fixed buffer did not round-trip");
            REQUIRE((renamed.value->Id == 8 && std::string(renamed.value->Text) == "renamed" &&
                     renamed.value->Text[15] == 0 && renamed.value->Wide[0] == u'b' &&
                     renamed.value->Wide[3] == u'\u03c0'));
        }

        // [InlineArray] structs of floats, structs, enums and bools; all reversed by the callee.
        using Fixture::Gear;
        const auto reverse = values.FindMethod<Fixture::Bundle(Fixture::Bundle)>("Reverse");
        {
            INFO("A struct with inline arrays was not found: " + reverse.diagnostic);
            REQUIRE(reverse.Succeeded());
        }
        Fixture::Bundle bundle{
            {1, 2, 3, 4}, {{1, 2, 3}, {4, 5, 6}}, {Gear::Off, Gear::On, Gear::Auto}, {true, false, false}};
        std::memset(&bundle.Ready[1], 2, 1); // any nonzero byte is true
        const auto reversed = reverse.value->Invoke(bundle);
        {
            INFO("A struct with inline arrays did not cross: " + reversed.diagnostic);
            REQUIRE(reversed.Succeeded());
        }
        {
            INFO("Inline array elements moved or were lost");
            REQUIRE((reversed.value->Weights[0] == 4 && reversed.value->Weights[3] == 1 &&
                     reversed.value->Points[0].X == 4 && reversed.value->Points[1].Z == 3 &&
                     reversed.value->Gears[0] == Gear::Auto && reversed.value->Gears[2] == Gear::Off));
        }
        // Ready was {true, true(2), false}; reversed and negated it is {true, false, false}.
        {
            INFO("An inline array of bools was not read as 0 or 1");
            REQUIRE((reversed.value->Ready[0] && !reversed.value->Ready[1] && !reversed.value->Ready[2]));
        }

        // The length, the element type and the shape are all checked.
        const auto shorter = values.FindMethod<Fixture::ShortLabel(Fixture::ShortLabel)>("Rename");
        {
            INFO("A length mismatch was not diagnosed: " + shorter.diagnostic);
            REQUIRE((shorter.status == CallStatus::InvalidArgument &&
                     shorter.diagnostic.find("Text") != std::string::npos &&
                     shorter.diagnostic.find("16") != std::string::npos &&
                     shorter.diagnostic.find("15") != std::string::npos));
        }
        const auto signedText = values.FindMethod<Fixture::SignedLabel(Fixture::SignedLabel)>("Rename");
        {
            INFO("An element type mismatch was not diagnosed: " + signedText.diagnostic);
            REQUIRE((signedText.status == CallStatus::InvalidArgument &&
                     signedText.diagnostic.find("Text") != std::string::npos));
        }
        // Mapped as a scalar field, or as a different struct, an array is not accepted.
        {
            INFO("A different struct matched");
            REQUIRE(values.FindMethod<Fixture::Label(Fixture::Bundle)>("Rename").status == CallStatus::NotFound);
        }
        {
            INFO("A good mapping failed after bad ones");
            REQUIRE(values.FindMethod<Fixture::Label(Fixture::Label)>("Rename").Succeeded());
        }
    }

    TEST_CASE("a struct mapping can be validated on its own, before any call uses it", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        using Fixture::Vec3;
        {
            INFO("A matching mapping was not validated");
            REQUIRE(component.ValidateStruct<Vec3>().Succeeded());
        }
        {
            INFO("Validating twice failed");
            REQUIRE(component.ValidateStruct<Vec3>().Succeeded());
        }
        {
            INFO("Nested, enum and array mappings were not validated");
            REQUIRE((component.ValidateStruct<Fixture::Pose>().Succeeded() &&
                     component.ValidateStruct<Fixture::Setting>().Succeeded() &&
                     component.ValidateStruct<Fixture::Label>().Succeeded() &&
                     component.ValidateStruct<Fixture::Bundle>().Succeeded()));
        }

        // Each kind of mismatch is diagnosed, names what differs, and leaves the component usable.
        const auto wide = component.ValidateStruct<Fixture::WideVec3>();
        {
            INFO("A size mismatch was not diagnosed: " + wide.diagnostic);
            REQUIRE((wide.status == CallStatus::InvalidArgument && wide.diagnostic.find("24") != std::string::npos));
        }
        const auto swapped = component.ValidateStruct<Fixture::SwappedVec3>();
        {
            INFO("A field-order mismatch was not diagnosed: " + swapped.diagnostic);
            REQUIRE((swapped.status == CallStatus::InvalidArgument &&
                     swapped.diagnostic.find("order") != std::string::npos));
        }
        const auto nested = component.ValidateStruct<Fixture::WidePose>();
        {
            INFO("A nested mismatch was accepted: " + nested.diagnostic);
            REQUIRE(nested.status == CallStatus::InvalidArgument);
        }
        const auto text = component.ValidateStruct<Fixture::TextHolder>();
        {
            INFO("A non-blittable type was accepted: " + text.diagnostic);
            REQUIRE(
                (text.status == CallStatus::InvalidArgument && text.diagnostic.find("blittable") != std::string::npos));
        }
        {
            INFO("A missing type was not NotFound");
            REQUIRE(component.ValidateStruct<Fixture::Ghost>().status == CallStatus::NotFound);
        }
        const auto length = component.ValidateStruct<Fixture::ShortLabel>();
        {
            INFO("A field mismatch did not name the field: " + length.diagnostic);
            REQUIRE(
                (length.status == CallStatus::InvalidArgument && length.diagnostic.find("Text") != std::string::npos));
        }

        {
            INFO("The component was not usable after failed validations");
            REQUIRE((component.ValidateStruct<Vec3>().Succeeded() && component.FindType(StructsType).Succeeded()));
        }

        // A released component refuses.
        {
            INFO("The component was not released");
            REQUIRE(component.Release().Succeeded());
        }
        {
            INFO("A released component validated a struct");
            REQUIRE(component.ValidateStruct<Vec3>().status == CallStatus::InvalidHandle);
        }
    }

    TEST_CASE("a type's fields, properties and methods are listed with kinds, offsets, visibility and attributes",
              "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto names = [](const auto& members) {
            std::vector<std::string> result;
            for (const auto& member : members)
            {
                result.push_back(member.name);
            }
            return result;
        };
        using Names = std::vector<std::string>;
        const std::string script = "Iris.Examples.Component.ScriptAttribute";

        // A struct: declaration order, kinds, byte offsets, statics and constants.
        const auto probe = *component.FindType("Iris.Examples.Component.Probe").value;
        const auto fields = probe.GetFields();
        {
            INFO("Fields were not listed: " + fields.diagnostic);
            REQUIRE(fields.Succeeded());
        }
        {
            INFO("A struct's fields were listed in the wrong order, or a compiler-generated one appeared");
            REQUIRE(names(*fields.value) == Names{"Opacity", "Position", "Active", "hidden", "Count", "Max", "Style"});
        }
        const auto& opacity = (*fields.value)[0];
        {
            INFO("A float field was described wrongly");
            REQUIRE((opacity.kind == TypeKind::Float && opacity.type.fullName == "System.Single" && !opacity.isStatic &&
                     !opacity.isReadOnly && opacity.visibility == MemberVisibility::Public &&
                     opacity.offset == std::optional<std::size_t>(0)));
        }
        {
            INFO("A field's attribute was not read with its arguments");
            REQUIRE((opacity.attributes.size() == 1 && opacity.attributes[0].type.fullName == script &&
                     opacity.attributes[0].arguments.size() == 3 &&
                     std::get<std::string>(opacity.attributes[0].arguments[0].value) == "opacity" &&
                     std::get<std::int64_t>(opacity.attributes[0].arguments[1].value) == 2 &&
                     opacity.attributes[0].arguments[2].name == "Weight" &&
                     std::get<double>(opacity.attributes[0].arguments[2].value) == 0.5));
        }
        const auto& position = (*fields.value)[1];
        {
            INFO("A struct field was described wrongly");
            REQUIRE((position.kind == TypeKind::Struct && position.type.fullName == "Iris.Examples.Component.Vec3" &&
                     position.offset == std::optional<std::size_t>(4)));
        }
        {
            INFO("A bool field was described wrongly");
            REQUIRE(((*fields.value)[2].kind == TypeKind::Bool &&
                     (*fields.value)[2].offset == std::optional<std::size_t>(16)));
        }
        {
            INFO("A private field was described wrongly");
            REQUIRE(((*fields.value)[3].visibility == MemberVisibility::Private && !(*fields.value)[3].isStatic &&
                     (*fields.value)[3].kind == TypeKind::Int32));
        }
        {
            INFO("A static field has no offset");
            REQUIRE(((*fields.value)[4].isStatic && !(*fields.value)[4].offset && !(*fields.value)[4].isReadOnly));
        }
        {
            INFO("A constant is read-only");
            REQUIRE(((*fields.value)[5].isStatic && (*fields.value)[5].isReadOnly));
        }
        {
            INFO("An enum field was described wrongly");
            REQUIRE(((*fields.value)[6].kind == TypeKind::Enum &&
                     (*fields.value)[6].type.fullName == "Iris.Examples.Component.Mode"));
        }

        // Offsets agree with the layout struct-mapping validation checks.
        const auto pose = *component.FindType("Iris.Examples.Component.Pose").value;
        const auto poseFields = pose.GetFields();
        {
            INFO("Reported offsets disagree with the validated layout");
            REQUIRE((poseFields.Succeeded() && poseFields.value->size() == 4 &&
                     (*poseFields.value)[0].offset == std::optional<std::size_t>(offsetof(Fixture::Pose, Position)) &&
                     (*poseFields.value)[1].offset == std::optional<std::size_t>(offsetof(Fixture::Pose, Scale)) &&
                     (*poseFields.value)[2].offset == std::optional<std::size_t>(offsetof(Fixture::Pose, Weight)) &&
                     (*poseFields.value)[3].offset == std::optional<std::size_t>(offsetof(Fixture::Pose, Flags)) &&
                     component.ValidateStruct<Fixture::Pose>().Succeeded()));
        }

        const auto probeProperties = probe.GetProperties();
        {
            INFO("A read-only property was described wrongly");
            REQUIRE((probeProperties.Succeeded() && names(*probeProperties.value) == Names{"Hidden"} &&
                     (*probeProperties.value)[0].canRead && !(*probeProperties.value)[0].canWrite));
        }

        // A class: only declared members, no backing fields, no indexer, no accessors.
        const auto probed = *component.FindType("Iris.Examples.Component.Probed").value;
        const auto classFields = probed.GetFields();
        {
            INFO("A class's declared fields were wrong (inherited and compiler-generated ones are excluded)");
            REQUIRE(
                (classFields.Succeeded() && names(*classFields.value) == Names{"Legacy", "Wide", "Fixed", "Shared"}));
        }
        const auto& legacy = (*classFields.value)[0];
        {
            INFO("A class field's offset or attribute was wrong");
            REQUIRE((!legacy.offset && legacy.attributes.size() == 1 &&
                     legacy.attributes[0].type.fullName == "System.ObsoleteAttribute" &&
                     std::get<std::string>(legacy.attributes[0].arguments[0].value) == "old"));
        }
        {
            INFO("A class field's visibility, kind or flags were wrong");
            REQUIRE(((*classFields.value)[1].visibility == MemberVisibility::ProtectedInternal &&
                     (*classFields.value)[1].kind == TypeKind::Int64 && (*classFields.value)[2].isReadOnly &&
                     (*classFields.value)[2].kind == TypeKind::String && (*classFields.value)[3].isStatic &&
                     (*classFields.value)[3].kind == TypeKind::Double));
        }

        const auto properties = probed.GetProperties();
        {
            INFO("A class's properties were wrong (the indexer is excluded)");
            REQUIRE((properties.Succeeded() && names(*properties.value) == Names{"Title", "Size", "Computed", "Flag"}));
        }
        const auto& title = (*properties.value)[0];
        {
            INFO("A read-write property was described wrongly");
            REQUIRE((title.kind == TypeKind::String && title.canRead && title.canWrite && !title.isStatic &&
                     title.visibility == MemberVisibility::Public));
        }
        const auto& size = (*properties.value)[1];
        {
            INFO("A property's attribute or accessors were wrong");
            REQUIRE((size.canWrite && size.visibility == MemberVisibility::Public && size.attributes.size() == 1 &&
                     size.attributes[0].arguments.size() == 2 && size.attributes[0].arguments[1].name == "Tag" &&
                     std::get<std::string>(size.attributes[0].arguments[1].value) == "range"));
        }
        {
            INFO("A computed or static property was described wrongly");
            REQUIRE((!(*properties.value)[2].canWrite && (*properties.value)[3].isStatic &&
                     (*properties.value)[3].kind == TypeKind::Bool));
        }

        const auto methods = probed.GetMethods();
        {
            INFO("A class's methods were wrong (accessors, constructors and inherited methods are excluded)");
            REQUIRE((methods.Succeeded() && names(*methods.value) == Names{"Scale", "Reset", "Secret"}));
        }
        const auto& scale = (*methods.value)[0];
        {
            INFO("A method was described wrongly");
            REQUIRE((scale.returnKind == TypeKind::Float && !scale.isStatic && scale.parameters.size() == 2 &&
                     scale.parameters[0].name == "factor" && scale.parameters[0].kind == TypeKind::Float &&
                     scale.parameters[1].name == "label" && scale.parameters[1].kind == TypeKind::String &&
                     std::any_of(scale.attributes.begin(), scale.attributes.end(),
                                 [&](const AttributeInfo& attribute) { return attribute.type.fullName == script; })));
        }
        {
            INFO("A static void method was described wrongly");
            REQUIRE(((*methods.value)[1].isStatic && (*methods.value)[1].returnKind == TypeKind::Void &&
                     (*methods.value)[1].parameters.empty()));
        }
        const auto& secret = (*methods.value)[2];
        {
            INFO("Parameter kinds were wrong");
            REQUIRE((secret.visibility == MemberVisibility::Private && secret.parameters.size() == 3 &&
                     secret.parameters[0].kind == TypeKind::Struct && secret.parameters[1].kind == TypeKind::Array &&
                     secret.parameters[2].kind == TypeKind::Object));
        }

        // Listings are snapshots: nothing references the component afterwards.
        {
            INFO("The component was not released");
            REQUIRE(component.Release().Succeeded());
        }
        {
            INFO("A released component still listed members");
            REQUIRE((probe.GetFields().status == CallStatus::InvalidHandle &&
                     probed.GetProperties().status == CallStatus::InvalidHandle &&
                     probed.GetMethods().status == CallStatus::InvalidHandle));
        }
        {
            INFO("A snapshot changed after release");
            REQUIRE(fields.value->size() == 7);
        }
    }

    TEST_CASE("waiting for an unload reports Collected once a released component is gone", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        // Nothing released: harmless, and no collection is forced.
        const auto loaded = component.WaitForUnload(std::chrono::milliseconds(50));
        {
            INFO("Waiting on a loaded component was not NotRequested");
            REQUIRE((loaded.Succeeded() && *loaded.value == ComponentUnloadState::NotRequested));
        }
        {
            INFO("A negative timeout was accepted");
            REQUIRE(component.WaitForUnload(std::chrono::milliseconds(-1)).status == CallStatus::InvalidArgument);
        }

        {
            INFO("The component was not released");
            REQUIRE(component.Release().Succeeded());
        }
        const auto waited = component.WaitForUnload(std::chrono::seconds(10));
        {
            INFO("A released component with no roots was not collected: " + waited.diagnostic);
            REQUIRE((waited.Succeeded() && *waited.value == ComponentUnloadState::Collected));
        }
        {
            INFO("GetUnloadState disagrees after a wait");
            REQUIRE(*component.GetUnloadState().value == ComponentUnloadState::Collected);
        }
        // Already collected: harmless, and quick.
        const auto again = component.WaitForUnload(std::chrono::seconds(10));
        {
            INFO("Waiting on a collected component failed");
            REQUIRE((again.Succeeded() && *again.value == ComponentUnloadState::Collected));
        }

        // A moved-from owner is InvalidHandle.
        auto moved = std::move(component);
        {
            INFO("A moved-from owner waited");
            REQUIRE(component.WaitForUnload(std::chrono::milliseconds(1)).status == CallStatus::InvalidHandle);
        }
    }

    TEST_CASE("a leaked root keeps the component Pending until it ends, and the host stays usable", "[.components]")
    {
        auto host = StartHost(checkArguments);
        Examples::leakStop = false;
        auto component = Load(*host, checkArguments.component);
        const auto leaks = *component.FindType("Iris.Examples.Component.Leaks").value;
        {
            INFO("The leak did not start");
            REQUIRE(leaks.FindMethod<void()>("Start").value->Invoke().Succeeded());
        }
        {
            INFO("The component was not released");
            REQUIRE(component.Release().Succeeded());
        }

        const auto started = std::chrono::steady_clock::now();
        const auto pending = component.WaitForUnload(std::chrono::milliseconds(300));
        const auto elapsed = std::chrono::steady_clock::now() - started;
        {
            INFO("A leaked component was reported as unloaded");
            REQUIRE((pending.Succeeded() && *pending.value == ComponentUnloadState::Pending));
        }
        {
            INFO("The wait did not honour its timeout");
            REQUIRE((elapsed >= std::chrono::milliseconds(250) && elapsed < std::chrono::seconds(20)));
        }
        {
            INFO("A zero timeout failed");
            REQUIRE(component.WaitForUnload(std::chrono::milliseconds(0)).Succeeded());
        }

        // The host works throughout: another component loads and runs.
        auto other = Load(*host, checkArguments.component);
        {
            INFO("The host was unusable after a timed-out wait");
            REQUIRE(other.FindType(CalculationType).Succeeded());
        }
        {
            INFO("The other component was not released");
            REQUIRE(other.Release().Succeeded());
        }

        // Once the root ends, the same wait reports Collected.
        Examples::leakStop = true;
        const auto collected = component.WaitForUnload(std::chrono::seconds(20));
        {
            INFO("The component was not collected after its root ended");
            REQUIRE((collected.Succeeded() && *collected.value == ComponentUnloadState::Collected));
        }
        {
            INFO("The other component was not collected");
            REQUIRE(other.WaitForUnload(std::chrono::seconds(20)).Succeeded());
        }
    }

    TEST_CASE("a struct defined in a shared assembly can be validated and passed, under managed field names",
              "[.components]")
    {
        auto configuration = checkArguments.configuration;
        configuration.sharedAssemblies = {checkArguments.component.parent_path() / "Iris.Examples.Contracts.dll"};
        auto host = std::make_unique<Host>();
        {
            INFO("Initialization with a shared assembly failed");
            REQUIRE(host->Initialize(configuration).success);
        }
        const auto component = Load(*host, checkArguments.component);

        // The type is in the contract, not the component, and the C++ members are x and y.
        const auto validated = component.ValidateStruct<Fixture::SharedPoint>();
        {
            INFO("A shared struct was not validated: " + validated.diagnostic);
            REQUIRE(validated.Succeeded());
        }
        const auto shift = (*component.FindType(SharedCalculatorType).value)
                               .FindMethod<Fixture::SharedPoint(Fixture::SharedPoint)>("Shift");
        {
            INFO("A method over a shared struct was not found: " + shift.diagnostic);
            REQUIRE(shift.Succeeded());
        }
        const auto shifted = shift.value->Invoke(Fixture::SharedPoint{10, 20});
        {
            INFO("A shared struct did not round-trip");
            REQUIRE((shifted.Succeeded() && shifted.value->x == 11 && shifted.value->y == 22));
        }
    }

    TEST_CASE("arrays of primitives, strings and structs: create, access, bulk copy and ranges", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        auto ints = Array<std::int32_t>::Create(component, 4);
        {
            INFO("Array creation failed: " + ints.diagnostic);
            REQUIRE(ints.Succeeded());
        }
        {
            INFO("A new array was wrong");
            REQUIRE((ints.value->Length().value == 4U && ints.value->Get(2).value == 0));
        }
        {
            INFO("An element did not round-trip");
            REQUIRE((ints.value->Set(2, -7).Succeeded() && ints.value->Get(2).value == -7));
        }
        const std::int32_t source[] = {10, 20, 30};
        std::int32_t back[4] = {};
        {
            INFO("A bulk copy was wrong");
            REQUIRE((ints.value->CopyIn(1, source).Succeeded() && ints.value->CopyOut(0, back).Succeeded() &&
                     back[0] == 0 && back[1] == 10 && back[3] == 30));
        }
        const auto past = ints.value->Get(4);
        {
            INFO("An out-of-range read was not diagnosed: " + past.diagnostic);
            REQUIRE((past.status == CallStatus::InvalidArgument && !past.value &&
                     past.diagnostic.find("outside") != std::string::npos));
        }
        {
            INFO("An out-of-range write or copy was accepted");
            REQUIRE((ints.value->Set(99, 1).status == CallStatus::InvalidArgument &&
                     ints.value->CopyIn(2, source).status == CallStatus::InvalidArgument &&
                     ints.value->CopyOut(5, std::span<std::int32_t>(back, 0)).status == CallStatus::InvalidArgument));
        }
        {
            INFO("An empty array failed");
            REQUIRE(Array<std::int32_t>::Create(component, 0).value->Length().value == 0U);
        }
        {
            INFO("An impossible length was accepted");
            REQUIRE(Array<std::int32_t>::Create(component, std::numeric_limits<std::size_t>::max()).status ==
                    CallStatus::InvalidArgument);
        }
        auto emptyArray = Array<std::int32_t>::Create(component, 0);
        {
            INFO("An empty bulk copy failed");
            REQUIRE((emptyArray.value->CopyIn(0, std::span<const std::int32_t>()).Succeeded() &&
                     emptyArray.value->CopyOut(0, std::span<std::int32_t>()).Succeeded()));
        }
        auto flagArray = Array<bool>::Create(component, 3);
        const bool flagSource[] = {true, false, true};
        bool flagBack[3] = {};
        {
            INFO("A bool bulk copy was wrong");
            REQUIRE((flagArray.value->CopyIn(0, flagSource).Succeeded() &&
                     flagArray.value->CopyOut(0, flagBack).Succeeded() && flagBack[0] && !flagBack[1] && flagBack[2]));
        }
        // An object that is not an array is diagnosed, not misread. Only the library can wrap an Object as
        // an Array (Detail is not API); the check guards against a bridge that returns the wrong object.
        auto counter = component.FindType(CounterType).value->CreateObject();
        auto adopted = Detail::ArrayAccess::Adopt<Array<std::int32_t>>(std::move(*counter.value));
        {
            INFO("A non-array object was used as an array");
            REQUIRE((adopted.Length().status == CallStatus::InvalidArgument &&
                     adopted.Get(0).status == CallStatus::InvalidArgument));
        }

        auto doubles = Array<double>::Create(component, 2);
        {
            INFO("A double element failed");
            REQUIRE((doubles.value->Set(1, 2.5).Succeeded() && doubles.value->Get(1).value == 2.5));
        }
        auto flags = Array<bool>::Create(component, 2);
        {
            INFO("A bool element failed");
            REQUIRE((flags.value->Set(1, true).Succeeded() && flags.value->Get(1).value == true &&
                     flags.value->Get(0).value == false));
        }

        auto words = Array<std::string>::Create(component, 3);
        {
            INFO("A new string element was not null");
            REQUIRE((words.value->Get(0).Succeeded() && !*words.value->Get(0).value));
        }
        const std::string wolf = "\xCF\x80\xF0\x9F\x90\xBA";
        {
            INFO("A string element did not round-trip");
            REQUIRE((words.value->Set(1, std::string_view(wolf)).Succeeded() && **words.value->Get(1).value == wolf));
        }
        {
            INFO("An empty string element came back wrong");
            REQUIRE((words.value->Set(2, std::string_view()).Succeeded() && **words.value->Get(2).value == ""));
        }
        {
            INFO("A null string element was not null");
            REQUIRE((words.value->Set(1, std::nullopt).Succeeded() && !*words.value->Get(1).value));
        }
        {
            INFO("Malformed UTF-8 was stored in an array");
            REQUIRE(words.value->Set(0, std::string_view("\xFF")).status == CallStatus::InvalidArgument);
        }

        using Fixture::Vec3;
        auto points = Array<Vec3>::Create(component, 3);
        {
            INFO("A struct array was not created: " + points.diagnostic);
            REQUIRE(points.Succeeded());
        }
        const Vec3 line[] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
        Vec3 readBack[3] = {};
        {
            INFO("A struct array did not round-trip");
            REQUIRE((points.value->CopyIn(0, line).Succeeded() && points.value->CopyOut(0, readBack).Succeeded() &&
                     readBack[2].Z == 9 && points.value->Get(1).value->Y == 5));
        }
        {
            INFO("A struct element did not store");
            REQUIRE((points.value->Set(0, Vec3{9, 9, 9}).Succeeded() && points.value->Get(0).value->X == 9));
        }
        {
            INFO("A bad struct mapping created an array");
            REQUIRE(Array<Fixture::WideVec3>::Create(component, 1).status == CallStatus::InvalidArgument);
        }
    }

    TEST_CASE("arrays pass to and return from managed methods, and null differs from empty", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto arrays = *component.FindType(ArraysType).value;
        using Ints = Array<std::int32_t>;
        const auto range = *arrays.FindMethod<std::optional<Ints>(std::int32_t)>("Range").value;
        auto made = range.Invoke(5);
        {
            INFO("A returned array was missing");
            REQUIRE((made.Succeeded() && made.value && *made.value));
        }
        {
            INFO("A returned array was wrong");
            REQUIRE(((*made.value)->Length().value == 5U && (*made.value)->Get(4).value == 4));
        }
        const auto sum = *arrays.FindMethod<std::int32_t(const Ints&)>("Sum").value;
        {
            INFO("An array argument was not passed");
            REQUIRE(sum.Invoke(**made.value).value == 10);
        }

        // Null versus empty, in both directions.
        const auto isNull = *arrays.FindMethod<bool(const Ints*)>("IsNull").value;
        const Ints* none = nullptr;
        {
            INFO("Managed code could not tell a null array from an array");
            REQUIRE((isNull.Invoke(none).value == true && isNull.Invoke(&**made.value).value == false));
        }
        const auto empty = *arrays.FindMethod<std::optional<Ints>()>("Empty").value;
        const auto nothing = *arrays.FindMethod<std::optional<Ints>()>("Null").value;
        const auto emptyResult = empty.Invoke();
        const auto nullResult = nothing.Invoke();
        {
            INFO("An empty array return was null or wrong");
            REQUIRE((emptyResult.Succeeded() && emptyResult.value && *emptyResult.value &&
                     (*emptyResult.value)->Length().value == 0U));
        }
        {
            INFO("A null array return was not empty");
            REQUIRE((nullResult.Succeeded() && nullResult.value && !*nullResult.value));
        }
        const auto echo = *arrays.FindMethod<std::optional<Ints>(const Ints*)>("EchoOrNull").value;
        {
            INFO("A null array did not round-trip");
            REQUIRE((!*echo.Invoke(none).value && *echo.Invoke(&**made.value).value));
        }

        // The managed array is shared: a callee's writes are visible through the handle.
        const auto fill = *arrays.FindMethod<std::optional<Ints>(const Ints&, std::int32_t)>("Fill").value;
        const auto filled = fill.Invoke(**made.value, 7);
        {
            INFO("An array was copied rather than shared");
            REQUIRE(((*made.value)->Get(0).value == 7 && (*filled.value)->Get(3).value == 7));
        }

        const auto scale =
            *arrays.FindMethod<std::optional<Array<double>>(const Array<double>&, double)>("Scale").value;
        auto doubles = Array<double>::Create(component, 2);
        (void)doubles.value->Set(0, 1.5);
        {
            INFO("A double array did not round-trip");
            REQUIRE((*scale.Invoke(*doubles.value, 2.0).value)->Get(0).value == 3.0);
        }

        // String arrays, including a null element, and struct arrays.
        const auto words = *arrays.FindMethod<std::optional<Array<std::string>>()>("Words").value;
        const auto wordResult = words.Invoke();
        const auto& list = **wordResult.value;
        {
            INFO("A string array return was wrong");
            REQUIRE((list.Length().value == 4U && **list.Get(1).value == "\xCF\x80\xF0\x9F\x90\xBA" &&
                     **list.Get(2).value == "" && **list.Get(3).value == std::string("A\0B", 3)));
        }
        const auto withNull = *arrays.FindMethod<std::optional<Array<std::string>>()>("WordsWithNull").value;
        const auto nullable = withNull.Invoke();
        {
            INFO("A null string element was lost");
            REQUIRE((!*(*nullable.value)->Get(1).value && **(*nullable.value)->Get(0).value == "first"));
        }
        const auto join = *arrays.FindMethod<std::optional<std::string>(const Array<std::string>&)>("Join").value;
        {
            INFO("A string array argument was wrong");
            REQUIRE(**join.Invoke(**nullable.value).value == "first+<null>+third");
        }

        using Fixture::Vec3;
        const auto lineMethod = *arrays.FindMethod<std::optional<Array<Vec3>>(std::int32_t)>("Line").value;
        const auto sumX = *arrays.FindMethod<float(const Array<Vec3>&)>("SumX").value;
        const auto lineResult = lineMethod.Invoke(4);
        {
            INFO("A struct array return or argument was wrong");
            REQUIRE(((*lineResult.value)->Get(3).value->Z == 9 && sumX.Invoke(**lineResult.value).value == 6.0f));
        }

        // Signatures are exact.
        {
            INFO("An array signature matched something else");
            REQUIRE((arrays.FindMethod<std::int32_t(const Array<double>&)>("Sum").status == CallStatus::NotFound &&
                     arrays.FindMethod<std::int32_t(std::int32_t)>("Sum").status == CallStatus::NotFound &&
                     arrays.FindMethod<std::int32_t(const Object&)>("Count").status == CallStatus::NotFound &&
                     arrays.FindMethod<std::int32_t(const Ints&)>("Jagged").status == CallStatus::NotFound));
        }
    }

    TEST_CASE("array handles are owned, validated and block release until freed", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto other = Load(*host, checkArguments.component);
        using Ints = Array<std::int32_t>;
        auto owned = Ints::Create(component, 3);
        {
            INFO("A component with a live array was released");
            REQUIRE((component.Release().status == CallStatus::InUse && component.IsLoaded()));
        }

        // Moves transfer ownership; the source stops working.
        Ints moved = std::move(*owned.value);
        {
            INFO("A moved array did not transfer ownership");
            REQUIRE((moved.IsValid() && moved.Length().value == 3U && !owned.value->IsValid() &&
                     owned.value->Length().status == CallStatus::InvalidHandle));
        }

        // Two returns of one managed array are independent handles over shared elements.
        const auto arrays = *component.FindType(ArraysType).value;
        const auto same = *arrays.FindMethod<std::optional<Ints>()>("SameArray").value;
        auto first = std::move(*same.Invoke().value);
        auto second = std::move(*same.Invoke().value);
        {
            INFO("Handles did not share the array");
            REQUIRE((first->Set(0, 99).Succeeded() && second->Get(0).value == 99));
        }
        {
            INFO("Releasing one handle affected the other");
            REQUIRE((first->Release().Succeeded() && !first->IsValid() && second->Get(0).value == 99));
        }

        // An array from another component is rejected before the call.
        const auto sum = *arrays.FindMethod<std::int32_t(const Ints&)>("Sum").value;
        auto foreign = Ints::Create(other, 1);
        {
            INFO("A foreign array was accepted");
            REQUIRE(sum.Invoke(*foreign.value).status == CallStatus::InvalidArgument);
        }
        {
            INFO("A released array was accepted as an argument");
            REQUIRE((moved.Release().Succeeded() && sum.Invoke(moved).status == CallStatus::InvalidHandle));
        }

        // An array outlives nothing: once its component is gone it reports InvalidHandle.
        std::optional<Ints> orphan;
        {
            Component dying = Load(*host, checkArguments.component);
            orphan.emplace(std::move(*Ints::Create(dying, 2).value));
            {
                INFO("An array was unusable in its own component");
                REQUIRE(orphan->Length().value == 2U);
            }
        }
        {
            INFO("An array still worked after its component was destroyed");
            REQUIRE((orphan->Length().status == CallStatus::InvalidHandle &&
                     orphan->Get(0).status == CallStatus::InvalidHandle));
        }

        // With everything freed the component releases; destroying a component force-releases arrays.
        (void)second->Release();
        {
            INFO("A component with no live arrays could not be released");
            REQUIRE(component.Release().Succeeded());
        }
        auto survivor = Ints::Create(other, 1);
        Ints stale = std::move(*survivor.value);
        {
            Component dying = Load(*host, checkArguments.component);
            auto inner = Ints::Create(dying, 1);
            {
                INFO("Creating an array in a second component failed");
                REQUIRE(inner.Succeeded());
            }
        }
        {
            INFO("Destroying another component affected this array");
            REQUIRE(stale.Length().Succeeded());
        }
    }

    TEST_CASE("bound internal calls reach native code with arguments and results", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto natives = *component.FindType(NativesType).value;
        const auto add = *natives.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("CallAdd").value;
        {
            INFO("A bound function returned the wrong value");
            REQUIRE(add.Invoke(20, 22).value == 42);
        }
        {
            INFO("A double function was wrong");
            REQUIRE(natives.FindMethod<double(double)>("CallHalf").value->Invoke(5.0).value == 2.5);
        }
        {
            INFO("Mixed argument widths were wrong");
            REQUIRE(natives.FindMethod<std::int64_t(std::int64_t, std::uint8_t)>("CallCombine")
                        .value->Invoke(7, 255)
                        .value == 7255);
        }
        {
            INFO("A UTF-8 string did not arrive as bytes and a length");
            REQUIRE(natives.FindMethod<std::int32_t(std::string_view)>("CallMeasure")
                        .value->Invoke("\xCF\x80\xF0\x9F\x90\xBA")
                        .value == 6);
        }
        using Fixture::Vec3;
        const auto normalized = natives.FindMethod<Vec3(Vec3)>("CallNormalize").value->Invoke(Vec3{3, 0, 4});
        {
            INFO("A struct passed by pointer was not updated");
            REQUIRE((normalized.Succeeded() && std::abs(normalized.value->X - 0.6f) < 1e-6f &&
                     std::abs(normalized.value->Z - 0.8f) < 1e-6f));
        }
        const auto negate = natives.FindMethod<bool(bool)>("CallNegate");
        {
            INFO("A bool crossed an internal call as something other than one byte");
            REQUIRE((negate.Succeeded() && negate.value->Invoke(true).value == false &&
                     negate.value->Invoke(false).value == true));
        }
        Examples::recordedValue = 0;
        {
            INFO("A void function was not called");
            REQUIRE((natives.FindMethod<void(std::int32_t)>("CallRecord").value->Invoke(9).Succeeded() &&
                     Examples::recordedValue == 9));
        }
    }

    TEST_CASE("a C++ exception in an internal call is stopped at the boundary", "[.components]")
    {
        SinkGuard guard(checkArguments);
        auto host = StartHost(checkArguments);
        std::vector<Logged> logged;
        std::mutex mutex;
        {
            INFO("The log sink was not set");
            REQUIRE(host->SetLogSink([&](const LogMessage& message) {
                            const std::lock_guard lock(mutex);
                            logged.push_back({message.level, std::string(message.text)});
                        })
                        .Succeeded());
        }
        const auto component = Load(*host, checkArguments.component);
        const auto natives = *component.FindType(NativesType).value;
        const auto thrown = natives.FindMethod<std::int32_t()>("CallThrows").value->Invoke();
        {
            INFO("The managed caller did not get a value-initialized result");
            REQUIRE((thrown.Succeeded() && thrown.value == 0));
        }
        const std::lock_guard lock(mutex);
        const bool reported = std::any_of(logged.begin(), logged.end(), [](const Logged& entry) {
            return entry.level == LogLevel::Error && entry.text.find("native failure") != std::string::npos;
        });
        {
            INFO("The escaped exception was not reported through the log sink");
            REQUIRE(reported);
        }
        // The runtime is intact and later calls still work.
        {
            INFO("Internal calls stopped working after an exception");
            REQUIRE(natives.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("CallAdd").value->Invoke(1, 2).value ==
                    3);
        }
    }

    TEST_CASE("internal call registration is idempotent and rejects conflicts", "[.components]")
    {
        const std::string name = "Iris.Examples.Component.Natives.Add";
        {
            INFO("Registering the same function again failed");
            REQUIRE(Iris::RegisterInternalCall<&Examples::Add>(name).Succeeded());
        }
        {
            INFO("A different function under a used name was accepted");
            REQUIRE(Iris::RegisterInternalCall<&Examples::Combine>(name).status == CallStatus::InvalidArgument);
        }
        {
            INFO("An empty name was accepted");
            REQUIRE(Iris::RegisterInternalCall<&Examples::Add>("").status == CallStatus::InvalidArgument);
        }
        auto host = StartHost(checkArguments);
        // A registration made after the host is up is accepted, and harmless without a matching field.
        {
            INFO("Registering after start-up failed");
            REQUIRE(Iris::RegisterInternalCall<&Examples::Half>("Iris.Examples.Component.Natives.Unused").Succeeded());
        }
        {
            INFO("Registering twice after start-up failed");
            REQUIRE(Iris::RegisterInternalCall<&Examples::Half>("Iris.Examples.Component.Natives.Unused").Succeeded());
        }
        CheckCalculation(Load(*host, checkArguments.component));
    }

    TEST_CASE("a component lists its public types in declaration order", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto component = Load(*host, checkArguments.component);
        const auto listed = component.GetTypes();
        {
            INFO("Listing returned too few types: " + listed.diagnostic);
            REQUIRE((listed.Succeeded() && listed.value->size() > 10));
        }
        std::vector<std::string> names;
        for (const auto& type : *listed.value)
        {
            const auto info = type.GetInfo();
            {
                INFO("A listed type did not belong to the component");
                REQUIRE((info.Succeeded() && info.value->assemblyName == "Iris.Examples.Component"));
            }
            names.push_back(info.value->fullName);
        }
        const auto index = [&](std::string_view name) {
            return std::find(names.begin(), names.end(), std::string(name)) - names.begin();
        };
        const auto count = static_cast<std::ptrdiff_t>(names.size());
        {
            INFO("Types were missing or out of declaration order");
            REQUIRE((index("Iris.Examples.Component.Actor") < count && index("Iris.Examples.Component.Boss") < count &&
                     index("Iris.Examples.Component.Actor") < index("Iris.Examples.Component.Player") &&
                     index("Iris.Examples.Component.Player") < index("Iris.Examples.Component.Enemy") &&
                     index("Iris.Examples.Component.Enemy") < index("Iris.Examples.Component.Boss")));
        }
        {
            INFO("An internal or generic type was listed");
            REQUIRE((index("Iris.Examples.Component.HiddenActor") == count &&
                     std::none_of(names.begin(), names.end(),
                                  [](const std::string& name) { return name.find("Box") != std::string::npos; })));
        }
        // Every listed type can also be found by name, and listing again gives the same set.
        for (const auto& name : names)
        {
            {
                INFO("A listed type could not be found: " + name);
                REQUIRE(component.FindType(name).Succeeded());
            }
        }
        {
            INFO("Listing twice differed");
            REQUIRE(component.GetTypes().value->size() == names.size());
        }
        {
            INFO("A nested public type was not listed after its enclosing type");
            REQUIRE((index("Iris.Examples.Component.Plain+Inner") < count &&
                     index("Iris.Examples.Component.Plain") < index("Iris.Examples.Component.Plain+Inner")));
        }

        // The list is a set of handles: it stops working with the component, and holds nothing of its own.
        auto types = std::move(*listed.value);
        {
            INFO("Listed types kept the component from being released");
            REQUIRE(component.Release().Succeeded());
        }
        {
            INFO("Listed types worked after their component was released");
            REQUIRE((types.front().GetInfo().status == CallStatus::InvalidHandle &&
                     component.GetTypes().status == CallStatus::InvalidHandle));
        }
    }

    TEST_CASE("base types, interfaces and assignability across the component, a contract and the framework",
              "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto player = *component.FindType("Iris.Examples.Component.Player").value;
        const auto actor = *component.FindType("Iris.Examples.Component.Actor").value;
        const auto boss = *component.FindType("Iris.Examples.Component.Boss").value;
        const auto base = player.GetBaseType();
        {
            INFO("The base type was wrong");
            REQUIRE((base.Succeeded() && base.value->has_value() &&
                     (*base.value)->fullName == "Iris.Examples.Component.Actor" &&
                     (*base.value)->assemblyName == "Iris.Examples.Component"));
        }
        const auto objectBase = actor.GetBaseType();
        {
            INFO("A class without a base declared the wrong base");
            REQUIRE((objectBase.Succeeded() && objectBase.value->has_value() &&
                     (*objectBase.value)->fullName == "System.Object"));
        }
        const auto interfaces = player.GetInterfaces();
        {
            INFO("The interfaces were wrong or unsorted");
            REQUIRE((interfaces.Succeeded() && interfaces.value->size() == 2 &&
                     (*interfaces.value)[0].fullName == "Iris.Examples.Contracts.IScript" &&
                     (*interfaces.value)[0].assemblyName == "Iris.Examples.Contracts" &&
                     (*interfaces.value)[1].fullName == "System.IDisposable"));
        }
        {
            INFO("An inherited interface was missed");
            REQUIRE(boss.GetInterfaces().value->size() == 1);
        }

        {
            INFO("Assignability between component types was wrong");
            REQUIRE((player.IsAssignableTo(actor).value == true && boss.IsAssignableTo(actor).value == true &&
                     actor.IsAssignableTo(player).value == false && player.IsAssignableTo(player).value == true));
        }
        {
            INFO("Assignability to framework and contract types was wrong");
            REQUIRE((player.IsAssignableTo("System.IDisposable").value == true &&
                     player.IsAssignableTo("Iris.Examples.Contracts.IScript").value == true &&
                     boss.IsAssignableTo("Iris.Examples.Contracts.IScript").value == true &&
                     player.IsAssignableTo("System.Object").value == true &&
                     component.FindType("Iris.Examples.Component.Plain")
                             .value->IsAssignableTo("Iris.Examples.Contracts.IScript")
                             .value == false));
        }
        const auto unknown = player.IsAssignableTo("No.Such.Type");
        {
            INFO("An unknown target was not NotFound");
            REQUIRE((unknown.status == CallStatus::NotFound && !unknown.value));
        }
        {
            INFO("An empty target name was accepted");
            REQUIRE(player.IsAssignableTo("").status == CallStatus::InvalidArgument);
        }
        const auto second = Load(*host, checkArguments.component);
        {
            INFO("A type of another component was accepted");
            REQUIRE(player.IsAssignableTo(*second.FindType("Iris.Examples.Component.Actor").value).status ==
                    CallStatus::InvalidArgument);
        }
    }

    TEST_CASE("custom attributes list their constructor and named arguments without running component code",
              "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto attributes = component.FindType("Iris.Examples.Component.Player").value->GetAttributes();
        {
            INFO("Attributes failed: " + attributes.diagnostic);
            REQUIRE(attributes.Succeeded());
        }
        std::vector<AttributeInfo> scripts, others;
        for (const auto& attribute : *attributes.value)
        {
            (attribute.type.fullName == "Iris.Examples.Component.ScriptAttribute" ? scripts : others)
                .push_back(attribute);
        }
        {
            INFO("The attributes of Player were not listed");
            REQUIRE((scripts.size() == 2 && others.size() == 2));
        }
        // [Script("player", 3, Enabled = true, Weight = 0.5)]
        const auto& player = scripts[0];
        {
            INFO("A constructor or named argument was wrong");
            REQUIRE((player.arguments.size() == 4 && player.arguments[0].name.empty() &&
                     std::get<std::string>(player.arguments[0].value) == "player" &&
                     std::get<std::int64_t>(player.arguments[1].value) == 3 &&
                     player.arguments[1].typeName == "System.Int32" && player.arguments[2].name == "Enabled" &&
                     std::get<bool>(player.arguments[2].value) && player.arguments[3].name == "Weight" &&
                     std::get<double>(player.arguments[3].value) == 0.5));
        }
        {
            INFO("A second attribute of the same type was wrong");
            REQUIRE(
                (scripts[1].arguments.size() == 1 && std::get<std::string>(scripts[1].arguments[0].value) == "hero"));
        }
        {
            INFO("An attribute's assembly was wrong");
            REQUIRE(player.type.assemblyName == "Iris.Examples.Component");
        }

        // The framework's attribute, with a string argument.
        const auto obsolete = std::find_if(others.begin(), others.end(), [](const AttributeInfo& attribute) {
            return attribute.type.fullName == "System.ObsoleteAttribute";
        });
        {
            INFO("A framework attribute was wrong");
            REQUIRE((obsolete != others.end() && std::get<std::string>(obsolete->arguments.at(0).value) == "replaced"));
        }

        // Every shape of constructor argument.
        const auto details = std::find_if(others.begin(), others.end(), [](const AttributeInfo& attribute) {
            return attribute.type.fullName == "Iris.Examples.Component.DetailsAttribute";
        });
        {
            INFO("The Details attribute was wrong");
            REQUIRE((details != others.end() && details->arguments.size() == 8));
        }
        const auto& a = details->arguments;
        {
            INFO("An enum argument was wrong");
            REQUIRE((std::get<std::int64_t>(a[0].value) == 2 && a[0].typeName == "Iris.Examples.Component.Mode"));
        }
        {
            INFO("A Type argument was wrong");
            REQUIRE((std::get<std::string>(a[1].value) == "Iris.Examples.Component.Actor" &&
                     a[1].typeName == "System.Type"));
        }
        {
            INFO("An unsigned argument was wrong");
            REQUIRE((std::get<std::uint64_t>(a[2].value) == 200 &&
                     std::get<std::uint64_t>(a[3].value) == std::numeric_limits<std::uint64_t>::max()));
        }
        {
            INFO("A float or char argument was wrong");
            REQUIRE((std::get<double>(a[4].value) == 1.5 && std::get<std::uint64_t>(a[5].value) == 'x'));
        }
        {
            INFO("A null argument was wrong");
            REQUIRE((std::holds_alternative<std::monostate>(a[6].value) && a[6].typeName == "System.String"));
        }
        {
            INFO("An array argument was not reported as unrepresentable");
            REQUIRE((std::holds_alternative<std::monostate>(a[7].value) && a[7].typeName == "System.Int32[]"));
        }

        // A type with none.
        {
            INFO("A type without attributes listed some");
            REQUIRE(component.FindType("Iris.Examples.Component.Plain").value->GetAttributes().value->empty());
        }
    }

    TEST_CASE("replacing a component with version B: new behaviour, and no handle from A ever works again",
              "[.components]")
    {
        const auto pathB = VersionBPath();
        auto host = StartHost(checkArguments);
        for (int cycle = 0; cycle < 4; ++cycle)
        {
            std::optional<Component> a;
            a.emplace(Load(*host, checkArguments.component));
            {
                INFO("Version A did not behave as A");
                REQUIRE(VersionNumber(*a) == 1);
            }
            const auto aVersion = *a->FindType("Iris.Examples.Component.ComponentVersion").value;
            const auto aNumber = *aVersion.FindMethod<std::int32_t()>("Number").value;
            const auto aMember = *a->FindType(SettingsType).value->FindField<std::int32_t>("StaticCount").value;
            const auto aTypes = *a->GetTypes().value;
            auto aObject = a->FindType(CounterType).value->CreateObject();
            auto aArray = Array<std::int32_t>::Create(*a, 3);
            {
                INFO("Version A's object or array was not created");
                REQUIRE((aObject.Succeeded() && aArray.Succeeded()));
            }
            {
                INFO("Live objects did not block replacing the component");
                REQUIRE(a->Release().status == CallStatus::InUse);
            }
            {
                INFO("Version A could not be released");
                REQUIRE((aObject.value->Release().Succeeded() && aArray.value->Release().Succeeded() &&
                         a->Release().Succeeded()));
            }

            // Version B loads from its own path and behaves differently.
            const auto b = Load(*host, pathB);
            {
                INFO("Version B did not behave as B");
                REQUIRE(VersionNumber(b) == 2);
            }
            {
                INFO("Version B's types were wrong");
                REQUIRE((b.FindType("Iris.Examples.Component.OnlyInB").Succeeded() &&
                         b.FindType("Iris.Examples.Component.OnlyInA").status == CallStatus::NotFound));
            }

            // Nothing from A works, and A's handles are not reinterpreted against B.
            {
                INFO("A handle from version A survived the replacement");
                REQUIRE((aVersion.GetInfo().status == CallStatus::InvalidHandle &&
                         aNumber.Invoke().status == CallStatus::InvalidHandle &&
                         aMember.Get().status == CallStatus::InvalidHandle &&
                         aTypes.front().GetInfo().status == CallStatus::InvalidHandle &&
                         a->GetInfo().status == CallStatus::InvalidHandle &&
                         a->FindType(CounterType).status == CallStatus::InvalidHandle &&
                         aObject.value->Release().Succeeded() && !aObject.value->IsValid() &&
                         aArray.value->Length().status == CallStatus::InvalidHandle));
            }
            {
                INFO("Using A's stale handles disturbed version B");
                REQUIRE(VersionNumber(b) == 2);
            }
        }
    }

    TEST_CASE("a failed replacement leaves the host usable and the next version loads", "[.components]")
    {
        const auto pathB = VersionBPath();
        auto host = StartHost(checkArguments);
        {
            const auto a = Load(*host, checkArguments.component);
            {
                INFO("Version A did not behave as A");
                REQUIRE(VersionNumber(a) == 1);
            }
        }
        const auto missing = host->LoadComponent(pathB.parent_path() / "Missing.Component.dll");
        {
            INFO("Loading a missing component did not fail with a diagnostic");
            REQUIRE((!missing.Succeeded() && !missing.value && !missing.diagnostic.empty()));
        }
        const auto b = Load(*host, pathB);
        {
            INFO("Version B did not load after a failed replacement");
            REQUIRE(VersionNumber(b) == 2);
        }
        {
            INFO("Version A could not be loaded again");
            REQUIRE(VersionNumber(Load(*host, checkArguments.component)) == 1);
        }
    }

    TEST_CASE("member handles stop working when their component is released", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto released = Load(*host, checkArguments.component);
        const auto other = Load(*host, checkArguments.component);
        const auto stale = *released.FindType(SettingsType).value->FindField<std::int32_t>("StaticCount").value;
        const auto live = *other.FindType(SettingsType).value->FindField<std::int32_t>("StaticCount").value;
        {
            INFO("Release failed");
            REQUIRE(released.Release().Succeeded());
        }
        {
            INFO("A released component's member still worked");
            REQUIRE(
                (stale.Get().status == CallStatus::InvalidHandle && stale.Set(1).status == CallStatus::InvalidHandle));
        }
        {
            INFO("Releasing one component invalidated another's member");
            REQUIRE(live.Get().Succeeded());
        }
    }

    TEST_CASE("overloads that differ only by object parameter types are ambiguous", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto component = Load(*host, checkArguments.component);
        const auto factory = *component.FindType(FactoryType).value;
        const auto describe = factory.FindMethod<std::int32_t(const Object&)>("Describe");
        {
            INFO("An ambiguous object signature was not InvalidArgument");
            REQUIRE((describe.status == CallStatus::InvalidArgument &&
                     describe.diagnostic.find("ambiguous") != std::string::npos));
        }
        {
            INFO("A by-value signature matched an object overload");
            REQUIRE(factory.FindMethod<std::int32_t(std::int32_t)>("Describe").status == CallStatus::NotFound);
        }
    }

    TEST_CASE("a Unicode path loads, and a missing dependency fails only when used", "[.components]")
    {
        auto host = StartHost(checkArguments);
        const auto directory =
            checkArguments.component.parent_path() / std::filesystem::path(u8"component-π-\U0001F43A");
        std::filesystem::create_directories(directory);
        const auto path = directory / checkArguments.component.filename();
        std::filesystem::copy_file(checkArguments.component, path, std::filesystem::copy_options::overwrite_existing);
        const auto component = Load(*host, path);
        CheckInfo(component);
        // The copy lacks its private dependency, which loads lazily.
        const auto noDependency = Call(component, CalculationType, "Add", 20, 22);
        {
            INFO("Missing dependency escaped or omitted diagnostics");
            REQUIRE((noDependency.status == CallStatus::ManagedException && !noDependency.value &&
                     noDependency.diagnostic.find("FileNotFoundException") != std::string::npos));
        }
        {
            INFO("A call that needs no dependency failed");
            REQUIRE(Call(component, CalculationType, "LocalAdd", 20, 22).value == 42);
        }
    }

    TEST_CASE("moves and release transfer ownership and unload observation", "[.components]")
    {
        auto host = StartHost(checkArguments);
        auto original = Load(*host, checkArguments.component);
        Component moved(std::move(original));
        {
            INFO("A move left the old owner usable");
            REQUIRE((!original.IsLoaded() && original.GetUnloadState().status == CallStatus::InvalidHandle));
        }
        CheckInvalidHandle(original, "A moved-from owner was callable");
        CheckInfo(moved);
        {
            INFO("Release failed");
            REQUIRE((moved.Release().Succeeded() && !moved.IsLoaded()));
        }
        CheckUnload(moved, true);
        {
            INFO("A repeated release failed");
            REQUIRE(moved.Release().Succeeded());
        }
        CheckInvalidHandle(moved, "A released owner was callable");

        // A released owner keeps its weak observation; both forms of move transfer it, and
        // move-assignment also releases the registration it replaces.
        Component observation(std::move(moved));
        CheckUnload(observation, true);
        {
            INFO("Move construction kept the old observation");
            REQUIRE(moved.GetUnloadState().status == CallStatus::InvalidHandle);
        }
        auto replacement = Load(*host, checkArguments.component);
        replacement = std::move(observation);
        CheckUnload(replacement, true);
        {
            INFO("Move assignment kept the old observation");
            REQUIRE(observation.GetUnloadState().status == CallStatus::InvalidHandle);
        }
    }

    TEST_CASE("owners and results outlive host reset and destruction", "[.components]")
    {
        std::optional<Component> retained;
        CallResult<std::int32_t> failure;
        ComponentInfo snapshot;
        {
            auto host = StartHost(checkArguments);
            retained = Load(*host, checkArguments.component);
            snapshot = *retained->GetInfo().value;
            failure = Call(*retained, CalculationType, "Throw", 0, 0);
            // A failed attempt resets readiness; the owner keeps its bridge table.
            const auto failed = host->Initialize({checkArguments.configuration.runtimeConfigPath, {}, {}});
            {
                INFO("A failed initialization kept readiness");
                REQUIRE((!failed.success && !host->IsInitialized()));
            }
            CheckCalculation(*retained);
            {
                INFO("The host did not recover after a failed initialization");
                REQUIRE(host->Initialize(checkArguments.configuration).success);
            }
        }
        CheckInfo(*retained);
        CheckCalculation(*retained);
        CheckUnload(*retained, false);
        {
            INFO("Release after host destruction failed");
            REQUIRE((retained->Release().Succeeded() && !retained->IsLoaded()));
        }
        CheckUnload(*retained, true);
        {
            INFO("A released owner remained callable");
            REQUIRE(Call(*retained, CalculationType, "Add", 20, 22).status == CallStatus::InvalidHandle);
        }
        {
            INFO("A diagnostic did not outlive its host");
            REQUIRE(failure.diagnostic.find("InvalidOperationException") != std::string::npos);
        }
        {
            INFO("An info snapshot did not outlive its owner");
            REQUIRE(snapshot.assemblyName == "Iris.Examples.Component");
        }
    }

    TEST_CASE("a declared shared assembly binds every component to one copy", "[.components]")
    {
        auto configuration = checkArguments.configuration;
        configuration.sharedAssemblies = {checkArguments.component.parent_path() / "Iris.Examples.Contracts.dll"};
        auto host = std::make_unique<Host>();
        const auto initialized = host->Initialize(configuration);
        {
            INFO("Initialization with a shared assembly failed: " + initialized.diagnostic);
            REQUIRE(initialized.success);
        }
        const auto first = Load(*host, checkArguments.component);
        const auto second = Load(*host, checkArguments.component);
        {
            INFO("A component bound to its private contract copy");
            REQUIRE((Call(first, SharedCalculatorType, "ContractIsShared", 0, 0).value == 1 &&
                     Call(second, SharedCalculatorType, "ContractIsShared", 0, 0).value == 1));
        }
        // The shared contract's own internal call was bound when it was shared, and both components reach it.
        const auto triple = [&](const Component& component) {
            return (*component.FindType(NativesType).value)
                .FindMethod<std::int32_t(std::int32_t)>("CallSharedTriple")
                .value->Invoke(14)
                .value;
        };
        {
            INFO("A shared contract's internal call was not bound");
            REQUIRE((triple(first) == 42 && triple(second) == 42));
        }
        // Sharing the same file again is a no-op; a contract cannot also be a component.
        {
            INFO("Re-sharing the same file failed");
            REQUIRE(host->Initialize(configuration).success);
        }
        const auto asComponent = host->LoadComponent(configuration.sharedAssemblies.front());
        {
            INFO("A shared assembly loaded as a component");
            REQUIRE(asComponent.status == CallStatus::InvalidArgument);
        }
    }

    TEST_CASE("type queries give the same answers when the contract assembly is shared", "[.components]")
    {
        auto configuration = checkArguments.configuration;
        configuration.sharedAssemblies = {checkArguments.component.parent_path() / "Iris.Examples.Contracts.dll"};
        auto host = std::make_unique<Host>();
        {
            INFO("Initialization with a shared assembly failed");
            REQUIRE(host->Initialize(configuration).success);
        }
        const auto component = Load(*host, checkArguments.component);
        {
            INFO("The contract was not shared");
            REQUIRE(Call(component, SharedCalculatorType, "ContractIsShared", 0, 0).value == 1);
        }
        const auto player = *component.FindType("Iris.Examples.Component.Player").value;
        const auto interfaces = player.GetInterfaces();
        {
            INFO("The shared contract interface was not reported");
            REQUIRE((interfaces.Succeeded() && interfaces.value->size() == 2 &&
                     (*interfaces.value)[0].fullName == "Iris.Examples.Contracts.IScript"));
        }
        {
            INFO("Assignability to a shared contract type was wrong");
            REQUIRE((player.IsAssignableTo("Iris.Examples.Contracts.IScript").value == true &&
                     component.FindType(SharedCalculatorType)
                             .value->IsAssignableTo("Iris.Examples.Contracts.CalculatorBase")
                             .value == true &&
                     player.IsAssignableTo("Iris.Examples.Contracts.CalculatorBase").value == false));
        }
    }

    TEST_CASE("sharing a missing assembly fails initialization", "[.components]")
    {
        auto configuration = checkArguments.configuration;
        configuration.sharedAssemblies = {checkArguments.component.parent_path() / "Missing.Contracts.dll"};
        Host host;
        const auto initialized = host.Initialize(configuration);
        {
            INFO("A missing shared assembly was accepted or undiagnosed");
            REQUIRE((!initialized.success && !host.IsInitialized() &&
                     initialized.diagnostic.find("Sharing assembly failed") != std::string::npos));
        }
    }

    TEST_CASE("repeated scoped loads release on destruction", "[.components]")
    {
        auto host = StartHost(checkArguments);
        for (int i = 0; i < 16; ++i)
        {
            const auto scoped = Load(*host, checkArguments.component);
            CheckInfo(scoped);
            CheckCalculation(scoped);
        }
    }

    // Nothing is registered in this process: the component cannot load until its internal calls are.
    TEST_CASE("sharing a contract whose internal call is unregistered fails initialization, then works once registered",
              "[.unbound]")
    {
        auto configuration = checkArguments.configuration;
        configuration.sharedAssemblies = {checkArguments.component.parent_path() / "Iris.Examples.Contracts.dll"};
        Host host;
        const auto failed = host.Initialize(configuration);
        {
            INFO("An unbound shared contract field was not diagnosed: " + failed.diagnostic);
            REQUIRE((!failed.success && !host.IsInitialized() &&
                     failed.diagnostic.find("Iris.Examples.Contracts.SharedNatives.Triple") != std::string::npos &&
                     failed.diagnostic.find("no internal call is registered") != std::string::npos));
        }
        // The failed attempt shared nothing: once the function is registered the same sharing works.
        {
            INFO("Registering the shared contract's function failed");
            REQUIRE(Iris::RegisterInternalCall<&Iris::Examples::Triple>("Iris.Examples.Contracts.SharedNatives.Triple")
                        .Succeeded());
        }
        Host again;
        const auto shared = again.Initialize(configuration);
        {
            INFO("Sharing after registering failed: " + shared.diagnostic);
            REQUIRE(shared.success);
        }
    }

    TEST_CASE("a component with unbound internal calls fails to load, then loads once they are registered",
              "[.unbound]")
    {
        auto host = StartHost(checkArguments);
        const auto missing = host->LoadComponent(checkArguments.component);
        {
            INFO("Unbound internal calls were not diagnosed: " + missing.diagnostic);
            REQUIRE((missing.status == CallStatus::InvalidArgument && !missing.value &&
                     missing.diagnostic.find("10 internal call(s)") != std::string::npos &&
                     missing.diagnostic.find("Iris.Examples.Component.Natives.Add") != std::string::npos &&
                     missing.diagnostic.find("no internal call is registered") != std::string::npos));
        }
        // Registering after start-up, then loading, works; the failed load left nothing behind.
        {
            INFO("Registering after a failed load failed");
            REQUIRE(Iris::Examples::RegisterNatives().Succeeded());
        }
        const auto component = Load(*host, checkArguments.component);
        const auto natives = *component.FindType(NativesType).value;
        {
            INFO("A function registered after start-up did not bind");
            REQUIRE(natives.FindMethod<std::int32_t(std::int32_t, std::int32_t)>("CallAdd").value->Invoke(2, 3).value ==
                    5);
        }
    }

    // The component's internal calls are registered before any host exists, so every case but the unbound ones starts
    // with them bound. The unbound cases run in a process of their own, selected by the [unbound] tag.
    class NativesListener : public Catch::EventListenerBase
    {
      public:
        using Catch::EventListenerBase::EventListenerBase;

        void testRunStarting(const Catch::TestRunInfo&) override
        {
            const auto& selected = m_config->getTestsOrTags();
            if (std::find(selected.begin(), selected.end(), "[unbound]") != selected.end())
            {
                return;
            }
            if (const auto natives = Iris::Examples::RegisterNatives(); !natives.Succeeded())
            {
                throw std::runtime_error("Registering native functions failed: " + natives.diagnostic);
            }
        }
    };
} // namespace

CATCH_REGISTER_LISTENER(NativesListener)
