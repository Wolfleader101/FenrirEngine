// What a loaded script assembly may define, with no .NET: reconcile decides, from the assembly's report, what is
// refused and what is kept. The assembly side is a small interface with two adapters, one over a loaded Iris component
// and, here, one that returns literals, so every refusal can be checked without an assembly that lies.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "../HeadlessApp.hpp"
#include "RulesTypes.hpp"

#include "ComponentRegistry.hpp"
#include "ScriptSchema.hpp"

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"

#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace Fenrir;
    using Fenrir::Detail::ComponentRegistry;
    using Fenrir::Detail::OwnedScriptData;
    using Fenrir::Detail::ScriptAssembly;
    using Fenrir::Detail::ScriptComponentDescription;
    using Fenrir::Detail::ScriptKind;

    // Names found by brute force against the storage-id hash (see FenrirECS's tests): these two have the same id.
    constexpr const char* CollidingFirst = "Test.Collide.39238";
    constexpr const char* CollidingSecond = "Test.Collide.644126";

    Iris::CallResult<void> Succeeded()
    {
        return Iris::CallResult<void>{Iris::CallStatus::Success, {}, false};
    }

    // An assembly that reports exactly what a case says, and answers every other question as the case says.
    class LiteralAssembly final : public ScriptAssembly
    {
      public:
        std::vector<ScriptComponentDescription> components;
        std::vector<ScriptComponentDescription> events;
        std::map<std::string, std::string> fingerprints; // By name; a name with none has an empty fingerprint.
        Iris::CallResult<void> report = Succeeded();     // What reporting the components fails with, if it does.
        Iris::CallResult<void> nativeCheck = Succeeded();
        int nativeChecks = 0; // How many times reconcile asked for the native components to be checked.

        Iris::CallResult<void> Report(ScriptKind kind, std::vector<ScriptComponentDescription>& found) override
        {
            if (kind == ScriptKind::Component && !report.Succeeded())
            {
                return report;
            }
            found = kind == ScriptKind::Component ? components : events;
            return Succeeded();
        }

        std::string Fingerprint(const ScriptComponentDescription& description) override
        {
            const auto at = fingerprints.find(description.name);
            return at == fingerprints.end() ? std::string() : at->second;
        }

        Iris::CallResult<void> CheckNativeAgainstCSharp(const ComponentRegistry&) override
        {
            ++nativeChecks;
            return nativeCheck;
        }
    };

    // The App, the registry (the engine's own components in it, as a Scripting has them) and what is still owned.
    struct World
    {
        Testing::HeadlessApp headless;
        ComponentRegistry registry;
        OwnedScriptData owned;

        World()
        {
            RulesTest::AddEngineComponents(registry);
        }

        EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        Iris::CallResult<void> Reconcile(ScriptAssembly& assembly)
        {
            return Fenrir::Detail::Reconcile(headless.Get(), registry, assembly, owned);
        }

        // What a reload does between two generations: the script-defined registrations go, the data stays until
        // reconcile decides.
        void ReleaseGeneration()
        {
            registry.RemoveScriptDefined();
        }

        // A script component of this name on a new entity, as a script would have added it.
        std::uint32_t EntityWith(const char* componentName)
        {
            const auto id = registry.Find(componentName);
            REQUIRE(id >= 0);
            const auto entity = Entities().CreateEntity().GetId();
            REQUIRE(Entities().Add(registry.Get(id)->type, entity));
            return entity;
        }

        [[nodiscard]] bool Has(const char* componentName, std::uint32_t entity)
        {
            const auto id = registry.Find(componentName);
            return id >= 0 && Entities().Has(registry.Get(id)->type, entity);
        }
    };

    ScriptComponentDescription Component(const char* name, std::size_t size = 8, std::size_t alignment = 4)
    {
        return ScriptComponentDescription{name, size, alignment};
    }
} // namespace

