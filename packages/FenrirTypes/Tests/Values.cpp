#include "TestTypes.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

using Fenrir::ConstValueRef;
using Fenrir::Kind;
using Fenrir::ValueRef;

namespace
{
    // A value reference to the field of `instance` that `field` describes, at its offset.
    template <typename T>
    ValueRef FieldOf(T& instance, const Fenrir::FieldInfo& field)
    {
        return ValueRef(reinterpret_cast<std::byte*>(&instance) + field.offset, field.type);
    }
} // namespace

TEST_CASE("A scalar reads and writes as its C++ type, and as no other", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef weight = FieldOf(probe, TypesTest::ProbeFields[7]);

    INFO("an f64 reads as a double");
    REQUIRE(weight.Get<double>() != nullptr);
    REQUIRE(*weight.Get<double>() == 2.5);

    *weight.Get<double>() = 7.0;
    INFO("writing through it writes the field");
    REQUIRE(probe.weight == 7.0);

    INFO("an f64 is not a float");
    REQUIRE(weight.Get<float>() == nullptr);

    const ValueRef key = FieldOf(probe, TypesTest::ProbeFields[6]);
    INFO("a char16 reads as a char16_t");
    REQUIRE(*key.Get<char16_t>() == u'K');

    const ValueRef visible = FieldOf(probe, TypesTest::ProbeFields[1]);
    INFO("a bool reads as a bool");
    REQUIRE(*visible.Get<bool>());
}

TEST_CASE("An enum reads and writes at its underlying width", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef mobility = FieldOf(probe, TypesTest::ProbeFields[0]);

    INFO("Movable reads as its value, 7");
    REQUIRE(mobility.EnumValue() == 7);

    INFO("its description carries the enumerators");
    REQUIRE(mobility.Description() == &TypesTest::MobilityDescription);
    REQUIRE(mobility.Description()->enumerators[1].name == "Movable");

    mobility.SetEnumValue(0);
    INFO("writing 0 makes it Static");
    REQUIRE(probe.mobility == TypesTest::Mobility::Static);

    INFO("writing wrote one byte, leaving the bool beside it");
    REQUIRE(probe.visible);
}

TEST_CASE("A fixed string reads as text up to its terminator", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef label = FieldOf(probe, TypesTest::ProbeFields[2]);

    INFO("the label reads as the text it holds");
    REQUIRE(label.Text() == "probe");

    label.SetText("abc");
    INFO("shorter text is written with its terminator, the rest cleared");
    REQUIRE(std::string(probe.label) == "abc");
    REQUIRE(probe.label[4] == '\0');
}

TEST_CASE("A fixed string cuts text that does not fit at N-1 bytes, on a UTF-8 boundary", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef label = FieldOf(probe, TypesTest::ProbeFields[2]);

    label.SetText("abcdefghij");
    INFO("ten ASCII bytes cut to seven, the eighth the terminator");
    REQUIRE(label.Text() == "abcdefg");

    // "abcdef" then U+00E9 (two bytes, C3 A9): the 7-byte cut would split it, so it ends before it.
    label.SetText("abcdef\xC3\xA9");
    INFO("a code point the cut would split is left out whole");
    REQUIRE(label.Text() == "abcdef");

    // "abcde" then U+00E9 fits in seven bytes exactly.
    label.SetText("abcde\xC3\xA9xyz");
    INFO("a code point that fits is kept");
    REQUIRE(label.Text() == "abcde\xC3\xA9");
}

TEST_CASE("A fixed array reads its elements and its size", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef layers = FieldOf(probe, TypesTest::ProbeFields[3]);

    INFO("the array has its three elements");
    REQUIRE(layers.Size() == 3);
    REQUIRE(*layers.Element(0).Get<std::uint32_t>() == 1);
    REQUIRE(*layers.Element(2).Get<std::uint32_t>() == 3);

    *layers.Element(1).Get<std::uint32_t>() = 20;
    INFO("an element written through its reference is written in place");
    REQUIRE(probe.layers[1] == 20);
}

TEST_CASE("A reference and an entity read and write their two fields", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef mesh = FieldOf(probe, TypesTest::ProbeFields[4]);
    const ValueRef target = FieldOf(probe, TypesTest::ProbeFields[5]);

    mesh.SetReference({.index = 3, .generation = 9});
    INFO("the reference's index and generation are the handle's");
    REQUIRE(probe.mesh.index == 3);
    REQUIRE(probe.mesh.generation == 9);
    REQUIRE(mesh.Reference() == Fenrir::ReferenceValue{3, 9});

    INFO("the reference names its target type");
    REQUIRE(mesh.Type().target == "TypesTest::Mesh");

    int list = 0;
    target.SetEntity({.id = 12, .list = &list});
    INFO("the entity's id and list are the entity's");
    REQUIRE(probe.target.id == 12);
    REQUIRE(probe.target.list == &list);
    REQUIRE(target.Entity() == Fenrir::EntityValue{12, &list});
}

