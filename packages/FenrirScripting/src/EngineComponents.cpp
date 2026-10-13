#include "ComponentRegistry.hpp"

#include <cstddef>
#include <cstdint>

#include "FenrirECS/DefaultComponents.hpp"

// How the engine's own components look to C#. Each mapping is checked against the C# struct in the Fenrir.Scripting
// contract whenever a script assembly loads, so a drift between the two fails the load and names the field.

template <>
struct Iris::StructMapping<glm::vec3>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Vector3";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(glm::vec3, x, "X"), IRIS_FIELD_AS(glm::vec3, y, "Y"),
                                                      IRIS_FIELD_AS(glm::vec3, z, "Z"));
};

template <>
struct Iris::StructMapping<glm::quat>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Quaternion";
    // glm keeps w first in memory, and the C# Quaternion declares its fields in that order (fields are listed in
    // declaration order, and each offset is checked).
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(glm::quat, w, "W"), IRIS_FIELD_AS(glm::quat, x, "X"),
                                                      IRIS_FIELD_AS(glm::quat, y, "Y"), IRIS_FIELD_AS(glm::quat, z, "Z"));
};

template <>
struct Iris::StructMapping<Fenrir::Transform>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Transform";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Transform, pos, "Position"),
                                                      IRIS_FIELD_AS(Fenrir::Transform, rot, "Rotation"),
                                                      IRIS_FIELD_AS(Fenrir::Transform, scale, "Scale"));
};

namespace Fenrir::Detail
{
    // Fenrir::Name keeps its 256-byte buffer private, so it stands in for it: the same bytes, nameable by a mapping.
    struct NameView
    {
        char text[256];
    };
    static_assert(sizeof(Name) == sizeof(NameView) && alignof(Name) == alignof(NameView),
                  "Name changed shape: update NameView and the C# Name.");
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::NameView>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Name";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD_AS(Fenrir::Detail::NameView, text, "Text"));
};

namespace Fenrir::Detail
{
    // Fenrir::Entity is a 32-bit entity value and a pointer to its entity list. Scripts see the value; the rest is the engine's.
    // These stand in for Entity and Relationship, which have members Iris cannot map, and must have exactly their size
    // and layout (checked below, and by a test that reads real hierarchy memory from C#).
    struct EntityLinkView
    {
        std::uint32_t value;
        std::uint32_t reserved;
        std::uint64_t owner;
    };

    struct RelationshipView
    {
        EntityLinkView Parent;
        EntityLinkView FirstChild;
        EntityLinkView NextSibling;
        EntityLinkView PrevSibling;
    };

    static_assert(sizeof(void*) == 8, "Scripting maps Entity as a 32-bit value and a 64-bit pointer: it needs a 64-bit build.");
    static_assert(sizeof(Entity) == sizeof(EntityLinkView) && alignof(Entity) == alignof(EntityLinkView),
                  "Entity changed shape: update EntityLinkView and the C# EntityLink.");
    static_assert(sizeof(Relationship) == sizeof(RelationshipView) && alignof(Relationship) == alignof(RelationshipView),
                  "Relationship changed shape: update RelationshipView and the C# Relationship.");
} // namespace Fenrir::Detail

template <>
struct Iris::StructMapping<Fenrir::Detail::EntityLinkView>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.EntityLink";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fenrir::Detail::EntityLinkView, value),
                                                      IRIS_FIELD(Fenrir::Detail::EntityLinkView, reserved),
                                                      IRIS_FIELD(Fenrir::Detail::EntityLinkView, owner));
};

template <>
struct Iris::StructMapping<Fenrir::Detail::RelationshipView>
{
    static constexpr std::string_view typeName = "Fenrir.Scripting.Relationship";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Fenrir::Detail::RelationshipView, Parent),
                                                      IRIS_FIELD(Fenrir::Detail::RelationshipView, FirstChild),
                                                      IRIS_FIELD(Fenrir::Detail::RelationshipView, NextSibling),
                                                      IRIS_FIELD(Fenrir::Detail::RelationshipView, PrevSibling));
};

namespace Fenrir::Detail
{
    bool RegisterEngineComponents(ComponentRegistry& registry)
    {
        // Every entity the engine makes has all three, and the renderer and hierarchy rely on it: scripts cannot remove them.
        auto transform = MakeComponent<Transform>("Fenrir.Transform");
        auto name = MakeComponent<Name, NameView>("Fenrir.Name");
        auto relationship = MakeReadOnlyComponent<Relationship, RelationshipView>("Fenrir.Relationship");
        transform.required = name.required = relationship.required = true;
        return registry.Add(std::move(transform)) >= 0 && registry.Add(std::move(name)) >= 0 &&
               registry.Add(std::move(relationship)) >= 0;
    }
} // namespace Fenrir::Detail
