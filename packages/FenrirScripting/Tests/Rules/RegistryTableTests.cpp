// The registry table's rules, with no .NET: one table stands behind the registry's components, events, shared structs
// and resources, so what is tested here through the component and event tables holds for every kind. The table is
// private to FenrirScripting (this executable is given its source directory) and is tested through its interface.
#include <catch2/catch_test_macros.hpp>

#include "RulesTypes.hpp"

#include "ComponentRegistry.hpp"

#include "FenrirScripting/ComponentRegistry.hpp"
#include "FenrirScripting/Errors.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace Fenrir;
    using Fenrir::Detail::ComponentRegistry;
    using RulesTest::Ping;
    using RulesTest::Velocity;

    ComponentRegistration Component(const char* name, bool scriptDefined = false)
    {
        auto registration = MakeComponent<Velocity>(name);
        registration.scriptDefined = scriptDefined;
        return registration;
    }

    EventRegistration Event(const char* name)
    {
        return MakeEvent<Ping>(name);
    }
} // namespace

TEST_CASE("A table hands out ids in order of registration and finds them by name", "[registry-table]")
{
    ComponentRegistry registry;
    auto& components = registry.Components();

    const auto first = components.Add(Component("Rules.First"));
    const auto second = components.Add(Component("Rules.Second"));

    INFO("Ids are positions, so the first registration is 0 and the second 1");
    REQUIRE((first == 0 && second == 1));
    INFO("Find did not give back the id Add issued");
    REQUIRE((components.Find("Rules.First") == first && components.Find("Rules.Second") == second));
    INFO("Get did not give back the registration under its id");
    REQUIRE((components.Get(second) != nullptr && components.Get(second)->name == "Rules.Second"));
    INFO("All did not list both registrations in id order");
    REQUIRE((components.All().size() == 2 && components.All()[0].name == "Rules.First"));
    INFO("A name nobody registered, or an id nobody issued, should be nothing");
    REQUIRE((components.Find("Rules.Nothing") == -1 && components.Get(2) == nullptr && components.Get(-1) == nullptr));
    INFO("The table's noun is what its callers' messages say");
    REQUIRE(components.Noun() == "component");
}

TEST_CASE("A table refuses an empty name and a name it already holds", "[registry-table]")
{
    ComponentRegistry registry;
    auto& components = registry.Components();
    REQUIRE(components.Add(Component("Rules.Taken")) == 0);

    INFO("An empty name was registered");
    REQUIRE(components.Add(Component("")) == -1);
    INFO("A name registered once was registered again");
    REQUIRE(components.Add(Component("Rules.Taken")) == -1);
    INFO("A refused registration should leave the table as it was");
    REQUIRE(components.All().size() == 1);
}

TEST_CASE("A name one table holds is refused by every other table of the registry", "[registry-table]")
{
    ComponentRegistry registry;
    REQUIRE(registry.Components().Add(Component("Rules.Shared")) == 0);

    INFO("The event table accepted a name the component table holds");
    REQUIRE(registry.Events().Add(Event("Rules.Shared")) == -1);
    INFO("The event table's batch accepted a name the component table holds");
    std::vector<EventRegistration> batch;
    batch.push_back(Event("Rules.Shared"));
    REQUIRE_FALSE(registry.Events().AddAll(std::move(batch)));
    INFO("The event table should still accept a name no table holds");
    REQUIRE(registry.Events().Add(Event("Rules.Own")) == 0);
}

TEST_CASE("AddAll adds every registration or none of them", "[registry-table]")
{
    ComponentRegistry registry;
    auto& components = registry.Components();
    REQUIRE(components.Add(Component("Rules.Taken")) == 0);

    SECTION("one has an empty name")
    {
        std::vector<ComponentRegistration> batch;
        batch.push_back(Component("Rules.A"));
        batch.push_back(Component(""));
        REQUIRE_FALSE(components.AddAll(std::move(batch)));
    }
    SECTION("one has a taken name")
    {
        std::vector<ComponentRegistration> batch;
        batch.push_back(Component("Rules.A"));
        batch.push_back(Component("Rules.Taken"));
        REQUIRE_FALSE(components.AddAll(std::move(batch)));
    }
    SECTION("one name appears twice")
    {
        std::vector<ComponentRegistration> batch;
        batch.push_back(Component("Rules.A"));
        batch.push_back(Component("Rules.A"));
        REQUIRE_FALSE(components.AddAll(std::move(batch)));
    }
    INFO("A refused batch registered some of its members");
    REQUIRE((components.All().size() == 1 && components.Find("Rules.A") == -1));

    std::vector<ComponentRegistration> batch;
    batch.push_back(Component("Rules.A"));
    batch.push_back(Component("Rules.B"));
    INFO("A batch of usable, free, distinct names was refused");
    REQUIRE(components.AddAll(std::move(batch)));
    INFO("The batch's members should follow the ids already issued, in batch order");
    REQUIRE((components.Find("Rules.A") == 1 && components.Find("Rules.B") == 2));
}

