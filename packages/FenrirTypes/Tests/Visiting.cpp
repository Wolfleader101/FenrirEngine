#include "TestTypes.hpp"

#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "FenrirTypes/ValueRef.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <map>
#include <string>
#include <vector>

using Fenrir::ConstValueRef;
using Fenrir::FieldInfo;
using Fenrir::Kind;
using Fenrir::ValueRef;

namespace
{
    // One field a visitor was given: its name, kind and offset, as the description says.
    struct Seen
    {
        std::string name;
        Kind kind;
        std::size_t offset;

        friend bool operator==(const Seen&, const Seen&) = default;
    };

    // Records each field it is given, walking nested structs, and every f32 leaf's value by its dotted path.
    class RecordingVisitor final : public Fenrir::ConstFieldVisitor
    {
      public:
        std::vector<Seen> seen;
        std::map<std::string, float> floats;

        void Field(const FieldInfo& field, ConstValueRef value) override
        {
            seen.push_back({m_prefix + std::string(field.name), value.GetKind(), field.offset});
            if (const float* f = value.Get<float>())
                floats[m_prefix + std::string(field.name)] = *f;
            if (value.GetKind() == Kind::Struct)
            {
                const std::string outer = m_prefix;
                m_prefix += std::string(field.name) + ".";
                value.Visit(*this);
                m_prefix = outer;
            }
        }

      private:
        std::string m_prefix;
    };

    // Writes one f32 field of a nested struct, found by its outer and inner names.
    class WritingVisitor final : public Fenrir::FieldVisitor
    {
      public:
        WritingVisitor(std::string_view outer, std::string_view inner, float value)
            : m_outer(outer), m_inner(inner), m_value(value)
        {
        }

        void Field(const FieldInfo& field, ValueRef value) override
        {
            if (m_inOuter && field.name == m_inner)
                *value.Get<float>() = m_value;
            else if (!m_inOuter && field.name == m_outer)
            {
                m_inOuter = true;
                value.Visit(*this);
                m_inOuter = false;
            }
        }

      private:
        std::string_view m_outer;
        std::string_view m_inner;
        float m_value;
        bool m_inOuter = false;
    };
} // namespace

TEST_CASE("A fixed-layout struct's visit sees every field once with its kind, offset and value", "[types][visiting]")
{
    Fenrir::TypeRegistry registry;
    REQUIRE(registry.Register(TypesTest::TransformDescription));

    TypesTest::Transform transform;
    transform.position = {1.0f, 2.0f, 3.0f};
    transform.rotation = {0.5f, 0.25f, 0.125f, 0.0625f};
    transform.scale = {4.0f, 5.0f, 6.0f};

    RecordingVisitor visitor;
    const TypesTest::Transform& readOnly = transform;
    registry.Find("TypesTest::Transform")->Visit(&readOnly, visitor);

    // Offsets by hand: a Vec3 is 12 bytes and a Quat 16, all of 4-byte floats.
    const std::vector<Seen> expected = {
        {"position", Kind::Struct, 0}, {"position.x", Kind::F32, 0},   {"position.y", Kind::F32, 4},
        {"position.z", Kind::F32, 8},  {"rotation", Kind::Struct, 12}, {"rotation.w", Kind::F32, 0},
        {"rotation.x", Kind::F32, 4},  {"rotation.y", Kind::F32, 8},   {"rotation.z", Kind::F32, 12},
        {"scale", Kind::Struct, 28},   {"scale.x", Kind::F32, 0},      {"scale.y", Kind::F32, 4},
        {"scale.z", Kind::F32, 8},
    };
    INFO("each field and nested field is seen once, in storage order, with the kind and offset the struct has");
    REQUIRE(visitor.seen == expected);

    const std::map<std::string, float> values = {
        {"position.x", 1.0f},  {"position.y", 2.0f},   {"position.z", 3.0f},    {"rotation.w", 0.5f},
        {"rotation.x", 0.25f}, {"rotation.y", 0.125f}, {"rotation.z", 0.0625f}, {"scale.x", 4.0f},
        {"scale.y", 5.0f},     {"scale.z", 6.0f},
    };
    INFO("each value is the one the struct holds");
    REQUIRE(visitor.floats == values);
}

TEST_CASE("A mutable visit writes a field back into the instance", "[types][visiting]")
{
    TypesTest::Transform transform;

    WritingVisitor writer("scale", "y", 9.0f);
    TypesTest::TransformDescription.Visit(&transform, writer);

    INFO("the visited field took the written value");
    REQUIRE(transform.scale.y == 9.0f);

    INFO("its neighbours kept their defaults");
    REQUIRE(transform.scale.x == 1.0f);
    REQUIRE(transform.position.y == 0.0f);
}

TEST_CASE("A visit hands each field its own description", "[types][visiting]")
{
    struct Fields final : Fenrir::ConstFieldVisitor
    {
        std::vector<const FieldInfo*> fields;

        void Field(const FieldInfo& field, ConstValueRef) override
        {
            fields.push_back(&field);
        }
    };

    const TypesTest::Transform transform;
    Fields visitor;
    TypesTest::TransformDescription.Visit(&transform, visitor);

    INFO("the visitor is given the description's own fields, metadata and all");
    REQUIRE(visitor.fields.size() == 3);
    REQUIRE(visitor.fields[0] == &TypesTest::TransformFields[0]);
    REQUIRE(visitor.fields[0]->metadata.group == "Placement");
    REQUIRE(visitor.fields[2]->formerly.size() == 1);
    REQUIRE(visitor.fields[2]->metadata.range.has_value());
    REQUIRE(visitor.fields[2]->metadata.range->max == 100.0);
}

TEST_CASE("A struct holding every other fixed-layout kind visits each at its offset", "[types][visiting]")
{
    const TypesTest::Probe probe;
    RecordingVisitor visitor;
    TypesTest::ProbeDescription.Visit(&probe, visitor);

    // Offsets by hand: a u8 enum, a bool, char[8] (ending at 10), u32[3] aligned to 12, an 8-byte handle at 24, a
    // 16-byte entity aligned to 32, a char16_t at 48 and a double aligned to 56.
    const std::vector<Seen> expected = {
        {"mobility", Kind::Enum, 0}, {"visible", Kind::Bool, 1},    {"label", Kind::FixedString, 2},
        {"layers", Kind::Array, 12}, {"mesh", Kind::Reference, 24}, {"target", Kind::Entity, 32},
        {"key", Kind::Char16, 48},   {"weight", Kind::F64, 56},
    };
    INFO("each field is seen once with its kind and offset");
    REQUIRE(visitor.seen == expected);
}

TEST_CASE("A description constructs and destroys an instance in place", "[types][visiting]")
{
    alignas(TypesTest::Sword) std::byte storage[sizeof(TypesTest::Sword)];
    TypesTest::SwordDescription.Construct(storage);
    auto* sword = reinterpret_cast<TypesTest::Sword*>(storage);

    INFO("construction gives the type's defaults, its base's included");
    REQUIRE(sword->damage == 10.0f);
    REQUIRE(sword->gems.empty());
    REQUIRE_FALSE(sword->edge.has_value());

    sword->gems.resize(4);
    TypesTest::SwordDescription.Destroy(storage);
}
