// The registration tools/generate-type-registry writes from describe-api's fixture (Generated/FixtureTypes.*), compiled
// against the fixture header: what a package's generated registration gives the registry. The expected names, kinds,
// offsets and values are the fixture header's and its committed Fixture.api.json's.

#include "Generated/FixtureTypes.hpp"

#include "FenrirTypes/Describe.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/Kind.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using Fenrir::ConstValueRef;
using Fenrir::FieldInfo;
using Fenrir::Kind;
using Fenrir::Role;
using Fenrir::TypeDescription;
using Fenrir::TypeRegistry;

namespace
{
    // Every fixture type the registration describes: all but the tagged unions Shape and Sky, and Collider, which
    // holds a Shape.
    const std::vector<std::string_view> RegisteredNames = {
        "Fixture::Handle",   "Fixture::Limits",       "Fixture::Marker",      "Fixture::Motion",
        "Fixture::Offset",   "Fixture::Numbers",      "Fixture::Quat",        "Fixture::Spring",
        "Fixture::Vec3",     "Fixture::Body",         "Fixture::BodyId",      "Fixture::BoxShape",
        "Fixture::Clock",    "Fixture::ShapeType",    "Fixture::SphereShape", "Fixture::Columns",
        "Fixture::Contacts", "Fixture::Dial",         "Fixture::Folder",      "Fixture::Heading",
        "Fixture::Hit",      "Fixture::Pair<double>", "Fixture::Size",        "Fixture::Socket",
        "Fixture::Tag",      "Fixture::Ticket",       "Fixture::Touched",     "Fixture::Weather",
        "Fixture::Paths",    "Fixture::Stat",         "Fixture::WeaponData",  "Fixture::Sword",
    };

    // A registry holding the fixture's registration, and every diagnostic it reported.
    struct Registered
    {
        std::vector<std::string> diagnostics;
        TypeRegistry registry{[this](std::string_view message) { diagnostics.emplace_back(message); }};
        bool added = Fenrir::Generated::RegisterFixtureTypes(registry);

        // The type registered under `name`, which the registration must have added.
        const TypeDescription& Get(std::string_view name) const
        {
            const TypeDescription* description = registry.Find(name);
            INFO(name << " should be registered");
            REQUIRE(description != nullptr);
            return *description;
        }
    };

    // One field a visitor was given, as the description says it.
    struct Seen
    {
        std::string name;
        Kind kind;
        std::size_t offset;

        friend bool operator==(const Seen&, const Seen&) = default;
    };

    // Records each field it is given, without walking nested structs.
    class RecordingVisitor final : public Fenrir::ConstFieldVisitor
    {
      public:
        std::vector<Seen> seen;
        std::vector<ConstValueRef> values;

        void Field(const FieldInfo& field, ConstValueRef value) override
        {
            seen.push_back({std::string(field.name), value.GetKind(), field.offset});
            values.push_back(value);
        }
    };
} // namespace

TEST_CASE("The generated registration adds every fixture type, each found by its canonical name")
{
    Registered fixture;

    INFO("diagnostics: " << (fixture.diagnostics.empty() ? std::string("none") : fixture.diagnostics.front()));
    REQUIRE(fixture.added);
    REQUIRE(fixture.diagnostics.empty());
    for (std::string_view name : RegisteredNames)
    {
        INFO(name << " should be registered under its canonical name");
        const TypeDescription* description = fixture.registry.Find(name);
        REQUIRE(description != nullptr);
        REQUIRE(description->name == name);
        REQUIRE(description->package == "Fixture");
    }
}

TEST_CASE("The generated registration leaves out the types generate-type-registry cannot write yet")
{
    Registered fixture;

    for (std::string_view name : {"Fixture::Shape", "Fixture::Sky", "Fixture::Collider"})
    {
        INFO(name << " is a tagged union or holds one, which FenrirTypes does not describe yet, so is skipped");
        REQUIRE(fixture.registry.Find(name) == nullptr);
    }
}

TEST_CASE("A data asset extending another is visited base first, and is one over the chain")
{
    Registered fixture;
    Fixture::Sword sword;
    RecordingVisitor visitor;

    fixture.Get("Fixture::Sword").Visit(static_cast<const void*>(&sword), visitor);

    std::vector<std::string> names;
    for (const Seen& seen : visitor.seen)
        names.push_back(seen.name);
    REQUIRE(names == std::vector<std::string>{"displayName", "stats", "cooldown", "mesh", "damage", "reach", "combos",
                                              "titles"});
    REQUIRE(visitor.values.front().Text() == "Weapon");
    REQUIRE(fixture.Get("Fixture::Sword").base == "Fixture::WeaponData");
    REQUIRE(fixture.registry.IsA("Fixture::Sword", "Fixture::WeaponData"));
    REQUIRE_FALSE(fixture.registry.IsA("Fixture::WeaponData", "Fixture::Sword"));
}

