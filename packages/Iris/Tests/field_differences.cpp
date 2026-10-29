// Iris::FieldDifferences: two values of a mapped struct compared field by field through the mapping, with no bridge
// and no managed build. The expected lines are worked out from the structs' layouts below.
#include "Iris/FieldDifferences.hpp"
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace Fixture
{
    enum class Gear : std::int32_t
    {
        Off = 0,
        On = 1,
        Auto = 2
    };

    struct Vec2
    {
        float X;
        float Y;
    };

    // A byte followed by padding, so its three padding bytes sit at offsets 1 to 3.
    struct Reading
    {
        std::uint8_t Flags;
        std::int32_t Count;
        Vec2 Position;
        float Samples[3];
        Gear Mode;
    };

    enum class ShapeKind : std::uint8_t
    {
        Circle = 1,
        Box = 2
    };

    struct Circle
    {
        float Radius;
    };

    struct Box
    {
        Vec2 Extent;
    };

    // A tagged union: the tag first, then the kinds over the same bytes.
    struct Shape
    {
        ShapeKind Kind;
        union {
            Circle AsCircle;
            Box AsBox;
        };
    };
} // namespace Fixture

template <>
struct Iris::EnumMapping<Fixture::Gear>
{
    static constexpr std::string_view typeName = "Fixture.Gear";
};

template <>
struct Iris::StructMapping<Fixture::Vec2>
{
    static constexpr std::string_view typeName = "Fixture.Vec2";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fixture::Vec2, X), IRIS_FIELD(Fixture::Vec2, Y));
};

template <>
struct Iris::StructMapping<Fixture::Reading>
{
    static constexpr std::string_view typeName = "Fixture.Reading";
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::Reading, Flags), IRIS_FIELD(Fixture::Reading, Count),
                           IRIS_FIELD(Fixture::Reading, Position), IRIS_FIELD(Fixture::Reading, Samples),
                           IRIS_FIELD(Fixture::Reading, Mode));
};

template <>
struct Iris::EnumMapping<Fixture::ShapeKind>
{
    static constexpr std::string_view typeName = "Fixture.ShapeKind";
};

template <>
struct Iris::StructMapping<Fixture::Circle>
{
    static constexpr std::string_view typeName = "Fixture.Circle";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fixture::Circle, Radius));
};

template <>
struct Iris::StructMapping<Fixture::Box>
{
    static constexpr std::string_view typeName = "Fixture.Box";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fixture::Box, Extent));
};

template <>
struct Iris::StructMapping<Fixture::Shape>
{
    static constexpr std::string_view typeName = "Fixture.Shape";
    // The kinds are named by offset: an anonymous union's members cannot be named through the enclosing struct.
    static constexpr auto fields =
        Iris::StructFields(IRIS_FIELD(Fixture::Shape, Kind), Iris::Field<Fixture::Circle>("AsCircle", sizeof(float)),
                           Iris::Field<Fixture::Box>("AsBox", sizeof(float)));
};

namespace
{
    using Fixture::Gear;
    using Fixture::Reading;
    using Fixture::Shape;
    using Fixture::ShapeKind;

    // Which kind each tag selects: the binding's knowledge, handed to the comparison.
    const std::vector<Iris::UnionLayout> shapeUnion = {
        {"Fixture.Shape", {{1u, 1}, {2u, 2}}},
    };

    Reading SampleReading()
    {
        return Reading{
            .Flags = 3, .Count = 42, .Position = {1.0f, 2.0f}, .Samples = {0.5f, 1.5f, 2.5f}, .Mode = Gear::On};
    }

    std::string Joined(const std::vector<std::string>& lines)
    {
        std::string text;
        for (const std::string& line : lines)
            text += "\n  " + line;
        return text;
    }
} // namespace

TEST_CASE("equal values have no field differences", "[differences]")
{
    const Reading left = SampleReading();
    const Reading right = SampleReading();

    const auto differences = Iris::FieldDifferences(left, right);

    INFO("Two equal values should differ in no field, but these were reported:" << Joined(differences));
    REQUIRE(differences.empty());
}

