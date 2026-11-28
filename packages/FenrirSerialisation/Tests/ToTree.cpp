// ToTree (the spec, "The bridge"): a described value written as a value tree through its description's visitor, each
// kind in its own form, fields in declaration order and every field equal to its default left out. Each test changes
// one field of a pair and leaves its partner at its default, so the tree it expects holds the changed field alone.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "BridgeTypes.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

using namespace Fenrir::Serialisation;
using namespace BridgeTest;

namespace
{
    // The tree of `value`, written against a registry holding the bridge's test types.
    Value Tree(const void* value, const Fenrir::TypeDescription& type)
    {
        Fenrir::TypeRegistry registry;
        RegisterBridgeTypes(registry);
        return ToTree(value, type, registry, Resolvers{});
    }

    Value Tree(const Fixed& fixed)
    {
        return Tree(&fixed, FixedDescription);
    }

    Value Tree(const Owned& owned)
    {
        return Tree(&owned, OwnedDescription);
    }

    // An object holding the members given, in order.
    Value ObjectOf(std::initializer_list<std::pair<const char*, Value>> members)
    {
        Object object;
        for (const auto& [key, value] : members)
            object.Add(key, value);
        return object;
    }

    Value ArrayOf(std::initializer_list<Value> elements)
    {
        Array array;
        for (const Value& element : elements)
            array.Add(element);
        return array;
    }
} // namespace

TEST_CASE("The bridge's test types register without a refusal", "[serialisation][totree]")
{
    Fenrir::TypeRegistry registry;

    INFO("every hand-written description is one the registry accepts, so the tests write registered types");
    REQUIRE(RegisterBridgeTypes(registry));
}

TEST_CASE("A value whose every field is at its default writes an empty object", "[serialisation][totree]")
{
    INFO("every field of a constructed Fixed equals its default, so none is written");
    REQUIRE(Tree(Fixed{}) == Value(Object{}));
}

TEST_CASE("Fields are written in declaration order", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.selected = true;
    fixed.score = 9;
    fixed.drag = 0.5f;

    INFO("score, drag and selected are declared in that order, whatever order they were set in");
    REQUIRE(Tree(fixed) == ObjectOf({{"score", std::int64_t{9}}, {"drag", 0.5f}, {"selected", true}}));
}

TEST_CASE("Signed integers write as signed numbers, the one at its default left out", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.score = std::int64_t{1} << 40;

    INFO("score is an i64 off its default, written signed; lives is at its default 3");
    REQUIRE(Tree(fixed) == ObjectOf({{"score", Number(std::int64_t{1} << 40)}}));
}

TEST_CASE("Unsigned integers write as unsigned numbers, the one at its default left out", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.team = 200;

    INFO("team is a u8 off its default, written unsigned; seed is at its default 7");
    REQUIRE(Tree(fixed) == ObjectOf({{"team", Number(std::uint64_t{200})}}));
}

TEST_CASE("Single floats write as single-width numbers, the one at its default left out", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.drag = 0.25f;

    INFO("drag is an f32 off its default, written single; speed is at its default 1.5");
    REQUIRE(Tree(fixed) == ObjectOf({{"drag", Number(0.25f)}}));
}

TEST_CASE("Double floats write as double-width numbers, the one at its default left out", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.charge = -1.25;

    INFO("charge is an f64 off its default, written double; mass is at its default 2");
    REQUIRE(Tree(fixed) == ObjectOf({{"charge", Number(-1.25)}}));
}

TEST_CASE("A float field holding Infinity writes Infinity", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.drag = std::numeric_limits<float>::infinity();

    INFO("an infinite f32 is written as itself, which the writer prints as Infinity");
    REQUIRE(Tree(fixed) == ObjectOf({{"drag", std::numeric_limits<float>::infinity()}}));
}

TEST_CASE("Bools write as bools, the one at its default left out", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.selected = true;

    INFO("selected is off its default false; visible is at its default true");
    REQUIRE(Tree(fixed) == ObjectOf({{"selected", true}}));
}

TEST_CASE("A char16 writes as an unsigned number holding its code unit", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.altKey = u'Z';

    INFO("altKey holds 'Z', code unit 90; key is at its default 'K'");
    REQUIRE(Tree(fixed) == ObjectOf({{"altKey", Number(std::uint64_t{90})}}));
}