TEST_CASE("Resolve gives the id to the C# type the name is registered for and refuses another", "[registry-table]")
{
    ComponentRegistry registry;
    auto& components = registry.Components();
    const auto velocity = components.Add(Component("Rules.Velocity"));
    const std::string typeName(Iris::StructMapping<Velocity>::typeName);

    INFO("The type the registration names did not get the id");
    REQUIRE(components.Resolve("Rules.Velocity", typeName) == velocity);
    INFO("A different C# type claiming the name should be refused as WrongType");
    REQUIRE(components.Resolve("Rules.Velocity", "Other.Struct") ==
            static_cast<std::int32_t>(RegistryError::WrongType));
    INFO("A name nobody registered should be NotRegistered whatever type asks");
    REQUIRE(components.Resolve("Rules.Nothing", typeName) == static_cast<std::int32_t>(RegistryError::NotRegistered));
}

TEST_CASE("RemoveWhere forgets what it selects and re-ids what remains", "[registry-table]")
{
    ComponentRegistry registry;
    auto& components = registry.Components();
    REQUIRE(components.Add(Component("Rules.Native")) == 0);
    REQUIRE(components.Add(Component("Rules.Script", true)) == 1);
    REQUIRE(components.Add(Component("Rules.Native2")) == 2);
    const auto before = registry.Generation();

    components.RemoveWhere([](const ComponentRegistration& registration) { return registration.scriptDefined; });

    INFO("The selected registration was kept");
    REQUIRE((components.Find("Rules.Script") == -1 && components.All().size() == 2));
    INFO("Ids are positions: the registration after the removed one should move down to its place");
    REQUIRE((components.Find("Rules.Native") == 0 && components.Find("Rules.Native2") == 1));
    INFO("Get should follow the new ids");
    REQUIRE(components.Get(1)->name == "Rules.Native2");
    INFO("The ids moved, so the generation should have changed");
    REQUIRE(registry.Generation() != before);
    INFO("A removed name should be free again, for any table");
    REQUIRE(registry.Events().Add(Event("Rules.Script")) == 0);
}

TEST_CASE("The generation changes when a table changes and not when it is read", "[registry-table]")
{
    ComponentRegistry registry;
    const auto fresh = registry.Generation();
    ComponentRegistry other;
    INFO("Two registries should never share a generation");
    REQUIRE(other.Generation() != fresh);

    (void)registry.Components().Find("Rules.Nothing");
    (void)registry.Components().Resolve("Rules.Nothing", "Any");
    INFO("A lookup changed the generation");
    REQUIRE(registry.Generation() == fresh);

    REQUIRE(registry.Events().Add(Event("Rules.Ping")) == 0);
    const auto afterEvent = registry.Generation();
    INFO("Adding an event did not change the registry's one generation");
    REQUIRE(afterEvent != fresh);

    REQUIRE(registry.Resources().Add(MakeSceneResource<Velocity>("Rules.Wind")) == 0);
    INFO("Adding a resource did not change the registry's one generation");
    REQUIRE(registry.Generation() != afterEvent);

    INFO("A refused Add changed the generation");
    const auto settled = registry.Generation();
    REQUIRE(registry.Events().Add(Event("Rules.Ping")) == -1);
    REQUIRE(registry.Generation() == settled);
}

TEST_CASE("RemoveScriptDefined sweeps the components and events a script defined and keeps the native ones",
          "[registry-table]")
{
    ComponentRegistry registry;
    REQUIRE(registry.Components().Add(Component("Rules.Native")) == 0);
    REQUIRE(registry.Components().Add(Component("Rules.ScriptComponent", true)) == 1);
    auto scriptEvent = Event("Rules.ScriptEvent");
    scriptEvent.scriptDefined = true;
    REQUIRE(registry.Events().Add(Event("Rules.NativeEvent")) == 0);
    REQUIRE(registry.Events().Add(std::move(scriptEvent)) == 1);

    registry.RemoveScriptDefined();

    INFO("A script-defined component or event survived the sweep");
    REQUIRE((registry.Components().Find("Rules.ScriptComponent") == -1 &&
             registry.Events().Find("Rules.ScriptEvent") == -1));
    INFO("A native component or event was swept");
    REQUIRE((registry.Components().Find("Rules.Native") == 0 && registry.Events().Find("Rules.NativeEvent") == 0));
}