TEST_CASE("one changed scalar is one difference naming its path and offset", "[differences]")
{
    const Reading left = SampleReading();
    Reading right = SampleReading();
    right.Position.Y = 0.0f;

    const auto differences = Iris::FieldDifferences(left, right);

    INFO("One changed field should be one difference, but these were reported:" << Joined(differences));
    REQUIRE(differences.size() == 1);
    // Position is at 8 (Flags, padding, Count), and Y four bytes into it; 2.0f is 0x40000000 little-endian.
    INFO("The difference should name the field's path from the type name, its offset and both sides' bytes");
    REQUIRE(differences[0] == "Fixture.Reading.Position.Y at offset 12: left 00000040, right 00000000");
}

TEST_CASE("a changed array element and a changed enum are named by index and field", "[differences]")
{
    const Reading left = SampleReading();
    Reading right = SampleReading();
    right.Samples[2] = 0.0f;
    right.Mode = Gear::Auto;

    const auto differences = Iris::FieldDifferences(left, right);

    INFO("The element and the enum should each be one difference, but these were reported:" << Joined(differences));
    REQUIRE(differences.size() == 2);
    // Samples starts at 16, its third element at 24; Mode follows the array at 28.
    REQUIRE(differences[0] == "Fixture.Reading.Samples[2] at offset 24: left 00002040, right 00000000");
    REQUIRE(differences[1] == "Fixture.Reading.Mode at offset 28: left 01000000, right 02000000");
}

TEST_CASE("padding bytes that differ are no difference", "[differences]")
{
    static_assert(offsetof(Reading, Count) == 4, "Flags is followed by three padding bytes");
    Reading left = SampleReading();
    Reading right = SampleReading();
    std::memset(reinterpret_cast<std::byte*>(&left) + 1, 0xAA, 3);
    std::memset(reinterpret_cast<std::byte*>(&right) + 1, 0x55, 3);

    const auto differences = Iris::FieldDifferences(left, right);

    INFO("Padding is not a field, so it should not be compared, but these were reported:" << Joined(differences));
    REQUIRE(differences.empty());
}

TEST_CASE("of a tagged union only the tag and the kind it selects are compared", "[differences]")
{
    Shape left{};
    Shape right{};
    left.Kind = ShapeKind::Circle;
    right.Kind = ShapeKind::Circle;
    // The box's Y lies past the circle's radius, so with Circle selected it is not compared.
    left.AsBox.Extent = {1.0f, 2.0f};
    right.AsBox.Extent = {1.0f, 3.0f};

    const auto differences = Iris::FieldDifferences(left, right, shapeUnion);

    INFO("Only the selected kind should be compared, but these were reported:" << Joined(differences));
    REQUIRE(differences.empty());

    right.AsBox.Extent.X = 0.0f;
    const auto radius = Iris::FieldDifferences(left, right, shapeUnion);
    INFO("The selected kind's bytes should be compared once, under its own name:" << Joined(radius));
    REQUIRE(radius.size() == 1);
    REQUIRE(radius[0] == "Fixture.Shape.AsCircle.Radius at offset 4: left 0000803f, right 00000000");
}

TEST_CASE("a tagged union's tag is compared, and the left value's tag selects the kind", "[differences]")
{
    Shape left{};
    Shape right{};
    left.Kind = ShapeKind::Box;
    right.Kind = ShapeKind::Circle;
    left.AsBox.Extent = {1.0f, 2.0f};
    right.AsBox.Extent = {1.0f, 2.0f};

    const auto differences = Iris::FieldDifferences(left, right, shapeUnion);

    INFO("The tags differ and the box's bytes do not, so only the tag should be reported:" << Joined(differences));
    REQUIRE(differences.size() == 1);
    REQUIRE(differences[0] == "Fixture.Shape.Kind at offset 0: left 02, right 01");
}
