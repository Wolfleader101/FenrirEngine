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
    // Every fixture type the registration describes.
    const std::vector<std::string_view> RegisteredNames = {
        "Fixture::Handle",   "Fixture::Limits",       "Fixture::Marker",      "Fixture::Motion",
        "Fixture::Offset",   "Fixture::Numbers",      "Fixture::Quat",        "Fixture::Spring",
        "Fixture::Vec3",     "Fixture::Body",         "Fixture::BodyId",      "Fixture::BoxShape",
        "Fixture::Clock",    "Fixture::ShapeType",    "Fixture::SphereShape", "Fixture::Columns",
        "Fixture::Contacts", "Fixture::Dial",         "Fixture::Folder",      "Fixture::Heading",
        "Fixture::Hit",      "Fixture::Pair<double>", "Fixture::Size",        "Fixture::Socket",
        "Fixture::Tag",      "Fixture::Ticket",       "Fixture::Touched",     "Fixture::Weather",
        "Fixture::Paths",    "Fixture::Stat",         "Fixture::WeaponData",  "Fixture::Sword",
        "Fixture::Shape",    "Fixture::Collider",     "Fixture::Sky",
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

TEST_CASE("A read-only visit of a data asset extending another gives its base's fields first")
{
    Registered fixture;
    const Fixture::Sword sword;
    RecordingVisitor visitor;

    fixture.Get("Fixture::Sword").Visit(static_cast<const void*>(&sword), visitor);

    std::vector<std::string> names;
    for (const Seen& seen : visitor.seen)
        names.push_back(seen.name);
    INFO("WeaponData's five fields should come before Sword's three, through the read-only visit");
    REQUIRE(names == std::vector<std::string>{"displayName", "stats", "cooldown", "mesh", "damage", "reach", "combos",
                                              "titles"});
    INFO("the base's string should read as its default, the header's literal");
    REQUIRE(visitor.values.front().Text() == "Weapon");
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
        {"enabled", Kind::Bool, 12}, {"scaled", Kind::F32, 16}, {"reading", Kind::F32, 20},
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

TEST_CASE("A component's transient field is in its layout and says it is transient")
{
    Registered fixture;
    const TypeDescription& dial = fixture.Get("Fixture::Dial");

    INFO("Dial::reading, marked @transient, is still one of Dial's fields, at its offset");
    REQUIRE(dial.fields.size() == 6);
    REQUIRE(dial.fields[5].name == "reading");
    REQUIRE(dial.fields[5].offset == 20);
    INFO("and its FieldInfo says it is transient, where Dial::fill's does not");
    REQUIRE(dial.fields[5].transient);
    REQUIRE_FALSE(dial.fields[0].transient);
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
    INFO("Hit's description records the compiler's size and alignment");
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

TEST_CASE("Registering the fixture twice registers nothing the second time, with no diagnostic")
{
    Registered fixture;
    const TypeDescription* sword = fixture.registry.Find("Fixture::Sword");

    INFO("a second run of the fixture's registration reports success");
    REQUIRE(Fenrir::Generated::RegisterFixtureTypes(fixture.registry));

    INFO("every type the second run meets is already registered as the same description, so it is left alone");
    REQUIRE(fixture.diagnostics.empty());
    REQUIRE(fixture.registry.Find("Fixture::Sword") == sword);
}

TEST_CASE("A tagged union's description holds its tag, union offset, alternatives and default alternative")
{
    Registered fixture;
    const TypeDescription& shape = fixture.Get("Fixture::Shape");

    INFO("Shape is a tagged union: an m_type tag of ShapeType at 0, its union at 4");
    REQUIRE(shape.IsTaggedUnion());
    REQUIRE(shape.role == Role::Nested);
    REQUIRE(shape.tag.name == "m_type");
    REQUIRE(shape.tag.offset == 0);
    REQUIRE(shape.tag.type.kind == Kind::Enum);
    REQUIRE(shape.tag.type.type == fixture.registry.Find("Fixture::ShapeType"));
    REQUIRE(shape.unionOffset == 4);

    INFO("its alternatives are m_box for Box (0) and m_sphere for Sphere (1), each a struct at the union's offset");
    REQUIRE(shape.alternatives.size() == 2);
    REQUIRE(shape.alternatives[0].field.name == "m_box");
    REQUIRE(shape.alternatives[0].tag == 0);
    REQUIRE(shape.alternatives[0].field.type.kind == Kind::Struct);
    REQUIRE(shape.alternatives[0].field.type.type == fixture.registry.Find("Fixture::BoxShape"));
    REQUIRE(shape.alternatives[0].field.offset == 4);
    REQUIRE(shape.alternatives[0].field.doc == "Held when the tag is Box.");
    REQUIRE(shape.alternatives[1].field.name == "m_sphere");
    REQUIRE(shape.alternatives[1].tag == 1);
    REQUIRE(shape.alternatives[1].field.type.type == fixture.registry.Find("Fixture::SphereShape"));

    INFO("its default alternative is m_box, which the default constructor holds");
    REQUIRE(shape.defaultAlternative == 0);
}

TEST_CASE("A struct holding a tagged union is no tagged union itself")
{
    Registered fixture;

    INFO("Collider holds a Shape, but is a struct of fields with no alternatives");
    const TypeDescription& collider = fixture.Get("Fixture::Collider");
    REQUIRE_FALSE(collider.IsTaggedUnion());
    REQUIRE(collider.fields.size() == 2);
}

TEST_CASE("A tagged union in an app resource registers in its role")
{
    Registered fixture;

    INFO("Sky is an app resource and a tagged union");
    const TypeDescription& sky = fixture.Get("Fixture::Sky");
    REQUIRE(sky.role == Role::AppResource);
    REQUIRE(sky.IsTaggedUnion());
    REQUIRE(sky.alternatives.size() == 2);
}

TEST_CASE("Visiting a box shape gives the tag, then the box alone")
{
    Registered fixture;
    const Fixture::Shape box(Fixture::BoxShape{Fixture::Vec3(2.0f)});

    RecordingVisitor visitor;
    fixture.Get("Fixture::Shape").Visit(static_cast<const void*>(&box), visitor);

    INFO("the tag first, then m_box at the union's offset; m_sphere is never pushed");
    REQUIRE(visitor.seen == std::vector<Seen>{{"m_type", Kind::Enum, 0}, {"m_box", Kind::Struct, 4}});
    REQUIRE(visitor.values[0].EnumValue() == 0);

    INFO("the box's half extents are the instance's");
    RecordingVisitor inner;
    visitor.values[1].Visit(inner);
    RecordingVisitor extents;
    inner.values[0].Visit(extents);
    REQUIRE(*extents.values[0].Get<float>() == 2.0f);
}

TEST_CASE("Visiting a sphere shape gives the tag, then the sphere alone")
{
    Registered fixture;
    const Fixture::Shape sphere(Fixture::SphereShape{3.0f});

    RecordingVisitor visitor;
    fixture.Get("Fixture::Shape").Visit(static_cast<const void*>(&sphere), visitor);

    INFO("the tag first, then m_sphere at the union's offset; m_box is never pushed");
    REQUIRE(visitor.seen == std::vector<Seen>{{"m_type", Kind::Enum, 0}, {"m_sphere", Kind::Struct, 4}});
    REQUIRE(visitor.values[0].EnumValue() == 1);

    INFO("the sphere's radius is the instance's");
    RecordingVisitor inner;
    visitor.values[1].Visit(inner);
    REQUIRE(inner.seen == std::vector<Seen>{{"radius", Kind::F32, 0}});
    REQUIRE(*inner.values[0].Get<float>() == 3.0f);
}

TEST_CASE("A component holding a tagged union reaches its live alternative through the nested visit")
{
    Registered fixture;
    Fixture::Collider collider;
    collider.shape = Fixture::Shape(Fixture::SphereShape{4.0f});

    RecordingVisitor visitor;
    fixture.Get("Fixture::Collider").Visit(static_cast<const void*>(&collider), visitor);
    INFO("Collider's visit sees its tagged-union shape at 0 and solid at 16");
    REQUIRE(visitor.seen == std::vector<Seen>{{"shape", Kind::Struct, 0}, {"solid", Kind::Bool, 16}});
    RecordingVisitor shape;
    visitor.values[0].Visit(shape);

    INFO("Collider::shape, a struct, is visited as its tag and its sphere");
    REQUIRE(shape.seen == std::vector<Seen>{{"m_type", Kind::Enum, 0}, {"m_sphere", Kind::Struct, 4}});
}

TEST_CASE("Construct gives a tagged union its default alternative")
{
    Registered fixture;
    const TypeDescription& shape = fixture.Get("Fixture::Shape");
    alignas(Fixture::Shape) std::byte storage[sizeof(Fixture::Shape)];

    shape.Construct(storage);

    INFO("the default constructor holds a box, the default alternative");
    REQUIRE(shape.LiveAlternative(storage) == &shape.alternatives[shape.defaultAlternative]);
    REQUIRE(reinterpret_cast<const Fixture::Shape*>(storage)->Type() == Fixture::ShapeType::Box);
    shape.Destroy(storage);
}

TEST_CASE("Selecting Sphere on a box writes the tag and a sphere with its default radius")
{
    Registered fixture;
    const TypeDescription& description = fixture.Get("Fixture::Shape");
    Fixture::Shape shape(Fixture::BoxShape{Fixture::Vec3(7.0f)});

    INFO("selecting the Sphere alternative on a box succeeds");
    REQUIRE(description.SelectAlternative(&shape, 1));

    INFO("the tag says Sphere, and the sphere is the only alternative visited");
    REQUIRE(shape.Type() == Fixture::ShapeType::Sphere);
    RecordingVisitor visitor;
    description.Visit(static_cast<const void*>(&shape), visitor);
    REQUIRE(visitor.seen == std::vector<Seen>{{"m_type", Kind::Enum, 0}, {"m_sphere", Kind::Struct, 4}});

    INFO("the sphere's radius is SphereShape's own default, 0.5, not the box's bytes");
    RecordingVisitor sphere;
    visitor.values[1].Visit(sphere);
    REQUIRE(*sphere.values[0].Get<float>() == 0.5f);
}

TEST_CASE("Selecting a tag value no alternative has changes nothing")
{
    Registered fixture;
    const TypeDescription& description = fixture.Get("Fixture::Shape");
    Fixture::Shape shape(Fixture::SphereShape{3.0f});

    INFO("ShapeType has no value 9, so nothing is selected and the sphere stays");
    REQUIRE_FALSE(description.SelectAlternative(&shape, 9));
    REQUIRE(shape.Type() == Fixture::ShapeType::Sphere);
}

TEST_CASE("A value reference to a tagged union selects its alternative through the description")
{
    Registered fixture;
    const TypeDescription& description = fixture.Get("Fixture::Shape");
    Fixture::Shape shape(Fixture::BoxShape{Fixture::Vec3(7.0f)});
    const Fenrir::ValueType type{.kind = Kind::Struct, .type = &description};

    INFO("a reader holding only a reference to a nested union, as a field visitor gives it, selects Sphere");
    REQUIRE(Fenrir::ValueRef(&shape, type).SelectAlternative(1));
    REQUIRE(shape.Type() == Fixture::ShapeType::Sphere);
}

TEST_CASE("A value reference to a struct that is no tagged union selects nothing")
{
    Registered fixture;
    Fixture::Collider collider;
    collider.solid = false;
    const Fenrir::ValueType type{.kind = Kind::Struct, .type = &fixture.Get("Fixture::Collider")};

    INFO("Collider has no alternatives, so nothing is selected or written");
    REQUIRE_FALSE(Fenrir::ValueRef(&collider, type).SelectAlternative(0));
    REQUIRE_FALSE(collider.solid);
}

TEST_CASE("A value reference to a scalar selects nothing")
{
    float number = 2.0f;
    const Fenrir::ValueType f32{.kind = Kind::F32};

    INFO("a float has no description, so nothing is selected or written");
    REQUIRE_FALSE(Fenrir::ValueRef(&number, f32).SelectAlternative(0));
    REQUIRE(number == 2.0f);
}

TEST_CASE("Selecting an alternative of a struct, which has none, changes nothing")
{
    Registered fixture;
    Fixture::Collider collider;
    collider.solid = false;

    INFO("Collider is a struct, so no tag value selects anything in it");
    REQUIRE_FALSE(fixture.Get("Fixture::Collider").SelectAlternative(&collider, 0));
    REQUIRE_FALSE(collider.solid);
}

namespace
{
    // Tries to write each enum field it is given to `value`, recording whether each write was taken.
    class EnumWriter final : public Fenrir::FieldVisitor
    {
      public:
        explicit EnumWriter(std::int64_t value) : m_value(value)
        {
        }

        std::vector<bool> written;

        void Field(const FieldInfo&, Fenrir::ValueRef value) override
        {
            if (value.GetKind() == Kind::Enum)
                written.push_back(value.SetEnumValue(m_value));
        }

      private:
        std::int64_t m_value;
    };
} // namespace

TEST_CASE("Writing a tagged union's tag through a value reference is refused")
{
    Registered fixture;
    Fixture::Shape shape(Fixture::BoxShape{Fixture::Vec3(2.0f)});

    EnumWriter writer(1);
    fixture.Get("Fixture::Shape").Visit(static_cast<void*>(&shape), writer);

    INFO("the tag's write is refused, so the shape is still a box");
    REQUIRE(writer.written == std::vector<bool>{false});
    REQUIRE(shape.Type() == Fixture::ShapeType::Box);
}

TEST_CASE("Writing an enum field that is no tag through a value reference is taken")
{
    Registered fixture;
    Fixture::Body body;

    EnumWriter writer(0);
    fixture.Get("Fixture::Body").Visit(static_cast<void*>(&body), writer);

    INFO("Body::motion is a plain enum field, so its write is taken and it becomes Still");
    REQUIRE(writer.written.front());
    REQUIRE(body.motion == Fixture::Motion::Still);
}
