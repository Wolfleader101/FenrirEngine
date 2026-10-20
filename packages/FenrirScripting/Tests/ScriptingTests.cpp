#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirScripting/EngineMappings.hpp"
#include "FenrirScripting/Scripting.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

// A component the tests register from outside FenrirScripting, and a deliberately wrong mapping of the same C# struct.
namespace TestNative
{
    struct Health
    {
        std::int32_t Current;
        std::int32_t Max;
    };

    // The fields are in the wrong order for the C# Health.
    struct BadHealth
    {
        std::int32_t Max;
        std::int32_t Current;
    };

    // The right order, but Current has the wrong type.
    struct FloatHealth
    {
        float Current;
        std::int32_t Max;
    };
} // namespace TestNative

template <>
struct Iris::StructMapping<TestNative::Health>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Health";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestNative::Health, Current), IRIS_FIELD(TestNative::Health, Max));
};

template <>
struct Iris::StructMapping<TestNative::BadHealth>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Health";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestNative::BadHealth, Max), IRIS_FIELD(TestNative::BadHealth, Current));
};

template <>
struct Iris::StructMapping<TestNative::FloatHealth>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Health";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestNative::FloatHealth, Current), IRIS_FIELD(TestNative::FloatHealth, Max));
};

// A component with an Entity field, as a joint names its bodies, and the stand-in that maps it to the fixture's Tether.
namespace TestNative
{
    struct Tether
    {
        Fenrir::Entity target;
        std::int32_t length = 0;
    };

    struct TetherView
    {
        Fenrir::Detail::EntityLinkView target;
        std::int32_t length;
    };
} // namespace TestNative

template <>
struct Iris::StructMapping<TestNative::TetherView>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Tether";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(TestNative::TetherView, target, "Target"),
                                                      IRIS_FIELD_AS(TestNative::TetherView, length, "Length"));
};

// A component holding a tagged union, shaped like FenrirPhysics's Collider and CollisionShape: a private tag and
// overlapping storage for one of several kinds, implicit conversions from each kind and a checked view.
namespace TestNative
{
    struct Vec3
    {
        float X, Y, Z;
    };

    enum class ShapeType : std::int32_t
    {
        Box,
        Sphere,
    };

    struct BoxShape
    {
        Vec3 HalfExtents;
    };

    struct SphereShape
    {
        float Radius;
    };

    class Shape
    {
      public:
        Shape() : m_box{}
        {
        }
        Shape(BoxShape box) : m_type(ShapeType::Box), m_box(box) // NOLINT(google-explicit-constructor)
        {
        }
        Shape(SphereShape sphere) : m_type(ShapeType::Sphere), m_sphere(sphere) // NOLINT(google-explicit-constructor)
        {
        }

        [[nodiscard]] ShapeType Type() const
        {
            return m_type;
        }

        template <typename T>
        [[nodiscard]] bool Is() const
        {
            if constexpr (std::is_same_v<T, BoxShape>)
            {
                return m_type == ShapeType::Box;
            }
            else
            {
                static_assert(std::is_same_v<T, SphereShape>, "A Shape is a BoxShape or a SphereShape.");
                return m_type == ShapeType::Sphere;
            }
        }

        // The shape as kind T, which it must be.
        template <typename T>
        [[nodiscard]] T& As()
        {
            assert(Is<T>());
            if constexpr (std::is_same_v<T, BoxShape>)
            {
                return m_box;
            }
            else
            {
                return m_sphere;
            }
        }

      private:
        // The mapping names the private members, as FenrirPhysics's does.
        friend struct Iris::StructMapping<Shape>;

        ShapeType m_type = ShapeType::Box;
        union {
            BoxShape m_box;
            SphereShape m_sphere;
        };
    };

    struct Collider
    {
        Shape shape;
    };

    // The same layout as Shape and Collider, mapped to the fixture's deliberately wrong C# mirrors (one per variant).
    template <int Variant>
    struct ShapeLayout
    {
        ShapeType type;
        union {
            BoxShape box;
            SphereShape sphere;
        };
    };

    template <int Variant>
    struct ColliderLayout
    {
        ShapeLayout<Variant> shape;
    };

    constexpr int Misplaced = 1; // C#'s _sphere is not where the union is
    constexpr int Misnamed = 2;  // C# calls the sphere something else
} // namespace TestNative

template <>
struct Iris::EnumMapping<TestNative::ShapeType>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.ShapeType";
};

template <>
struct Iris::StructMapping<TestNative::Vec3>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Vec3";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(TestNative::Vec3, X), IRIS_FIELD(TestNative::Vec3, Y),
                                                      IRIS_FIELD(TestNative::Vec3, Z));
};

template <>
struct Iris::StructMapping<TestNative::BoxShape>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.BoxShape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(TestNative::BoxShape, HalfExtents));
};

template <>
struct Iris::StructMapping<TestNative::SphereShape>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.SphereShape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(TestNative::SphereShape, Radius));
};

// The tag and every kind, in declaration order, each kind at the union's offset.
template <>
struct Iris::StructMapping<TestNative::Shape>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Shape";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(TestNative::Shape, m_type, "_type"),
                                                      IRIS_FIELD_AS(TestNative::Shape, m_box, "_box"),
                                                      IRIS_FIELD_AS(TestNative::Shape, m_sphere, "_sphere"));
};

template <>
struct Iris::StructMapping<TestNative::Collider>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Collider";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(TestNative::Collider, shape, "Shape"));
};

template <>
struct Iris::StructMapping<TestNative::ShapeLayout<TestNative::Misplaced>>
{
    using Layout = TestNative::ShapeLayout<TestNative::Misplaced>;
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.MisplacedShape";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, box, "_box"),
                           IRIS_FIELD_AS(Layout, sphere, "_sphere"));
};

template <>
struct Iris::StructMapping<TestNative::ColliderLayout<TestNative::Misplaced>>
{
    using Layout = TestNative::ColliderLayout<TestNative::Misplaced>;
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.MisplacedCollider";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Layout, shape, "Shape"));
};

template <>
struct Iris::StructMapping<TestNative::ShapeLayout<TestNative::Misnamed>>
{
    using Layout = TestNative::ShapeLayout<TestNative::Misnamed>;
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.MisnamedShape";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD_AS(Layout, type, "_type"), IRIS_FIELD_AS(Layout, box, "_box"),
                           IRIS_FIELD_AS(Layout, sphere, "_sphere"));
};

template <>
struct Iris::StructMapping<TestNative::ColliderLayout<TestNative::Misnamed>>
{
    using Layout = TestNative::ColliderLayout<TestNative::Misnamed>;
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.MisnamedCollider";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Layout, shape, "Shape"));
};

// A component with a part only C++ writes, shaped like a character body and its state: C# mirrors the reading as a
// readonly field, so scripts read it through the same Ref<T> that writes the rate.
namespace TestNative
{
    struct GaugeReading
    {
        float level = 0.0F;
        std::int32_t samples = 0;
    };

    struct Gauge
    {
        float rate = 0.0F;
        GaugeReading reading;
    };
} // namespace TestNative

template <>
struct Iris::StructMapping<TestNative::GaugeReading>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.GaugeReading";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(TestNative::GaugeReading, level, "Level"),
                                                      IRIS_FIELD_AS(TestNative::GaugeReading, samples, "Samples"));
};

template <>
struct Iris::StructMapping<TestNative::Gauge>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Gauge";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(TestNative::Gauge, rate, "Rate"),
                                                      IRIS_FIELD_AS(TestNative::Gauge, reading, "Reading"));
};

// Events the tests register from outside FenrirScripting, and a deliberately wrong mapping of the same C# struct.
namespace TestNative
{
    struct Alert
    {
        std::int32_t Code;
        float Level;
    };

    // Code has the wrong type.
    struct BadAlert
    {
        float Code;
        float Level;
    };
} // namespace TestNative

template <>
struct Iris::StructMapping<TestNative::Alert>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Alert";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestNative::Alert, Code), IRIS_FIELD(TestNative::Alert, Level));
};

template <>
struct Iris::StructMapping<TestNative::BadAlert>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Alert";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestNative::BadAlert, Code), IRIS_FIELD(TestNative::BadAlert, Level));
};

// The native side of a test-only scripting extension: what an engine package would write. Its C# contract is
// Tests/Extension, and Tests/ExtensionScript is a script assembly that uses it.
namespace TestExtension
{
    struct Charge
    {
        std::int32_t Level;
    };

    struct Ping
    {
        std::int32_t Value;
    };

    std::int32_t Add(const Fenrir::CallContext&, std::int32_t left, std::int32_t right)
    {
        return left + right;
    }

    std::uint32_t EchoEntity(const Fenrir::CallContext&, std::uint32_t entity)
    {
        return entity;
    }

    // The entity's Transform height in the active scene, or -1 if it has none.
    float HeightOf(const Fenrir::CallContext& context, std::uint32_t entity)
    {
        const Fenrir::Entity found = context.Entities().GetEntity(entity);
        return found.IsValid() && found.HasComponent<Fenrir::Transform>()
                   ? found.GetComponent<Fenrir::Transform>().pos.y
                   : -1.0F;
    }

    void Lift(const Fenrir::CallContext& context, std::uint32_t entity, float by)
    {
        const Fenrir::Entity found = context.Entities().GetEntity(entity);
        if (found.IsValid() && found.HasComponent<Fenrir::Transform>())
        {
            found.GetComponent<Fenrir::Transform>().pos.y += by;
        }
    }

    // The last text Label received, so a test can compare it with what C# sent.
    std::string labelled;

    std::int32_t Label(const Fenrir::CallContext&, std::string_view text, std::int32_t number)
    {
        labelled = std::string(text);
        return static_cast<std::int32_t>(text.size()) * 10 + number;
    }

    // Declared for the broken contract's Mismatched field, which takes an int: this takes a float.
    std::int32_t Mismatched(const Fenrir::CallContext&, float)
    {
        return 0;
    }
} // namespace TestExtension

template <>
struct Iris::StructMapping<TestExtension::Charge>
{
    static constexpr std::string_view typeName = "Fenrir.TestExtension.Charge";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(TestExtension::Charge, Level));
};