TEST_CASE("Each type's role is the one its selection gives, and an enum's is the enum role")
{
    Registered fixture;

    INFO("roles follow the selection, and an enum is in the enum role");
    REQUIRE(fixture.Get("Fixture::Body").role == Role::Component);
    REQUIRE(fixture.Get("Fixture::Touched").role == Role::Event);
    REQUIRE(fixture.Get("Fixture::Weather").role == Role::SceneResource);
    REQUIRE(fixture.Get("Fixture::Clock").role == Role::AppResource);
    REQUIRE(fixture.Get("Fixture::Hit").role == Role::SharedStruct);
    REQUIRE(fixture.Get("Fixture::Vec3").role == Role::BoundType);
    REQUIRE(fixture.Get("Fixture::Spring").role == Role::Nested);
    REQUIRE(fixture.Get("Fixture::Motion").role == Role::Enum);
}

TEST_CASE("A type's @version is its version, and each @formerly name finds it")
{
    Registered fixture;

    INFO("Dial is at version 2 and is found by both of its former names; Body, with no @version, is at 1");
    const TypeDescription& dial = fixture.Get("Fixture::Dial");
    REQUIRE(dial.version == 2);
    REQUIRE(fixture.registry.Find("Fixture::Gauge") == &dial);
    REQUIRE(fixture.registry.Find("Legacy::Meter") == &dial);
    REQUIRE(fixture.Get("Fixture::Body").version == 1);
}

TEST_CASE("A type's @transient and @editor-only are its flags")
{
    Registered fixture;

    INFO("Contacts is @transient and Folder @editor-only; Body is neither");
    REQUIRE(fixture.Get("Fixture::Contacts").transient);
    REQUIRE(fixture.Get("Fixture::Folder").editorOnly);
    REQUIRE_FALSE(fixture.Get("Fixture::Body").transient);
    REQUIRE_FALSE(fixture.Get("Fixture::Body").editorOnly);
}

TEST_CASE("A type's doc text is compiled in, its paragraphs kept apart")
{
    Registered fixture;

    INFO("Numbers' doc comment has two paragraphs");
    REQUIRE(
        fixture.Get("Fixture::Numbers").doc ==
        "Every number kind, each with a literal default.\n\nA second paragraph, which is kept apart from the first.");
}

TEST_CASE("Visiting a fixed-layout component gives each field once with its kind, offset and value")
{
    Registered fixture;
    const TypeDescription& socket = fixture.Get("Fixture::Socket");
    alignas(Fixture::Socket) std::byte storage[sizeof(Fixture::Socket)];
    socket.Construct(storage);

    RecordingVisitor visitor;
    socket.Visit(static_cast<const void*>(storage), visitor);

    const std::vector<Seen> expected = {
        {"owner", Kind::Entity, 0},       {"target", Kind::Entity, 16}, {"label", Kind::FixedString, 32},
        {"title", Kind::FixedString, 64}, {"bytes", Kind::Array, 72},   {"mesh", Kind::Reference, 76},
        {"lod", Kind::Reference, 84},
    };
    INFO("Socket's fields, in storage order, with Fixture.api.json's kinds and offsets");
    REQUIRE(visitor.seen == expected);

    INFO("the values are the header's defaults, which Construct gives");
    REQUIRE(visitor.values[0].Entity() == Fenrir::EntityValue{});
    REQUIRE(visitor.values[2].Text() == "Socket");
    REQUIRE(visitor.values[3].Text().empty());
    REQUIRE(visitor.values[4].Size() == 4);
    REQUIRE(*visitor.values[4].Element(3).Get<std::uint8_t>() == 0);
    REQUIRE(visitor.values[5].Type().target == "Fenrir::Mesh");
    REQUIRE(visitor.values[5].Reference() == Fenrir::ReferenceValue{});
    socket.Destroy(storage);
}