TEST_CASE("A string and a path read and write as UTF-8 text", "[types][values]")
{
    TypesTest::Sword sword;
    const ValueRef name(&sword.name, TypesTest::WeaponDataFields[0].type);
    const ValueRef icon(&sword.icon, TypesTest::SwordFields[2].type);

    name.SetText("Gram \xE2\x9A\x94");
    INFO("a string holds the text written");
    REQUIRE(sword.name == "Gram \xE2\x9A\x94");
    REQUIRE(name.Text() == sword.name);

    icon.SetText("icons/gram \xC3\xA9.png");
    INFO("a path holds the UTF-8 text written, and reads back in its generic form");
    REQUIRE(sword.icon == std::filesystem::path(L"icons/gram \x00e9.png"));
    REQUIRE(icon.Text() == "icons/gram \xC3\xA9.png");
}

TEST_CASE("A list of nested structs resizes and walks its elements", "[types][values]")
{
    TypesTest::Sword sword;
    const ValueRef gems(&sword.gems, TypesTest::SwordFields[0].type);

    INFO("a new list is empty");
    REQUIRE(gems.Size() == 0);

    gems.Resize(2);
    INFO("resizing gives value-initialised elements");
    REQUIRE(sword.gems.size() == 2);
    REQUIRE(sword.gems[1].carats == 1);

    struct GemWriter final : Fenrir::FieldVisitor
    {
        void Field(const Fenrir::FieldInfo& field, ValueRef value) override
        {
            if (field.name == "colour")
                value.SetText("red");
            else if (std::int32_t* carats = value.Get<std::int32_t>())
                *carats = 5;
        }
    } writer;

    const ValueRef second = gems.Element(1);
    INFO("an element is a struct of the element type");
    REQUIRE(second.GetKind() == Kind::Struct);
    REQUIRE(second.Description() == &TypesTest::GemDescription);

    second.Visit(writer);
    INFO("visiting the element writes that element only");
    REQUIRE(std::string(sword.gems[1].colour) == "red");
    REQUIRE(sword.gems[1].carats == 5);
    REQUIRE(std::string(sword.gems[0].colour).empty());
}

TEST_CASE("An optional reports its presence, emplaces and resets", "[types][values]")
{
    TypesTest::Sword sword;
    const ValueRef edge(&sword.edge, TypesTest::SwordFields[1].type);

    INFO("a new optional holds nothing");
    REQUIRE_FALSE(edge.HasValue());

    *edge.Emplace().Get<float>() = 0.75f;
    INFO("emplacing gives a value to write");
    REQUIRE(sword.edge == 0.75f);
    REQUIRE(edge.HasValue());
    REQUIRE(*edge.Value().Get<float>() == 0.75f);

    edge.Reset();
    INFO("resetting empties it");
    REQUIRE_FALSE(sword.edge.has_value());
}

TEST_CASE("A read-only value reference reads what the mutable one does", "[types][values]")
{
    TypesTest::Sword sword;
    sword.gems.resize(3);
    std::memcpy(sword.gems[2].colour, "blue", 5);
    sword.edge = 2.0f;

    const ConstValueRef gems(&sword.gems, TypesTest::SwordFields[0].type);
    const ConstValueRef edge(&sword.edge, TypesTest::SwordFields[1].type);

    struct ColourReader final : Fenrir::ConstFieldVisitor
    {
        std::string colour;

        void Field(const Fenrir::FieldInfo& field, ConstValueRef value) override
        {
            if (field.name == "colour")
                colour = value.Text();
        }
    } reader;
    gems.Element(2).Visit(reader);

    INFO("a list's size and elements read through a read-only reference");
    REQUIRE(gems.Size() == 3);
    REQUIRE(reader.colour == "blue");

    INFO("an optional's presence and value read through a read-only reference");
    REQUIRE(edge.HasValue());
    REQUIRE(*edge.Value().Get<float>() == 2.0f);
}

TEST_CASE("A call meant for another kind reads as empty and writes nothing", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef weight = FieldOf(probe, TypesTest::ProbeFields[7]);

    weight.SetText("hello");
    weight.SetEnumValue(3);
    weight.Resize(4);

    INFO("an f64 has no text, no elements, no value and no enum value");
    REQUIRE(weight.Text().empty());
    REQUIRE(weight.Size() == 0);
    REQUIRE_FALSE(weight.HasValue());
    REQUIRE(weight.EnumValue() == 0);
    REQUIRE(weight.Description() == nullptr);

    INFO("and writing those wrote nothing");
    REQUIRE(probe.weight == 2.5);
}