template <>
struct Iris::StructMapping<TestExtension::Ping>
{
    static constexpr std::string_view typeName = "Fenrir.TestExtension.Ping";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(TestExtension::Ping, Value));
};

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using TestNative::BadHealth;
    using TestNative::FloatHealth;
    using TestNative::Health;

    // CTest names the assemblies it built, and the directory of this executable, in the environment.
    std::filesystem::path FromEnvironment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
        {
            std::cerr << "Set " << name << " (CTest does): the scripting tests need the fixture assemblies.\n";
            return {};
        }
        return std::filesystem::absolute(value);
    }

    const std::filesystem::path fixture = FromEnvironment("FENRIR_TEST_FIXTURE");
    // The same fixture built with VARIANT2: the "new build" the reload tests swap in.
    const std::filesystem::path fixtureV2 = FromEnvironment("FENRIR_TEST_FIXTURE_V2");
    // The hand-written assembly whose tables report what the generator never would.
    const std::filesystem::path badReports = FromEnvironment("FENRIR_TEST_BAD_REPORTS");
    // The file the bad-report fixture reads its mode from (see BadReportMode).
    const std::filesystem::path badReportModeFile = FromEnvironment("FENRIR_BAD_REPORT_FILE");
    // The test extension's contract, the script assembly that uses it, and a contract no extension satisfies.
    const std::filesystem::path testExtension = FromEnvironment("FENRIR_TEST_EXTENSION");
    const std::filesystem::path extensionScript = FromEnvironment("FENRIR_TEST_EXTENSION_SCRIPT");
    const std::filesystem::path brokenExtension = FromEnvironment("FENRIR_TEST_BROKEN_EXTENSION");
    const std::filesystem::path executableDirectory = FromEnvironment("FENRIR_TEST_EXECUTABLE_DIR");

    constexpr int Frames = 5;

    constexpr const char* FixtureType = "Fenrir.ScriptFixture.Fixture";

    constexpr const char* AccessType = "Fenrir.ScriptFixture.Access";
    constexpr const char* RegistryType = "Fenrir.ScriptFixture.Registry";

    // Calls a static method of a type in the fixture script assembly through Iris, as the runner will call scripts.
    template <typename Result, typename... Args>
    Result CallIn(const char* typeName, Scripting& scripting, const char* method, Args... arguments)
    {
        auto* component = ScriptingProbe::Component(scripting);
        {
            INFO("There is no loaded script assembly");
            REQUIRE(component != nullptr);
        }
        const auto type = component->FindType(typeName);
        {
            INFO(std::string("The fixture type was not found: ") + type.diagnostic);
            REQUIRE(type.Succeeded());
        }
        const auto handle = type.value->FindMethod<Result(Args...)>(method);
        {
            INFO(std::string("The fixture method ") + method + " was not found: " + handle.diagnostic);
            REQUIRE(handle.Succeeded());
        }
        const auto result = handle.value->Invoke(arguments...);
        {
            INFO(std::string("The fixture method ") + method + " failed: " + result.diagnostic);
            REQUIRE(result.Succeeded());
        }
        if constexpr (!std::is_void_v<Result>)
        {
            return *result.value;
        }
    }

    template <typename Result, typename... Args>
    Result Call(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(FixtureType, scripting, method, arguments...);
    }

    template <typename Result, typename... Args>
    Result Access(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(AccessType, scripting, method, arguments...);
    }

    template <typename Result, typename... Args>
    Result Registry(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(RegistryType, scripting, method, arguments...);
    }

    constexpr std::uint32_t NullEntity = 0xFFFFFFFFU;

    // The native side of the components the fixture declares (same layout as the C# structs).
    struct NativeVec3
    {
        float X, Y, Z;
    };
    struct NativeTransform
    {
        NativeVec3 Position;
        float Scale;
        std::int32_t Flags;
    };
    struct NativeNested
    {
        double Weight;
    };
    struct NativeHidden
    {
        std::int32_t Count;
    };
    struct NativeClash
    {
        std::int32_t Value, IsNull, Fine;
    };
    struct NativeSplit
    {
        std::int32_t A, B;
    };
    struct NativeReserved
    {
        std::int32_t Class, Event;
    };

    std::uint64_t AddressOf(const void* memory)
    {
        return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(memory));
    }

    // A directory holding copies of the files Scripting looks for beside the executable, minus the ones left out.
    struct BridgeCopy
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() / "fenrir-bridge-copy";
        BridgeCopy(const std::filesystem::path& from, bool withBridge, bool withContract)
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
            std::filesystem::create_directories(path);
            if (withBridge)
            {
                for (const char* name : {"Iris.Bridge.dll", "Iris.Bridge.runtimeconfig.json", "Iris.Bridge.deps.json"})
                {
                    std::filesystem::copy_file(from / name, path / name);
                }
            }
            if (withContract)
            {
                std::filesystem::copy_file(from / "Fenrir.Scripting.dll", path / "Fenrir.Scripting.dll");
            }
        }
        ~BridgeCopy()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };

    // Every test starts from the same config, so a field added to ScriptingConfig later is not a warning each time.
    ScriptingConfig ConfigFor(const std::filesystem::path& scriptAssembly)
    {
        ScriptingConfig config;
        config.scriptAssembly = scriptAssembly;
        return config;
    }

    // The name of the extension the test gives its own components and events in, as an app would.
    constexpr const char* TestAppExtension = "test app";

    // The config for `scriptAssembly` with the test's own native components and events, which it gives scripting as an
    // app does: in an extension of its own, named TestAppExtension.
    ScriptingConfig ConfigFor(const std::filesystem::path& scriptAssembly,
                              std::vector<ComponentRegistration> components, std::vector<EventRegistration> events = {})
    {
        ScriptingConfig config = ConfigFor(scriptAssembly);
        ScriptingExtension app;
        app.name = TestAppExtension;
        app.components = std::move(components);
        app.events = std::move(events);
        config.extensions.push_back(std::move(app));
        return config;
    }

    void RunsFramesWithTheFixture()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto messages = headless.Logger().Messages();
        {
            INFO("The runtime was not started: " +
                 (messages.empty() ? std::string("no messages") : messages.back().text));
            REQUIRE(scripting.IsRunning());
        }
        {
            INFO("The fixture assembly did not load");
            REQUIRE(scripting.HasScripts());
        }
        headless.Run(Frames);
        {
            INFO("The app did not run the requested frames");
            REQUIRE(headless.FramesRun() == Frames);
        }
        // Iris's own messages arrive through the engine logger, tagged as Iris's.
        const auto logged = headless.Logger().Messages();
        const bool fromIris = std::any_of(logged.begin(), logged.end(), [](const LoggedMessage& message) {
            return message.level == LogLevel::Info && message.text.rfind("[Iris] ", 0) == 0 &&
                   message.text.find("Fenrir.ScriptFixture") != std::string::npos;
        });
        {
            INFO("Iris's log messages did not reach the engine logger");
            REQUIRE(fromIris);
        }
    }

    void MissingAssemblyIsLoggedAndTheAppRuns()
    {
        HeadlessApp headless;
        const auto missing = fixture.parent_path() / "does-not-exist.dll";
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(missing));
        {
            INFO("A missing assembly started scripting");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("A missing assembly was not logged as an error naming it");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "does-not-exist.dll"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running without scripts");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void InvalidAssemblyIsLoggedAndTheAppRuns()
    {
        // Removed even when a check below throws.
        struct TempFile
        {
            std::filesystem::path path = std::filesystem::temp_directory_path() / "fenrir-not-an-assembly.dll";
            TempFile()
            {
                std::ofstream(path) << "this is not a .NET assembly";
            }
            ~TempFile()
            {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        } temp;
        const auto& invalid = temp.path;

        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(invalid));
        {
            INFO("An invalid assembly loaded, or left the runtime running with nothing to run");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("An invalid assembly was not logged as an error");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "could not be loaded"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running after an invalid assembly");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void MissingBridgeIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noBridge(executableDirectory, /*withBridge=*/false, /*withContract=*/true);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noBridge.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        {
            INFO("A missing bridge started scripting");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("A missing bridge was not logged as an error");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, ".NET could not be started"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running without the bridge");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void MissingContractIsLoggedAndTheAppRuns()
    {
        const BridgeCopy noContract(executableDirectory, /*withBridge=*/true, /*withContract=*/false);
        HeadlessApp headless;
        auto config = ConfigFor(fixture);
        config.bridgeDirectory = noContract.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        {
            INFO("A missing contract started scripting");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("A missing contract was not logged as an error naming it");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "Fenrir.Scripting.dll"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running without the contract");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void ASecondActiveInstallIsRefused()
    {
        HeadlessApp first;
        auto firstScripting = Scripting::Install(first.Get(), ConfigFor(fixture));
        {
            INFO("The first install did not load");
            REQUIRE(firstScripting.HasScripts());
        }
        HeadlessApp second;
        auto secondScripting = Scripting::Install(second.Get(), ConfigFor(fixture));
        {
            INFO("A second active install loaded scripts");
            REQUIRE(!secondScripting.HasScripts());
        }
        {
            INFO("A second install was not explained");
            REQUIRE(second.Logger().Contains(LogLevel::Error, "already active"));
        }
        second.Run(2);
        {
            INFO("The second app did not run");
            REQUIRE(second.FramesRun() == 2);
        }
        // The first is untouched, and a new install works once it is gone.
        {
            INFO("The first install stopped working");
            REQUIRE(Call<int>(firstScripting, "Answer") == 42);
        }
    }

    void ScriptsLogThroughTheEngineLogger()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        Call<void>(scripting, "LogAll");
        const auto& logger = headless.Logger();
        {
            INFO("Log.Info did not reach the engine logger");
            REQUIRE(logger.Contains(LogLevel::Info, "hello from C#"));
        }
        {
            INFO("Log.Warn did not reach the engine logger");
            REQUIRE(logger.Contains(LogLevel::Warn, "careful now"));
        }
        {
            INFO("Log.Error did not reach the engine logger");
            REQUIRE(logger.Contains(LogLevel::Error, "boom"));
        }
        // UTF-8 survives, and braces are text rather than a format string.
        {
            INFO("Non-ASCII text or braces were mangled");
            REQUIRE(logger.Contains(LogLevel::Info, "pi \xCF\x80 {0} {} {name}"));
        }
        // There is no length limit, and null is an empty message.
        {
            INFO("A long message was truncated");
            REQUIRE(logger.Contains(LogLevel::Info, std::string(2000, 'x')));
        }
        const auto messages = logger.Messages();
        {
            INFO("A null message was not logged as an empty one");
            REQUIRE(std::any_of(messages.begin(), messages.end(), [](const LoggedMessage& message) {
                return message.level == LogLevel::Info && message.text.empty();
            }));
        }
    }

    void ScriptsReadTheEngineClock()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        bool sawDelta = false;
        bool matched = true;
        headless.Get().AddSequentialSystems(SchedulePriority::Update, {[&](App& app) {
                                                const float delta = Call<float>(scripting, "DeltaTime");
                                                const float tick = Call<float>(scripting, "TickRate");
                                                matched = matched &&
                                                          delta == static_cast<float>(app.GetTime().deltaTime) &&
                                                          tick == static_cast<float>(app.GetTime().tickRate);
                                                sawDelta = sawDelta || delta > 0.0F;
                                            }});
        headless.Run(Frames);
        {
            INFO("Time did not match the engine's clock");
            REQUIRE(matched);
        }
        {
            INFO("Time.DeltaTime was never positive");
            REQUIRE(sawDelta);
        }
    }

    void EntityValidityFollowsTheActiveScene()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto isValid = [&](std::uint32_t value) { return Call<bool>(scripting, "IsValid", value); };

        const auto first = entities.CreateEntity().GetId();
        {
            INFO("A live entity was not valid");
            REQUIRE(isValid(first));
        }
        entities.DestroyEntity(first);
        {
            INFO("A destroyed entity was still valid");
            REQUIRE(!isValid(first));
        }

        // The slot is reused with a new version, so the old value stays invalid and the new one is valid.
        const auto second = entities.CreateEntity().GetId();
        {
            INFO("The test did not reuse the entity's slot with a new version");
            REQUIRE(((second & 0xFFFFFU) == (first & 0xFFFFFU) && second != first));
        }
        {
            INFO("The entity that reused the slot was not valid");
            REQUIRE(isValid(second));
        }
        {
            INFO("A stale entity became valid when its slot was reused");
            REQUIRE(!isValid(first));
        }
        {
            INFO("The null entity was valid");
            REQUIRE(!isValid(0xFFFFFFFFU));
        }
    }

    void RefWritesThroughGeneratedAccessorsIntoNativeMemory()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }

        // t.Position.X += 1 on a nested struct field, a plain assignment, a compound multiply and a flag: all land in
        // memory this test owns, with no ref at the call site.
        NativeTransform transform{{1, 2, 3}, 4, 1};
        Access<void>(scripting, "Nudge", AddressOf(&transform));
        {
            INFO("Writes through the generated accessors did not reach native memory");
            REQUIRE((transform.Position.X == 2 && transform.Position.Y == 5 && transform.Position.Z == 3 &&
                     transform.Scale == 8 && transform.Flags == 5));
        }
        {
            INFO("Reads through the generated accessors did not see native memory");
            REQUIRE(Access<float>(scripting, "ReadSum", AddressOf(&transform)) == 18.0F);
        }

        // Writes from C++ are seen too: it is the same memory.
        transform.Position.Z = 10;
        {
            INFO("A native write was not seen by the script");
            REQUIRE(Access<float>(scripting, "ReadSum", AddressOf(&transform)) == 25.0F);
        }

        NativeNested nested{1.0};
        {
            INFO("A nested component type got no accessors");
            REQUIRE((Access<double>(scripting, "NudgeNested", AddressOf(&nested)) == 1.5 && nested.Weight == 1.5));
        }
        NativeHidden hidden{4};
        {
            INFO("An internal component got no accessors");
            REQUIRE((Access<std::int32_t>(scripting, "NudgeHidden", AddressOf(&hidden)) == 7 && hidden.Count == 7));
        }
        NativeClash clash{0, 0, 0};
        {
            INFO("Fields named like Ref<T> members were not reachable through Value");
            REQUIRE((Access<std::int32_t>(scripting, "NudgeClash", AddressOf(&clash)) == 24 && clash.Fine == 7 &&
                     clash.Value == 8 && clash.IsNull == 9));
        }
        NativeSplit split{100, 200};
        {
            INFO("A partial component did not get accessors for all of its fields");
            REQUIRE((Access<std::int32_t>(scripting, "NudgeSplit", AddressOf(&split)) == 303 && split.A == 101 &&
                     split.B == 202));
        }
        NativeReserved reserved{10, 20};
        {
            INFO("Keyword-named fields were not exposed");
            REQUIRE((Access<std::int32_t>(scripting, "NudgeReserved", AddressOf(&reserved)) == 33 &&
                     reserved.Class == 11 && reserved.Event == 22));
        }
    }

    void ANullRefSaysSoInsteadOfCrashing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("A null Ref was not reported as null");
            REQUIRE(Access<bool>(scripting, "NullRefIsNull"));
        }
        const auto message = Access<std::optional<std::string>>(scripting, "NullRefMessage");
        {
            INFO("Reading a null Ref did not throw a clear exception");
            REQUIRE((message.has_value() && message->find("Pose") != std::string::npos));
        }
    }

    // ---- the native component registry

    void ATransformIsTheSameMemoryInCppAndInCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();

        auto entity = entities.CreateEntity();
        entity.AddComponent<Transform>(Math::Vec3(1, 2, 3), Math::Quat(1, 0, 0, 0), Math::Vec3(1, 1, 1));
        const auto id = entity.GetId();

        // C++ wrote it; C# reads the same values (Rotation.W is the first float in glm's layout).
        {
            INFO("C# did not read the position C++ wrote (got " +
                 std::to_string(Registry<float>(scripting, "ReadPositionX", id)) + ")");
            REQUIRE(Registry<float>(scripting, "ReadPositionX", id) == 1.0F);
        }
        {
            INFO("C# did not read the rotation C++ wrote");
            REQUIRE(Registry<float>(scripting, "ReadRotationW", id) == 1.0F);
        }
        {
            INFO("C# did not read the scale C++ wrote");
            REQUIRE(Registry<float>(scripting, "ReadScaleZ", id) == 1.0F);
        }

        // C# writes through its Ref<Transform>; C++ sees it through GetComponent.
        Registry<void>(scripting, "Move", id);
        const auto& transform = entity.GetComponent<Transform>();
        {
            INFO("C# writes to the position were not seen");
            REQUIRE((transform.pos.x == 2 && transform.pos.y == 2 && transform.pos.z == 3));
        }
        {
            INFO("C# writes to the rotation were not seen");
            REQUIRE((transform.rot.w == 0.5F && transform.rot.x == 0 && transform.rot.y == 0 && transform.rot.z == 0));
        }
        {
            INFO("C# writes to the scale were not seen");
            REQUIRE((transform.scale.x == 3 && transform.scale.y == 3 && transform.scale.z == 3));
        }

        // And the other way round, in the same memory.
        entity.GetComponent<Transform>().pos.x = 10;
        {
            INFO("C# did not see a later C++ write");
            REQUIRE(Registry<float>(scripting, "ReadPositionX", id) == 10.0F);
        }

        // Has: live state, for an entity with and without the component (every engine entity starts with a Transform).
        auto bare = entities.CreateEntity();
        bare.RemoveComponent<Transform>();
        {
            INFO("Has<Transform> was wrong");
            REQUIRE((Registry<bool>(scripting, "HasTransform", id) &&
                     !Registry<bool>(scripting, "HasTransform", bare.GetId())));
        }
        entities.DestroyEntity(id);
        {
            INFO("Has<Transform> was true for a destroyed entity");
            REQUIRE(!Registry<bool>(scripting, "HasTransform", id));
        }
    }

    void ANameRoundTripsBothWays()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto entity = entities.CreateEntity();
        const auto id = entity.GetId(); // Every engine entity starts with a Name.

        // C++ to C#, with a multi-byte character.
        entity.GetComponent<Name>().Set("h\xC3\xA9llo");
        const auto read = Registry<std::optional<std::string>>(scripting, "ReadName", id);
        {
            INFO("C# did not read the name C++ wrote");
            REQUIRE((read.has_value() && *read == "h\xC3\xA9llo"));
        }

        // C# to C++.
        Registry<void>(scripting, "WriteName", id, std::string_view("from C# \xCF\x80"));
        {
            INFO("C++ did not read the name C# wrote");
            REQUIRE(std::string(entity.GetComponent<Name>().Get()) == "from C# \xCF\x80");
        }
        {
            INFO("C# did not read back its own name");
            REQUIRE(Registry<std::optional<std::string>>(scripting, "ReadName", id).value() == "from C# \xCF\x80");
        }

        // A name longer than the buffer is cut between characters, and stays terminated.
        std::string tooLong;
        for (int index = 0; index < 200; ++index)
        {
            tooLong += "\xC3\xA9"; // 400 bytes
        }
        Registry<void>(scripting, "WriteName", id, std::string_view(tooLong));
        const std::string stored = entity.GetComponent<Name>().Get();
        {
            INFO("A long name was not cut on a character boundary (it was " + std::to_string(stored.size()) +
                 " bytes)");
            REQUIRE((stored.size() == 254 && stored.size() % 2 == 0 && stored.substr(0, 4) == "\xC3\xA9\xC3\xA9"));
        }

        // An empty name clears the buffer.
        Registry<void>(scripting, "WriteName", id, std::string_view(""));
        {
            INFO("An empty name did not clear the buffer");
            REQUIRE(entity.GetComponent<Name>().IsEmpty());
        }
    }

    void TheHierarchyIsReadableButNotWritable()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto parent = entities.CreateEntity();
        auto first = entities.CreateEntity();
        auto second = entities.CreateEntity();
        parent.AddChild(first);
        parent.AddChild(second);

        // C# reads exactly the links C++ made (the layout of the stand-in is the layout of the real thing).
        {
            INFO("The parent link was wrong");
            REQUIRE(Registry<std::uint32_t>(scripting, "ParentOf", first.GetId()) == parent.GetId());
        }
        {
            INFO("The second parent link was wrong");
            REQUIRE(Registry<std::uint32_t>(scripting, "ParentOf", second.GetId()) == parent.GetId());
        }
        {
            INFO("The first child link was wrong");
            REQUIRE(Registry<std::uint32_t>(scripting, "FirstChildOf", parent.GetId()) == first.GetId());
        }
        {
            INFO("The sibling link was wrong");
            REQUIRE(Registry<std::uint32_t>(scripting, "NextSiblingOf", first.GetId()) == second.GetId());
        }
        {
            INFO("A missing parent was not a null link");
            REQUIRE(Registry<std::uint32_t>(scripting, "ParentOf", parent.GetId()) == NullEntity);
        }
        {
            INFO("A missing sibling was not null");
            REQUIRE(Registry<std::uint32_t>(scripting, "NextSiblingOf", second.GetId()) == NullEntity);
        }

        // It cannot be written from C#: no Ref<Relationship>, and assignments through ReadOnlyRef do not compile (see
        // the RefCompile-Relationship* tests). Native code can still change it, and C# sees that.
        parent.RemoveChild(first);
        {
            INFO("C# did not see the hierarchy change");
            REQUIRE(Registry<std::uint32_t>(scripting, "FirstChildOf", parent.GetId()) == second.GetId());
        }
    }

    void AWorldTransformIsReadAndSetFromCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto parent = entities.CreateEntity();
        auto child = entities.CreateEntity();
        parent.AddChild(child);
        // The parent sits at (10, 0, 0), turned a quarter about +Y (+X to -Z) and doubled; the child is 1 along its +X.
        parent.GetComponent<Transform>() =
            Transform(Math::Vec3(10.0F, 0.0F, 0.0F),
                      Math::AngleAxis(Math::DegToRad(90.0F), Math::Vec3(0.0F, 1.0F, 0.0F)), Math::Vec3(2.0F));
        child.GetComponent<Transform>().pos = Math::Vec3(1.0F, 0.0F, 0.0F);
        const auto near = [](float left, float right) { return std::abs(left - right) < 1e-4F; };

        const float worldZ = Registry<float>(scripting, "WorldPositionZ", child.GetId());
        {
            INFO("C# read world Z " + std::to_string(worldZ) + ", not -2");
            REQUIRE(near(worldZ, -2.0F));
        }

        // C# sets the child's world position and reads it back in the same call, before anything returns to C++.
        const float readBack = Registry<float>(scripting, "PlaceAndReadWorldY", child.GetId(), 4.0F, 6.0F, -8.0F);
        {
            INFO("C# read back world Y " + std::to_string(readBack) + " after setting 6");
            REQUIRE(near(readBack, 6.0F));
        }
        const Transform world = GetWorldTransform(child);
        {
            INFO("C++ did not see the world position C# set");
            REQUIRE((near(world.pos.x, 4.0F) && near(world.pos.y, 6.0F) && near(world.pos.z, -8.0F)));
        }
        {
            INFO("The child's local Transform was not set through its parent (expected (4, 3, -3))");
            const Math::Vec3 local = child.GetComponent<Transform>().pos;
            REQUIRE((near(local.x, 4.0F) && near(local.y, 3.0F) && near(local.z, -3.0F)));
        }
        {
            INFO("A dead entity's world transform was not refused");
            const auto message = Registry<std::optional<std::string>>(scripting, "DeadWorldTransform", NullEntity);
            REQUIRE(message.has_value());
        }
    }

    void ACustomComponentRegisteredByTheTestWorksWithNoEngineChanges()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<Health>("Test.Health")}));
        {
            INFO("The fixture did not load with a matching component");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto entity = entities.CreateEntity();
        const auto id = entity.GetId();

        // The engine knows nothing of a component nobody registered.
        const auto unregistered = Registry<std::optional<std::string>>(scripting, "NotRegistered", id);
        {
            INFO("A component nobody registered was not explained");
            REQUIRE((unregistered.has_value() && unregistered->find("Test.Nothing") != std::string::npos));
        }

        // A registered one is found, and the entity simply does not have it yet.
        const auto lookup = Registry<std::optional<std::string>>(scripting, "HealthLookup", id);
        {
            INFO("A registered component the entity lacks was not explained: " + lookup.value_or("<no exception>"));
            REQUIRE((lookup.has_value() && lookup->find("has no Health") != std::string::npos));
        }
        {
            INFO("Has<Health> was true before it was added");
            REQUIRE(!Registry<bool>(scripting, "HasHealth", id));
        }
        entity.AddComponent<Health>();
        entity.GetComponent<Health>() = Health{20, 30};
        {
            INFO("Has<Health> was false after it was added");
            REQUIRE(Registry<bool>(scripting, "HasHealth", id));
        }
        {
            INFO("C# did not read the custom component");
            REQUIRE(Registry<std::int32_t>(scripting, "HealthSum", id) == 55);
        }
        {
            INFO("C# writes to the custom component were not seen");
            REQUIRE(entity.GetComponent<Health>().Current == 25);
        }
    }

    // Installs the fixture with `config` and checks that it failed, explaining `diagnostic` (one message, naming the
    // test's own extension).
    void ExpectInstallRefused(ScriptingConfig config, const std::string& diagnostic, const char* what)
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), std::move(config));
        {
            INFO(std::string(what) + " was accepted");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO(std::string(what) + " was not explained as: " + diagnostic);
            REQUIRE(headless.Logger().Contains(LogLevel::Error, diagnostic));
        }
    }

    void ADuplicateComponentNameFailsInstallAndNamesTheExtension()
    {
        ExpectInstallRefused(
            ConfigFor(fixture, {MakeComponent<Health>("Test.Health"), MakeComponent<Health>("Test.Health")}),
            "a component in extension '" + std::string(TestAppExtension) + "' has an empty or duplicate name",
            "A duplicate component name");
    }

    void AnEmptyComponentNameFailsInstallAndNamesTheExtension()
    {
        ExpectInstallRefused(ConfigFor(fixture, {MakeComponent<Health>("")}),
                             "a component in extension '" + std::string(TestAppExtension) +
                                 "' has an empty or duplicate name",
                             "An empty component name");
    }

    void ADuplicateEventNameFailsInstallAndNamesTheExtension()
    {
        ExpectInstallRefused(
            ConfigFor(fixture, {},
                      {MakeEvent<TestNative::Alert>("Test.Alert"), MakeEvent<TestNative::Alert>("Test.Alert")}),
            "an event in extension '" + std::string(TestAppExtension) + "' has an empty or duplicate name",
            "A duplicate event name");
    }

    void LookupFailuresSayWhatWentWrong()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto bare = entities.CreateEntity();
        bare.RemoveComponent<Transform>();
        auto gone = entities.CreateEntity();
        const auto goneId = gone.GetId();
        entities.DestroyEntity(goneId);

        const auto missing = Registry<std::optional<std::string>>(scripting, "MissingComponent", bare.GetId());
        {
            INFO("A missing component was not explained (got " + missing.value_or("<no exception>") + ")");
            REQUIRE((missing.has_value() && missing->find("has no Transform") != std::string::npos));
        }
        const auto dead = Registry<std::optional<std::string>>(scripting, "MissingComponent", goneId);
        {
            INFO("A dead entity was not explained");
            REQUIRE((dead.has_value() && dead->find("not alive") != std::string::npos));
        }
    }

    void AMismatchedComponentFailsTheLoadAndNamesTheField()
    {
        HeadlessApp headless;
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<BadHealth>("Test.BadHealth")}));

        // All or nothing: the assembly is not left loaded with a component that does not match.
        {
            INFO("A mismatched component left scripts loaded");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("The mismatch did not name the component and the field");
            REQUIRE((headless.Logger().Contains(LogLevel::Error, "Test.BadHealth") &&
                     headless.Logger().Contains(LogLevel::Error, "'Current'") &&
                     headless.Logger().Contains(LogLevel::Error, "'Max'")));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running after a failed load");
            REQUIRE(headless.FramesRun() == Frames);
        }

        // The engine's own components still load once the bad one is gone.
        HeadlessApp again;
        auto fine = Scripting::Install(again.Get(), ConfigFor(fixture));
        {
            INFO("A later install without the bad component did not load");
            REQUIRE(fine.HasScripts());
        }
    }

    void AScriptCannotClaimANativeComponentWithAStructOfItsOwn()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity();

        // The fixture declares a writable component that names the hierarchy ("Fenrir.Relationship"). The engine only
        // hands out a native component to the C# type it validated, so the claim is refused.
        const auto refused = Registry<std::optional<std::string>>(scripting, "FakeHierarchy", entity.GetId());
        {
            INFO("A script's own struct got a native component: " + refused.value_or("<no exception>"));
            REQUIRE((refused.has_value() && refused->find("different C# type") != std::string::npos));
        }
    }

    void AWrongFieldTypeIsCaughtToo()
    {
        HeadlessApp headless;
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<FloatHealth>("Test.FloatHealth")}));
        {
            INFO("A field of the wrong type left the script assembly loaded");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("A field of the wrong type was not named");
            REQUIRE((headless.Logger().Contains(LogLevel::Error, "Test.FloatHealth") &&
                     headless.Logger().Contains(LogLevel::Error, "'Current'")));
        }
    }

    // ---- a native component holding a tagged union

    // Every error logged, for a failure message.
    std::string Errors(const HeadlessApp& headless)
    {
        std::string errors;
        for (const auto& message : headless.Logger().Messages())
        {
            if (message.level == LogLevel::Error)
            {
                errors += message.text + "\n";
            }
        }
        return errors;
    }

    void ANativeTaggedUnionValidatesAgainstItsCSharpMirror()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(),
                                            ConfigFor(fixture, {MakeComponent<TestNative::Collider>("Test.Collider")}));
        {
            INFO("The tagged union was refused: " + Errors(headless));
            REQUIRE((scripting.HasScripts() && scripting.FindComponent("Test.Collider") != nullptr));
        }
    }

    constexpr const char* ShapeProbeType = "Fenrir.ScriptFixture.ShapeProbe";

    // A C# system finds a sphere (set here), reads its radius, makes it a box by assigning the kind by value, and edits
    // the box through its view; reading the box view of the sphere first throws.
    void ATaggedUnionRoundTripsThroughACSharpSystem()
    {
        HeadlessApp headless;
        // Scripting is released in the Exit phase, so what C# saw is read by an Exit system registered first.
        Scripting* installed = nullptr;
        float seenRadius = -1.0F;
        headless.Get().AddSequentialSystems(SchedulePriority::Exit, {[&](App&) {
                                                seenRadius = CallIn<float>(ShapeProbeType, *installed, "SeenRadius");
                                            }});
        auto scripting = Scripting::Install(headless.Get(),
                                            ConfigFor(fixture, {MakeComponent<TestNative::Collider>("Test.Collider")}));
        installed = &scripting;
        {
            INFO("The tagged union was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity();
        entity.AddComponent<TestNative::Collider>().shape = TestNative::SphereShape{0.25F};
        CallIn<void>(ShapeProbeType, scripting, "Enable");

        headless.Run(1);

        {
            INFO("C# did not read the sphere C++ wrote");
            REQUIRE(seenRadius == 0.25F);
        }
        auto& shape = entity.GetComponent<TestNative::Collider>().shape;
        {
            INFO("The kind C# assigned did not reach C++");
            REQUIRE(shape.Type() == TestNative::ShapeType::Box);
        }
        const auto halfExtents = shape.As<TestNative::BoxShape>().HalfExtents;
        {
            INFO("The box C# assigned and edited through its view did not reach C++");
            REQUIRE((halfExtents.X == 1.0F && halfExtents.Y == 2.0F && halfExtents.Z == 1.0F));
        }
        {
            INFO("The system failed: " + Errors(headless));
            REQUIRE(Errors(headless).empty());
        }
    }

    void ReadingATaggedUnionViewForTheWrongKindThrows()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        // The C# method reads a sphere's box view and returns the InvalidOperationException's message, or null.
        const auto message = CallIn<std::optional<std::string>>(ShapeProbeType, scripting, "WrongKindMessage");
        {
            INFO("Reading a sphere as a box did not throw InvalidOperationException");
            REQUIRE(message.has_value());
        }
        {
            INFO("The exception did not say which kind the shape is: " + *message);
            REQUIRE((message->find("Sphere") != std::string::npos && message->find("Box") != std::string::npos));
        }
    }

    // The [TaggedUnion] generator's message calls the union by its noun, or by its type's name when it has none.
    void AGeneratedTaggedUnionsMessageNamesItsNounOrItsType()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto withNoun = CallIn<std::optional<std::string>>(ShapeProbeType, scripting, "WrongKindMessage");
        {
            INFO("The message of a union with Noun = \"shape\" was: " + withNoun.value_or("(no exception)"));
            REQUIRE(withNoun == std::optional<std::string>{"The shape is a Sphere, not a Box."});
        }
        const auto withoutNoun =
            CallIn<std::optional<std::string>>(ShapeProbeType, scripting, "WrongKindMessageWithoutNoun");
        {
            INFO("The message of the union Solid, with no noun, was: " + withoutNoun.value_or("(no exception)"));
            REQUIRE(withoutNoun == std::optional<std::string>{"The Solid is a Sphere, not a Box."});
        }
    }

    // Installs the fixture with a component whose C# mirror is wrong, and checks the load fails naming the component
    // and the field (and, for an offset, where each side puts it).
    void ExpectTheMirrorRefused(const ComponentRegistration& registration, const std::string& field,
                                const std::string& detail)
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {registration}));
        {
            INFO("A wrong tagged-union mirror left scripts loaded");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The refusal did not name the component, the field and what is wrong: " + Errors(headless));
            REQUIRE((headless.Logger().Contains(LogLevel::Error, registration.name) &&
                     headless.Logger().Contains(LogLevel::Error, field) &&
                     headless.Logger().Contains(LogLevel::Error, detail)));
        }
    }

    void ATaggedUnionMirrorWithAWrongOffsetOrNameIsRefusedAtLoad()
    {
        // C#'s _sphere is at offset 8; the union is at 4.
        ExpectTheMirrorRefused(MakeComponent<TestNative::Collider, TestNative::ColliderLayout<TestNative::Misplaced>>(
                                   "Test.MisplacedCollider"),
                               "'_sphere'", "offset 8 but the native field is at 4");
        // C# calls the sphere _ball.
        ExpectTheMirrorRefused(MakeComponent<TestNative::Collider, TestNative::ColliderLayout<TestNative::Misnamed>>(
                                   "Test.MisnamedCollider"),
                               "'_ball'", "'_sphere'");
    }

    // ---- a native component with a read-only part

    constexpr const char* GaugeProbeType = "Fenrir.ScriptFixture.GaugeProbe";

    // A C# system reads a reading C++ wrote through the writable Ref<Gauge> its query hands out, from the component's
    // own storage, and writes the rate from it.
    void AReadOnlyFieldIsReadThroughRefInPlace()
    {
        HeadlessApp headless;
        // Scripting is released in the Exit phase, so what C# saw is read by an Exit system registered first.
        Scripting* installed = nullptr;
        float seenLevel = -1.0F;
        std::int32_t seenSamples = -1;
        std::uint64_t seenAddress = 0;
        headless.Get().AddSequentialSystems(SchedulePriority::Exit, {[&](App&) {
                                                seenLevel = CallIn<float>(GaugeProbeType, *installed, "SeenLevel");
                                                seenSamples =
                                                    CallIn<std::int32_t>(GaugeProbeType, *installed, "SeenSamples");
                                                seenAddress =
                                                    CallIn<std::uint64_t>(GaugeProbeType, *installed, "SeenAddress");
                                            }});
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<TestNative::Gauge>("Test.Gauge")}));
        installed = &scripting;
        {
            INFO("The gauge was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity();
        entity.AddComponent<TestNative::Gauge>().reading = {0.5F, 3};
        CallIn<void>(GaugeProbeType, scripting, "Enable");

        headless.Run(1);

        auto& gauge = entity.GetComponent<TestNative::Gauge>();
        {
            INFO("C# did not read the reading C++ wrote");
            REQUIRE((seenLevel == 0.5F && seenSamples == 3));
        }
        {
            INFO("C# read a copy of the reading, not the component's storage");
            REQUIRE(seenAddress == AddressOf(&gauge.reading));
        }
        {
            INFO("The rate C# wrote through the same Ref did not reach C++");
            REQUIRE(gauge.rate == 1.5F);
        }

        {
            INFO("The system failed: " + Errors(headless));
            REQUIRE(Errors(headless).empty());
        }
    }

    // ---- entity links written from C#

    constexpr const char* LinksType = "Fenrir.ScriptFixture.Links";

    template <typename Result, typename... Args>
    Result Links(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(LinksType, scripting, method, arguments...);
    }

    ComponentRegistration TetherComponent()
    {
        return MakeComponent<TestNative::Tether, TestNative::TetherView>("Test.Tether");
    }

    void AnEntityFieldWrittenFromCSharpReadsCorrectlyInCpp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {TetherComponent()}));
        {
            INFO("The component with an Entity field was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto holder = entities.CreateEntity();
        auto target = entities.CreateEntity();
        holder.AddComponent<TestNative::Tether>();

        Links<void>(scripting, "LinkTo", holder.GetId(), target.GetId());

        // Equal means the same entity value in the same entity list: C# filled the owner with the active scene's.
        const Entity written = holder.GetComponent<TestNative::Tether>().target;
        {
            INFO("The Entity C# wrote is not the target in the active scene");
            REQUIRE(written == target);
        }
        {
            INFO("C++ could not use the Entity C# wrote");
            REQUIRE((written.IsValid() && written.HasComponent<Transform>()));
        }
    }

    void ALinkToAnEntityCreatedInTheSameSystemReadsBackAfterPlayback()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {TetherComponent()}));
        {
            INFO("The component with an Entity field was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();

        const auto holderId = Links<std::uint32_t>(scripting, "CreateLinkedPair");
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Playback did not create both entities and add the Tether");
            REQUIRE((summary.applied == 3 && summary.skipped == 0));
        }

        const auto targetId = Links<std::uint32_t>(scripting, "TargetOf", holderId);
        {
            INFO("C# did not read the link back after playback");
            REQUIRE((targetId != NullEntity && targetId != holderId));
        }
        const auto& tether = entities.GetEntity(holderId).GetComponent<TestNative::Tether>();
        {
            INFO("C++ did not read the link to the created entity in the active scene");
            REQUIRE((tether.target == entities.GetEntity(targetId) && tether.target.HasComponent<Transform>()));
        }
        {
            INFO("The rest of the component did not arrive");
            REQUIRE(tether.length == 3);
        }
    }

    void AZeroedLinkNamesEntityZeroWithNoSceneAndReadsSafely()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {TetherComponent()}));
        {
            INFO("The component with an Entity field was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto holder = entities.CreateEntity();
        {
            INFO("The first entity of a new scene is not entity 0, so this test cannot tell entity 0 apart");
            REQUIRE(holder.GetId() == 0);
        }
        holder.AddComponent<TestNative::Tether>();

        Links<void>(scripting, "WriteZeroed", holder.GetId());

        {
            INFO("C# did not read a zeroed link as naming entity 0");
            REQUIRE(Links<std::uint32_t>(scripting, "TargetOf", holder.GetId()) == 0);
        }
        // Entity 0 is alive in the active scene, but the zeroed link has no scene, so it is neither it nor valid.
        const Entity zeroed = holder.GetComponent<TestNative::Tether>().target;
        {
            INFO("A zeroed link read in C++ as entity 0 of the active scene");
            REQUIRE((zeroed.GetId() == 0 && zeroed != holder));
        }
        {
            INFO("A zeroed link read in C++ as valid");
            REQUIRE(!zeroed.IsValid());
        }
    }

    void AnEntityLinksOwnerIsNotReadableFromCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        // The spec's whole readable surface: whether it is empty, the entity it names, and the empty link.
        const auto members = Links<std::optional<std::string>>(scripting, "ReadableMembers");
        {
            INFO("EntityLink exposes more than IsNull, Target and None to scripts: " + members.value_or("<null>"));
            REQUIRE(members == "IsNull:Boolean,None:EntityLink,Target:Entity");
        }
    }

    void EntityLinkNoneIsTheEmptyLink()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture, {TetherComponent()}));
        {
            INFO("The component with an Entity field was refused: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("EntityLink.None was not null in C#");
            REQUIRE(Links<bool>(scripting, "NoneIsNull"));
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto holder = entities.CreateEntity();
        auto target = entities.CreateEntity();
        holder.AddComponent<TestNative::Tether>().target = target;

        Links<void>(scripting, "Unlink", holder.GetId());

        // The empty link is a default Entity: the null value and no entity list.
        {
            INFO("EntityLink.None did not read in C++ as an empty Entity");
            REQUIRE(holder.GetComponent<TestNative::Tether>().target == Entity());
        }
        {
            INFO("C# did not read the empty link back as null");
            REQUIRE(Links<std::uint32_t>(scripting, "TargetOf", holder.GetId()) == NullEntity);
        }
    }

    // ---- components defined in C#

    constexpr const char* VelocityName = "Fenrir.ScriptFixture.Velocity";
    constexpr const char* CounterName = "Fenrir.ScriptFixture.Counter";
    constexpr const char* AlignedName = "Fenrir.ScriptFixture.Aligned";
    constexpr const char* TaggedName = "Fenrir.ScriptFixture.Tagged";
    constexpr const char* ScriptedType = "Fenrir.ScriptFixture.Scripted";

    template <typename Result, typename... Args>
    Result Scripted(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ScriptedType, scripting, method, arguments...);
    }

    std::size_t PoolCount(EntityList& entities)
    {
        return entities.ComponentStorageCount();
    }

    void AScriptAssemblyReportsTheComponentsItDefines()
    {
        HeadlessApp headless;
        auto& scene = headless.Get().GetActiveScene().GetEntityList();
        const std::size_t poolsBefore = PoolCount(scene);
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }

        // Registering components creates no storage, and neither does asking about them: a pool appears when a
        // component is first added in a scene. (So "a failed load leaves no pools" is a real property, not an accident
        // of timing.)
        {
            INFO("Loading a script assembly created storage");
            REQUIRE(PoolCount(scene) == poolsBefore);
        }
        const auto* velocity = scripting.FindComponent(VelocityName);
        const auto probe = scene.CreateEntity().GetId();
        const std::size_t poolsWithEntity = PoolCount(scene);
        {
            INFO("Reading or removing an absent script component misbehaved");
            REQUIRE((!scene.Has(velocity->type, probe) && scene.Get(velocity->type, probe) == nullptr &&
                     scene.Remove(velocity->type, probe)));
        }
        {
            INFO("Asking about a script component created storage");
            REQUIRE(PoolCount(scene) == poolsWithEntity);
        }
        {
            INFO("Adding a script component did not create its pool");
            REQUIRE((scene.Add(velocity->type, probe) && PoolCount(scene) == poolsWithEntity + 1));
        }

        const auto expect = [&](const char* name, std::size_t size, std::size_t alignment) {
            const auto* registration = scripting.FindComponent(name);
            {
                INFO(std::string("The script component ") + name + " was not registered");
                REQUIRE(registration != nullptr);
            }
            {
                INFO(std::string(name) + " was not described as a script component");
                REQUIRE((registration->scriptDefined && !registration->readOnly && registration->typeName == name));
            }
            {
                INFO(std::string(name) +
                     " has the wrong size or alignment: " + std::to_string(registration->type.Size()) + " / " +
                     std::to_string(registration->type.Alignment()));
                REQUIRE((registration->type.Size() == size && registration->type.Alignment() == alignment));
            }
        };
        expect(VelocityName, 12, 4);
        expect(CounterName, 16, 8);
        expect(AlignedName, 32, 16);
        expect(TaggedName, 1, 1);

        // Native components are not script-defined, and a [NativeComponent] struct in the script is not reported as
        // one.
        {
            INFO("A native component was described as script-defined");
            REQUIRE(!scripting.FindComponent("Fenrir.Transform")->scriptDefined);
        }
        {
            INFO("A [NativeComponent] struct was reported as a script-defined component");
            REQUIRE(scripting.FindComponent("Fenrir.ScriptFixture.Health") == nullptr);
        }
    }

    void AScriptComponentIsSharedWithCSharpAndRemovedWithItsEntity()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);
        {
            INFO("Velocity was not registered");
            REQUIRE(velocity != nullptr);
        }

        auto first = entities.CreateEntity();
        auto second = entities.CreateEntity();
        auto third = entities.CreateEntity();
        {
            INFO("A component was there before it was added");
            REQUIRE(!entities.Has(velocity->type, first.GetId()));
        }
        for (auto* entity : {&first, &second, &third})
        {
            {
                INFO("A component was not added");
                REQUIRE(entities.Add(velocity->type, entity->GetId()));
            }
        }
        {
            INFO("Adding twice failed");
            REQUIRE(entities.Add(velocity->type, first.GetId()));
        } // Harmless, and keeps the one it has.

        // A new component is all zero bytes, which is a C# default.
        {
            INFO("A new script component was not zeroed");
            REQUIRE(Scripted<float>(scripting, "SpeedX", first.GetId()) == 0.0F);
        }

        // C# writes to it; native code sees the same bytes.
        Scripted<void>(scripting, "Accelerate", first.GetId());
        Scripted<void>(scripting, "Accelerate", first.GetId());
        Scripted<void>(scripting, "Accelerate", third.GetId());
        const auto* values = static_cast<const float*>(entities.Get(velocity->type, first.GetId()));
        {
            INFO("C# writes were not in the engine's storage");
            REQUIRE((values != nullptr && values[0] == 2 && values[1] == 4 && values[2] == 6));
        }
        {
            INFO("A second entity's component was wrong");
            REQUIRE(Scripted<float>(scripting, "SpeedX", third.GetId()) == 1.0F);
        }
        {
            INFO("A write reached the wrong entity's component");
            REQUIRE(Scripted<float>(scripting, "SpeedX", second.GetId()) == 0.0F);
        }

        // Native code writes it too, and C# reads that.
        static_cast<float*>(entities.Get(velocity->type, second.GetId()))[0] = 42;
        {
            INFO("C# did not read a native write");
            REQUIRE(Scripted<float>(scripting, "SpeedX", second.GetId()) == 42.0F);
        }

        // The pool holds one element per entity that has the component.
        {
            INFO("The pool did not hold three elements");
            REQUIRE(entities.Count(velocity->type) == 3);
        }

        // Destroying an entity removes its element, and the others keep their values.
        entities.DestroyEntity(second.GetId());
        {
            INFO("Destroying an entity left its component in the pool");
            REQUIRE(entities.Count(velocity->type) == 2);
        }
        {
            INFO("A destroyed entity still had the component");
            REQUIRE(!entities.Has(velocity->type, second.GetId()));
        }
        {
            INFO("Removing an element disturbed the others");
            REQUIRE((Scripted<float>(scripting, "SpeedX", first.GetId()) == 2.0F &&
                     Scripted<float>(scripting, "SpeedX", third.GetId()) == 1.0F));
        }

        // The slot is reused by a new entity, which does not inherit the old component.
        const auto reused = entities.CreateEntity().GetId();
        {
            INFO("A new entity inherited a destroyed entity's component");
            REQUIRE(!entities.Has(velocity->type, reused));
        }
        {
            INFO("C# saw a component on an entity that has none");
            REQUIRE(!Scripted<bool>(scripting, "HasVelocity", reused));
        }
        {
            INFO("C# did not see a component that is there");
            REQUIRE(Scripted<bool>(scripting, "HasVelocity", first.GetId()));
        }

        // Removing one explicitly, and again (harmless).
        {
            INFO("A component was not removed");
            REQUIRE((entities.Remove(velocity->type, first.GetId()) && !entities.Has(velocity->type, first.GetId())));
        }
        {
            INFO("Removing twice failed");
            REQUIRE(entities.Remove(velocity->type, first.GetId()));
        }
        {
            INFO("The pool did not shrink");
            REQUIRE(entities.Count(velocity->type) == 1);
        }
    }

    // What C# sees of a script component the ECS aligns for it (the storage itself is FenrirECS's, tested there).
    void CSharpUsesAnAlignedComponentAndAnEmptyOneAsATag()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* aligned = scripting.FindComponent(AlignedName);
        const auto* tagged = scripting.FindComponent(TaggedName);
        {
            INFO("Aligned or Tagged was not registered");
            REQUIRE((aligned != nullptr && tagged != nullptr));
        }
        const auto holder = entities.CreateEntity().GetId();
        const auto other = entities.CreateEntity().GetId();
        {
            INFO("Aligned was not added");
            REQUIRE(entities.Add(aligned->type, holder));
        }

        // C# reads and writes a Vector128 in it.
        Scripted<void>(scripting, "FillAligned", holder);
        const auto* lanes = static_cast<const float*>(entities.Get(aligned->type, holder));
        {
            INFO("C# writes to an aligned component were not in the storage");
            REQUIRE((lanes[0] == 1 && lanes[3] == 4 &&
                     static_cast<const std::uint8_t*>(entities.Get(aligned->type, holder))[16] == 7));
        }
        {
            INFO("C# did not read a lane back");
            REQUIRE(Scripted<float>(scripting, "AlignedLane", holder, 2) == 3.0F);
        }

        // A tag with no data is still a component.
        {
            INFO("An empty component did not work as a tag");
            REQUIRE((entities.Add(tagged->type, holder) && Scripted<bool>(scripting, "HasTagged", holder) &&
                     !Scripted<bool>(scripting, "HasTagged", other)));
        }
    }

    constexpr const char* ChangesType = "Fenrir.ScriptFixture.Changes";

    template <typename Result, typename... Args>
    Result Changes(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ChangesType, scripting, method, arguments...);
    }

    // The script-defined Velocity, added to an entity from native code.
    std::uint32_t EntityWithVelocity(Scripting& scripting, EntityList& entities)
    {
        const auto id = entities.CreateEntity().GetId();
        {
            INFO("Velocity was not added");
            REQUIRE(entities.Add(scripting.FindComponent(VelocityName)->type, id));
        }
        return id;
    }

    float VelocityX(Scripting& scripting, EntityList& entities, std::uint32_t id)
    {
        const auto* values = static_cast<const float*>(entities.Get(scripting.FindComponent(VelocityName)->type, id));
        {
            INFO("The entity had no Velocity");
            REQUIRE(values != nullptr);
        }
        return values[0];
    }

    void ACreatedEntityIsUsableAtOnceAndExistsAfterwards()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);

        const auto id = Changes<std::uint32_t>(scripting, "CreateWithVelocity");
        // Alive and usable (the script added to it), but nothing is in the storage until playback.
        {
            INFO("The new entity was not alive at once");
            REQUIRE((id != NullEntity && entities.HasEntity(id)));
        }
        {
            INFO("The script's changes were not recorded");
            REQUIRE(ScriptingProbe::PendingChanges(scripting) == 3);
        }
        {
            INFO("A change reached the storage before playback");
            REQUIRE((!entities.Has(velocity->type, id) && !entities.GetEntity(id).HasComponent<Transform>()));
        }

        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Playback did not apply all");
            REQUIRE((summary.applied == 3 && summary.skipped == 0 && ScriptingProbe::PendingChanges(scripting) == 0));
        }
        auto entity = entities.GetEntity(id);
        {
            INFO("A created entity did not get the engine's components");
            REQUIRE((entity.HasComponent<Transform>() && entity.HasComponent<Name>() &&
                     entity.HasComponent<Relationship>()));
        }
        {
            INFO("A created entity did not get a name");
            REQUIRE(std::string(entity.GetComponent<Name>().Get()).starts_with("Entity "));
        }
        const auto* values = static_cast<const float*>(entities.Get(velocity->type, id));
        {
            INFO("The added value was not applied");
            REQUIRE((values != nullptr && values[0] == 1 && values[1] == 2 && values[2] == 3));
        }
        {
            INFO("A default component was not added");
            REQUIRE(entities.Has(scripting.FindComponent("Fenrir.ScriptFixture.Counter")->type, id));
        }

        // Nothing left over to play back.
        {
            INFO("Playing back twice applied something twice");
            REQUIRE(ScriptingProbe::ApplyChanges(scripting).applied == 0);
        }
    }

    void ARefStaysValidWhileTheSameComponentIsAddedAndRemovedElsewhere()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto victim =
            EntityWithVelocity(scripting, entities); // First in the pool: its removal moves the last one.
        const auto middle = EntityWithVelocity(scripting, entities);
        const auto kept = EntityWithVelocity(scripting, entities);
        (void)middle;

        // The script holds a Ref to `kept` while the victim loses its component and thousands of entities gain one.
        const auto sum = Changes<float>(scripting, "RefSurvivesChanges", kept, victim);
        {
            INFO("The Ref did not keep reading and writing the same memory");
            REQUIRE(sum == 22.0F);
        }
        {
            INFO("A removal reached the storage before playback");
            REQUIRE(entities.Has(scripting.FindComponent(VelocityName)->type, victim));
        }

        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Playback did not apply every change");
            REQUIRE((summary.skipped == 0 && summary.applied > 5000));
        }
        {
            INFO("The removal was not applied");
            REQUIRE(!entities.Has(scripting.FindComponent(VelocityName)->type, victim));
        }
        {
            INFO("The write through the Ref did not stay with the kept entity when the pool changed");
            REQUIRE(VelocityX(scripting, entities, kept) == 9.0F);
        }
        // 3 to begin with, minus the victim, plus 5000 new entities and the one made before them.
        {
            INFO("The pool did not end up with the right entities");
            REQUIRE(entities.Count(scripting.FindComponent(VelocityName)->type) == 5003);
        }
    }

    void ReadsSeeLiveStateAndChangesWaitForPlayback()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto bare = entities.CreateEntity().GetId();
        const auto holder = EntityWithVelocity(scripting, entities);
        const auto doomed = entities.CreateEntity().GetId();

        {
            INFO("Has saw a component that was only recorded");
            REQUIRE(!Changes<bool>(scripting, "AddThenHas", bare));
        }
        {
            INFO("Has missed a component that was only recorded as removed");
            REQUIRE(Changes<bool>(scripting, "RemoveThenHas", holder));
        }
        {
            INFO("A destroyed entity was gone before playback");
            REQUIRE(Changes<bool>(scripting, "DestroyThenIsValid", doomed));
        }
        {
            INFO("A destroy reached the ECS before playback");
            REQUIRE(entities.HasEntity(doomed));
        }

        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("The add was not applied");
            REQUIRE(entities.Has(scripting.FindComponent(VelocityName)->type, bare));
        }
        {
            INFO("The removal was not applied");
            REQUIRE(!entities.Has(scripting.FindComponent(VelocityName)->type, holder));
        }
        {
            INFO("The destroy was not applied");
            REQUIRE(!entities.HasEntity(doomed));
        }
    }

    void ChangesFromCSharpApplyInOrderAndCarryTheirValues()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto entity = entities.CreateEntity().GetId();

        Changes<void>(scripting, "ChangesApplyInOrder", entity);
        {
            INFO("Not every change was applied");
            REQUIRE(ScriptingProbe::ApplyChanges(scripting).applied == 4);
        }
        {
            INFO("The last add did not win: C# calls did not reach the buffer in the order they were made");
            REQUIRE(VelocityX(scripting, entities, entity) == 3.0F);
        }

        // A native component takes a value too.
        Changes<void>(scripting, "SetTransform", entity);
        (void)ScriptingProbe::ApplyChanges(scripting);
        const auto& transform = entities.GetEntity(entity).GetComponent<Transform>();
        {
            INFO("A native component did not take the value");
            REQUIRE((transform.pos == glm::vec3(9, 8, 7) && transform.scale == glm::vec3(2, 2, 2) &&
                     transform.rot.w == 1.0F));
        }
    }

    void GetAndStructuralChangesFailClearly()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto bare = entities.CreateEntity().GetId();
        const auto stale = entities.CreateEntity().GetId();
        entities.DestroyEntity(stale);
        const auto message = [&](const char* method, std::uint32_t entity) {
            return Changes<std::optional<std::string>>(scripting, method, entity).value_or("");
        };

        const auto missing = message("TryGetMissing", bare);
        {
            INFO("Get on a missing component said: " + missing);
            REQUIRE(missing.find("has no Velocity") != std::string::npos);
        }
        const auto staleGet = message("TryGetMissing", stale);
        {
            INFO("Get on a stale entity said: " + staleGet);
            REQUIRE(staleGet.find("not alive") != std::string::npos);
        }
        {
            INFO("Add on a stale entity did not say so");
            REQUIRE(message("TryAddOnStale", stale).find("not alive") != std::string::npos);
        }
        {
            INFO("Destroy of a stale entity did not say so");
            REQUIRE(message("TryDestroyStale", stale).find("not alive") != std::string::npos);
        }
        {
            INFO("Removing Transform was not refused clearly");
            REQUIRE(message("TryRemoveRequired", bare).find("needs every entity") != std::string::npos);
        }
        {
            INFO("Removing Name was not refused");
            REQUIRE(message("TryRemoveName", bare).find("needs every entity") != std::string::npos);
        }
        {
            INFO("A refused change was recorded");
            REQUIRE(ScriptingProbe::PendingChanges(scripting) == 0);
        }
        {
            INFO("A refused removal changed the entity");
            REQUIRE(entities.GetEntity(bare).HasComponent<Transform>());
        }
    }

    constexpr const char* QueriesType = "Fenrir.ScriptFixture.Queries";

    template <typename Result, typename... Args>
    Result Queries(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(QueriesType, scripting, method, arguments...);
    }

    void SetVelocityX(Scripting& scripting, EntityList& entities, std::uint32_t id, float x)
    {
        auto* values = static_cast<float*>(entities.Get(scripting.FindComponent(VelocityName)->type, id));
        {
            INFO("The entity had no Velocity");
            REQUIRE(values != nullptr);
        }
        values[0] = x;
        values[1] = 0;
        values[2] = 0;
    }

    void WritesThroughQueryRowsAreVisibleNatively()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();

        // Two entities with a Transform (native) and a Velocity (script), one with only a Velocity, one with only a
        // Transform.
        const auto first = EntityWithVelocity(scripting, entities);
        const auto second = EntityWithVelocity(scripting, entities);
        const auto velocityOnly = entities.CreateEntity().GetId();
        {
            INFO("Velocity was not added");
            REQUIRE(entities.Add(scripting.FindComponent(VelocityName)->type, velocityOnly));
        }
        entities.GetEntity(velocityOnly).RemoveComponent<Transform>();
        const auto transformOnly = entities.CreateEntity().GetId();
        SetVelocityX(scripting, entities, first, 1);
        SetVelocityX(scripting, entities, second, 10);
        SetVelocityX(scripting, entities, velocityOnly, 100);
        {
            INFO("A single-component query missed an entity");
            REQUIRE(Queries<float>(scripting, "SumVelocityX") == 111.0F);
        }

        // A query over a native and a script component visits exactly the entities that have both, and its writes land
        // in the engine's own storage.
        {
            INFO("The two-component query visited the wrong entities");
            REQUIRE(Queries<std::int32_t>(scripting, "Integrate") == 2);
        }
        {
            INFO("The query was not repeatable");
            REQUIRE(Queries<std::int32_t>(scripting, "Integrate") == 2);
        }
        {
            INFO("Writes through query rows did not reach the native Transform");
            REQUIRE((entities.GetEntity(first).GetComponent<Transform>().pos.x == 2.0F &&
                     entities.GetEntity(second).GetComponent<Transform>().pos.x == 20.0F));
        }
        {
            INFO("A query visited an entity it should not");
            REQUIRE(entities.GetEntity(transformOnly).GetComponent<Transform>().pos.x == 0.0F);
        }

        // Each row's entity owns the row's components.
        for (const auto id : {first, second})
        {
            {
                INFO("Counter was not added");
                REQUIRE(entities.Add(scripting.FindComponent("Fenrir.ScriptFixture.Counter")->type, id));
            }
        }
        {
            INFO("A row's entity and its components disagreed");
            REQUIRE(Queries<bool>(scripting, "RowsAgree"));
        }

        // The callback form works the same.
        {
            INFO("ForEach visited the wrong entities");
            REQUIRE(Queries<std::int32_t>(scripting, "CountUp") == 2);
        }
        const auto* hits = static_cast<const std::int32_t*>(
            entities.Get(scripting.FindComponent("Fenrir.ScriptFixture.Counter")->type, first));
        {
            INFO("A write through ForEach did not reach the component");
            REQUIRE((hits != nullptr && hits[0] == 1));
        }
    }

    void AQueryMakesTheSameNumberOfNativeCallsForAnyNumberOfEntities()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        int total = 0;
        const auto callsFor = [&](int more) {
            for (int index = 0; index < more; ++index)
            {
                SetVelocityX(scripting, entities, EntityWithVelocity(scripting, entities), 1);
            }
            total += more;
            const int count = total;
            const auto before = ScriptingProbe::QueryCalls(scripting);
            const auto start = std::chrono::steady_clock::now();
            const auto visited = Queries<std::int32_t>(scripting, "Integrate");
            const auto microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start);
            {
                INFO("The query did not visit every entity");
                REQUIRE(visited == count);
            }
            std::cout << "  query over " << count
                      << " entities (Transform + Velocity): " << (ScriptingProbe::QueryCalls(scripting) - before)
                      << " native calls, " << microseconds.count() << " us\n";
            return ScriptingProbe::QueryCalls(scripting) - before;
        };
        const auto small = callsFor(10);
        const auto large = callsFor(10000 - 10);
        {
            INFO("A query's native calls depended on the entity count");
            REQUIRE((small == large && small == 2));
        }
    }

    void StructuralChangesDuringIterationAreDeferred()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);
        const auto* counter = scripting.FindComponent("Fenrir.ScriptFixture.Counter");
        std::vector<std::uint32_t> originals;
        for (int index = 0; index < 5; ++index)
        {
            originals.push_back(EntityWithVelocity(scripting, entities));
        }

        // The script removes each entity's Velocity, adds Counter, creates a new Velocity entity and destroys one
        // entity, all inside the loop. The loop still visits exactly the five it started with.
        {
            INFO("Iteration was disturbed by changes made inside it");
            REQUIRE(Queries<std::int32_t>(scripting, "ChangeWhileIterating") == 5);
        }
        {
            INFO("A change reached the storage during iteration");
            REQUIRE((entities.Count(velocity->type) == 5 && entities.Count(counter->type) == 0));
        }
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("A change was skipped: the destroy came last, so nothing named a dead entity");
            REQUIRE(summary.skipped == 0);
        }
        {
            INFO("Velocity should now be on the five new entities");
            REQUIRE(entities.Count(velocity->type) == 5);
        }
        {
            INFO("Counter should be on the four survivors");
            REQUIRE(entities.Count(counter->type) == 4);
        }
        for (const auto id : originals)
        {
            {
                INFO("An original kept its Velocity");
                REQUIRE(!entities.Has(velocity->type, id));
            }
        }

        // Nested queries each see the whole set.
        {
            INFO("Nested queries did not each see every entity");
            REQUIRE(Queries<std::int32_t>(scripting, "Nested") == 5 * 4);
        }
        {
            INFO("Leaving a loop early failed");
            REQUIRE(Queries<std::int32_t>(scripting, "StopEarly") == 2);
        }
        {
            INFO("A query after breaking out of one was wrong");
            REQUIRE(Queries<float>(scripting, "SumVelocityX") == 500.0F);
        }
    }

    void ForEachDefersStructuralChangesToo()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto* velocity = scripting.FindComponent(VelocityName);
        for (int index = 0; index < 5; ++index)
        {
            (void)EntityWithVelocity(scripting, entities);
        }
        // Five visited, however many the loop creates; the new ones appear (and the old ones go) at playback.
        {
            INFO("ForEach was disturbed by changes made inside it");
            REQUIRE(Queries<std::int32_t>(scripting, "ChangeInsideForEach") == 5);
        }
        {
            INFO("A change reached the storage during ForEach");
            REQUIRE(entities.Count(velocity->type) == 5);
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("ForEach's deferred changes were not applied");
            REQUIRE(entities.Count(velocity->type) == 5);
        }
        {
            INFO("The replacements were not queryable");
            REQUIRE(Queries<std::int32_t>(scripting, "ChangeInsideForEach") == 5);
        }
    }

    void EmptyAndInvalidQueriesBehave()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        entities.CreateEntity();

        // Nothing has the component, and nothing has been added: empty, with no storage made for it.
        const auto storages = entities.ComponentStorageCount();
        {
            INFO("An empty query visited something");
            REQUIRE(Queries<std::int32_t>(scripting, "VisitEmpty") == 0);
        }
        {
            INFO("An empty sum was not zero");
            REQUIRE(Queries<float>(scripting, "SumVelocityX") == 0.0F);
        }
        {
            INFO("An empty query made storage");
            REQUIRE(entities.ComponentStorageCount() == storages);
        }

        // A component the engine never registered, and the same component twice: clear errors.
        const auto unregistered = Queries<std::optional<std::string>>(scripting, "QueryUnregistered").value_or("");
        {
            INFO("Querying an unregistered component said: " + unregistered);
            REQUIRE(unregistered.find("Test.Nothing") != std::string::npos);
        }
        const auto twice = Queries<std::optional<std::string>>(scripting, "QueryTwice").value_or("");
        {
            INFO("Querying a component twice said: " + twice);
            REQUIRE(twice.find("twice") != std::string::npos);
        }
    }

    void TheWidestQueryHasEightComponents()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const std::string prefix = "Fenrir.ScriptFixture.Q";

        // Three entities have all eight, with V = 1 in each; one has only seven of them.
        for (int entityIndex = 0; entityIndex < 4; ++entityIndex)
        {
            const auto id = entities.CreateEntity().GetId();
            for (int component = 1; component <= (entityIndex == 3 ? 7 : 8); ++component)
            {
                const auto* registration = scripting.FindComponent(prefix + std::to_string(component));
                {
                    INFO("A component was not added");
                    REQUIRE((registration != nullptr && entities.Add(registration->type, id)));
                }
                *static_cast<std::int32_t*>(entities.Get(registration->type, id)) = 1;
            }
        }
        {
            INFO("The eight-component query was wrong");
            REQUIRE(Queries<std::int32_t>(scripting, "AllEight") == 3 * 8);
        }
    }

    constexpr const char* ProbeType = "Fenrir.ScriptFixture.SystemProbe";

    template <typename Result, typename... Args>
    Result Probe(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ProbeType, scripting, method, arguments...);
    }

    std::vector<std::string> SplitEvents(const std::string& dump)
    {
        std::vector<std::string> events;
        std::string current;
        for (const char character : dump)
        {
            if (character == ',')
            {
                events.push_back(current);
                current.clear();
            }
            else
            {
                current += character;
            }
        }
        if (!current.empty())
        {
            events.push_back(current);
        }
        return events;
    }

    std::size_t CountMessages(const HeadlessApp& headless, LogLevel level, const std::string& text)
    {
        const auto messages = headless.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const LoggedMessage& message) {
                return message.level == level && message.text.find(text) != std::string::npos;
            }));
    }

    void SystemsRunInPhaseAndOrderAndChangesApplyAfterEachOne()
    {
        constexpr int frames = 4;
        HeadlessApp headless;
        int nativeTicks = 0;
        headless.Get().AddSequentialSystems(SchedulePriority::Tick, {[&](App&) { ++nativeTicks; }});
        // Scripting is released in the Exit phase, so what the systems did is read by an Exit system registered first.
        Scripting* installed = nullptr;
        std::string dump;
        headless.Get().AddSequentialSystems(
            SchedulePriority::Exit,
            {[&](App&) { dump = Probe<std::optional<std::string>>(*installed, "Dump").value_or(""); }});
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        installed = &scripting;
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        Probe<void>(scripting, "Enable");
        headless.Run(frames);

        const auto events = SplitEvents(dump);
        std::vector<std::string> withoutTicks;
        std::size_t scriptTicks = 0;
        std::string previous;
        for (const auto& event : events)
        {
            if (event == "Tick")
            {
                ++scriptTicks;
                // Fixed steps happen after PreUpdate and before Update, never anywhere else in the frame.
                {
                    INFO("A Tick ran outside the gap between PreUpdate and Update");
                    REQUIRE((previous == "Pre" || previous == "Tick"));
                }
            }
            else
            {
                withoutTicks.push_back(event);
            }
            previous = event;
        }
        // Each frame: PreUpdate, then Update in Order (Lead 1 although its name sorts last, Creator 5 makes an entity,
        // Early 10 already sees it, TieA and TieB 12 by name, Thrower 15 throws on frame one and is dropped, Late 20,
        // Observer 30), then PostUpdate. The thrower's entity never appears.
        std::vector<std::string> expected;
        for (int frame = 1; frame <= frames; ++frame)
        {
            for (const auto& event :
                 {std::string("Pre"), std::string("Lead"), "Early=" + std::to_string(frame), std::string("TieA"),
                  std::string("TieB"), std::string("Late"), "Seen=" + std::to_string(frame), std::string("Post")})
            {
                expected.push_back(event);
            }
        }
        {
            INFO("The systems did not run in phase and Order, with changes applied after each");
            REQUIRE(withoutTicks == expected);
        }

        // Tick runs once per fixed step, as often as a native Tick system does.
        {
            INFO("Tick did not run once per fixed step");
            REQUIRE((scriptTicks == static_cast<std::size_t>(nativeTicks) && nativeTicks > 0));
        }

        // One transition per phase that has systems, per frame, and per fixed step for Tick.
        {
            INFO("The engine did not enter the script exactly once per phase");
            REQUIRE(ScriptingProbe::PhaseCalls(scripting) == static_cast<std::size_t>(3 * frames + nativeTicks));
        }

        // The thrower was logged once with its name, and the others kept running.
        {
            INFO("A throwing system was not logged exactly once");
            REQUIRE(CountMessages(headless, LogLevel::Error, "ThrowerSystem") == 1);
        }
        {
            INFO("The exception's message was not logged");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, "kaboom"));
        }
    }

    void OnCreateRunsAfterLoadAndOnDestroyBeforeRelease()
    {
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("OnCreate did not run after load");
                REQUIRE(headless.Logger().Contains(LogLevel::Info, "Creator.OnCreate"));
            }
            {
                INFO("OnDestroy ran too early");
                REQUIRE(!headless.Logger().Contains(LogLevel::Info, "Creator.OnDestroy"));
            }
            headless.Run(2); // The Exit phase releases scripting.
            {
                INFO("OnDestroy did not run at shutdown");
                REQUIRE(headless.Logger().Contains(LogLevel::Info, "Creator.OnDestroy"));
            }
            {
                INFO("Scripting was still running after the app finished");
                REQUIRE(!scripting.IsRunning());
            }
        }
        {
            // An explicit Shutdown says goodbye too, and only once.
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            scripting.Shutdown();
            scripting.Shutdown();
            {
                INFO("Shutdown did not run OnDestroy exactly once");
                REQUIRE(CountMessages(headless, LogLevel::Info, "Creator.OnDestroy") == 1);
            }
        }
    }

    void ASystemsCallsAfterShutdownDoNothing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        scripting.Shutdown();
        headless.Run(2); // The scheduled phase systems find nothing to run.
        {
            INFO("A phase ran after Shutdown");
            REQUIRE(ScriptingProbe::PhaseCalls(scripting) == 0);
        }
    }

    constexpr const char* EventScriptsType = "Fenrir.ScriptFixture.EventScripts";
    constexpr const char* EventProbeType = "Fenrir.ScriptFixture.EventProbe";

    template <typename Result, typename... Args>
    Result EventScript(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(EventScriptsType, scripting, method, arguments...);
    }

    ScriptingConfig ConfigWithAlert()
    {
        return ConfigFor(fixture, {}, {MakeEvent<TestNative::Alert>("Test.Alert")});
    }

    void ACppEventIsReadInCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigWithAlert());
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("There were events before any was sent");
            REQUIRE(EventScript<std::optional<std::string>>(scripting, "ReadAlerts").value_or("?").empty());
        }

        headless.Get().SendEvent(TestNative::Alert{5, 0.5F});
        headless.Get().SendEvent(TestNative::Alert{6, 1.0F});
        {
            INFO("C# did not read the events C++ sent");
            REQUIRE(EventScript<std::optional<std::string>>(scripting, "ReadAlerts").value_or("?") == "5,6");
        }
    }

    void ACSharpReaderMadeAfterSwapsStartsAtTheOldestEventHeld()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigWithAlert());
        // One Alert a frame, each frame ticking, so the queue swaps three times and drops Alerts 1 and 2.
        for (std::int32_t code = 1; code <= 3; ++code)
        {
            headless.Get().SendEvent(TestNative::Alert{code, 0.0F});
            headless.Get().RunFrame(HeadlessApp::FrameDelta);
        }
        headless.Get().SendEvent(TestNative::Alert{4, 0.0F});
        {
            INFO("A new C# reader did not start at the oldest Alert held, or counted ones sent before it as missed");
            REQUIRE(EventScript<std::optional<std::string>>(scripting, "ReadAlertsWithNewReader").value_or("?") ==
                    "3,4;missed=0");
        }
        headless.Get().RunExit();
    }

    void ACSharpEventIsReadInCpp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigWithAlert());
        EventReader<TestNative::Alert> alerts;
        EventScript<void>(scripting, "SendAlert", std::int32_t{9}, 2.5F);
        // Held until the script returns: nothing has reached the queue yet.
        {
            INFO("A send reached the queue before the system returned");
            REQUIRE(alerts.Read(headless.Get()).empty());
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        const auto events = alerts.Read(headless.Get());
        {
            INFO("C++ did not read the event C# sent");
            REQUIRE((events.size() == 1 && events.begin()->Code == 9 && events.begin()->Level == 2.5F));
        }
    }

    void ACSharpOnlyEventNeedsNoCpp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        EventScript<void>(scripting, "SendPing", std::int32_t{3});
        EventScript<void>(scripting, "SendPing", std::int32_t{4});
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("A C#-only event was not delivered");
            REQUIRE(EventScript<std::int32_t>(scripting, "SumPings") == 7);
        }

        // Nothing in C++ knows its type, but the queue is there by name, with the layout C# reported.
        const IEventQueue* queue = headless.Get().FindEventQueue("Fenrir.ScriptFixture.Ping");
        std::size_t seen = NewReaderCursor;
        {
            INFO("The C#-only event's queue was not there by name");
            REQUIRE((queue != nullptr && queue->EventSize() == 4 && queue->ReadRawAfter(seen).Count() == 2));
        }

        // The queue goes with the script assembly.
        scripting.Shutdown();
        {
            INFO("The queue outlived the script assembly");
            REQUIRE(headless.Get().FindEventQueue("Fenrir.ScriptFixture.Ping") == nullptr);
        }
        auto again = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The script assembly did not load again on the same App");
            REQUIRE(again.HasScripts());
        }
        // The reloaded assembly sends and reads through a fresh queue (ids cached for the old registry are not reused).
        EventScript<void>(again, "SendPing", std::int32_t{5});
        (void)ScriptingProbe::ApplyChanges(again);
        {
            INFO("A reloaded script assembly could not use its events");
            REQUIRE(EventScript<std::int32_t>(again, "SumPings") == 5);
        }
    }

    void EachReaderSeesEachEventOnceInBothLanguages()
    {
        HeadlessApp headless;
        App& app = headless.Get();
        int frame = 0;
        // C++ sends an Alert in Update of the first frame, before C#'s Update systems run, and another in PostUpdate of
        // the second frame, after they ran (a late send).
        app.AddSequentialSystems(SchedulePriority::Update, {[&](App& running) {
                                     if (++frame == 1)
                                     {
                                         running.SendEvent(TestNative::Alert{1, 0});
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App& running) {
                                     if (frame == 2)
                                     {
                                         running.SendEvent(TestNative::Alert{2, 0});
                                     }
                                 }});
        Scripting* installed = nullptr;
        std::string dump;
        app.AddSequentialSystems(
            SchedulePriority::Exit, {[&](App&) {
                dump = CallIn<std::optional<std::string>>(EventProbeType, *installed, "Dump").value_or("");
            }});
        auto scripting = Scripting::Install(app, ConfigWithAlert());
        installed = &scripting;
        // After all of C#'s Update systems, with readers of its own: the Alert by type, and the C#-only Ping by name,
        // as bytes, with no C++ type.
        std::vector<std::size_t> nativeAlerts;
        std::vector<std::size_t> nativePings;
        app.AddSequentialSystems(
            SchedulePriority::Update,
            {[&, alerts = EventReader<TestNative::Alert>{}, pingsSeen = NewReaderCursor](App& running) mutable {
                nativeAlerts.push_back(alerts.Read(running).size());
                const IEventQueue* pings = running.FindEventQueue("Fenrir.ScriptFixture.Ping");
                nativePings.push_back(pings == nullptr ? 99 : pings->ReadRawAfter(pingsSeen).Count());
            }});
        CallIn<void>(EventProbeType, scripting, "Enable");

        // One tick, no tick, three ticks, one tick.
        constexpr double tick = 1.0 / 60.0;
        for (const double delta : {tick, 0.0, 3.5 * tick, tick})
        {
            app.RunFrame(delta);
        }
        app.RunExit();

        // Frame 1: the Ping is sent by the sender (which does not see its own send until it returns) and read by the
        // system after it, and the Alert is read once (the Tick reader ran before it was sent). Frame 2 (no tick): the
        // sender sees its own Ping, once; nothing else is new. Frame 3: the Tick reader sees both Alerts on its first
        // run and none on the next two; the late Alert reaches the Update reader once. Frame 4: nothing is seen again.
        const std::string expected = "Tick=0,Own=0,Later=1,Alerts=1,"
                                     "Own=1,Later=0,Alerts=0,"
                                     "Tick=2,Tick=0,Tick=0,Own=0,Later=0,Alerts=1,"
                                     "Tick=0,Own=0,Later=0,Alerts=0";
        {
            INFO("C# readers did not see each event once: " + dump);
            REQUIRE(dump == expected);
        }
        // C++ readers of the same queues are independent of the C# ones and see each event once too.
        {
            INFO("A C++ reader did not see each Alert once");
            REQUIRE((nativeAlerts == std::vector<std::size_t>{1, 0, 1, 0}));
        }
        {
            INFO("A C++ reader of the C#-only Ping did not see it once");
            REQUIRE((nativePings == std::vector<std::size_t>{1, 0, 0, 0}));
        }
    }

    void ASystemNeverSeesItsOwnSendsAndAViewStaysValid()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("A send was readable by its own sender");
            REQUIRE(EventScript<std::int32_t>(scripting, "SendThenCount", std::int32_t{1}) == 0);
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("A send was visible before its system returned, or the earlier one was lost");
            REQUIRE(EventScript<std::int32_t>(scripting, "SendThenCount", std::int32_t{2}) == 1);
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Both sends did not arrive");
            REQUIRE(EventScript<std::int32_t>(scripting, "SumPings") == 3);
        }

        // A hundred sends in one system do not move or change the view the system already holds.
        {
            INFO("A view of events changed while its system sent more");
            REQUIRE(EventScript<bool>(scripting, "ViewStaysValid"));
        }
        {
            INFO("The sends made in that system were not delivered together");
            REQUIRE(ScriptingProbe::ApplyChanges(scripting).applied == 100);
        }
    }

    void EventLookupFailuresSayWhatWentWrong()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigWithAlert());
        const auto orphan = EventScript<std::optional<std::string>>(scripting, "TryReadOrphan").value_or("");
        {
            INFO("An unknown event said: " + orphan);
            REQUIRE(orphan.find("no event registered as 'Test.NoSuchEvent'") != std::string::npos);
        }
        const auto fake = EventScript<std::optional<std::string>>(scripting, "TryReadFake").value_or("");
        {
            INFO("A script claiming a native event said: " + fake);
            REQUIRE(fake.find("different C# type") != std::string::npos);
        }
    }

    void AMismatchedEventFailsTheLoadAndNamesTheField()
    {
        HeadlessApp headless;
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {}, {MakeEvent<TestNative::BadAlert>("Test.Alert")}));
        {
            INFO("A mismatched event left scripts loaded");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("The mismatch did not name the event and the field");
            REQUIRE((headless.Logger().Contains(LogLevel::Error, "Test.Alert") &&
                     headless.Logger().Contains(LogLevel::Error, "'Code'")));
        }
        {
            INFO("A failed load left a queue behind");
            REQUIRE(headless.Get().FindEventQueue("Fenrir.ScriptFixture.Ping") == nullptr);
        }
    }

    void CppEventUsersAreUnchangedByNamedAndByteQueues()
    {
        struct Plain
        {
            int value;
        };
        struct Other
        {
            int value;
        };
        struct Heavy
        {
            std::string text;
        };
        HeadlessApp headless;
        App& app = headless.Get();

        app.SendEvent(Plain{1});
        app.SendEvent(Plain{2});
        {
            INFO("Typed events changed");
            REQUIRE(HeldEvents<Plain>(app).size() == 2);
        }
        {
            INFO("A queue could not be named, or naming twice failed");
            REQUIRE((app.NameEvent<Plain>("Plain") && app.NameEvent<Plain>("Plain")));
        }
        {
            INFO("A name was given to two different queues");
            REQUIRE(!app.NameEvent<Other>("Plain"));
        }
        const IEventQueue* named = app.FindEventQueue("Plain");
        {
            INFO("The named queue is not the typed queue");
            REQUIRE((named != nullptr && named->EventSize() == sizeof(Plain) && HeldEventCount(*named) == 2));
        }
        const int third = 3;
        {
            INFO("Sending by name did not reach the typed queue");
            REQUIRE((app.FindEventQueue("Plain")->SendRaw(&third) && HeldEvents<Plain>(app).size() == 3 &&
                     HeldEvents<Plain>(app)[2].value == 3));
        }

        // An event that is not plain bytes still works as before and cannot be used by name.
        app.SendEvent(Heavy{"a string"});
        {
            INFO("A non-trivial event changed");
            REQUIRE((HeldEvents<Heavy>(app).size() == 1 && HeldEvents<Heavy>(app)[0].text == "a string"));
        }
        {
            INFO("A non-trivial event was usable as bytes");
            REQUIRE((app.NameEvent<Heavy>("Heavy") && app.FindEventQueue("Heavy")->EventSize() == 0 &&
                     !app.FindEventQueue("Heavy")->SendRaw(&third)));
        }

        // A byte queue: the same two buffers, and it rotates with the frame.
        IEventQueue* bytes = app.CreateEventQueue("Bytes", 8, 4);
        {
            INFO("A byte queue could be made twice, or over a typed one");
            REQUIRE((bytes != nullptr && app.CreateEventQueue("Bytes", 8, 4) == nullptr &&
                     app.CreateEventQueue("Plain", 8, 4) == nullptr));
        }
        {
            INFO("An unusable byte queue was made");
            REQUIRE((app.CreateEventQueue("Odd", 6, 4) == nullptr && app.CreateEventQueue("Wide", 32, 32) == nullptr &&
                     app.CreateEventQueue("", 4, 4) == nullptr));
        }
        const std::int64_t value = 42;
        {
            INFO("A byte queue did not hold its event");
            REQUIRE((bytes->SendRaw(&value) && HeldEventCount(*bytes) == 1));
        }
        {
            INFO("Removing a byte queue went wrong, or a typed queue could be removed");
            REQUIRE((app.RemoveEventQueue("Bytes") && app.FindEventQueue("Bytes") == nullptr &&
                     !app.RemoveEventQueue("Bytes") && !app.RemoveEventQueue("Plain")));
        }
    }

    // A scratch directory of the test's own, removed afterwards.
    struct ScratchDirectory
    {
        std::filesystem::path path;
        ScratchDirectory()
        {
            static int counter = 0;
            path = std::filesystem::temp_directory_path() / ("fenrir-scripting-test-" + std::to_string(++counter));
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
        ~ScratchDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
        [[nodiscard]] std::size_t Entries() const
        {
            std::error_code ignored;
            std::size_t count = 0;
            if (std::filesystem::exists(path, ignored))
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ignored))
                {
                    (void)entry;
                    ++count;
                }
            }
            return count;
        }
    };

    void AReleasedGenerationUnloadsAndItsCopyIsDeleted()
    {
        ScratchDirectory scratch;
        HeadlessApp headless;
        ScriptingConfig config = ConfigFor(fixture);
        config.reloadDirectory = scratch.path;
        auto scripting = Scripting::Install(headless.Get(), config);
        {
            INFO("The fixture did not load as generation one");
            REQUIRE((scripting.HasScripts() && scripting.Generation() == 1));
        }
        {
            INFO("The assembly was not loaded from a copy");
            REQUIRE(scratch.Entries() > 0);
        }
        scripting.Shutdown();
        {
            INFO("The released generation did not unload");
            REQUIRE(scripting.PendingUnloadCount() == 0);
        }
        {
            INFO("The copy of the assembly was not deleted once it unloaded");
            REQUIRE(scratch.Entries() == 0);
        }
    }

    // A copy of the fixture the test may overwrite the way a build would, while an app has it loaded.
    struct WorkingFixture
    {
        ScratchDirectory directory;
        std::filesystem::path assembly;

        WorkingFixture()
        {
            std::filesystem::create_directories(directory.path / "build");
            Overwrite(fixture);
            assembly = directory.path / "build" / fixture.filename();
        }

        // Replaces the build with another one (a whole directory, as a build writes it).
        void Overwrite(const std::filesystem::path& assemblyFile) const
        {
            std::filesystem::copy(assemblyFile.parent_path(), directory.path / "build",
                                  std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing);
        }

        // A build that is not an assembly at all.
        void Break() const
        {
            std::ofstream out(directory.path / "build" / fixture.filename(), std::ios::binary | std::ios::trunc);
            out << "this is not a .NET assembly";
        }

        [[nodiscard]] ScriptingConfig Config(bool hotReload) const
        {
            ScriptingConfig config = ConfigFor(assembly);
            config.hotReload = hotReload;
            config.reloadDirectory = directory.path / "copies";
            return config;
        }

        [[nodiscard]] std::size_t Generations() const
        {
            std::size_t count = 0;
            std::error_code ignored;
            const auto copies = directory.path / "copies";
            if (!std::filesystem::exists(copies, ignored))
            {
                return 0;
            }
            for (const auto& install : std::filesystem::directory_iterator(copies, ignored))
            {
                for (const auto& generation : std::filesystem::directory_iterator(install.path(), ignored))
                {
                    (void)generation;
                    ++count;
                }
            }
            return count;
        }
    };

    std::string Join(const std::vector<std::int32_t>& values)
    {
        std::string text;
        for (const auto value : values)
        {
            text += (text.empty() ? "" : ",") + std::to_string(value);
        }
        return "[" + text + "]";
    }

    constexpr const char* StampedName = "Fenrir.ScriptFixture.Stamped";
    constexpr const char* ReloadProbeType = "Fenrir.ScriptFixture.ReloadProbe";

    template <typename Result, typename... Args>
    Result Reloaded(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ReloadProbeType, scripting, method, arguments...);
    }

    std::int32_t StampedValue(Scripting& scripting, EntityList& entities, std::uint32_t id)
    {
        const auto* values =
            static_cast<const std::int32_t*>(entities.Get(scripting.FindComponent(StampedName)->type, id));
        {
            INFO("The entity had no Stamped");
            REQUIRE(values != nullptr);
        }
        return values[0];
    }

    void ARebuiltAssemblyChangesBehaviourWithoutARestartAndKeepsUnchangedData()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        auto& entities = app.GetActiveScene().GetEntityList();
        Scripting* installed = nullptr;
        int frame = 0;
        std::vector<std::int32_t> stampedAfterFrame;
        std::vector<std::int32_t> answers;
        std::uint32_t stamped = 0;
        std::uint32_t velocityHolder = 0;
        std::uint32_t counterHolder = 0;
        bool reloadedOk = false;

        // Before anything else in the frame (so between frames): at the start of frame four, a build lands and is
        // reloaded.
        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     if (++frame != 4)
                                     {
                                         return;
                                     }
                                     // Data to carry across: a Ping-like event that is unchanged (Echo) and one whose
                                     // layout changes (Ripple).
                                     Reloaded<void>(*installed, "SendEcho", std::int32_t{9});
                                     Reloaded<void>(*installed, "SendRipple", std::int32_t{1});
                                     (void)ScriptingProbe::ApplyChanges(*installed);
                                     answers.push_back(Reloaded<std::int32_t>(*installed, "Answer"));
                                     build.Overwrite(fixtureV2);
                                     reloadedOk = installed->Reload().Succeeded();
                                     answers.push_back(Reloaded<std::int32_t>(*installed, "Answer"));
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App&) {
                                     stampedAfterFrame.push_back(StampedValue(*installed, entities, stamped));
                                 }});
        auto scripting = Scripting::Install(app, build.Config(false));
        installed = &scripting;
        {
            INFO("The fixture did not load");
            REQUIRE((scripting.HasScripts() && scripting.Generation() == 1));
        }

        // The data to carry: a Stamped (unchanged in version 2), a Velocity (unchanged), a Counter (its layout
        // changes).
        const auto add = [&](const char* name, std::uint32_t& holder) {
            holder = entities.CreateEntity().GetId();
            {
                INFO("A component was not added");
                REQUIRE(entities.Add(scripting.FindComponent(name)->type, holder));
            }
        };
        add(StampedName, stamped);
        add(VelocityName, velocityHolder);
        add(CounterName, counterHolder);
        static_cast<std::int32_t*>(entities.Get(scripting.FindComponent(StampedName)->type, stamped))[0] = 5;
        static_cast<float*>(entities.Get(scripting.FindComponent(VelocityName)->type, velocityHolder))[0] = 3.0F;
        static_cast<std::int32_t*>(entities.Get(scripting.FindComponent(CounterName)->type, counterHolder))[0] = 77;

        headless.Run(6);

        {
            INFO("The new build's code did not take effect without a restart");
            REQUIRE((reloadedOk && answers == std::vector<std::int32_t>{1, 2}));
        }
        // Frames one to three added 1 each (version 1), frames four to six added 2 each (version 2): the value carried
        // over.
        {
            INFO("Stamped did not keep its value across the reload and change its step: " + Join(stampedAfterFrame));
            REQUIRE((stampedAfterFrame == std::vector<std::int32_t>{6, 7, 8, 10, 12, 14}));
        }
        // One generation said goodbye at the reload and the other at shutdown; a new set of systems was made for each
        // load.
        {
            INFO("The old systems did not say goodbye, or the new ones were not created");
            REQUIRE((CountMessages(headless, LogLevel::Info, "Creator.OnDestroy") == 2 &&
                     CountMessages(headless, LogLevel::Info, "Creator.OnCreate") == 2));
        }
        {
            INFO("The reload was not logged");
            REQUIRE(headless.Logger().Contains(LogLevel::Info, "reloaded"));
        }
        {
            INFO("Shutdown should have removed the queues");
            REQUIRE(app.FindEventQueue("Fenrir.ScriptFixture.Echo") == nullptr);
        }
    }

    void AChangedLayoutDropsTheComponentAndTheEventsAndNothingReadsStaleBytes()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        auto& entities = app.GetActiveScene().GetEntityList();
        auto scripting = Scripting::Install(app, build.Config(false));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }

        const auto* counter = scripting.FindComponent(CounterName);
        std::vector<std::uint32_t> holders;
        for (int index = 0; index < 3; ++index)
        {
            holders.push_back(entities.CreateEntity().GetId());
            {
                INFO("A Counter was not added");
                REQUIRE(entities.Add(counter->type, holders.back()));
            }
            static_cast<std::int32_t*>(entities.Get(counter->type, holders.back()))[0] = 100 + index;
        }
        const auto stamped = entities.CreateEntity().GetId();
        {
            INFO("A Stamped was not added");
            REQUIRE(entities.Add(scripting.FindComponent(StampedName)->type, stamped));
        }
        static_cast<std::int32_t*>(entities.Get(scripting.FindComponent(StampedName)->type, stamped))[0] = 41;
        {
            INFO("The test assumes version 1 of Counter is sixteen bytes");
            REQUIRE(counter->type.Size() == 16);
        }

        Reloaded<void>(scripting, "SendEcho", std::int32_t{9});
        Reloaded<void>(scripting, "SendEcho", std::int32_t{10});
        Reloaded<void>(scripting, "SendRipple", std::int32_t{1});
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("The test assumes version 1 of Ripple is four bytes");
            REQUIRE(app.FindEventQueue("Fenrir.ScriptFixture.Ripple")->EventSize() == 4);
        }

        build.Overwrite(fixtureV2);
        {
            INFO("The new build did not load");
            REQUIRE((scripting.Reload().Succeeded() && scripting.Generation() == 2));
        }

        // The component whose layout changed is gone from every entity, with a warning, and is the new size.
        const auto* changed = scripting.FindComponent(CounterName);
        {
            INFO("The new Counter layout was not registered");
            REQUIRE((changed != nullptr && changed->type.Size() == 24));
        }
        {
            INFO("Old Counter bytes are still on entities");
            REQUIRE(entities.Count(changed->type) == 0);
        }
        for (const auto id : holders)
        {
            {
                INFO("An entity still has the old Counter");
                REQUIRE((!entities.Has(changed->type, id) && entities.Get(changed->type, id) == nullptr));
            }
        }
        {
            INFO("The dropped component was not reported with how many were removed");
            REQUIRE((headless.Logger().Contains(LogLevel::Warn, "'Fenrir.ScriptFixture.Counter' changed its layout") &&
                     headless.Logger().Contains(LogLevel::Warn, "3 component(s) were removed")));
        }
        // A new one starts zeroed, at the new size, rather than inheriting bytes.
        {
            INFO("A Counter of the new layout was not added");
            REQUIRE(entities.Add(changed->type, holders.front()));
        }
        const auto* fresh = static_cast<const std::uint8_t*>(entities.Get(changed->type, holders.front()));
        {
            INFO("A new Counter was not zeroed");
            REQUIRE((fresh != nullptr && std::all_of(fresh, fresh + 24, [](std::uint8_t byte) { return byte == 0; })));
        }

        // The unchanged component kept its value.
        {
            INFO("An unchanged component lost its value");
            REQUIRE(StampedValue(scripting, entities, stamped) == 41);
        }

        // The event queue whose layout is unchanged kept its events, and the one that changed was recreated empty.
        const IEventQueue* echo = app.FindEventQueue("Fenrir.ScriptFixture.Echo");
        const IEventQueue* ripple = app.FindEventQueue("Fenrir.ScriptFixture.Ripple");
        {
            INFO("An unchanged event queue lost its events");
            REQUIRE((echo != nullptr && HeldEventCount(*echo) == 2));
        }
        {
            INFO("A changed event queue was not recreated empty");
            REQUIRE((ripple != nullptr && ripple->EventSize() == 8 && HeldEventCount(*ripple) == 0));
        }
        {
            INFO("The dropped event was not reported");
            REQUIRE(headless.Logger().Contains(LogLevel::Warn, "'Fenrir.ScriptFixture.Ripple' changed its layout"));
        }
    }

    void ABrokenBuildKeepsTheOldGenerationRunning()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        auto& entities = app.GetActiveScene().GetEntityList();
        Scripting* installed = nullptr;
        int frame = 0;
        bool failed = false;
        bool fixed = false;
        std::vector<std::int32_t> stampedAfterFrame;
        std::vector<std::int32_t> answers;
        std::uint32_t stamped = 0;

        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     ++frame;
                                     if (frame == 3)
                                     {
                                         build.Break();
                                         failed = !installed->Reload().Succeeded();
                                         answers.push_back(
                                             Reloaded<std::int32_t>(*installed, "Answer")); // Still version one.
                                     }
                                     if (frame == 5)
                                     {
                                         build.Overwrite(fixtureV2);
                                         fixed = installed->Reload().Succeeded();
                                         answers.push_back(Reloaded<std::int32_t>(*installed, "Answer"));
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App&) {
                                     stampedAfterFrame.push_back(StampedValue(*installed, entities, stamped));
                                 }});
        auto scripting = Scripting::Install(app, build.Config(false));
        installed = &scripting;
        stamped = entities.CreateEntity().GetId();
        {
            INFO("A Stamped was not added");
            REQUIRE(entities.Add(scripting.FindComponent(StampedName)->type, stamped));
        }

        headless.Run(6);

        {
            INFO("A build that is not an assembly was reported as loaded");
            REQUIRE(failed);
        }
        {
            INFO("The failed build and the restore were not logged");
            REQUIRE((headless.Logger().Contains(LogLevel::Error, "did not load") &&
                     headless.Logger().Contains(LogLevel::Warn, "previous build")));
        }
        // Version one kept running through the broken build: +1 a frame, no gap, no restart of the data; then version
        // two adds 2.
        {
            INFO("The old generation did not keep running through a broken build: " + Join(stampedAfterFrame));
            REQUIRE((stampedAfterFrame == std::vector<std::int32_t>{1, 2, 3, 4, 6, 8}));
        }
        {
            INFO("A later good build did not load after a broken one");
            REQUIRE(((answers == std::vector<std::int32_t>{1, 2}) && fixed));
        }
    }

    void RepeatedReloadsLeaveNoGenerationsBehind()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        auto& entities = app.GetActiveScene().GetEntityList();
        Scripting* installed = nullptr;
        std::vector<std::string> problems;
        std::int32_t finalValue = 0;
        std::uint32_t stamped = 0;
        constexpr int cycles = 12;

        bool done = false;
        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     if (std::exchange(done, true))
                                     {
                                         return;
                                     }
                                     for (int cycle = 0; cycle < cycles; ++cycle)
                                     {
                                         build.Overwrite(cycle % 2 == 0 ? fixtureV2 : fixture);
                                         if (!installed->Reload().Succeeded())
                                         {
                                             problems.push_back("cycle " + std::to_string(cycle) + " did not reload");
                                         }
                                         if (installed->PendingUnloadCount() != 0)
                                         {
                                             problems.push_back("cycle " + std::to_string(cycle) +
                                                                " left a generation pending");
                                         }
                                         if (build.Generations() != 1)
                                         {
                                             problems.push_back("cycle " + std::to_string(cycle) + " left " +
                                                                std::to_string(build.Generations()) + " copies");
                                         }
                                     }
                                     finalValue = StampedValue(*installed, entities, stamped);
                                 }});
        auto scripting = Scripting::Install(app, build.Config(false));
        installed = &scripting;
        stamped = entities.CreateEntity().GetId();
        {
            INFO("A Stamped was not added");
            REQUIRE(entities.Add(scripting.FindComponent(StampedName)->type, stamped));
        }
        static_cast<std::int32_t*>(entities.Get(scripting.FindComponent(StampedName)->type, stamped))[0] = 1234;

        headless.Run(2);

        {
            INFO((problems.empty() ? "" : problems.front()));
            REQUIRE(problems.empty());
        }
        {
            INFO("The generation count was off: " + std::to_string(scripting.Generation()));
            REQUIRE(scripting.Generation() == 1 + cycles);
        }
        {
            INFO("Data was lost over repeated reloads");
            REQUIRE(finalValue == 1234);
        }
        {
            INFO("A generation or its copy outlived shutdown");
            REQUIRE((scripting.PendingUnloadCount() == 0 && build.Generations() == 0));
        }
    }

    void TheWatcherReloadsAStableBuildBetweenFrames()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        int frame = 0;
        Scripting* installed = nullptr;
        std::int32_t answerAtExit = 0;
        int generationAtExit = 0;
        std::vector<int> generationByFrame;

        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     // A build that is still being written: it lands, is replaced by a broken file, and
                                     // lands again, all inside the debounce. Only the last, stable one may be loaded.
                                     ++frame;
                                     if (frame == 5 || frame == 9)
                                     {
                                         build.Overwrite(fixtureV2);
                                     }
                                     else if (frame == 7)
                                     {
                                         build.Break();
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App&) {
                                     generationByFrame.push_back(installed->Generation());
                                     std::this_thread::sleep_for(std::chrono::milliseconds(
                                         5)); // Frames take real time, so the debounce can pass.
                                 }});
        // Read before scripting is released in the Exit phase.
        app.AddSequentialSystems(SchedulePriority::Exit, {[&](App&) {
                                     answerAtExit = Reloaded<std::int32_t>(*installed, "Answer");
                                     generationAtExit = installed->Generation();
                                 }});
        ScriptingConfig config = build.Config(true);
        config.reloadInterval = std::chrono::milliseconds(0);
        config.reloadDebounce = std::chrono::milliseconds(60);
        auto scripting = Scripting::Install(app, config);
        installed = &scripting;

        headless.Run(80);

        {
            INFO("The watcher did not reload the new build");
            REQUIRE((answerAtExit == 2 && generationAtExit == 2));
        }
        // Not before the build landed, and only once it had been stable for the debounce.
        {
            INFO("The reload did not happen after the build, between frames");
            REQUIRE((generationByFrame.size() >= 6 && generationByFrame[3] == 1 && generationByFrame.back() == 2));
        }
        {
            INFO("One build caused more or fewer than one reload");
            REQUIRE(CountMessages(headless, LogLevel::Info, "Scripting: reloading") == 1);
        }
        {
            INFO("A build that was still changing was loaded");
            REQUIRE(CountMessages(headless, LogLevel::Error, "did not load") == 0);
        }
    }

    void WithHotReloadOffTheWatcherNeverActsButReloadStillWorks()
    {
        WorkingFixture build;
        HeadlessApp headless;
        auto& app = headless.Get();
        int frame = 0;
        Scripting* installed = nullptr;
        int generationBeforeManualReload = 0;
        bool reloaded = false;
        int generationAtExit = 0;
        std::int32_t answerAtExit = 0;

        app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App&) {
                                     ++frame;
                                     if (frame == 2)
                                     {
                                         build.Overwrite(fixtureV2);
                                     }
                                     if (frame == 30)
                                     {
                                         generationBeforeManualReload = installed->Generation();
                                         reloaded = installed->Reload().Succeeded();
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate,
                                 {[&](App&) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }});
        app.AddSequentialSystems(SchedulePriority::Exit, {[&](App&) {
                                     generationAtExit = installed->Generation();
                                     answerAtExit = Reloaded<std::int32_t>(*installed, "Answer");
                                 }});
        // The watcher would look every frame and settle within 20ms, were it on.
        ScriptingConfig config = build.Config(false);
        config.reloadInterval = std::chrono::milliseconds(0);
        config.reloadDebounce = std::chrono::milliseconds(20);
        auto scripting = Scripting::Install(app, config);
        installed = &scripting;

        headless.Run(40);

        {
            INFO("The watcher reloaded a build although hot reload was off");
            REQUIRE(generationBeforeManualReload == 1);
        }
        {
            INFO("An explicit Reload did not load the new build");
            REQUIRE((reloaded && generationAtExit == 2 && answerAtExit == 2));
        }
        {
            INFO("Something other than the one explicit Reload reloaded");
            REQUIRE(CountMessages(headless, LogLevel::Info, "Scripting: reloading") == 1);
        }
    }

    void ReloadingWithNothingLoadedFailsCleanly()
    {
        // No script assembly configured: .NET never starts.
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ScriptingConfig{});
            const auto result = scripting.Reload();
            {
                INFO("Reloading with no script assembly configured did not say so");
                REQUIRE((!result.Succeeded() && result.status == Iris::CallStatus::NotInitialized));
            }
            headless.Run(Frames);
            {
                INFO("The app did not keep running after a refused reload");
                REQUIRE(headless.FramesRun() == Frames);
            }
        }
        // A configured assembly that does not exist.
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture.parent_path() / "missing.dll"));
            const auto result = scripting.Reload();
            {
                INFO("Reloading a script assembly that never loaded worked");
                REQUIRE((!result.Succeeded() && !scripting.HasScripts()));
            }
        }
    }

    void AFailedLoadLeavesNoComponentsAndNoPools()
    {
        HeadlessApp headless;
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const std::size_t poolsBefore = PoolCount(entities);

        // The script assembly defines components and loads fine, then a native mapping fails: the load is rolled back.
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<BadHealth>("Test.BadHealth")}));
        {
            INFO("A mismatched component left scripts loaded");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("A failed load left script components registered");
            REQUIRE(
                (scripting.FindComponent(VelocityName) == nullptr && scripting.FindComponent(CounterName) == nullptr &&
                 scripting.FindComponent(AlignedName) == nullptr && scripting.FindComponent(TaggedName) == nullptr));
        }
        {
            INFO("A failed load disturbed the components that were registered before it");
            REQUIRE((scripting.FindComponent("Fenrir.Transform") != nullptr &&
                     scripting.FindComponent("Test.BadHealth") != nullptr));
        }
        {
            INFO("A failed load left storage behind");
            REQUIRE(PoolCount(entities) == poolsBefore);
        }

        // And a clean load afterwards registers them, with nothing carried over from the failure.
        HeadlessApp again;
        auto fine = Scripting::Install(again.Get(), ConfigFor(fixture));
        {
            INFO("A later clean load did not register the script components");
            REQUIRE(fine.FindComponent(VelocityName) != nullptr);
        }
    }

    // Tells the bad-report fixture what to report as it loads (see BadReports.cs); restored to a sound report on exit.
    // The mode goes through a file, not the environment: on Linux and macOS .NET reads the environment once, as the
    // runtime starts, so a variable set afterwards never reaches the fixture.
    struct BadReportMode
    {
        explicit BadReportMode(const char* mode)
        {
            Set(mode);
        }
        ~BadReportMode()
        {
            Set("");
        }
        BadReportMode(const BadReportMode&) = delete;
        BadReportMode& operator=(const BadReportMode&) = delete;

        static void Set(const char* mode)
        {
            std::ofstream(badReportModeFile, std::ios::trunc) << mode;
        }
    };

    constexpr const char* BadAlphaName = "Fenrir.BadReportFixture.Alpha";
    constexpr const char* BadBeepName = "Fenrir.BadReportFixture.Beep";

    void ASoundReportLoadsAndRegistersEverythingTogether()
    {
        const BadReportMode mode("");
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(badReports));
        {
            INFO("The sound bad-report fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("The script component was not registered");
            REQUIRE(scripting.FindComponent(BadAlphaName) != nullptr);
        }
        {
            INFO("The script event's queue was not made");
            REQUIRE(headless.Get().FindEventQueue(BadBeepName) != nullptr);
        }
    }

    // A script assembly that reports something the engine cannot accept is refused whole: nothing it reported stays
    // registered or stored, whichever kind the bad report was and whichever kind came before it.
    void ExpectReportRefusedWholesale(const char* reportMode, const char* explanation)
    {
        const BadReportMode mode(reportMode);
        HeadlessApp headless;
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const std::size_t poolsBefore = PoolCount(entities);
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(badReports));
        {
            INFO(std::string("A script assembly that reported ") + reportMode + " was kept");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO(std::string("The refusal of ") + reportMode + " was not explained");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, explanation));
        }
        {
            INFO(std::string(reportMode) + " left a component registered");
            REQUIRE(scripting.FindComponent(BadAlphaName) == nullptr);
        }
        {
            INFO(std::string(reportMode) + " left an event queue");
            REQUIRE(headless.Get().FindEventQueue(BadBeepName) == nullptr);
        }
        {
            INFO(std::string(reportMode) + " left storage behind");
            REQUIRE(PoolCount(entities) == poolsBefore);
        }
        {
            INFO(std::string(reportMode) + " disturbed the engine's own components");
            REQUIRE(scripting.FindComponent("Fenrir.Transform") != nullptr);
        }
    }

    void ADuplicateScriptComponentIsRefused()
    {
        ExpectReportRefusedWholesale("duplicate-component", "more than once");
    }

    void ADuplicateScriptEventIsRefused()
    {
        ExpectReportRefusedWholesale("duplicate-event", "more than once");
    }

    void TheContractIsOneSharedCopyAcrossGenerations()
    {
        int firstIdentity = 0;
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("The contract was not the host's shared copy in the default load context");
                REQUIRE(Call<bool>(scripting, "ContractIsShared"));
            }
            firstIdentity = Call<int>(scripting, "ContractIdentity");
            // And it is the host's deployed file (beside the executable), not a copy a script brought along.
            const auto reported = Call<std::optional<std::string>>(scripting, "ContractLocation");
            {
                INFO("The contract reported no location");
                REQUIRE(reported.has_value());
            }
            const auto& location = *reported;
            {
                INFO("The contract was not loaded from the host's directory: " + location);
                REQUIRE(std::filesystem::equivalent(location, executableDirectory / "Fenrir.Scripting.dll"));
            }
        }
        // A fresh install loads the script assembly again; the contract it sees is the very same assembly.
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The contract was not shared on the second install");
            REQUIRE(Call<bool>(scripting, "ContractIsShared"));
        }
        {
            INFO("The contract changed identity between generations");
            REQUIRE(Call<int>(scripting, "ContractIdentity") == firstIdentity);
        }
    }

    void TheBuildDeploysTheBridgeNextToTheExecutable()
    {
        // iris_deploy puts these beside the executable, which is where Scripting looks by default.
        for (const char* name : {"Iris.Bridge.dll", "Iris.Bridge.runtimeconfig.json", "Iris.Bridge.deps.json"})
        {
            {
                INFO(std::string(name) + " is not next to the executable");
                REQUIRE(std::filesystem::exists(executableDirectory / name));
            }
        }
        {
            INFO("The executable directory is empty");
            REQUIRE(!std::filesystem::is_empty(executableDirectory));
        }
    }

    void NoAssemblyConfiguredDoesNothing()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ScriptingConfig{});
        {
            INFO("An empty config started scripting");
            REQUIRE((!scripting.IsRunning() && !scripting.HasScripts()));
        }
        headless.Run(Frames);
        {
            INFO("The app did not run");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    // What every public call answers when scripting is not live: nothing runs, nothing is recorded, nothing throws.
    void ExpectNotLive(Scripting& scripting, const char* what)
    {
        const std::string name = what;
        {
            INFO(name + " claimed to be running");
            REQUIRE((!scripting.IsRunning() && !scripting.HasScripts()));
        }
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO(name + " applied changes");
            REQUIRE((summary.applied == 0 && summary.skipped == 0));
        }
        ScriptingProbe::DiscardChanges(scripting);
        const auto reloaded = scripting.Reload();
        {
            INFO(name + " did not refuse a reload");
            REQUIRE((!reloaded.Succeeded() && reloaded.status == Iris::CallStatus::NotInitialized));
        }
        {
            INFO(name + " reported generations waiting to unload");
            REQUIRE(scripting.PendingUnloadCount() == 0);
        }
    }

    void ScriptingThatIsNotLiveAnswersEveryCallTheSameWay()
    {
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ScriptingConfig{});
            ExpectNotLive(scripting, "A scripting with no assembly configured");
            {
                INFO("No assembly was reported as a generation");
                REQUIRE(scripting.Generation() == 0);
            }
        }
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture.parent_path() / "missing.dll"));
            ExpectNotLive(scripting, "A scripting whose install failed");
            {
                INFO("A failed install was reported as a generation");
                REQUIRE(scripting.Generation() == 0);
            }
        }
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("The fixture did not load");
                REQUIRE(scripting.HasScripts());
            }
            const Scripting taken = std::move(scripting);
            ExpectNotLive(scripting, "A moved-from scripting"); // NOLINT(bugprone-use-after-move): that is the case
            {
                INFO("A moved-from scripting reported state");
                REQUIRE((scripting.Generation() == 0 && scripting.FindComponent("Fenrir.Transform") == nullptr));
            }
        }
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("The fixture did not load");
                REQUIRE(scripting.HasScripts());
            }
            scripting.Shutdown();
            ExpectNotLive(scripting, "A shut-down scripting");
        }
    }

    void ShutdownReleasesEverythingWhenTheAppFinishes()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE((scripting.IsRunning() && scripting.HasScripts()));
        }
        headless.Run(Frames);
        // The App's Exit phase has released the script assembly and the runtime host: nothing of Iris remains
        // before the owner is even destroyed.
        {
            INFO("Iris still held the runtime after the app finished");
            REQUIRE((!scripting.IsRunning() && !scripting.HasScripts()));
        }
        scripting.Shutdown(); // Idempotent.
        {
            INFO("Shutdown twice restarted something");
            REQUIRE(!scripting.IsRunning());
        }
    }

    void DestroyingTheOwnerWithoutRunningReleasesEverything()
    {
        // Another install in the same process works after the previous one was released.
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            {
                INFO("The first install did not load");
                REQUIRE(scripting.HasScripts());
            }
        }
        HeadlessApp second;
        auto scripting = Scripting::Install(second.Get(), ConfigFor(fixture));
        {
            INFO("A second install after a destroyed one did not load");
            REQUIRE(scripting.HasScripts());
        }
        second.Run(2);
        {
            INFO("The second app did not run");
            REQUIRE(second.FramesRun() == 2);
        }
    }

    constexpr const char* ExtensionCallsType = "Fenrir.TestExtensionScript.Calls";

    template <typename Result, typename... Args>
    Result ExtensionCall(Scripting& scripting, const char* method, Args... arguments)
    {
        return CallIn<Result>(ExtensionCallsType, scripting, method, arguments...);
    }

    // The test extension, as an engine package would describe what it gives scripting.
    ScriptingExtension TheTestExtension()
    {
        ScriptingExtension extension;
        extension.name = "test extension";
        extension.sharedAssemblies = {testExtension};
        extension.functions = {
            NativeFunction::Make<&TestExtension::Add>("Fenrir.TestExtension.Gadgets.Add"),
            NativeFunction::Make<&TestExtension::EchoEntity>("Fenrir.TestExtension.Gadgets.EchoEntity"),
            NativeFunction::Make<&TestExtension::HeightOf>("Fenrir.TestExtension.Gadgets.HeightOf"),
            NativeFunction::Make<&TestExtension::Lift>("Fenrir.TestExtension.Gadgets.Lift"),
            NativeFunction::Make<&TestExtension::Label>("Fenrir.TestExtension.Gadgets.Label"),
        };
        extension.components = {MakeComponent<TestExtension::Charge>("Fenrir.TestExtension.Charge")};
        extension.events = {MakeEvent<TestExtension::Ping>("Fenrir.TestExtension.Ping")};
        return extension;
    }

    ScriptingConfig ExtensionConfig(std::vector<ScriptingExtension> extensions)
    {
        ScriptingConfig config = ConfigFor(extensionScript);
        config.extensions = std::move(extensions);
        return config;
    }

    void ANativeFunctionGetsCSharpArgumentsAndItsResultReachesCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("The script assembly using the extension did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("20 + 22 through the extension's native function was not 42");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Add", std::int32_t{20}, std::int32_t{22}) == 42);
        }
        const auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity().GetId();
        {
            INFO("An entity did not cross to the native function and back unchanged");
            REQUIRE(ExtensionCall<std::uint32_t>(scripting, "EchoEntity", entity) == entity);
        }
    }

    void ANativeFunctionReceivesAStringFromCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        TestExtension::labelled = "untouched";
        {
            INFO("Multi-byte text and a number did not both arrive (6 UTF-8 bytes * 10 + 3 is 63)");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Label", std::string_view("\xCF\x80 \xE2\x82\xAC"),
                                                std::int32_t{3}) == 63);
            REQUIRE(TestExtension::labelled == "\xCF\x80 \xE2\x82\xAC");
        }
        {
            INFO("Empty text did not arrive as an empty string");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Label", std::string_view(), std::int32_t{4}) == 4);
            REQUIRE(TestExtension::labelled.empty());
        }
    }

    void ALoneSurrogateInAStringArrivesAsTheReplacementCharacter()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        TestExtension::labelled = "untouched";
        {
            INFO("'a', a lone surrogate and 'b' (1 + 3 + 1 UTF-8 bytes, times 10, plus 7) did not give 57");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "LabelWithLoneSurrogate", std::int32_t{7}) == 57);
        }
        {
            INFO("The lone surrogate did not arrive as U+FFFD");
            REQUIRE(TestExtension::labelled == "a\xEF\xBF\xBD"
                                               "b");
        }
    }

    void ANativeFunctionReachesTheActiveSceneThroughItsCallContext()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto entity = entities.CreateEntity();
        entity.GetComponent<Transform>().pos.y = 2.5F;
        {
            INFO("The native function did not read the entity's Transform through its call context");
            REQUIRE(ExtensionCall<float>(scripting, "HeightOf", entity.GetId()) == 2.5F);
        }
        ExtensionCall<void>(scripting, "Lift", entity.GetId(), 1.0F);
        {
            INFO("The native function's write did not reach the engine's storage");
            REQUIRE(entity.GetComponent<Transform>().pos.y == 3.5F);
        }
        {
            INFO("C# did not see what the native function wrote");
            REQUIRE(ExtensionCall<float>(scripting, "ScriptHeightOf", entity.GetId()) == 3.5F);
        }
    }

    void AnExtensionsComponentsAndEventsAreRegisteredAndCheckedAgainstCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("The extension's component and event did not pass their checks against C#");
            REQUIRE(scripting.HasScripts());
        }
        const auto* charge = scripting.FindComponent("Fenrir.TestExtension.Charge");
        {
            INFO("The extension's component was not registered");
            REQUIRE(charge != nullptr);
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto entity = entities.CreateEntity().GetId();
        {
            INFO("The extension's component could not be added natively");
            REQUIRE(entities.Add(charge->type, entity));
        }
        static_cast<TestExtension::Charge*>(entities.Get(charge->type, entity))->Level = 7;
        {
            INFO("C# did not read the extension's component");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "ChargeLevel", entity) == 7);
        }
        {
            INFO("The extension's event has no named queue");
            REQUIRE(headless.Get().FindEventQueue("Fenrir.TestExtension.Ping") != nullptr);
        }
    }

    void ACSharpFieldWithNoNativeFunctionFailsInstallAndNamesIt()
    {
        HeadlessApp headless;
        ScriptingExtension broken;
        broken.name = "broken extension";
        broken.sharedAssemblies = {brokenExtension};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), broken}));
        {
            INFO("Scripts ran although a C# field has no native function");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the unbound field and say the app runs without scripts");
            REQUIRE((headless.Logger().Contains(Testing::LogLevel::Error, "Broken.__native_Unregistered") &&
                     headless.Logger().Contains(Testing::LogLevel::Error, "running without scripts")));
        }
    }

    void ANativeFunctionWhoseSignatureDiffersFromItsFieldFailsInstallAndNamesIt()
    {
        HeadlessApp headless;
        ScriptingExtension broken;
        broken.name = "broken extension";
        broken.sharedAssemblies = {brokenExtension};
        broken.functions = {
            NativeFunction::Make<&TestExtension::Mismatched>("Fenrir.BrokenExtension.Broken.Mismatched")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), broken}));
        {
            INFO("Scripts ran although a native function's signature differs from its C# field");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the mismatched field and say the signatures differ");
            REQUIRE((headless.Logger().Contains(Testing::LogLevel::Error, "Broken.__native_Mismatched") &&
                     headless.Logger().Contains(Testing::LogLevel::Error, "does not match")));
        }
    }

    void TwoExtensionsDeclaringOneNativeFunctionFailInstallAndNameBoth()
    {
        HeadlessApp headless;
        ScriptingExtension copy;
        copy.name = "copying extension";
        copy.functions = {NativeFunction::Make<&TestExtension::Add>("Fenrir.TestExtension.Gadgets.Add")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), copy}));
        {
            INFO("Scripts ran although two extensions declare one native function");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the function and both extensions");
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Fenrir.TestExtension.Gadgets.Add"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'test extension'"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'copying extension'"));
        }
    }

    void TheEngineComponentsComeWithTheirRules()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        const auto* transform = scripting.FindComponent("Fenrir.Transform");
        const auto* name = scripting.FindComponent("Fenrir.Name");
        const auto* relationship = scripting.FindComponent("Fenrir.Relationship");
        {
            INFO("An engine component was not registered");
            REQUIRE((transform != nullptr && name != nullptr && relationship != nullptr));
        }
        {
            INFO("Transform or Name is not required, or is read-only");
            REQUIRE((transform->required && !transform->readOnly && name->required && !name->readOnly));
        }
        {
            INFO("Relationship is not both required and read-only");
            REQUIRE((relationship->required && relationship->readOnly));
        }
    }

    void AnExtensionCannotTakeAnEngineComponentsName()
    {
        HeadlessApp headless;
        ScriptingExtension shadowing;
        shadowing.name = "shadowing extension";
        shadowing.components = {MakeComponent<TestExtension::Charge>("Fenrir.Transform")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), shadowing}));
        {
            INFO("Scripts ran although an extension took an engine component's name");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the extension and say the app runs without scripts");
            REQUIRE((headless.Logger().Contains(Testing::LogLevel::Error, "'shadowing extension'") &&
                     headless.Logger().Contains(Testing::LogLevel::Error, "running without scripts")));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running after the failed install");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void TheBindingNameOfANativeFunctionFollowsOneRule()
    {
        {
            INFO("Namespace.Type.Method did not bind to the field __native_Method of Namespace.Type");
            REQUIRE(NativeFunction::BindingNameFor("Fenrir.Physics.Raycast") == "Fenrir.Physics.__native_Raycast");
        }
        {
            INFO("A nested type's name (Outer+Inner) did not carry over");
            REQUIRE(NativeFunction::BindingNameFor("Game.Outer+Inner.Run") == "Game.Outer+Inner.__native_Run");
        }
        {
            INFO("A name with no type part had a binding name");
            REQUIRE(NativeFunction::BindingNameFor("Run").empty());
            REQUIRE(NativeFunction::BindingNameFor(".Run").empty());
            REQUIRE(NativeFunction::BindingNameFor("Game.").empty());
        }
    }

} // namespace

