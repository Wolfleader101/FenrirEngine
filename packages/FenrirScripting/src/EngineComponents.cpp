#include "ComponentRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/EngineMappings.hpp"

// How the engine's own components look to C# (the math types and Transform are in EngineMappings.hpp). Each mapping is
// checked against the C# struct in the Fenrir.Scripting contract whenever a script assembly loads, so a drift between
// the two fails the load and names the field.

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
    // Relationship's members are Entities, which Iris cannot map, so this stands in for it with exactly its size and
    // layout (checked below, and by a test that reads real hierarchy memory from C#). EntityLinkView is in
    // EngineMappings.hpp.
    struct RelationshipView
    {
        EntityLinkView Parent;
        EntityLinkView FirstChild;
        EntityLinkView NextSibling;
        EntityLinkView PrevSibling;
    };

    static_assert(sizeof(Relationship) == sizeof(RelationshipView) &&
                      alignof(Relationship) == alignof(RelationshipView),
                  "Relationship changed shape: update RelationshipView and the C# Relationship.");
} // namespace Fenrir::Detail

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
    std::vector<ComponentRegistration> EngineComponents()
    {
        // Every entity the engine makes has all three, and the renderer and hierarchy rely on it: scripts cannot remove
        // them.
        auto transform = MakeComponent<Transform>("Fenrir.Transform");
        auto name = MakeComponent<Name, NameView>("Fenrir.Name");
        auto relationship = MakeReadOnlyComponent<Relationship, RelationshipView>("Fenrir.Relationship");
        transform.required = name.required = relationship.required = true;
        return {std::move(transform), std::move(name), std::move(relationship)};
    }
} // namespace Fenrir::Detail