TEST_CASE("a sound report registers every component it defines", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health", 4, 4), Component("Rules.Wide", 32, 16)};

    const auto result = world.Reconcile(assembly);

    INFO("A sound report should be accepted: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    const auto health = world.registry.Find("Rules.Health");
    const auto wide = world.registry.Find("Rules.Wide");
    INFO("Both components should be registered");
    REQUIRE((health >= 0 && wide >= 0));
    INFO("They should be script-defined, with the size and alignment the assembly reported");
    REQUIRE((world.registry.Get(health)->scriptDefined && world.registry.Get(health)->type.Size() == 4));
    REQUIRE((world.registry.Get(wide)->type.Size() == 32 && world.registry.Get(wide)->type.Alignment() == 16));
}

TEST_CASE("a component reported twice is refused", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health"), Component("Rules.Health")};

    const auto result = world.Reconcile(assembly);

    INFO("A component reported twice should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say which and why");
    REQUIRE(result.diagnostic == "The script reported the component 'Rules.Health' more than once.");
}

TEST_CASE("a component with a layout no C# struct has is refused", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    const auto [size, alignment] = GENERATE(table<std::size_t, std::size_t>({
        {0, 4}, // No size.
        {8, 3}, // An alignment that is not a power of two.
        {6, 4}, // A size that is not a multiple of the alignment.
        {8, 0}, // No alignment.
    }));
    assembly.components = {Component("Rules.Odd", size, alignment)};

    const auto result = world.Reconcile(assembly);

    INFO("A size of " << size << " with an alignment of " << alignment << " should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say what was reported");
    REQUIRE(result.diagnostic == "The script component 'Rules.Odd' reported a size of " + std::to_string(size) +
                                     " and an alignment of " + std::to_string(alignment) + ", which no C# struct has.");
}

TEST_CASE("a script component cannot take the name of a native component", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Fenrir.Transform")};

    const auto result = world.Reconcile(assembly);

    INFO("A script defining the engine's Transform should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say the engine already has it");
    REQUIRE(result.diagnostic ==
            "The script defines a component 'Fenrir.Transform', but the engine already has a component of that name.");
}

TEST_CASE("two script components whose names share a storage id are refused, naming both", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component(CollidingFirst), Component(CollidingSecond)};

    const auto result = world.Reconcile(assembly);

    INFO("Two names with one storage id should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should name both");
    REQUIRE(result.diagnostic == std::string("The script components '") + CollidingFirst + "' and '" + CollidingSecond +
                                     "' map to the same storage; rename one.");
}

TEST_CASE("a refused report leaves the registry unchanged and makes no storage", "[reconcile]")
{
    World world;
    const auto registered = world.registry.All().size();
    const auto storages = world.Entities().ComponentStorageCount();
    LiteralAssembly assembly;
    // A sound component first, then one that is refused: nothing of either may remain.
    assembly.components = {Component("Rules.Health"), Component("Fenrir.Transform")};

    const auto result = world.Reconcile(assembly);

    INFO("The report should be refused");
    REQUIRE(!result.Succeeded());
    INFO("No component should have been registered");
    REQUIRE((world.registry.All().size() == registered && world.registry.Find("Rules.Health") < 0));
    INFO("No storage should have been made");
    REQUIRE(world.Entities().ComponentStorageCount() == storages);
    INFO("Nothing should be recorded as owned");
    REQUIRE(world.owned.components.empty());
}

TEST_CASE("a report the assembly could not give is refused with its explanation", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.report = Iris::CallResult<void>{Iris::CallStatus::ManagedException,
                                             "The script assembly could not report its components: it threw", false};

    const auto result = world.Reconcile(assembly);

    INFO("A report that failed should fail the load");
    REQUIRE(!result.Succeeded());
    INFO("The explanation and the status should be the assembly's own");
    REQUIRE((result.status == Iris::CallStatus::ManagedException &&
             result.diagnostic == "The script assembly could not report its components: it threw"));
    INFO("Nothing should have been registered");
    REQUIRE(world.registry.Find("Rules.Health") < 0);
}

// Events.

TEST_CASE("an event reported twice is refused", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Beep", 4, 4), Component("Rules.Beep", 4, 4)};

    const auto result = world.Reconcile(assembly);

    INFO("An event reported twice should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say which and why");
    REQUIRE(result.diagnostic == "The script reported the event 'Rules.Beep' more than once.");
}