TEST_CASE("the fixture loads, frames run and Iris's messages reach the engine logger", "[scripting]")
{
    RunsFramesWithTheFixture();
}

TEST_CASE("a missing script assembly is logged and the app keeps running", "[scripting]")
{
    MissingAssemblyIsLoggedAndTheAppRuns();
}

TEST_CASE("an invalid script assembly is logged and the app keeps running", "[scripting]")
{
    InvalidAssemblyIsLoggedAndTheAppRuns();
}

TEST_CASE("a missing bridge is logged and the app keeps running", "[scripting]")
{
    MissingBridgeIsLoggedAndTheAppRuns();
}

TEST_CASE("a missing contract is logged and the app keeps running", "[scripting]")
{
    MissingContractIsLoggedAndTheAppRuns();
}

TEST_CASE("a second active install is refused and explained", "[scripting]")
{
    ASecondActiveInstallIsRefused();
}

TEST_CASE("scripts log through the engine logger at every level", "[scripting]")
{
    ScriptsLogThroughTheEngineLogger();
}

TEST_CASE("scripts read the engine clock", "[scripting]")
{
    ScriptsReadTheEngineClock();
}

TEST_CASE("entity validity follows the active scene, including slot reuse", "[scripting]")
{
    EntityValidityFollowsTheActiveScene();
}

