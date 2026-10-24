#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/ComponentWatch.hpp"
#include "FenrirECS/DynamicComponent.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

using Fenrir::ComponentChange;
using Fenrir::ComponentType;
using Fenrir::ComponentWatch;
using Fenrir::DynamicComponentType;
using Fenrir::EntityList;

namespace
{
    struct Health
    {
        int points = 100;
    };

    using Changes = std::vector<ComponentChange>;

    ComponentChange Added(std::uint32_t entity)
    {
        return {entity, ComponentChange::Kind::Added};
    }

    ComponentChange Removed(std::uint32_t entity)
    {
        return {entity, ComponentChange::Kind::Removed};
    }
} // namespace

TEST_CASE("a watch records adds and removes of a native type in the order they happened", "[watch]")
{
    EntityList entities;
    const ComponentType health = ComponentType::Native<Health>();
    auto first = entities.CreateEntity();
    auto second = entities.CreateEntity();
    ComponentWatch watch = entities.Watch(health);

    first.AddComponent<Health>();
    second.AddComponent<Health>();
    first.RemoveComponent<Health>();

    INFO("typed adds and removes appear as they happened");
    REQUIRE(watch.Drain() == Changes{Added(first.GetId()), Added(second.GetId()), Removed(first.GetId())});
}

TEST_CASE("a watch records adds and removes of a dynamic type, even one nobody has added yet", "[watch]")
{
    EntityList entities;
    const ComponentType velocity = ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4));
    const auto first = entities.CreateEntity().GetId();
    const auto second = entities.CreateEntity().GetId();
    ComponentWatch watch = entities.Watch(velocity);

    REQUIRE(entities.Add(velocity, first));
    REQUIRE(entities.Add(velocity, second));
    REQUIRE(entities.Remove(velocity, first));

    INFO("adds and removes through the ComponentType calls (the ones C# changes use) appear as they happened");
    REQUIRE(watch.Drain() == Changes{Added(first), Added(second), Removed(first)});
}

TEST_CASE("draining hands back the record and empties it", "[watch]")
{
    EntityList entities;
    const ComponentType velocity = ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4));
    const auto entity = entities.CreateEntity().GetId();
    ComponentWatch watch = entities.Watch(velocity);
    REQUIRE(entities.Add(velocity, entity));

    INFO("the first drain has the add, the second nothing, and a later change is all the next one has");
    REQUIRE(watch.Drain() == Changes{Added(entity)});
    REQUIRE(watch.Drain().empty());
    REQUIRE(entities.Remove(velocity, entity));
    REQUIRE(watch.Drain() == Changes{Removed(entity)});
}

TEST_CASE("replacing a native component records a removal then an add", "[watch]")
{
    EntityList entities;
    auto entity = entities.CreateEntity();
    entity.AddComponent<Health>();
    ComponentWatch watch = entities.Watch(ComponentType::Native<Health>());

    entity.AddComponent<Health>(Health{5});

    INFO("Entity::AddComponent on a component the entity has replaces it, and the watch sees both halves");
    REQUIRE(watch.Drain() == Changes{Removed(entity.GetId()), Added(entity.GetId())});
}

TEST_CASE("adding a component the entity has through its ComponentType records nothing", "[watch]")
{
    EntityList entities;
    const ComponentType velocity = ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4));
    const ComponentType health = ComponentType::Native<Health>();
    const auto entity = entities.CreateEntity().GetId();
    REQUIRE(entities.Add(velocity, entity));
    REQUIRE(entities.Add(health, entity));
    ComponentWatch velocityWatch = entities.Watch(velocity);
    ComponentWatch healthWatch = entities.Watch(health);

    REQUIRE(entities.Add(velocity, entity));
    REQUIRE(entities.Add(health, entity));

    INFO("EntityList::Add keeps the component the entity has, so nothing was added or removed");
    REQUIRE(velocityWatch.Drain().empty());
    REQUIRE(healthWatch.Drain().empty());
}

TEST_CASE("destroying an entity records a removal for each watched component it had", "[watch]")
{
    EntityList entities;
    const ComponentType velocity = ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4));
    const ComponentType health = ComponentType::Native<Health>();
    const auto doomed = entities.CreateEntity().GetId();
    const auto bystander = entities.CreateEntity().GetId();
    for (const auto entity : {doomed, bystander})
    {
        REQUIRE(entities.Add(velocity, entity));
        REQUIRE(entities.Add(health, entity));
    }
    ComponentWatch velocityWatch = entities.Watch(velocity);
    ComponentWatch healthWatch = entities.Watch(health);

    entities.DestroyEntity(doomed);

    INFO("each watch records the destroyed entity's removal, and nothing for the entity left alone");
    REQUIRE(velocityWatch.Drain() == Changes{Removed(doomed)});
    REQUIRE(healthWatch.Drain() == Changes{Removed(doomed)});
}

TEST_CASE("clearing the list or a type records a removal for each component", "[watch]")
{
    const ComponentType velocity = ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4));
    const ComponentType health = ComponentType::Native<Health>();
    const auto removals = [](const Changes& changes) {
        std::vector<std::uint32_t> entities;
        for (const ComponentChange& change : changes)
        {
            REQUIRE(change.kind == ComponentChange::Kind::Removed);
            entities.push_back(change.entity);
        }
        std::sort(entities.begin(), entities.end());
        return entities;
    };

    for (const bool wholeList : {true, false})
    {
        EntityList entities;
        std::vector<std::uint32_t> holders;
        for (int index = 0; index < 3; ++index)
        {
            holders.push_back(entities.CreateEntity().GetId());
            REQUIRE(entities.Add(velocity, holders.back()));
            REQUIRE(entities.Add(health, holders.back()));
        }
        (void)entities.CreateEntity(); // One without either.
        ComponentWatch velocityWatch = entities.Watch(velocity);
        ComponentWatch healthWatch = entities.Watch(health);

        if (wholeList)
        {
            entities.Clear();
        }
        else
        {
            REQUIRE(entities.Clear(velocity) == 3);
            REQUIRE(entities.Clear(health) == 3);
        }

        INFO((wholeList ? "EntityList::Clear" : "EntityList::Clear(type)")
             << " records each holder's removal once, in some order");
        REQUIRE(removals(velocityWatch.Drain()) == holders);
        REQUIRE(removals(healthWatch.Drain()) == holders);
    }
}