TEST_CASE("An enum writes as its enumerator's name, the one at its default left out", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.fallback = Mobility::Movable;

    INFO("fallback is Movable; mobility is at its default Static");
    REQUIRE(Tree(fixed) == ObjectOf({{"fallback", "Movable"}}));
}

TEST_CASE("An enum value no enumerator has writes as its number", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.fallback = static_cast<Mobility>(3);

    INFO("Mobility has no enumerator 3, so the value is written as the number itself");
    REQUIRE(Tree(fixed) == ObjectOf({{"fallback", Number(std::int64_t{3})}}));
}

TEST_CASE("A bound type writes as an array of its components, the one at its default left out",
          "[serialisation][totree]")
{
    Fixed fixed;
    fixed.position = Vec3{0.0f, 1.8f, 0.0f};

    INFO("position is [0, 1.8, 0], every component written; scale is at its default [1, 1, 1]");
    REQUIRE(Tree(fixed) == ObjectOf({{"position", ArrayOf({0.0f, 1.8f, 0.0f})}}));
}

TEST_CASE("A nested struct writes as an object of the fields off their defaults", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.limits.max = 5.0f;

    INFO("limits.max is off its default and limits.min is not; range is at its default");
    REQUIRE(Tree(fixed) == ObjectOf({{"limits", ObjectOf({{"max", 5.0f}})}}));
}

TEST_CASE("Written against another value, a field equal to that value's is left out and one off it is written, though "
          "at the type's default",
          "[serialisation][totree]")
{
    Fixed source;
    source.lives = 5;
    source.limits.max = 5.0f;
    source.speed = 2.0f;
    Fixed live = source;
    live.lives = 3;
    live.limits.min = -1.0f;

    Fenrir::TypeRegistry registry;
    RegisterBridgeTypes(registry);
    const Value tree = ToTree(&live, FixedDescription, registry, Resolvers{}, nullptr, &source);

    INFO("lives is back at the type's default 3, still written; limits.min alone differs; speed and limits.max match");
    REQUIRE(tree == ObjectOf({{"lives", Number(std::int64_t{3})}, {"limits", ObjectOf({{"min", -1.0f}})}}));
}

TEST_CASE("A fixed array writes as an array of its elements, the one at its default left out",
          "[serialisation][totree]")
{
    Fixed fixed;
    fixed.masks[0] = 4;

    INFO("masks is [4, 0], every element written; layers is at its default [1, 2, 3]");
    REQUIRE(Tree(fixed) == ObjectOf({{"masks", ArrayOf({Number(std::uint64_t{4}), Number(std::uint64_t{0})})}}));
}

TEST_CASE("A fixed string writes as a string, the one at its default left out", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.title[0] = 'S';
    fixed.title[1] = 'u';
    fixed.title[2] = 'n';

    INFO("title holds Sun; label is at its default probe");
    REQUIRE(Tree(fixed) == ObjectOf({{"title", "Sun"}}));
}

TEST_CASE("A reference off its default writes null with no writeReference", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.mesh = HandleLayout{1, 1};

    INFO("mesh is a live handle, written null without a resolver; material is the empty handle, its default");
    REQUIRE(Tree(fixed) == ObjectOf({{"mesh", nullptr}}));
}

TEST_CASE("An entity off its default writes null with no writeEntity", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.target = EntityLayout{4, nullptr};

    INFO("target is entity 4, written null without a resolver; owner is the null entity, its default");
    REQUIRE(Tree(fixed) == ObjectOf({{"target", nullptr}}));
}

TEST_CASE("A pointer is never written", "[serialisation][totree]")
{
    Fixed fixed;
    int somewhere = 0;
    fixed.cache = &somewhere;

    INFO("cache is off its default null, but a pointer's bytes mean nothing in a file");
    REQUIRE(Tree(fixed) == Value(Object{}));
}

TEST_CASE("A tagged union writes kind, then the fields of its live alternative alone", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.shape.kind = ShapeKind::Sphere;
    fixed.shape.sphere = SphereShape{2.5f};

    INFO("the sphere is live with radius 2.5: kind first, then radius, and nothing of the box");
    REQUIRE(Tree(fixed) == ObjectOf({{"shape", ObjectOf({{"kind", "Sphere"}, {"radius", 2.5f}})}}));
}