TEST_CASE("the contract is one shared copy across generations", "[scripting]")
{
    TheContractIsOneSharedCopyAcrossGenerations();
}

TEST_CASE("Ref<T> writes through generated accessors into native memory", "[scripting]")
{
    RefWritesThroughGeneratedAccessorsIntoNativeMemory();
}

TEST_CASE("a null Ref says so instead of crashing", "[scripting]")
{
    ANullRefSaysSoInsteadOfCrashing();
}

TEST_CASE("a Transform is the same memory in C++ and C#", "[scripting]")
{
    ATransformIsTheSameMemoryInCppAndInCSharp();
}

TEST_CASE("a Name round-trips both ways, and long text is cut between characters", "[scripting]")
{
    ANameRoundTripsBothWays();
}

TEST_CASE("the hierarchy is readable from C# but not writable", "[scripting]")
{
    TheHierarchyIsReadableButNotWritable();
}

TEST_CASE("C# reads and sets a child's world transform and sees its own edit", "[scripting]")
{
    AWorldTransformIsReadAndSetFromCSharp();
}

TEST_CASE("a component registered by the test works with no engine changes", "[scripting]")
{
    ACustomComponentRegisteredByTheTestWorksWithNoEngineChanges();
}

TEST_CASE("lookup failures say what went wrong", "[scripting]")
{
    LookupFailuresSayWhatWentWrong();
}