TEST_CASE("an event with a layout the engine cannot store is refused", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    const auto [size, alignment] = GENERATE(table<std::size_t, std::size_t>({
        {0, 4},   // No size.
        {8, 3},   // An alignment that is not a power of two.
        {6, 4},   // A size that is not a multiple of the alignment.
        {8, 0},   // No alignment.
        {64, 32}, // An alignment past the largest the engine's queues keep.
    }));
    assembly.events = {Component("Rules.Odd", size, alignment)};

    const auto result = world.Reconcile(assembly);

    INFO("A size of " << size << " with an alignment of " << alignment << " should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say what was reported");
    REQUIRE(result.diagnostic == "The script event 'Rules.Odd' reported a size of " + std::to_string(size) +
                                     " and an alignment of " + std::to_string(alignment) +
                                     ", which the engine cannot store.");
}

TEST_CASE("an event cannot take the name of an event the engine already has", "[reconcile]")
{
    World world;
    auto registration = MakeEvent<RulesTest::Ping>("Rules.Ping");
    REQUIRE(registration.bind(world.headless.Get()));
    REQUIRE(world.registry.AddEvent(std::move(registration)) >= 0);
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Ping", 4, 4)};

    const auto result = world.Reconcile(assembly);

    INFO("A script event named like a native one should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say the engine already has it");
    REQUIRE(result.diagnostic ==
            "The script defines an event 'Rules.Ping', but the engine already has an event of that name.");
}

TEST_CASE("an event cannot take the name of a queue the app already has", "[reconcile]")
{
    World world;
    REQUIRE(world.headless.Get().CreateEventQueue("Rules.Taken", 4, 4) != nullptr);
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Taken", 4, 4)};

    const auto result = world.Reconcile(assembly);

    INFO("A script event named like an app queue should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say the engine already has it");
    REQUIRE(result.diagnostic ==
            "The script defines an event 'Rules.Taken', but the engine already has an event of that name.");
}

TEST_CASE("an event may keep the name of a queue the previous generation made", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Beep", 4, 4)};
    REQUIRE(world.Reconcile(assembly).Succeeded());
    world.ReleaseGeneration();

    const auto again = world.Reconcile(assembly);

    INFO("The next generation should be allowed the queue the last one made: " + again.diagnostic);
    REQUIRE(again.Succeeded());
}

TEST_CASE("a sound report registers its events and makes their queues", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4), Component("Rules.Blast", 32, 16)};

    const auto result = world.Reconcile(assembly);

    INFO("A sound report of components and events should be accepted: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("Both events should be registered");
    REQUIRE((world.registry.FindEvent("Rules.Beep") >= 0 && world.registry.FindEvent("Rules.Blast") >= 0));
    INFO("Each should have a queue with its own event size");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") != nullptr);
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->EventSize() == 4);
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Blast")->EventSize() == 32);
}

TEST_CASE("a refused event leaves no component registered and no queue made", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4), Component("Rules.Odd", 8, 3)};

    const auto result = world.Reconcile(assembly);

    INFO("The report should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The sound component and event of the same report should not remain");
    REQUIRE((world.registry.Find("Rules.Health") < 0 && world.registry.FindEvent("Rules.Beep") < 0));
    INFO("No queue should have been made");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") == nullptr);
}

// What a reload leaves behind.

