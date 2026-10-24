// The tracked set's interface names no EnTT type (ADR-0008): its header compiles on its own without bringing EnTT in.
// This file includes it first, so if the header ever reaches for EnTT the build fails here.
#include "FenrirECS/TrackedComponents.hpp"

#ifdef ENTT_VERSION_MAJOR
#error "FenrirECS/TrackedComponents.hpp must not include EnTT."
#endif

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"

#include <cstdint>
#include <set>
#include <utility>
#include <vector>

using Fenrir::ComponentType;
using Fenrir::DynamicComponentType;
using Fenrir::EntityList;
using Fenrir::TrackedComponents;

namespace
{
    struct Health
    {
        int points = 100;
    };

    using Ids = std::vector<std::uint32_t>;
    using IdSet = std::set<std::uint32_t>;
} // namespace

TEST_CASE("a default tracked set is empty and drains empty", "[tracked]")
{
    TrackedComponents tracked;

    INFO("made without a list, it tracks nothing");
    REQUIRE(tracked.Entities().empty());
    REQUIRE(tracked.Drain().empty());
}

TEST_CASE("a tracked set starts with the entities that have the type when it is made", "[tracked]")
{
    EntityList entities;
    auto first = entities.CreateEntity();
    auto second = entities.CreateEntity();
    auto third = entities.CreateEntity();
    third.AddComponent<Health>();
    first.AddComponent<Health>();

    TrackedComponents tracked(entities, ComponentType::Native<Health>());

    INFO("the scan finds the components already there, in entity order, and they are not reported as having come");
    REQUIRE(tracked.Entities() == IdSet{first.GetId(), third.GetId()});
    REQUIRE(tracked.Contains(first.GetId()));
    REQUIRE_FALSE(tracked.Contains(second.GetId()));
    REQUIRE(tracked.Drain().empty());
}

TEST_CASE("a drain names each entity whose component came or went once, in entity order", "[tracked]")
{
    EntityList entities;
    auto first = entities.CreateEntity();
    auto second = entities.CreateEntity();
    auto third = entities.CreateEntity();
    first.AddComponent<Health>();
    TrackedComponents tracked(entities, ComponentType::Native<Health>());

    third.AddComponent<Health>();
    second.AddComponent<Health>();
    third.RemoveComponent<Health>();
    first.RemoveComponent<Health>();
    third.AddComponent<Health>();

    INFO("third changed three times and second once, after it; each is named once and in entity order");
    REQUIRE(tracked.Drain() == Ids{first.GetId(), second.GetId(), third.GetId()});
    INFO("a second drain names nothing new");
    REQUIRE(tracked.Drain().empty());
}

TEST_CASE("a component removed and added again, or replaced, counts as having come and gone", "[tracked]")
{
    EntityList entities;
    auto readded = entities.CreateEntity();
    auto replaced = entities.CreateEntity();
    readded.AddComponent<Health>();
    replaced.AddComponent<Health>();
    TrackedComponents tracked(entities, ComponentType::Native<Health>());

    readded.RemoveComponent<Health>();
    readded.AddComponent<Health>();
    replaced.AddComponent<Health>(Health{5});

    INFO("both are named though both still have the type, and both stay in the set");
    REQUIRE(tracked.Drain() == Ids{readded.GetId(), replaced.GetId()});
    REQUIRE(tracked.Entities() == IdSet{readded.GetId(), replaced.GetId()});
}

TEST_CASE("the set changes only at a drain, to the entities alive with the type then", "[tracked]")
{
    EntityList entities;
    auto kept = entities.CreateEntity();
    auto removed = entities.CreateEntity();
    auto destroyed = entities.CreateEntity();
    kept.AddComponent<Health>();
    removed.AddComponent<Health>();
    destroyed.AddComponent<Health>();
    TrackedComponents tracked(entities, ComponentType::Native<Health>());
    auto added = entities.CreateEntity();

    added.AddComponent<Health>();
    removed.RemoveComponent<Health>();
    const std::uint32_t destroyedId = destroyed.GetId();
    entities.DestroyEntity(destroyedId);

    INFO("before the drain the set is as the scan left it");
    REQUIRE(tracked.Entities() == IdSet{kept.GetId(), removed.GetId(), destroyedId});

    (void)tracked.Drain();
    INFO("after it: the added entity is in, the removed and destroyed ones are out");
    REQUIRE(tracked.Entities() == IdSet{kept.GetId(), added.GetId()});
    REQUIRE_FALSE(tracked.Contains(destroyedId));
}