TEST_CASE("Visiting a fixed-layout component reads the values an instance holds")
{
    Registered fixture;
    Fixture::Dial dial;
    dial.fill = 75.0f;
    dial.ticks = -3;

    RecordingVisitor visitor;
    fixture.Get("Fixture::Dial").Visit(static_cast<const void*>(&dial), visitor);

    const std::vector<Seen> expected = {
        {"fill", Kind::F32, 0},      {"ticks", Kind::I32, 4},   {"span", Kind::I32, 8},
        {"enabled", Kind::Bool, 12}, {"scaled", Kind::F32, 16},
    };
    INFO("Dial's fields, with Fixture.api.json's kinds and offsets");
    REQUIRE(visitor.seen == expected);
    INFO("the values are the instance's, and the defaults its header gives the rest");
    REQUIRE(*visitor.values[0].Get<float>() == 75.0f);
    REQUIRE(*visitor.values[1].Get<std::int32_t>() == -3);
    REQUIRE(*visitor.values[2].Get<std::int32_t>() == 8);
    REQUIRE(*visitor.values[3].Get<bool>());
    REQUIRE(*visitor.values[4].Get<float>() == 6.0f);
}

TEST_CASE("A field's doc, former names and metadata are the ones its doc commands give")
{
    Registered fixture;
    const FieldInfo& fill = fixture.Get("Fixture::Dial").fields[0];

    INFO("Dial::fill carries every metadata command and two former names");
    REQUIRE(fill.doc == "Every metadata command on one field, and two former names.");
    REQUIRE(fill.formerly.size() == 2);
    REQUIRE(fill.formerly[0] == "level");
    REQUIRE(fill.formerly[1] == "amount");
    REQUIRE(fill.metadata.range.has_value());
    REQUIRE(fill.metadata.range->min == 0.0);
    REQUIRE(fill.metadata.range->max == 100.0);
    REQUIRE(fill.metadata.step == 0.5);
    REQUIRE(fill.metadata.display == "Fill level");
    REQUIRE(fill.metadata.group == "Readout");
    REQUIRE(fill.metadata.units == "percent");
    REQUIRE(fill.metadata.readonly);
    REQUIRE(fill.metadata.hidden);
    REQUIRE(fill.metadata.multiline);

    INFO("Dial::ticks has only a range, its maximum fractional");
    const FieldInfo& ticks = fixture.Get("Fixture::Dial").fields[1];
    REQUIRE(ticks.metadata.range->min == -10.0);
    REQUIRE(ticks.metadata.range->max == 10.5);
    REQUIRE_FALSE(ticks.metadata.step.has_value());
    REQUIRE_FALSE(ticks.metadata.hidden);
}

TEST_CASE("A nested struct, an enum and an array of structs point at their own registered descriptions")
{
    Registered fixture;
    const TypeDescription& body = fixture.Get("Fixture::Body");

    INFO("Body::motion is an enum of Motion, Body::spring a struct of Spring");
    REQUIRE(body.fields[0].name == "motion");
    REQUIRE(body.fields[0].type.kind == Kind::Enum);
    REQUIRE(body.fields[0].type.type == fixture.registry.Find("Fixture::Motion"));
    REQUIRE(body.fields[5].name == "spring");
    REQUIRE(body.fields[5].type.kind == Kind::Struct);
    REQUIRE(body.fields[5].type.type == fixture.registry.Find("Fixture::Spring"));

    INFO("Body::springs is an array of two Springs at offset 240");
    const FieldInfo& springs = body.fields[15];
    REQUIRE(springs.name == "springs");
    REQUIRE(springs.offset == 240);
    REQUIRE(springs.type.kind == Kind::Array);
    REQUIRE(springs.type.count == 2);
    REQUIRE(springs.type.element->kind == Kind::Struct);
    REQUIRE(springs.type.element->type == fixture.registry.Find("Fixture::Spring"));

    INFO("visiting a Body's tuned Spring through its description gives the header's values");
    Fixture::Body instance;
    RecordingVisitor visitor;
    body.Visit(static_cast<const void*>(&instance), visitor);
    RecordingVisitor tuned;
    visitor.values[8].Visit(tuned);
    REQUIRE(tuned.seen == std::vector<Seen>{{"stiffness", Kind::F32, 0}, {"damping", Kind::F32, 4}});
    REQUIRE(*tuned.values[0].Get<float>() == 4.0f);
    REQUIRE(*tuned.values[1].Get<float>() == 0.5f);
}