TEST_CASE("A tagged union's alternative leaves out its fields at the alternative's defaults", "[serialisation][totree]")
{
    Fixed fixed;
    fixed.shape.kind = ShapeKind::Sphere;
    fixed.shape.sphere = SphereShape{};

    INFO("the sphere is live at its own default radius 1, so only kind is written");
    REQUIRE(Tree(fixed) == ObjectOf({{"shape", ObjectOf({{"kind", "Sphere"}})}}));
}

TEST_CASE("A tagged union's default alternative writes kind and its fields off their defaults",
          "[serialisation][totree]")
{
    Fixed fixed;
    fixed.shape.box.halfExtents.x = 2.0f;

    INFO("the box is live, as by default, with halfExtents changed: kind is written, so a reader knows the box");
    REQUIRE(Tree(fixed) ==
            ObjectOf({{"shape", ObjectOf({{"kind", "Box"}, {"halfExtents", ArrayOf({2.0f, 0.5f, 0.5f})}})}}));
}

TEST_CASE("A string writes as a string, the one at its default left out", "[serialisation][totree]")
{
    Owned owned;
    owned.name = "Iron";

    INFO("name is Iron; motto is at its default Onward");
    REQUIRE(Tree(owned) == ObjectOf({{"name", "Iron"}}));
}

TEST_CASE("A string emptied from a non-empty default writes the empty string", "[serialisation][totree]")
{
    Owned owned;
    owned.motto.clear();

    INFO("motto's default is Onward, so an empty motto is off its default and written");
    REQUIRE(Tree(owned) == ObjectOf({{"motto", ""}}));
}

TEST_CASE("A path writes as a string in its generic form, the one at its default left out", "[serialisation][totree]")
{
    Owned owned;
    owned.icon = std::filesystem::path("fx") / "spark.png";

    INFO("icon is fx/spark.png, with forward slashes on every platform; sound is at its default");
    REQUIRE(Tree(owned) == ObjectOf({{"icon", "fx/spark.png"}}));
}

TEST_CASE("A list writes as an array of its elements, the one at its default left out", "[serialisation][totree]")
{
    Owned owned;
    owned.tags = {3, 1, 2};

    INFO("tags is [3, 1, 2] in its own order; ranges is at its default, empty");
    REQUIRE(Tree(owned) ==
            ObjectOf({{"tags", ArrayOf({Number(std::int64_t{3}), Number(std::int64_t{1}), Number(std::int64_t{2})})}}));
}

TEST_CASE("A list of structs leaves out each element's fields at the element type's defaults",
          "[serialisation][totree]")
{
    Owned owned;
    owned.ranges = {Range{0.0f, 5.0f}, Range{}};

    INFO("the first range differs only in max; the second is a default range, written as an empty object");
    REQUIRE(Tree(owned) == ObjectOf({{"ranges", ArrayOf({ObjectOf({{"max", 5.0f}}), Value(Object{})})}}));
}

TEST_CASE("A disengaged optional whose default is disengaged is left out", "[serialisation][totree]")
{
    Owned owned;
    owned.edge.reset();

    INFO("edge is disengaged, as its default is, so it is not written");
    REQUIRE(Tree(owned) == Value(Object{}));
}

TEST_CASE("A disengaged optional whose default holds a value writes null", "[serialisation][totree]")
{
    Owned owned;
    owned.reach.reset();

    INFO("reach's default holds 2, so left out it would read back as 2: null is what reads as disengaged");
    REQUIRE(Tree(owned) == ObjectOf({{"reach", nullptr}}));
}

TEST_CASE("An engaged optional writes as its value, the one at its default left out", "[serialisation][totree]")
{
    Owned owned;
    owned.edge = 0.75f;

    INFO("edge holds 0.75 and is written as the number; reach holds its default 2");
    REQUIRE(Tree(owned) == ObjectOf({{"edge", 0.75f}}));
}

TEST_CASE("A transient field is never written", "[serialisation][totree]")
{
    Sensed sensed;
    sensed.speed = 3;
    sensed.state.max = 9.0f;

    INFO("speed differs from its default and is written; state, transient, is left out though it differs too");
    REQUIRE(Tree(&sensed, SensedDescription) == ObjectOf({{"speed", std::int64_t{3}}}));
}

TEST_CASE("A struct differing from its default only in a transient field is left out", "[serialisation][totree]")
{
    Sensor sensor;
    sensor.sensed.state.min = -2.0f;

    INFO("sensed differs only in its transient state, so it compares equal to its default and is not written");
    REQUIRE(Tree(&sensor, SensorDescription) == Value(Object{}));
}
