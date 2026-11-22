// PhysicsConfig as a described settings type (ADR-0084): its generated visitor, reached through Describe<T> with no
// App, walks its collision layers as a list of nested CollisionLayer values.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Generated/FenrirPhysicsTypes.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirTypes/FieldVisitor.hpp"
#include "FenrirTypes/ValueRef.hpp"

using namespace Fenrir;

namespace
{
    // One value the visit reached: its path from the config, its kind, and its text or list size where it has one.
    struct Reached
    {
        std::string path;
        Kind kind;
        std::string text;
        std::size_t size = 0;
    };

    // Records every value it is given, walking lists' elements and nested structs by path (`collisionLayers[1].name`).
    class RecordingVisitor final : public ConstFieldVisitor
    {
      public:
        std::vector<Reached> reached;

        void Field(const FieldInfo& field, ConstValueRef value) override
        {
            Reach(m_prefix + std::string(field.name), value);
        }

      private:
        void Reach(const std::string& path, ConstValueRef value)
        {
            Reached entry{.path = path, .kind = value.GetKind()};
            if (value.GetKind() == Kind::String)
                entry.text = value.Text();
            if (value.GetKind() == Kind::List)
                entry.size = value.Size();
            reached.push_back(entry);

            if (value.GetKind() == Kind::List)
            {
                for (std::size_t i = 0; i < value.Size(); ++i)
                    Reach(path + "[" + std::to_string(i) + "]", value.Element(i));
            }
            else if (value.GetKind() == Kind::Struct)
            {
                const std::string outer = m_prefix;
                m_prefix = path + ".";
                value.Visit(*this);
                m_prefix = outer;
            }
        }

        std::string m_prefix;
    };

    const Reached* Find(const std::vector<Reached>& reached, const std::string& path)
    {
        for (const Reached& entry : reached)
        {
            if (entry.path == path)
                return &entry;
        }
        return nullptr;
    }
} // namespace

TEST_CASE("A PhysicsConfig's visit reaches its collision layers as a list of nested CollisionLayers with string names",
          "[physics][types]")
{
    PhysicsConfig config;
    config.collisionLayers = {{.name = "Ground", .mask = 0b10u}, {.name = "Player", .mask = 0b01u}};

    RecordingVisitor visitor;
    Describe<PhysicsConfig>::Description().Visit(&config, visitor);

    const Reached* layers = Find(visitor.reached, "collisionLayers");
    INFO("the visit should reach collisionLayers");
    REQUIRE(layers != nullptr);
    INFO("collisionLayers should be a list, not kind " << static_cast<int>(layers->kind));
    REQUIRE(layers->kind == Kind::List);
    INFO("collisionLayers should hold the two layers set, not " << layers->size);
    REQUIRE(layers->size == 2);

    for (const auto& [index, name] : {std::pair{0, "Ground"}, std::pair{1, "Player"}})
    {
        const std::string element = "collisionLayers[" + std::to_string(index) + "]";
        const Reached* layer = Find(visitor.reached, element);
        INFO("the visit should reach " << element);
        REQUIRE(layer != nullptr);
        INFO(element << " should be a nested CollisionLayer, not kind " << static_cast<int>(layer->kind));
        REQUIRE(layer->kind == Kind::Struct);

        const Reached* layerName = Find(visitor.reached, element + ".name");
        INFO("the visit should reach " << element << ".name");
        REQUIRE(layerName != nullptr);
        INFO(element << ".name should be a string, not kind " << static_cast<int>(layerName->kind));
        REQUIRE(layerName->kind == Kind::String);
        INFO(element << ".name should be " << name << ", not " << layerName->text);
        REQUIRE(layerName->text == name);
    }
}
