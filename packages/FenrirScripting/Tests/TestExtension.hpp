#pragma once

// The native side of what the scripting tests register from outside FenrirScripting: the components and events a
// test app gives scripting (TestNative, each mapped to a C# struct of the fixture, some deliberately wrongly) and the
// test-only scripting extension (TestExtension), written as an engine package would write its own. The natives'
// definitions are in TestExtension.cpp.

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/Generated/FenrirScriptingMappings.hpp"
#include "FenrirScripting/Scripting.hpp"
#include "FenrirScripting/WarnOnceRecord.hpp"
#include "FenrirScripting/ZeroingOutputs.hpp"

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

// A component with an Entity field, as a joint names its bodies, mapped to the fixture's Tether. Entity maps to the
// contract's EntityLink through the built-in binding's generated mapping.
namespace TestNative
{
    struct Tether
    {
        Fenrir::Entity target;
        std::int32_t length = 0;
    };
} // namespace TestNative

template <>
struct Iris::StructMapping<TestNative::Tether>
{
    static constexpr std::string_view typeName = "Fenrir.ScriptFixture.Tether";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(TestNative::Tether, target, "Target"),
                                                      IRIS_FIELD_AS(TestNative::Tether, length, "Length"));
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

    std::int32_t Add(const Fenrir::CallContext&, std::int32_t left, std::int32_t right);

    std::uint32_t EchoEntity(const Fenrir::CallContext&, std::uint32_t entity);

    // The entity's Transform height in the active scene, or -1 if it has none.
    float HeightOf(const Fenrir::CallContext& context, std::uint32_t entity);

    void Lift(const Fenrir::CallContext& context, std::uint32_t entity, float by);

    // The last text Label received, so a test can compare it with what C# sent.
    extern std::string labelled;

    std::int32_t Label(const Fenrir::CallContext&, std::string_view text, std::int32_t number);

    // Declared for the broken contract's Mismatched field, which takes an int: this takes a float.
    std::int32_t Mismatched(const Fenrir::CallContext&, float);

    // The test extension's native resources: a scene resource, an app resource and a read-only app resource.
    struct Weather
    {
        float Wind = 0.5F;
        std::int32_t Gusts = 0;
    };

    struct Settings
    {
        std::int32_t Volume = 11;
    };

    struct Clock
    {
        double Seconds = 0.0;
    };

    // A wrong mapping of the C# Weather: its second field is a float where C# has an int.
    struct BadWeather
    {
        float Wind = 0.0F;
        float Gusts = 0.0F;
    };

    // Creates an entity through the change buffer and gives it a Charge in the same call: it is usable at once.
    std::uint32_t SpawnCharged(const Fenrir::CallContext& context, std::int32_t level);

    void Rearrange(const Fenrir::CallContext& context, std::uint32_t kept, std::uint32_t doomed, float wind);

    // Creates an entity and records parenting it under `parent`; a dead parent records no parenting.
    std::uint32_t SpawnUnder(const Fenrir::CallContext& context, std::uint32_t parent);

    // The test extension's extension state: a running total its Tally native adds to, and its warn-once record, as a
    // package's state holds one.
    struct Tally
    {
        std::int32_t total = 0;
        Fenrir::WarnOnceRecord warnings{"test extension"};
    };

    // A state of another type, which Tally does not take.
    struct Other
    {
    };

    std::int32_t AddToTally(const Fenrir::CallContext&, Tally& tally, std::int32_t by);

    // Raises the entity's Transform by one, behind the native guard: a dead entity warns once under the native's name,
    // which this function never spells.
    void Nudge(const Fenrir::CallContext& context, Tally& tally, std::uint32_t entity);

    // The test extension's shared struct, and a mapping of its C# mirror that has drifted: its first field renamed.
    struct Reading
    {
        std::int32_t Count;
        float Weight;
    };

    /**
     * @brief One reading of the entity (a count of one and its height), registered through ZeroingOutputs so
     * `reading` is zeroed before this runs: a dead entity returns zero readings and leaves it so.
     * @param[out] reading The reading.
     * @return How many readings: one, or zero for a dead entity.
     * @notrunning -1
     */
    std::int32_t Weigh(const Fenrir::CallContext& context, std::uint32_t entity, Reading* reading);
    using ZeroingWeigh = Fenrir::ZeroingOutputs<&Weigh, 2>;

    struct DriftedReading
    {
        std::int32_t Amount;
        float Weight;
    };

    // The C# Heading, which a native takes and returns by value as its underlying byte.
    enum class Heading : std::uint8_t
    {
        North = 1,
        East = 2,
        South = 3,
        West = 4
    };

    // The C# Tag: a wrapper (see its Iris::WrapperOf below), which a native takes and returns by value as its field.
    struct Tag
    {
        std::uint16_t code;
    };

    Heading TurnRight(const Fenrir::CallContext&, Heading heading);

    Tag Retag(const Fenrir::CallContext&, Tag tag, Heading by);
} // namespace TestExtension

template <>
struct Iris::WrapperOf<TestExtension::Tag>
{
    using Value = std::uint16_t;
    static TestExtension::Tag From(Value code)
    {
        return TestExtension::Tag{code};
    }
    static Value To(TestExtension::Tag tag)
    {
        return tag.code;
    }
};

template <>
struct Iris::StructMapping<TestExtension::Reading>
{
    static constexpr std::string_view typeName = "Fenrir.TestExtension.Reading";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestExtension::Reading, Count), IRIS_FIELD(TestExtension::Reading, Weight));
};

template <>
struct Iris::StructMapping<TestExtension::DriftedReading>
{
    static constexpr std::string_view typeName = "Fenrir.TestExtension.Reading";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(TestExtension::DriftedReading, Amount),
                                                      IRIS_FIELD(TestExtension::DriftedReading, Weight));
};

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

template <>
struct Iris::StructMapping<TestExtension::Weather>
{
    static constexpr std::string_view typeName = "Fenrir.TestExtension.Weather";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestExtension::Weather, Wind), IRIS_FIELD(TestExtension::Weather, Gusts));
};

template <>
struct Iris::StructMapping<TestExtension::Settings>
{
    static constexpr std::string_view typeName = "Fenrir.TestExtension.Settings";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(TestExtension::Settings, Volume));
};

template <>
struct Iris::StructMapping<TestExtension::Clock>
{
    static constexpr std::string_view typeName = "Fenrir.TestExtension.Clock";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(TestExtension::Clock, Seconds));
};

template <>
struct Iris::StructMapping<TestExtension::BadWeather>
{
    static constexpr std::string_view typeName = "Fenrir.TestExtension.Weather";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(TestExtension::BadWeather, Wind), IRIS_FIELD(TestExtension::BadWeather, Gusts));
};

namespace Fenrir::Testing::ScriptFixture
{
    // The test extension, as an engine package would describe what it gives scripting, carrying `tally` as its
    // extension state (none if it is null).
    ScriptingExtension TheTestExtension(
        std::shared_ptr<TestExtension::Tally> tally = std::make_shared<TestExtension::Tally>());

    // The config for the script assembly that uses the test extension, with `extensions` installed.
    ScriptingConfig ExtensionConfig(std::vector<ScriptingExtension> extensions);
} // namespace Fenrir::Testing::ScriptFixture