TEST_CASE("a mismatched component fails the load and names the field", "[scripting]")
{
    AMismatchedComponentFailsTheLoadAndNamesTheField();
}

TEST_CASE("a duplicate component name fails install and names the extension", "[scripting]")
{
    ADuplicateComponentNameFailsInstallAndNamesTheExtension();
}

TEST_CASE("an empty component name fails install and names the extension", "[scripting]")
{
    AnEmptyComponentNameFailsInstallAndNamesTheExtension();
}

TEST_CASE("a duplicate event name fails install and names the extension", "[scripting]")
{
    ADuplicateEventNameFailsInstallAndNamesTheExtension();
}

TEST_CASE("a script assembly reports the components it defines, with size and alignment", "[scripting]")
{
    AScriptAssemblyReportsTheComponentsItDefines();
}

TEST_CASE("a script component is shared with C# and removed with its entity", "[scripting]")
{
    AScriptComponentIsSharedWithCSharpAndRemovedWithItsEntity();
}

TEST_CASE("C# uses an aligned component, and an empty one as a tag", "[scripting]")
{
    CSharpUsesAnAlignedComponentAndAnEmptyOneAsATag();
}

TEST_CASE("a created entity is usable at once and exists afterwards", "[scripting]")
{
    ACreatedEntityIsUsableAtOnceAndExistsAfterwards();
}

