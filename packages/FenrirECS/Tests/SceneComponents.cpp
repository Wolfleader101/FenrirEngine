// The components a scene file needs (ADR-0078, ADR-0080): an entity id minted, spelled and parsed; the Transient tag
// reached through the typed API alone; opaque components kept as bytes; and EntityId and Instance found by name.
#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityId.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/Generated/FenrirECSTypes.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirECS/TypeAttachments.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using Fenrir::ComponentType;
using Fenrir::Entity;
using Fenrir::EntityId;
using Fenrir::EntityList;
using Fenrir::TypeRegistry;

namespace
{
    // A registry holding FenrirECS's described types (and FenrirMath's, which its registration runs first).
    TypeRegistry EcsRegistry()
    {
        TypeRegistry registry;
        INFO("setup: FenrirECS's generated registration adds its types with no diagnostic");
        REQUIRE(Fenrir::Generated::RegisterFenrirECSTypes(registry));
        return registry;
    }

    constexpr std::string_view k_crockford = "0123456789abcdefghjkmnpqrstvwxyz";

    bool IsLowerCrockford(std::string_view text)
    {
        for (const char c : text)
        {
            if (k_crockford.find(c) == std::string_view::npos)
            {
                return false;
            }
        }
        return true;
    }
} // namespace

TEST_CASE("a minted entity id is five lower-case Crockford characters", "[entity-id]")
{
    for (int i = 0; i < 256; ++i)
    {
        const EntityId id = Fenrir::MintEntityId();
        INFO("a minted id is never zero, which means no id");
        REQUIRE(id.value != 0);

        const std::string spelled = Fenrir::SpellEntityId(id);
        INFO("a minted id spells as five characters, not '" << spelled << "'");
        REQUIRE(spelled.size() == 5);
        INFO("a minted id spells in lower-case Crockford base32, not '" << spelled << "'");
        REQUIRE(IsLowerCrockford(spelled));
    }
}

TEST_CASE("a minted entity id parses back from its spelling", "[entity-id]")
{
    const EntityId id = Fenrir::MintEntityId();
    const std::optional<EntityId> parsed = Fenrir::ParseEntityId(Fenrir::SpellEntityId(id));

    INFO("the spelling of a minted id parses");
    REQUIRE(parsed.has_value());
    INFO("and gives back the same id");
    REQUIRE(parsed->value == id.value);
}

TEST_CASE("an id of another length parses and spells back unchanged", "[entity-id]")
{
    for (const std::string_view text : {"a7k2q", "0000000z", "00000", "zzzzzzzzzzzz", "x", "a7k2q9m3"})
    {
        const std::optional<EntityId> parsed = Fenrir::ParseEntityId(text);
        INFO("'" << text << "' is a valid id");
        REQUIRE(parsed.has_value());
        INFO("a parsed id is never zero");
        REQUIRE(parsed->value != 0);
        INFO("'" << text << "' spells back unchanged");
        REQUIRE(Fenrir::SpellEntityId(*parsed) == text);
    }
}

TEST_CASE("ids that differ only in leading zeros are different ids", "[entity-id]")
{
    const std::optional<EntityId> fiveCharacters = Fenrir::ParseEntityId("a7k2q");
    const std::optional<EntityId> leadingZeros = Fenrir::ParseEntityId("00a7k2q");
    INFO("both spellings are valid ids");
    REQUIRE(fiveCharacters.has_value());
    REQUIRE(leadingZeros.has_value());

    INFO("a file's '00a7k2q' is not its 'a7k2q'");
    REQUIRE(fiveCharacters->value != leadingZeros->value);
}

TEST_CASE("text that is not a lower-case Crockford id does not parse", "[entity-id]")
{
    for (const std::string_view text : {"", "A7K2Q", "a7k2i", "a7-2q", "a7k2l", "a7k2o", "a7k2u", "zzzzzzzzzzzzz"})
    {
        INFO("'" << text
                 << "' is refused: empty, upper case, a letter Crockford leaves out, or above twelve characters");
        REQUIRE_FALSE(Fenrir::ParseEntityId(text).has_value());
    }
}

TEST_CASE("a numbered id spells its number in base32 with no leading zeros", "[entity-id]")
{
    INFO("zero is one character, thirty-one the last of one, thirty-two the first of two");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::NumberedEntityId(0).value()) == "0");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::NumberedEntityId(1).value()) == "1");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::NumberedEntityId(31).value()) == "z");
    REQUIRE(Fenrir::SpellEntityId(Fenrir::NumberedEntityId(32).value()) == "10");
    INFO("a numbered id is the id its spelling parses to");
    REQUIRE(Fenrir::NumberedEntityId(1000)->value == Fenrir::ParseEntityId("z8")->value);
    INFO("the largest number twelve characters spell has an id, and the next has none");
    const std::uint64_t largest = (std::uint64_t{1} << 60) - 1;
    REQUIRE(Fenrir::SpellEntityId(Fenrir::NumberedEntityId(largest).value()) == "zzzzzzzzzzzz");
    REQUIRE_FALSE(Fenrir::NumberedEntityId(largest + 1).has_value());
}

