#include "TestTypes.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

using Fenrir::ConstValueRef;
using Fenrir::Kind;
using Fenrir::ValueRef;

namespace
{
    // A value reference to the probe's field named `name`, at the offset its description gives.
    ValueRef ProbeField(TypesTest::Probe& probe, std::string_view name)
    {
        const Fenrir::FieldInfo& field = TypesTest::FieldNamed(TypesTest::ProbeFields, name);
        return ValueRef(reinterpret_cast<std::byte*>(&probe) + field.offset, field.type);
    }

    // A value reference to one of a sword's members, typed by the sword's or its base's field of that name.
    ValueRef SwordField(void* member, std::string_view name)
    {
        for (auto fields : {std::span<const Fenrir::FieldInfo>(TypesTest::SwordFields),
                            std::span<const Fenrir::FieldInfo>(TypesTest::WeaponDataFields)})
            for (const Fenrir::FieldInfo& field : fields)
                if (field.name == name)
                    return ValueRef(member, field.type);
        throw std::logic_error("no such field");
    }
} // namespace

TEST_CASE("A scalar reads and writes as its C++ type", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef weight = ProbeField(probe, "weight");

    INFO("an f64 reads as a double holding the field's value");
    REQUIRE(weight.Get<double>() != nullptr);
    REQUIRE(*weight.Get<double>() == 2.5);

    *weight.Get<double>() = 7.0;
    INFO("writing through it writes the field");
    REQUIRE(probe.weight == 7.0);
}

TEST_CASE("A scalar reads as no C++ type but its own", "[types][values]")
{
    TypesTest::Probe probe;

    INFO("an f64 is not a float");
    REQUIRE(ProbeField(probe, "weight").Get<float>() == nullptr);

    INFO("a char16 is a char16_t, not a u16");
    REQUIRE(*ProbeField(probe, "key").Get<char16_t>() == u'K');
    REQUIRE(ProbeField(probe, "key").Get<std::uint16_t>() == nullptr);

    INFO("a bool is a bool, not a u8");
    REQUIRE(*ProbeField(probe, "visible").Get<bool>());
    REQUIRE(ProbeField(probe, "visible").Get<std::uint8_t>() == nullptr);
}

TEST_CASE("An enum reads and writes at its underlying width", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef mobility = ProbeField(probe, "mobility");

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
    const ValueRef label = ProbeField(probe, "label");

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
    const ValueRef label = ProbeField(probe, "label");

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
    const ValueRef layers = ProbeField(probe, "layers");

    INFO("the array has its three elements");
    REQUIRE(layers.Size() == 3);
    REQUIRE(*layers.Element(0).Get<std::uint32_t>() == 1);
    REQUIRE(*layers.Element(2).Get<std::uint32_t>() == 3);

    *layers.Element(1).Get<std::uint32_t>() = 20;
    INFO("an element written through its reference is written in place");
    REQUIRE(probe.layers[1] == 20);
}

TEST_CASE("A reference reads and writes the handle's index and generation", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef mesh = ProbeField(probe, "mesh");

    mesh.SetReference({.index = 3, .generation = 9});
    INFO("the reference's index and generation are the handle's");
    REQUIRE(probe.mesh.index == 3);
    REQUIRE(probe.mesh.generation == 9);
    REQUIRE(mesh.Reference() == Fenrir::ReferenceValue{3, 9});

    INFO("the reference names its target type");
    REQUIRE(mesh.Type().target == "TypesTest::Mesh");
}

TEST_CASE("An entity reads and writes its id and its entity list", "[types][values]")
{
    TypesTest::Probe probe;
    const ValueRef target = ProbeField(probe, "target");

    int list = 0;
    target.SetEntity({.id = 12, .list = &list});
    INFO("the entity's id and list are the entity's");
    REQUIRE(probe.target.id == 12);
    REQUIRE(probe.target.list == &list);
    REQUIRE(target.Entity() == Fenrir::EntityValue{12, &list});
}

TEST_CASE("An empty entity value is the null entity, its id all bits set", "[types][values]")
{
    const Fenrir::EntityValue empty;

    INFO("the null entity's id is Entity::Null's, all bits set, and it has no entity list");
    REQUIRE(empty.id == 0xFFFFFFFFu);
    REQUIRE(empty.list == nullptr);
}

TEST_CASE("A string holds UTF-8 text", "[types][values]")
{
    TypesTest::Sword sword;
    const ValueRef name = SwordField(&sword.name, "name");

    name.SetText("Gram \xE2\x9A\x94");
    INFO("a string holds the text written, and reads it back");
    REQUIRE(sword.name == "Gram \xE2\x9A\x94");
    REQUIRE(name.Text() == sword.name);
}

TEST_CASE("A path is written from UTF-8 text and reads back in its generic form", "[types][values]")
{
    TypesTest::Sword sword;
    const ValueRef icon = SwordField(&sword.icon, "icon");

    icon.SetText("icons/gram \xC3\xA9.png");
    INFO("the path holds the code points of the UTF-8 written");
    REQUIRE(sword.icon == std::filesystem::path(L"icons/gram \x00e9.png"));

    INFO("it reads back as the same UTF-8");
    REQUIRE(icon.Text() == "icons/gram \xC3\xA9.png");
}

TEST_CASE("A list of nested structs resizes and walks its elements", "[types][values]")
{
    TypesTest::Sword sword;
    const ValueRef gems = SwordField(&sword.gems, "gems");

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

TEST_CASE("An optional emplaces a value to write", "[types][values]")
{
    TypesTest::Sword sword;
    const ValueRef edge = SwordField(&sword.edge, "edge");

    INFO("a new optional holds nothing");
    REQUIRE_FALSE(edge.HasValue());

    *edge.Emplace().Get<float>() = 0.75f;
    INFO("emplacing gives a value to write, which it then holds");
    REQUIRE(sword.edge == 0.75f);
    REQUIRE(edge.HasValue());
    REQUIRE(*edge.Value().Get<float>() == 0.75f);
}

TEST_CASE("An optional resets to empty", "[types][values]")
{
    TypesTest::Sword sword;
    sword.edge = 0.5f;
    const ValueRef edge = SwordField(&sword.edge, "edge");

    edge.Reset();
    INFO("resetting empties it");
    REQUIRE_FALSE(sword.edge.has_value());
    REQUIRE_FALSE(edge.HasValue());
}

TEST_CASE("A read-only value reference reads what the mutable one does", "[types][values]")
{
    TypesTest::Sword sword;
    sword.gems.resize(3);
    std::memcpy(sword.gems[2].colour, "blue", 5);
    sword.edge = 2.0f;

    const ConstValueRef gems = SwordField(&sword.gems, "gems");
    const ConstValueRef edge = SwordField(&sword.edge, "edge");

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
    const ValueRef weight = ProbeField(probe, "weight");

    weight.SetText("hello");
    weight.SetEnumValue(3);
    weight.Resize(4);
    weight.SetEntity({.id = 1});

    INFO("an f64 has no text, no elements, no value and no enum value, and reads as the null entity");
    REQUIRE(weight.Text().empty());
    REQUIRE(weight.Size() == 0);
    REQUIRE_FALSE(weight.HasValue());
    REQUIRE(weight.EnumValue() == 0);
    REQUIRE(weight.Description() == nullptr);
    REQUIRE(weight.Entity() == Fenrir::EntityValue{});

    INFO("and writing those wrote nothing");
    REQUIRE(probe.weight == 2.5);
}