TEST_CASE("a Ref stays valid while the same component is added and removed elsewhere", "[scripting]")
{
    ARefStaysValidWhileTheSameComponentIsAddedAndRemovedElsewhere();
}

TEST_CASE("reads see live state and changes wait for playback", "[scripting]")
{
    ReadsSeeLiveStateAndChangesWaitForPlayback();
}

TEST_CASE("changes recorded from C# are applied in order and carry their values", "[scripting]")
{
    ChangesFromCSharpApplyInOrderAndCarryTheirValues();
}

TEST_CASE("get and structural changes fail clearly", "[scripting]")
{
    GetAndStructuralChangesFailClearly();
}

TEST_CASE("writes through query rows are visible natively", "[scripting]")
{
    WritesThroughQueryRowsAreVisibleNatively();
}

TEST_CASE("a query makes the same number of native calls for any number of entities", "[scripting]")
{
    AQueryMakesTheSameNumberOfNativeCallsForAnyNumberOfEntities();
}

TEST_CASE("structural changes during iteration are deferred", "[scripting]")
{
    StructuralChangesDuringIterationAreDeferred();
}

TEST_CASE("ForEach defers structural changes too", "[scripting]")
{
    ForEachDefersStructuralChangesToo();
}

TEST_CASE("empty and invalid queries behave", "[scripting]")
{
    EmptyAndInvalidQueriesBehave();
}

