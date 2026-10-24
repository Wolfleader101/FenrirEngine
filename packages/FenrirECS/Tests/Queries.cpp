#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"

#include <cstdint>
#include <set>
#include <span>
#include <vector>

using Fenrir::ComponentType;
using Fenrir::DynamicComponentType;
using Fenrir::EntityList;

namespace
{
    struct Health
    {
        int Current = 0;
    };

    struct Armour
    {
        int Points = 0;
    };

    // The entities a query visits, as a set so that order does not matter.
    std::set<std::uint32_t> Visited(EntityList& entities, const std::vector<ComponentType>& types)
    {
        std::set<std::uint32_t> visited;
        entities.ForEachWith(std::span<const ComponentType>(types),
                             [&](std::uint32_t entity) { visited.insert(entity); });
        return visited;
    }
} // namespace

TEST_CASE("a query over native components visits exactly the entities that have all of them", "[query]")
{
    EntityList entities;
    auto both = entities.CreateEntity();
    both.AddComponent<Health>();
    both.AddComponent<Armour>();
    auto onlyHealth = entities.CreateEntity();
    onlyHealth.AddComponent<Health>();
    auto onlyArmour = entities.CreateEntity();
    onlyArmour.AddComponent<Armour>();
    (void)entities.CreateEntity();
    const ComponentType health = ComponentType::Native<Health>();
    const ComponentType armour = ComponentType::Native<Armour>();

    INFO("only the entity with Health and Armour is visited for both");
    REQUIRE(Visited(entities, {health, armour}) == std::set<std::uint32_t>{both.GetId()});

    INFO("both entities with Health are visited for Health alone");
    REQUIRE(Visited(entities, {health}) == std::set<std::uint32_t>{both.GetId(), onlyHealth.GetId()});
}

TEST_CASE("a query over a dynamic component visits exactly the entities that have it", "[query]")
{
    EntityList entities;
    const ComponentType type = ComponentType::Dynamic(DynamicComponentType("Test.Marked", 4, 4));
    const auto marked = entities.CreateEntity().GetId();
    const auto alsoMarked = entities.CreateEntity().GetId();
    (void)entities.CreateEntity();
    INFO("both adds succeed");
    REQUIRE(entities.Add(type, marked));
    REQUIRE(entities.Add(type, alsoMarked));

    INFO("the two marked entities, and only they, are visited");
    REQUIRE(Visited(entities, {type}) == std::set<std::uint32_t>{marked, alsoMarked});
}

TEST_CASE("the order of the types in a query does not change what it visits", "[query]")
{
    EntityList entities;
    const ComponentType health = ComponentType::Native<Health>();
    const ComponentType mixed = ComponentType::Dynamic(DynamicComponentType("Test.Mixed", 4, 4));
    auto both = entities.CreateEntity();
    both.AddComponent<Health>();
    auto onlyNative = entities.CreateEntity();
    onlyNative.AddComponent<Health>();
    const auto onlyDynamic = entities.CreateEntity().GetId();
    INFO("both dynamic adds succeed");
    REQUIRE(entities.Add(mixed, both.GetId()));
    REQUIRE(entities.Add(mixed, onlyDynamic));

    INFO("native first visits only the entity with both");
    REQUIRE(Visited(entities, {health, mixed}) == std::set<std::uint32_t>{both.GetId()});

    INFO("dynamic first visits the same entity");
    REQUIRE(Visited(entities, {mixed, health}) == std::set<std::uint32_t>{both.GetId()});
}

TEST_CASE("a query nobody matches visits nothing", "[query]")
{
    EntityList entities;
    const ComponentType health = ComponentType::Native<Health>();
    const ComponentType rare = ComponentType::Dynamic(DynamicComponentType("Test.Rare", 4, 4));
    entities.CreateEntity().AddComponent<Health>();
    INFO("the dynamic add succeeds");
    REQUIRE(entities.Add(rare, entities.CreateEntity().GetId()));

    INFO("no entity has both, so none is visited");
    REQUIRE(Visited(entities, {health, rare}).empty());
}

TEST_CASE("querying a native type nobody added makes its empty storage", "[query]")
{
    EntityList entities;
    const std::size_t storages = entities.ComponentStorageCount();

    INFO("the query visits nothing but the list now has storage for Armour, as ADR-0008 documents");
    REQUIRE(Visited(entities, {ComponentType::Native<Armour>()}).empty());
    REQUIRE(entities.ComponentStorageCount() == storages + 1);
}

TEST_CASE("changes made between queries are seen by the next query", "[query]")
{
    EntityList entities;
    const ComponentType health = ComponentType::Native<Health>();
    const ComponentType changing = ComponentType::Dynamic(DynamicComponentType("Test.Changing", 4, 4));
    auto first = entities.CreateEntity();
    auto second = entities.CreateEntity();
    first.AddComponent<Health>();
    INFO("the first entity is given both");
    REQUIRE(entities.Add(changing, first.GetId()));
    REQUIRE(Visited(entities, {health, changing}) == std::set<std::uint32_t>{first.GetId()});

    INFO("a second entity given both is visited too");
    second.AddComponent<Health>();
    REQUIRE(entities.Add(changing, second.GetId()));
    REQUIRE(Visited(entities, {health, changing}) == std::set<std::uint32_t>{first.GetId(), second.GetId()});

    INFO("the first, once it loses the dynamic component, is not");
    REQUIRE(entities.Remove(changing, first.GetId()));
    REQUIRE(Visited(entities, {health, changing}) == std::set<std::uint32_t>{second.GetId()});

    INFO("the second, once destroyed, is not");
    entities.DestroyEntity(second.GetId());
    REQUIRE(Visited(entities, {health, changing}).empty());
}