TEST_CASE("no id spells as nothing", "[entity-id]")
{
    INFO("zero, which means no id, spells as the empty string");
    REQUIRE(Fenrir::SpellEntityId(EntityId{}).empty());
}

TEST_CASE("Transient is added, tested and removed through the typed API", "[transient]")
{
    EntityList entities;
    Entity entity = entities.CreateEntity();

    INFO("a new entity is not transient");
    REQUIRE_FALSE(entity.HasComponent<Fenrir::Transient>());

    entity.AddComponent<Fenrir::Transient>();
    INFO("an entity tagged Transient has it");
    REQUIRE(entity.HasComponent<Fenrir::Transient>());

    entity.AddComponent<Fenrir::Transient>();
    INFO("tagging it twice keeps it tagged");
    REQUIRE(entity.HasComponent<Fenrir::Transient>());

    entity.RemoveComponent<Fenrir::Transient>();
    INFO("removing the tag leaves it untagged");
    REQUIRE_FALSE(entity.HasComponent<Fenrir::Transient>());
}

TEST_CASE("Transient is in no registration", "[transient][types]")
{
    const TypeRegistry registry = EcsRegistry();

    INFO("Fenrir::Transient is undescribed, so no name finds it");
    REQUIRE_FALSE(ComponentType::ForName(registry, "Fenrir::Transient").has_value());
    INFO("nor is it registered at all");
    REQUIRE(registry.Find("Fenrir::Transient") == nullptr);
}

TEST_CASE("UnknownComponents gives an opaque component's bytes back unchanged", "[unknown-components]")
{
    EntityList entities;
    Entity entity = entities.CreateEntity();

    const std::vector<std::byte> bytes = {std::byte{0x00}, std::byte{0x07}, std::byte{0xff}, std::byte{0x00},
                                          std::byte{0x42}};
    auto& unknown = entity.AddComponent<Fenrir::UnknownComponents>();
    unknown.components.push_back({"Game::Missing", 3, bytes});

    const auto& kept = entity.GetComponent<Fenrir::UnknownComponents>();
    INFO("the holder keeps the one component it was given");
    REQUIRE(kept.components.size() == 1);
    INFO("with its canonical type name");
    REQUIRE(kept.components[0].typeName == "Game::Missing");
    INFO("its type version");
    REQUIRE(kept.components[0].version == 3);
    INFO("and its tree's bytes, byte for byte, the zero bytes included");
    REQUIRE(kept.components[0].tree == bytes);
}

TEST_CASE("UnknownComponents is in no registration", "[unknown-components][types]")
{
    const TypeRegistry registry = EcsRegistry();

    INFO("Fenrir::UnknownComponents is undescribed, so a file never names it");
    REQUIRE(registry.Find("Fenrir::UnknownComponents") == nullptr);
}

TEST_CASE("ForName finds EntityId as a ComponentType an entity accepts", "[entity-id][types]")
{
    const TypeRegistry registry = EcsRegistry();
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();

    const std::optional<ComponentType> id = ComponentType::ForName(registry, "Fenrir::EntityId");
    INFO("FenrirECS's registration should hang a ComponentType on Fenrir::EntityId");
    REQUIRE(id.has_value());
    INFO("the entity takes an EntityId through it");
    REQUIRE(entities.Add(*id, entity));
    INFO("and the C++ type sees the component it added");
    REQUIRE(entities.Get(ComponentType::Native<EntityId>(), entity) != nullptr);
}

TEST_CASE("ForName finds Instance as a ComponentType an entity accepts", "[instance][types]")
{
    const TypeRegistry registry = EcsRegistry();
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();

    const std::optional<ComponentType> instance = ComponentType::ForName(registry, "Fenrir::Instance");
    INFO("FenrirECS's registration should hang a ComponentType on Fenrir::Instance");
    REQUIRE(instance.has_value());
    INFO("the entity takes an Instance through it");
    REQUIRE(entities.Add(*instance, entity));
    INFO("and the C++ type sees the component it added");
    REQUIRE(entities.Get(ComponentType::Native<Fenrir::Instance>(), entity) != nullptr);
}

TEST_CASE("NameOf spells EntityId and Instance by their canonical names", "[entity-id][instance][types]")
{
    const TypeRegistry registry = EcsRegistry();

    INFO("EntityId's ComponentType is registered as Fenrir::EntityId");
    REQUIRE(ComponentType::NameOf(registry, ComponentType::Native<EntityId>()) == "Fenrir::EntityId");
    INFO("Instance's ComponentType is registered as Fenrir::Instance");
    REQUIRE(ComponentType::NameOf(registry, ComponentType::Native<Fenrir::Instance>()) == "Fenrir::Instance");
}