TEST_CASE("the widest query has eight components", "[scripting]")
{
    TheWidestQueryHasEightComponents();
}

TEST_CASE("systems run in phase and order, with changes applied after each", "[scripting]")
{
    SystemsRunInPhaseAndOrderAndChangesApplyAfterEachOne();
}

TEST_CASE("OnCreate runs after load and OnDestroy before release", "[scripting]")
{
    OnCreateRunsAfterLoadAndOnDestroyBeforeRelease();
}

TEST_CASE("a phase does nothing after Shutdown", "[scripting]")
{
    ASystemsCallsAfterShutdownDoNothing();
}

TEST_CASE("a C++ event is read in C#", "[scripting]")
{
    ACppEventIsReadInCSharp();
}

TEST_CASE("a C# reader made after its queue swapped starts at the oldest event held", "[scripting]")
{
    ACSharpReaderMadeAfterSwapsStartsAtTheOldestEventHeld();
}

TEST_CASE("a C# event is read in C++", "[scripting]")
{
    ACSharpEventIsReadInCpp();
}

TEST_CASE("a C#-only event needs no C++", "[scripting]")
{
    ACSharpOnlyEventNeedsNoCpp();
}

TEST_CASE("each reader sees each event once in both languages", "[scripting]")
{
    EachReaderSeesEachEventOnceInBothLanguages();
}