TEST_CASE("a component whose layout is unchanged keeps its data across a reload", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.fingerprints = {{"Rules.Health", "8/4;Current:int"}};
    REQUIRE(world.Reconcile(assembly).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");
    world.ReleaseGeneration();

    const auto again = world.Reconcile(assembly);

    INFO("The same layout should reload: " + again.diagnostic);
    REQUIRE(again.Succeeded());
    INFO("The entity should still have its data");
    REQUIRE(world.Has("Rules.Health", entity));
    INFO("No warning should have been given");
    REQUIRE(!world.headless.Logger().Contains(Testing::LogLevel::Warn, "were removed"));
}

TEST_CASE("a component whose layout changed is removed from every scene with a warning", "[reconcile]")
{
    World world;
    LiteralAssembly before;
    before.components = {Component("Rules.Health")};
    before.fingerprints = {{"Rules.Health", "8/4;Current:int"}};
    REQUIRE(world.Reconcile(before).Succeeded());
    const auto first = world.EntityWith("Rules.Health");
    const auto second = world.EntityWith("Rules.Health");
    world.ReleaseGeneration();
    LiteralAssembly after;
    after.components = {Component("Rules.Health", 16, 8)};
    after.fingerprints = {{"Rules.Health", "16/8;Current:long"}};

    const auto result = world.Reconcile(after);

    INFO("The new layout should load: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("No entity should keep the old bytes");
    REQUIRE((!world.Has("Rules.Health", first) && !world.Has("Rules.Health", second)));
    INFO("The warning should say the component changed and how many were removed");
    REQUIRE(world.headless.Logger().Contains(Testing::LogLevel::Warn,
                                             "the script component 'Rules.Health' changed its layout; 2 component(s) "
                                             "were removed so nothing reads the old bytes as the new layout."));
    INFO("The new layout is what is registered");
    REQUIRE(world.registry.Get(world.registry.Find("Rules.Health"))->type.Size() == 16);
}

TEST_CASE("a component that no longer exists is removed from every scene with a warning", "[reconcile]")
{
    World world;
    LiteralAssembly before;
    before.components = {Component("Rules.Health")};
    REQUIRE(world.Reconcile(before).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");
    world.ReleaseGeneration();
    LiteralAssembly after;

    const auto result = world.Reconcile(after);

    INFO("A script that dropped the component should load: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("The entity should not keep data nobody defines");
    REQUIRE(world.registry.Find("Rules.Health") < 0);
    REQUIRE(world.Entities().Count(ComponentType::Dynamic(DynamicComponentType("Rules.Health", 8, 4))) == 0);
    REQUIRE(world.Entities().HasEntity(entity));
    INFO("The warning should say it no longer exists");
    REQUIRE(world.headless.Logger().Contains(Testing::LogLevel::Warn,
                                             "the script component 'Rules.Health' no longer exists; 1 component(s) "
                                             "were removed so nothing reads the old bytes as the new layout."));
    INFO("It should no longer be owned");
    REQUIRE(world.owned.components.empty());
}

TEST_CASE("an event queue whose layout is unchanged is kept with its events", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Beep", 4, 4)};
    assembly.fingerprints = {{"Rules.Beep", "4/4;Value:int"}};
    REQUIRE(world.Reconcile(assembly).Succeeded());
    auto* queue = world.headless.Get().FindEventQueue("Rules.Beep");
    const std::int32_t event = 5;
    REQUIRE(queue->SendRaw(&event));
    world.ReleaseGeneration();

    REQUIRE(world.Reconcile(assembly).Succeeded());

    INFO("The queue should be the same one");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") == queue);
    INFO("Its event should still be in it");
    REQUIRE(queue->ReadRaw().count == 1);
}

TEST_CASE("an event that no longer exists loses its queue with a warning", "[reconcile]")
{
    World world;
    LiteralAssembly before;
    before.events = {Component("Rules.Beep", 4, 4)};
    REQUIRE(world.Reconcile(before).Succeeded());
    world.ReleaseGeneration();
    LiteralAssembly after;

    const auto result = world.Reconcile(after);

    INFO("A script that dropped the event should load: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("Its queue should be gone");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") == nullptr);
    INFO("The warning should say it no longer exists");
    REQUIRE(world.headless.Logger().Contains(
        Testing::LogLevel::Warn,
        "the script event 'Rules.Beep' no longer exists; its queue and the events in it were dropped."));
}

TEST_CASE("an event queue whose layout changed is removed with its events and made again", "[reconcile]")
{
    World world;
    LiteralAssembly before;
    before.events = {Component("Rules.Beep", 4, 4)};
    before.fingerprints = {{"Rules.Beep", "4/4;Value:int"}};
    REQUIRE(world.Reconcile(before).Succeeded());
    const std::int32_t event = 5;
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->SendRaw(&event));
    world.ReleaseGeneration();
    LiteralAssembly after;
    after.events = {Component("Rules.Beep", 8, 8)};
    after.fingerprints = {{"Rules.Beep", "8/8;Value:long"}};

    const auto result = world.Reconcile(after);

    INFO("The new layout should load: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    const auto* queue = world.headless.Get().FindEventQueue("Rules.Beep");
    INFO("The queue should exist with the new event size and none of the old events");
    REQUIRE(queue != nullptr);
    REQUIRE((queue->EventSize() == 8 && queue->ReadRaw().count == 0));
    INFO("The warning should say the queue and its events were dropped");
    REQUIRE(world.headless.Logger().Contains(
        Testing::LogLevel::Warn,
        "the script event 'Rules.Beep' changed its layout; its queue and the events in it were dropped."));
}

// The native check, and its place in the order.

TEST_CASE("a failure of the native components' check changes nothing", "[reconcile]")
{
    World world;
    LiteralAssembly before;
    before.components = {Component("Rules.Health")};
    before.events = {Component("Rules.Beep", 4, 4)};
    before.fingerprints = {{"Rules.Health", "8/4;Current:int"}, {"Rules.Beep", "4/4;Value:int"}};
    REQUIRE(world.Reconcile(before).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");
    const std::int32_t event = 5;
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->SendRaw(&event));
    world.ReleaseGeneration();
    LiteralAssembly after;
    after.components = {Component("Rules.Health", 16, 8), Component("Rules.Armour")};
    after.events = {Component("Rules.Beep", 8, 8)};
    after.fingerprints = {{"Rules.Health", "16/8;Current:long"}, {"Rules.Beep", "8/8;Value:long"}};
    after.nativeCheck = Iris::CallResult<void>{Iris::CallStatus::InvalidArgument,
                                               "Component 'Test.BadHealth' does not match its C# struct: Max", false};

    const auto result = world.Reconcile(after);

    INFO("The check's failure should be the result, unchanged");
    REQUIRE(
        (!result.Succeeded() && result.diagnostic == "Component 'Test.BadHealth' does not match its C# struct: Max"));
    INFO("The previous generation's component data should not have been dropped, even though the layout changed");
    REQUIRE(world.Entities().Count(ComponentType::Dynamic(DynamicComponentType("Rules.Health", 8, 4))) == 1);
    REQUIRE(world.Entities().HasEntity(entity));
    INFO("Nor should its event queue and the event in it");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->EventSize() == 4);
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->ReadRaw().count == 1);
    INFO("Nothing of the new report should be registered, and ownership should be as it was");
    REQUIRE((world.registry.Find("Rules.Armour") < 0 && world.registry.Find("Rules.Health") < 0));
    REQUIRE((world.owned.components.size() == 1 && world.owned.components.contains("Rules.Health")));
    INFO("A warning about dropping data should not have been given");
    REQUIRE(!world.headless.Logger().Contains(Testing::LogLevel::Warn, "were removed"));
}

TEST_CASE("the native components are checked only after the definitions' own rules pass", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health"), Component("Rules.Health")};
    assembly.nativeCheck = Iris::CallResult<void>{Iris::CallStatus::InvalidArgument, "the native check", false};

    const auto result = world.Reconcile(assembly);

    INFO("The definitions' own refusal should be the one reported");
    REQUIRE(result.diagnostic == "The script reported the component 'Rules.Health' more than once.");
    INFO("The native check should not have been asked");
    REQUIRE(assembly.nativeChecks == 0);
}

TEST_CASE("the native components are checked once for a report that passes its own rules", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4)};

    REQUIRE(world.Reconcile(assembly).Succeeded());

    INFO("Components and events share one native check");
    REQUIRE(assembly.nativeChecks == 1);
}

TEST_CASE("dropping all script data empties what is owned", "[reconcile]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4)};
    REQUIRE(world.Reconcile(assembly).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");
    world.ReleaseGeneration();

    Fenrir::Detail::DropScriptData(world.headless.Get(), world.owned);

    INFO("The component data should be gone from the entity");
    REQUIRE(world.Entities().Count(ComponentType::Dynamic(DynamicComponentType("Rules.Health", 8, 4))) == 0);
    REQUIRE(world.Entities().HasEntity(entity));
    INFO("The event queue should be gone");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") == nullptr);
    INFO("Nothing should be owned");
    REQUIRE((world.owned.components.empty() && world.owned.events.empty()));
}