TEST_CASE("a watch records nothing that happened before it was made", "[watch]")
{
    EntityList entities;
    const ComponentType velocity = ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4));
    auto entity = entities.CreateEntity();
    REQUIRE(entities.Add(velocity, entity.GetId()));
    entity.AddComponent<Health>();

    ComponentWatch velocityWatch = entities.Watch(velocity);
    ComponentWatch healthWatch = entities.Watch(ComponentType::Native<Health>());

    INFO("the components already there are not reported as adds");
    REQUIRE(velocityWatch.Drain().empty());
    REQUIRE(healthWatch.Drain().empty());
}

TEST_CASE("several watches on one type record independently", "[watch]")
{
    for (const ComponentType& type :
         {ComponentType::Native<Health>(), ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4))})
    {
        EntityList entities;
        const auto first = entities.CreateEntity().GetId();
        const auto second = entities.CreateEntity().GetId();
        ComponentWatch early = entities.Watch(type);
        REQUIRE(entities.Add(type, first));
        ComponentWatch late = entities.Watch(type);
        REQUIRE(entities.Add(type, second));

        INFO("draining one leaves the other's record alone, and each has what happened since it was made");
        REQUIRE(early.Drain() == Changes{Added(first), Added(second)});
        REQUIRE(late.Drain() == Changes{Added(second)});

        REQUIRE(entities.Remove(type, first));
        REQUIRE(late.Drain() == Changes{Removed(first)});
        REQUIRE(early.Drain() == Changes{Removed(first)});
    }
}

TEST_CASE("destroying a watch stops its recording and leaves other watches recording", "[watch]")
{
    for (const ComponentType& type :
         {ComponentType::Native<Health>(), ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4))})
    {
        EntityList entities;
        const auto entity = entities.CreateEntity().GetId();
        ComponentWatch kept = entities.Watch(type);
        {
            ComponentWatch dropped = entities.Watch(type);
            REQUIRE(entities.Add(type, entity));
        }

        INFO("changes after one watch is gone reach only the watch that remains");
        REQUIRE(entities.Remove(type, entity));
        entities.DestroyEntity(entity);
        REQUIRE(kept.Drain() == Changes{Added(entity), Removed(entity)});
    }
}

TEST_CASE("a moved watch keeps recording, and one assigned over stops", "[watch]")
{
    EntityList entities;
    const ComponentType health = ComponentType::Native<Health>();
    const auto entity = entities.CreateEntity().GetId();
    ComponentWatch original = entities.Watch(health);
    REQUIRE(entities.Add(health, entity));

    ComponentWatch moved = std::move(original);
    REQUIRE(entities.Remove(health, entity));
    INFO("the moved-to watch has the whole record; the moved-from one watches nothing");
    REQUIRE(moved.Drain() == Changes{Added(entity), Removed(entity)});
    REQUIRE(original.Drain().empty()); // NOLINT(bugprone-use-after-move): a moved-from watch watches nothing.

    moved = ComponentWatch();
    REQUIRE(entities.Add(health, entity));
    INFO("a watch assigned an empty one has stopped");
    REQUIRE(moved.Drain().empty());
}

TEST_CASE("a watch may outlive its list", "[watch]")
{
    ComponentWatch velocityWatch;
    ComponentWatch healthWatch;
    {
        EntityList entities;
        velocityWatch = entities.Watch(ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4)));
        healthWatch = entities.Watch(ComponentType::Native<Health>());
        entities.CreateEntity().AddComponent<Health>();
    }

    INFO("what it recorded is still there; destroying it afterwards is safe");
    REQUIRE(healthWatch.Drain().size() == 1);
    REQUIRE(velocityWatch.Drain().empty());
}

TEST_CASE("editing a component's value is not a change", "[watch]")
{
    EntityList entities;
    const ComponentType velocity = ComponentType::Dynamic(DynamicComponentType("Test.Velocity", 12, 4));
    auto entity = entities.CreateEntity();
    entity.AddComponent<Health>();
    REQUIRE(entities.Add(velocity, entity.GetId()));
    ComponentWatch velocityWatch = entities.Watch(velocity);
    ComponentWatch healthWatch = entities.Watch(ComponentType::Native<Health>());

    entity.GetComponent<Health>().points = 1;
    static_cast<float*>(entities.Get(velocity, entity.GetId()))[0] = 2.0F;
    entities.View<Health>().each([](Health& health) { health.points = 3; });

    INFO("writes to a component through a reference are edits, which watches do not report");
    REQUIRE(velocityWatch.Drain().empty());
    REQUIRE(healthWatch.Drain().empty());
}

TEST_CASE("an invalid dynamic type gives a watch that records nothing", "[watch]")
{
    EntityList entities;
    ComponentWatch watch = entities.Watch(ComponentType::Dynamic(DynamicComponentType("", 12, 4)));

    INFO("it drains empty and costs the list nothing");
    REQUIRE(watch.Drain().empty());
}