TEST_CASE("a system never sees its own sends and a view stays valid", "[scripting]")
{
    ASystemNeverSeesItsOwnSendsAndAViewStaysValid();
}

TEST_CASE("event lookup failures say what went wrong", "[scripting]")
{
    EventLookupFailuresSayWhatWentWrong();
}

TEST_CASE("a mismatched event fails the load and names the field", "[scripting]")
{
    AMismatchedEventFailsTheLoadAndNamesTheField();
}

TEST_CASE("C++ event users are unchanged by named and byte queues", "[scripting]")
{
    CppEventUsersAreUnchangedByNamedAndByteQueues();
}

TEST_CASE("a released generation unloads and its copy is deleted", "[scripting]")
{
    AReleasedGenerationUnloadsAndItsCopyIsDeleted();
}

TEST_CASE("a rebuilt assembly changes behaviour without a restart and keeps unchanged data", "[scripting]")
{
    ARebuiltAssemblyChangesBehaviourWithoutARestartAndKeepsUnchangedData();
}

TEST_CASE("a changed layout drops the component and the events, and nothing reads stale bytes", "[scripting]")
{
    AChangedLayoutDropsTheComponentAndTheEventsAndNothingReadsStaleBytes();
}

TEST_CASE("a broken build keeps the old generation running", "[scripting]")
{
    ABrokenBuildKeepsTheOldGenerationRunning();
}

TEST_CASE("repeated reloads leave no generations behind", "[scripting]")
{
    RepeatedReloadsLeaveNoGenerationsBehind();
}

TEST_CASE("the watcher reloads a stable build between frames", "[scripting]")
{
    TheWatcherReloadsAStableBuildBetweenFrames();
}

TEST_CASE("with hot reload off the watcher never acts but Reload still works", "[scripting]")
{
    WithHotReloadOffTheWatcherNeverActsButReloadStillWorks();
}

TEST_CASE("reloading with nothing loaded fails cleanly", "[scripting]")
{
    ReloadingWithNothingLoadedFailsCleanly();
}

TEST_CASE("a failed load leaves no components and no pools", "[scripting]")
{
    AFailedLoadLeavesNoComponentsAndNoPools();
}

TEST_CASE("a sound report loads and registers everything together", "[scripting]")
{
    ASoundReportLoadsAndRegistersEverythingTogether();
}

TEST_CASE("a duplicate script component is refused and leaves nothing behind", "[scripting]")
{
    ADuplicateScriptComponentIsRefused();
}

TEST_CASE("a duplicate script event is refused and leaves nothing behind", "[scripting]")
{
    ADuplicateScriptEventIsRefused();
}

TEST_CASE("a readonly field is read through Ref<T> in a C# system, in place", "[scripting]")
{
    AReadOnlyFieldIsReadThroughRefInPlace();
}

TEST_CASE("a native tagged union validates against its C# mirror", "[scripting]")
{
    ANativeTaggedUnionValidatesAgainstItsCSharpMirror();
}

TEST_CASE("a tagged union round-trips through a C# system", "[scripting]")
{
    ATaggedUnionRoundTripsThroughACSharpSystem();
}

TEST_CASE("reading a tagged union's view for the wrong kind throws", "[scripting]")
{
    ReadingATaggedUnionViewForTheWrongKindThrows();
}

TEST_CASE("a generated tagged union's message names its noun, or its type without one", "[scripting]")
{
    AGeneratedTaggedUnionsMessageNamesItsNounOrItsType();
}

TEST_CASE("a tagged-union mirror with a wrong offset or name is refused at load", "[scripting]")
{
    ATaggedUnionMirrorWithAWrongOffsetOrNameIsRefusedAtLoad();
}

TEST_CASE("a wrong field type is caught too", "[scripting]")
{
    AWrongFieldTypeIsCaughtToo();
}

TEST_CASE("a script cannot claim a native component with a struct of its own", "[scripting]")
{
    AScriptCannotClaimANativeComponentWithAStructOfItsOwn();
}

TEST_CASE("a native function from an extension gets C#'s arguments and its result reaches C#", "[scripting]")
{
    ANativeFunctionGetsCSharpArgumentsAndItsResultReachesCSharp();
}

TEST_CASE("a native function receives a string from C#", "[scripting]")
{
    ANativeFunctionReceivesAStringFromCSharp();
}

TEST_CASE("a lone surrogate in a string arrives as the replacement character", "[scripting]")
{
    ALoneSurrogateInAStringArrivesAsTheReplacementCharacter();
}

TEST_CASE("a native function reaches the active scene through its call context", "[scripting]")
{
    ANativeFunctionReachesTheActiveSceneThroughItsCallContext();
}

TEST_CASE("an extension's components and events are registered and checked against C#", "[scripting]")
{
    AnExtensionsComponentsAndEventsAreRegisteredAndCheckedAgainstCSharp();
}

TEST_CASE("a C# field with no native function fails Install and names it", "[scripting]")
{
    ACSharpFieldWithNoNativeFunctionFailsInstallAndNamesIt();
}

TEST_CASE("a native function whose signature differs from its C# field fails Install and names it", "[scripting]")
{
    ANativeFunctionWhoseSignatureDiffersFromItsFieldFailsInstallAndNamesIt();
}

TEST_CASE("two extensions declaring one native function fail Install and name both", "[scripting]")
{
    TwoExtensionsDeclaringOneNativeFunctionFailInstallAndNameBoth();
}

TEST_CASE("the binding name of a native function follows one rule", "[scripting]")
{
    TheBindingNameOfANativeFunctionFollowsOneRule();
}

TEST_CASE("the build deploys the bridge next to the executable", "[scripting]")
{
    TheBuildDeploysTheBridgeNextToTheExecutable();
}

TEST_CASE("no configured assembly does nothing and never starts .NET", "[scripting]")
{
    NoAssemblyConfiguredDoesNothing();
}

TEST_CASE("a scripting that is not live answers every call the same way", "[scripting]")
{
    ScriptingThatIsNotLiveAnswersEveryCallTheSameWay();
}

TEST_CASE("everything Iris owns is released when the app finishes", "[scripting]")
{
    ShutdownReleasesEverythingWhenTheAppFinishes();
}

TEST_CASE("destroying the owner releases everything, and a later install works", "[scripting]")
{
    DestroyingTheOwnerWithoutRunningReleasesEverything();
}

TEST_CASE("the engine components come with their rules", "[scripting]")
{
    TheEngineComponentsComeWithTheirRules();
}

TEST_CASE("an extension cannot take an engine component's name", "[scripting]")
{
    AnExtensionCannotTakeAnEngineComponentsName();
}

TEST_CASE("an Entity field written from C# reads correctly in C++", "[scripting]")
{
    AnEntityFieldWrittenFromCSharpReadsCorrectlyInCpp();
}

TEST_CASE("EntityLink.None is the empty link", "[scripting]")
{
    EntityLinkNoneIsTheEmptyLink();
}

TEST_CASE("a link to an entity created earlier in the same system reads back after playback", "[scripting]")
{
    ALinkToAnEntityCreatedInTheSameSystemReadsBackAfterPlayback();
}

TEST_CASE("a zeroed EntityLink names entity 0 with no scene and reads safely", "[scripting]")
{
    AZeroedLinkNamesEntityZeroWithNoSceneAndReadsSafely();
}

TEST_CASE("an EntityLink's owner is not readable from C#", "[scripting]")
{
    AnEntityLinksOwnerIsNotReadableFromCSharp();
}
