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
    const std::optional<EntityId> short_ = Fenrir::ParseEntityId("a7k2q");
    const std::optional<EntityId> long_ = Fenrir::ParseEntityId("00a7k2q");
    REQUIRE(short_.has_value());
    REQUIRE(long_.has_value());

    INFO("a file's '00a7k2q' is not its 'a7k2q'");
    REQUIRE(short_->value != long_->value);
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
    TypeRegistry registry;
    REQUIRE(Fenrir::Generated::RegisterFenrirECSTypes(registry));

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
    TypeRegistry registry;
    REQUIRE(Fenrir::Generated::RegisterFenrirECSTypes(registry));

    INFO("Fenrir::UnknownComponents is undescribed, so a file never names it");
    REQUIRE(registry.Find("Fenrir::UnknownComponents") == nullptr);
}

TEST_CASE("EntityId and Instance are found by name", "[entity-id][instance][types]")
{
    TypeRegistry registry;
    REQUIRE(Fenrir::Generated::RegisterFenrirECSTypes(registry));
    EntityList entities;
    const auto entity = entities.CreateEntity().GetId();

    const std::optional<ComponentType> id = ComponentType::ForName(registry, "Fenrir::EntityId");
    INFO("FenrirECS's registration should hang a ComponentType on Fenrir::EntityId");
    REQUIRE(id.has_value());
    INFO("an entity takes an EntityId through it, and the C++ type sees it");
    REQUIRE(entities.Add(*id, entity));
    REQUIRE(entities.Get(ComponentType::Native<EntityId>(), entity) != nullptr);

    const std::optional<ComponentType> instance = ComponentType::ForName(registry, "Fenrir::Instance");
    INFO("FenrirECS's registration should hang a ComponentType on Fenrir::Instance");
    REQUIRE(instance.has_value());
    INFO("an entity takes an Instance through it, and the C++ type sees it");
    REQUIRE(entities.Add(*instance, entity));
    REQUIRE(entities.Get(ComponentType::Native<Fenrir::Instance>(), entity) != nullptr);

    INFO("NameOf spells them back");
    REQUIRE(ComponentType::NameOf(registry, *id) == "Fenrir::EntityId");
    REQUIRE(ComponentType::NameOf(registry, *instance) == "Fenrir::Instance");
}