TEST_CASE("clearing the list or the type empties the set at the next drain", "[tracked]")
{
    for (const bool wholeList : {true, false})
    {
        EntityList entities;
        const ComponentType health = ComponentType::Native<Health>();
        auto first = entities.CreateEntity();
        auto second = entities.CreateEntity();
        first.AddComponent<Health>();
        second.AddComponent<Health>();
        TrackedComponents tracked(entities, health);

        if (wholeList)
        {
            entities.Clear();
        }
        else
        {
            REQUIRE(entities.Clear(health) == 2);
        }

        INFO((wholeList ? "EntityList::Clear" : "EntityList::Clear(type)")
             << ": every entity that had the type is named, and none is left in the set");
        REQUIRE(tracked.Drain() == Ids{first.GetId(), second.GetId()});
        REQUIRE(tracked.Entities().empty());
    }
}

TEST_CASE("a tracked set tracks a dynamic type, even one nobody has added yet", "[tracked]")
{
    EntityList entities;
    const ComponentType velocity = ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4));
    const auto first = entities.CreateEntity().GetId();
    const auto second = entities.CreateEntity().GetId();
    TrackedComponents tracked(entities, velocity);

    REQUIRE(entities.Add(velocity, second));
    REQUIRE(entities.Add(velocity, first));
    INFO("dynamic adds are named in entity order and join the set");
    REQUIRE(tracked.Drain() == Ids{first, second});
    REQUIRE(tracked.Entities() == IdSet{first, second});

    REQUIRE(entities.Remove(velocity, first));
    INFO("a dynamic removal is named and leaves the set");
    REQUIRE(tracked.Drain() == Ids{first});
    REQUIRE(tracked.Entities() == IdSet{second});

    INFO("a set made after the adds starts with them");
    TrackedComponents later(entities, velocity);
    REQUIRE(later.Entities() == IdSet{second});
}

TEST_CASE("several tracked sets of one type are independent", "[tracked]")
{
    EntityList entities;
    auto first = entities.CreateEntity();
    auto second = entities.CreateEntity();
    TrackedComponents a(entities, ComponentType::Native<Health>());
    TrackedComponents b(entities, ComponentType::Native<Health>());

    first.AddComponent<Health>();
    INFO("draining one leaves the other's changes undrained");
    REQUIRE(a.Drain() == Ids{first.GetId()});
    second.AddComponent<Health>();
    REQUIRE(b.Drain() == Ids{first.GetId(), second.GetId()});
    REQUIRE(a.Drain() == Ids{second.GetId()});
    REQUIRE(a.Entities() == b.Entities());
}

TEST_CASE("a moved tracked set keeps its entities and keeps tracking", "[tracked]")
{
    EntityList entities;
    auto first = entities.CreateEntity();
    auto second = entities.CreateEntity();
    first.AddComponent<Health>();
    TrackedComponents original(entities, ComponentType::Native<Health>());

    TrackedComponents moved = std::move(original);
    second.AddComponent<Health>();

    INFO("the moved-to set has the scan's entities and sees the later add");
    REQUIRE(moved.Drain() == Ids{second.GetId()});
    REQUIRE(moved.Entities() == IdSet{first.GetId(), second.GetId()});
}

TEST_CASE("a tracked set may outlive its list", "[tracked]")
{
    TrackedComponents tracked;
    std::uint32_t added = 0;
    std::uint32_t scanned = 0;
    {
        EntityList entities;
        auto first = entities.CreateEntity();
        first.AddComponent<Health>();
        scanned = first.GetId();
        tracked = TrackedComponents(entities, ComponentType::Native<Health>());
        auto second = entities.CreateEntity();
        second.AddComponent<Health>();
        added = second.GetId();
    }

    INFO("what came before the list went is still drained; after that nothing comes or goes");
    REQUIRE(tracked.Drain() == Ids{added});
    REQUIRE(tracked.Entities() == IdSet{scanned, added});
    REQUIRE(tracked.Drain().empty());
}
