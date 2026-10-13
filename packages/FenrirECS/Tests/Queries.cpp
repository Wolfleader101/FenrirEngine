#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <span>
#include <vector>

using Fenrir::ComponentSelector;
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
    std::set<std::uint32_t> Visited(EntityList& entities, std::vector<ComponentSelector> selectors)
    {
        std::set<std::uint32_t> visited;
        entities.ForEachWith(std::span<const ComponentSelector>(selectors),
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

    CHECK(Visited(entities, {ComponentSelector::Native<Health>(), ComponentSelector::Native<Armour>()}) ==
          std::set<std::uint32_t>{both.GetId()});
    CHECK(Visited(entities, {ComponentSelector::Native<Health>()}) ==
          std::set<std::uint32_t>{both.GetId(), onlyHealth.GetId()});
}

TEST_CASE("a query over a dynamic component visits exactly the entities that have it", "[query]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Marked", 4, 4);
    const auto marked = entities.CreateEntity().GetId();
    const auto alsoMarked = entities.CreateEntity().GetId();
    (void)entities.CreateEntity();
    REQUIRE(entities.AddDynamic(type, marked));
    REQUIRE(entities.AddDynamic(type, alsoMarked));

    CHECK(Visited(entities, {ComponentSelector::Dynamic(type)}) == std::set<std::uint32_t>{marked, alsoMarked});
}

TEST_CASE("a query can mix native and dynamic components", "[query]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Mixed", 4, 4);
    auto both = entities.CreateEntity();
    both.AddComponent<Health>();
    REQUIRE(entities.AddDynamic(type, both.GetId()));
    auto onlyNative = entities.CreateEntity();
    onlyNative.AddComponent<Health>();
    const auto onlyDynamic = entities.CreateEntity().GetId();
    REQUIRE(entities.AddDynamic(type, onlyDynamic));

    CHECK(Visited(entities, {ComponentSelector::Native<Health>(), ComponentSelector::Dynamic(type)}) ==
          std::set<std::uint32_t>{both.GetId()});
    CHECK(Visited(entities, {ComponentSelector::Dynamic(type), ComponentSelector::Native<Health>()}) ==
          std::set<std::uint32_t>{both.GetId()});
}

TEST_CASE("a query with no selectors visits nothing", "[query]")
{
    EntityList entities;
    (void)entities.CreateEntity();

    CHECK(Visited(entities, {}).empty());
}

TEST_CASE("a query nobody matches visits nothing", "[query]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Rare", 4, 4);
    entities.CreateEntity().AddComponent<Health>();
    REQUIRE(entities.AddDynamic(type, entities.CreateEntity().GetId()));

    CHECK(Visited(entities, {ComponentSelector::Native<Health>(), ComponentSelector::Dynamic(type)}).empty());
}

TEST_CASE("selecting a dynamic type nobody added makes no storage, but selecting a native one does", "[query]")
{
    EntityList entities;
    const std::size_t storages = entities.ComponentStorageCount();

    CHECK(Visited(entities, {ComponentSelector::Dynamic(DynamicComponentType("Test.Never", 4, 4))}).empty());
    CHECK(entities.ComponentStorageCount() == storages);

    CHECK(Visited(entities, {ComponentSelector::Native<Armour>()}).empty());
    CHECK(entities.ComponentStorageCount() == storages + 1);
}

TEST_CASE("CountOf agrees with what was added, for native and dynamic components", "[query]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Counted", 4, 4);
    for (int index = 0; index < 7; ++index)
    {
        entities.CreateEntity().AddComponent<Health>();
    }
    for (int index = 0; index < 4; ++index)
    {
        REQUIRE(entities.AddDynamic(type, entities.CreateEntity().GetId()));
    }

    CHECK(entities.CountOf(ComponentSelector::Native<Health>()) == 7);
    CHECK(entities.CountOf(ComponentSelector::Dynamic(type)) == 4);
    CHECK(entities.CountDynamic(type) == 4);
    CHECK(entities.CountOf(ComponentSelector::Dynamic(DynamicComponentType("Test.None", 4, 4))) == 0);
}

TEST_CASE("changes made between queries are seen by the next query", "[query]")
{
    EntityList entities;
    const DynamicComponentType type("Test.Changing", 4, 4);
    auto first = entities.CreateEntity();
    auto second = entities.CreateEntity();
    first.AddComponent<Health>();
    REQUIRE(entities.AddDynamic(type, first.GetId()));
    const std::vector<ComponentSelector> both{ComponentSelector::Native<Health>(), ComponentSelector::Dynamic(type)};
    REQUIRE(Visited(entities, both) == std::set<std::uint32_t>{first.GetId()});

    second.AddComponent<Health>();
    REQUIRE(entities.AddDynamic(type, second.GetId()));
    CHECK(Visited(entities, both) == std::set<std::uint32_t>{first.GetId(), second.GetId()});

    REQUIRE(entities.RemoveDynamic(type, first.GetId()));
    CHECK(Visited(entities, both) == std::set<std::uint32_t>{second.GetId()});

    entities.DestroyEntity(second.GetId());
    CHECK(Visited(entities, both).empty());
}