TEST_CASE("A bound type is visited as its scalar components")
{
    Registered fixture;
    const Fixture::Vec3 vector(1.0f, 2.0f, 3.0f);

    RecordingVisitor visitor;
    fixture.Get("Fixture::Vec3").Visit(static_cast<const void*>(&vector), visitor);

    INFO("Vec3 is its three f32 components, x, y and z");
    REQUIRE(visitor.seen == std::vector<Seen>{{"x", Kind::F32, 0}, {"y", Kind::F32, 4}, {"z", Kind::F32, 8}});
    REQUIRE(*visitor.values[0].Get<float>() == 1.0f);
    REQUIRE(*visitor.values[2].Get<float>() == 3.0f);

    INFO("Columns' private array of Vec3s is six components named by path");
    const TypeDescription& columns = fixture.Get("Fixture::Columns");
    REQUIRE(columns.fields.size() == 6);
    REQUIRE(columns.fields[4].name == "m_value[1].y");
    REQUIRE(columns.fields[4].offset == 16);

    INFO("Quat's components are in storage order, w last");
    const Fixture::Quat quat(1.0f, 0.25f, 0.5f, 0.75f);
    RecordingVisitor quatVisitor;
    fixture.Get("Fixture::Quat").Visit(static_cast<const void*>(&quat), quatVisitor);
    REQUIRE(quatVisitor.seen.back() == Seen{"w", Kind::F32, 12});
    REQUIRE(*quatVisitor.values.back().Get<float>() == 1.0f);

    INFO("a bound type holding a pointer has no components");
    REQUIRE(fixture.Get("Fixture::Handle").fields.empty());
}

TEST_CASE("An enum's description holds its enumerators and underlying kind")
{
    Registered fixture;

    INFO("Motion is u8: Still, Driven and Simulated from 0, with their docs");
    const TypeDescription& motion = fixture.Get("Fixture::Motion");
    REQUIRE(motion.underlying == Kind::U8);
    REQUIRE(motion.enumerators.size() == 3);
    REQUIRE(motion.enumerators[0].name == "Still");
    REQUIRE(motion.enumerators[0].doc == "Never moves.");
    REQUIRE(motion.enumerators[2].name == "Simulated");
    REQUIRE(motion.enumerators[2].value == 2);

    INFO("Offset is i32, with a negative value and a large one");
    const TypeDescription& offset = fixture.Get("Fixture::Offset");
    REQUIRE(offset.underlying == Kind::I32);
    REQUIRE(offset.enumerators[0].name == "Back");
    REQUIRE(offset.enumerators[0].value == -2);
    REQUIRE(offset.enumerators[2].value == 0x40000000);

    INFO("an enumerator of an enum field reads back through the field");
    Fixture::Body body;
    RecordingVisitor visitor;
    fixture.Get("Fixture::Body").Visit(static_cast<const void*>(&body), visitor);
    REQUIRE(visitor.values[0].EnumValue() == 2);
}

TEST_CASE("Describe<T> is the description the registration added, by identity")
{
    Registered fixture;

    INFO("Describe<Fixture::Body> and the registry's Fixture::Body are one object");
    REQUIRE(&Fenrir::Describe<Fixture::Body>::Description() == fixture.registry.Find("Fixture::Body"));
    REQUIRE(&Fenrir::Describe<Fixture::Motion>::Description() == fixture.registry.Find("Fixture::Motion"));
    REQUIRE(&Fenrir::Describe<Fixture::Size>::Description() == fixture.registry.Find("Fixture::Size"));
    REQUIRE(&Fenrir::Describe<Fixture::Pair<double>>::Description() == fixture.registry.Find("Fixture::Pair<double>"));
    STATIC_REQUIRE(Fenrir::Described<Fixture::Dial>);
}

TEST_CASE("Construct gives a type's defaults and Destroy ends it")
{
    Registered fixture;
    const TypeDescription& hit = fixture.Get("Fixture::Hit");
    REQUIRE(hit.size == sizeof(Fixture::Hit));
    REQUIRE(hit.alignment == alignof(Fixture::Hit));
    alignas(Fixture::Hit) std::byte storage[sizeof(Fixture::Hit)];

    hit.Construct(storage);
    RecordingVisitor visitor;
    hit.Visit(static_cast<const void*>(storage), visitor);

    INFO("Hit's distance defaults to infinity, as its header says");
    REQUIRE(visitor.seen.back() == Seen{"distance", Kind::F32, 28});
    REQUIRE(*visitor.values.back().Get<float>() > 1.0e38f);
    hit.Destroy(storage);
}

TEST_CASE("Registering the fixture twice refuses every type the second time, naming the package")
{
    Registered fixture;

    REQUIRE_FALSE(Fenrir::Generated::RegisterFixtureTypes(fixture.registry));

    INFO("each type is refused as a duplicate, and the diagnostic names the fixture package");
    REQUIRE(fixture.diagnostics.size() == RegisteredNames.size());
    REQUIRE(fixture.diagnostics.front().find("Fixture") != std::string::npos);
}
