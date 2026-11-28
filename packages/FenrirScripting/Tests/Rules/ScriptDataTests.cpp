// What a loaded script assembly may define, with no .NET: admission decides, from the assembly's script report, what is
// refused and what is kept, and keeps it all or nothing. The assembly side is a small interface with two adapters, one
// over a loaded Iris component and, here, one that returns literals, so every refusal can be checked without an
// assembly that lies.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "HeadlessApp.hpp"
#include "RulesTypes.hpp"

#include "ComponentRegistry.hpp"
#include "ScriptData.hpp"

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/TypeAttachments.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    using namespace Fenrir;
    using Fenrir::Detail::ComponentRegistry;
    using Fenrir::Detail::ScriptAssembly;
    using Fenrir::Detail::ScriptComponentDescription;
    using Fenrir::Detail::ScriptData;
    using Fenrir::Detail::ScriptReport;

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
        std::vector<ScriptComponentDescription> structs;
        std::map<std::string, std::string> fingerprints; // By name; a name with none has an empty fingerprint.
        Iris::CallResult<void> report = Succeeded();     // What giving the report fails with, if it does.
        Iris::CallResult<void> nativeCheck = Succeeded();
        int nativeChecks = 0; // How many times admission asked for the native components to be checked.

        Iris::CallResult<void> Report(ScriptReport& found) override
        {
            if (!report.Succeeded())
            {
                return report;
            }
            found.components = components;
            found.events = events;
            found.structs = structs;
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

    // The App, the registry (the engine's own components in it, as a Scripting has them) and the script data.
    struct World
    {
        Testing::HeadlessApp headless;
        ComponentRegistry registry;
        ScriptData data{registry};

        World()
        {
            RulesTest::AddEngineComponents(registry);
        }

        EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        Iris::CallResult<void> Admit(ScriptAssembly& assembly)
        {
            return data.Admit(headless.Get(), assembly);
        }

        // What a reload does between two generations: the script-defined registrations go, the data stays until the
        // next admission decides.
        void ReleaseGeneration()
        {
            data.Forget();
        }

        // A script component of this name on a new entity, as a script would have added it.
        std::uint32_t EntityWith(const char* componentName)
        {
            const auto id = registry.Components().Find(componentName);
            REQUIRE(id >= 0);
            const auto entity = Entities().CreateEntity().GetId();
            REQUIRE(Entities().Add(registry.Components().Get(id)->type, entity));
            return entity;
        }

        [[nodiscard]] bool Has(const char* componentName, std::uint32_t entity)
        {
            const auto id = registry.Components().Find(componentName);
            return id >= 0 && Entities().Has(registry.Components().Get(id)->type, entity);
        }

        // How many components of a script type the scene holds, by the type's layout, whether or not it is registered.
        [[nodiscard]] std::size_t CountOf(const char* componentName, std::size_t size = 8, std::size_t alignment = 4)
        {
            return Entities().Count(ComponentType::Dynamic(DynamicComponentType(componentName, size, alignment)));
        }

        [[nodiscard]] std::size_t ScriptDefinedCount() const
        {
            std::size_t count = 0;
            for (const auto& registration : registry.Components().All())
            {
                count += registration.scriptDefined ? 1 : 0;
            }
            for (const auto& registration : registry.Events().All())
            {
                count += registration.scriptDefined ? 1 : 0;
            }
            return count;
        }
    };

    ScriptComponentDescription Component(const char* name, std::size_t size = 8, std::size_t alignment = 4)
    {
        return ScriptComponentDescription{name, size, alignment};
    }
} // namespace

TEST_CASE("a sound report registers every component it defines", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health", 4, 4), Component("Rules.Wide", 32, 16)};

    const auto result = world.Admit(assembly);

    INFO("A sound report should be accepted: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    const auto health = world.registry.Components().Find("Rules.Health");
    const auto wide = world.registry.Components().Find("Rules.Wide");
    INFO("Both components should be registered");
    REQUIRE((health >= 0 && wide >= 0));
    INFO("They should be script-defined, with the size and alignment the assembly reported");
    REQUIRE((world.registry.Components().Get(health)->scriptDefined &&
             world.registry.Components().Get(health)->type.Size() == 4));
    REQUIRE((world.registry.Components().Get(wide)->type.Size() == 32 &&
             world.registry.Components().Get(wide)->type.Alignment() == 16));
}

TEST_CASE("a component reported twice is refused", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health"), Component("Rules.Health")};

    const auto result = world.Admit(assembly);

    INFO("A component reported twice should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say which and why");
    REQUIRE(result.diagnostic == "The script reported the component 'Rules.Health' more than once.");
}

TEST_CASE("a component with a layout no C# struct has is refused", "[script-data]")
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

    const auto result = world.Admit(assembly);

    INFO("A size of " << size << " with an alignment of " << alignment << " should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say what was reported");
    REQUIRE(result.diagnostic == "The script component 'Rules.Odd' reported a size of " + std::to_string(size) +
                                     " and an alignment of " + std::to_string(alignment) + ", which no C# struct has.");
}

TEST_CASE("a script component cannot take the name of a native component", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Fenrir.Transform")};

    const auto result = world.Admit(assembly);

    INFO("A script defining the engine's Transform should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say the engine already has it");
    REQUIRE(result.diagnostic ==
            "The script defines a component 'Fenrir.Transform', but the engine already has a component of that name.");
}

TEST_CASE("two script components whose names share a storage id are refused, naming both", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component(CollidingFirst), Component(CollidingSecond)};

    const auto result = world.Admit(assembly);

    INFO("Two names with one storage id should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should name both");
    REQUIRE(result.diagnostic == std::string("The script components '") + CollidingFirst + "' and '" + CollidingSecond +
                                     "' map to the same storage; rename one.");
}

TEST_CASE("a refused report leaves the registry unchanged and makes no storage", "[script-data]")
{
    World world;
    const auto registered = world.registry.Components().All().size();
    const auto storages = world.Entities().ComponentStorageCount();
    LiteralAssembly assembly;
    // A sound component first, then one that is refused: nothing of either may remain.
    assembly.components = {Component("Rules.Health"), Component("Fenrir.Transform")};

    const auto result = world.Admit(assembly);

    INFO("The report should be refused");
    REQUIRE(!result.Succeeded());
    INFO("No component should have been registered");
    REQUIRE((world.registry.Components().All().size() == registered &&
             world.registry.Components().Find("Rules.Health") < 0));
    INFO("No storage should have been made");
    REQUIRE(world.Entities().ComponentStorageCount() == storages);
    INFO("Nothing should be recorded as owned");
    REQUIRE(world.data.OwnedComponents().empty());
}

TEST_CASE("a report the assembly could not give is refused with its explanation", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.report = Iris::CallResult<void>{Iris::CallStatus::ManagedException,
                                             "The script assembly could not report its components: it threw", false};

    const auto result = world.Admit(assembly);

    INFO("A report that failed should fail the load");
    REQUIRE(!result.Succeeded());
    INFO("The explanation and the status should be the assembly's own");
    REQUIRE((result.status == Iris::CallStatus::ManagedException &&
             result.diagnostic == "The script assembly could not report its components: it threw"));
    INFO("Nothing should have been registered");
    REQUIRE(world.registry.Components().Find("Rules.Health") < 0);
}

// Events.

TEST_CASE("an event reported twice is refused", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Beep", 4, 4), Component("Rules.Beep", 4, 4)};

    const auto result = world.Admit(assembly);

    INFO("An event reported twice should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say which and why");
    REQUIRE(result.diagnostic == "The script reported the event 'Rules.Beep' more than once.");
}

TEST_CASE("an event with a layout the engine cannot store is refused", "[script-data]")
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

    const auto result = world.Admit(assembly);

    INFO("A size of " << size << " with an alignment of " << alignment << " should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say what was reported");
    REQUIRE(result.diagnostic == "The script event 'Rules.Odd' reported a size of " + std::to_string(size) +
                                     " and an alignment of " + std::to_string(alignment) +
                                     ", which the engine cannot store.");
}

TEST_CASE("an event cannot take the name of an event the engine already has", "[script-data]")
{
    World world;
    auto registration = MakeEvent<RulesTest::Ping>("Rules.Ping");
    REQUIRE(registration.bind(world.headless.Get()));
    REQUIRE(world.registry.Events().Add(std::move(registration)) >= 0);
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Ping", 4, 4)};

    const auto result = world.Admit(assembly);

    INFO("A script event named like a native one should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say the engine already has it");
    REQUIRE(result.diagnostic ==
            "The script defines an event 'Rules.Ping', but the engine already has an event of that name.");
}

TEST_CASE("an event cannot take the name of a queue the app already has", "[script-data]")
{
    World world;
    REQUIRE(world.headless.Get().CreateEventQueue("Rules.Taken", 4, 4) != nullptr);
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Taken", 4, 4)};

    const auto result = world.Admit(assembly);

    INFO("A script event named like an app queue should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The refusal should say the engine already has it");
    REQUIRE(result.diagnostic ==
            "The script defines an event 'Rules.Taken', but the engine already has an event of that name.");
}

TEST_CASE("an event may keep the name of a queue the previous generation made", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Beep", 4, 4)};
    REQUIRE(world.Admit(assembly).Succeeded());
    world.ReleaseGeneration();

    const auto again = world.Admit(assembly);

    INFO("The next generation should be allowed the queue the last one made: " + again.diagnostic);
    REQUIRE(again.Succeeded());
}

TEST_CASE("a sound report registers its events and makes their queues", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4), Component("Rules.Blast", 32, 16)};

    const auto result = world.Admit(assembly);

    INFO("A sound report of components and events should be accepted: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("Both events should be registered");
    REQUIRE((world.registry.Events().Find("Rules.Beep") >= 0 && world.registry.Events().Find("Rules.Blast") >= 0));
    INFO("Each should have a queue with its own event size");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") != nullptr);
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->EventSize() == 4);
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Blast")->EventSize() == 32);
}

TEST_CASE("a refused event leaves no component registered and no queue made", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4), Component("Rules.Odd", 8, 3)};

    const auto result = world.Admit(assembly);

    INFO("The report should be refused");
    REQUIRE(!result.Succeeded());
    INFO("The sound component and event of the same report should not remain");
    REQUIRE((world.registry.Components().Find("Rules.Health") < 0 && world.registry.Events().Find("Rules.Beep") < 0));
    INFO("No queue should have been made");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") == nullptr);
}

// What a reload leaves behind.

TEST_CASE("a component whose layout is unchanged keeps its data across a reload", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.fingerprints = {{"Rules.Health", "8/4;Current:int"}};
    REQUIRE(world.Admit(assembly).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");
    world.ReleaseGeneration();

    const auto again = world.Admit(assembly);

    INFO("The same layout should reload: " + again.diagnostic);
    REQUIRE(again.Succeeded());
    INFO("The entity should still have its data");
    REQUIRE(world.Has("Rules.Health", entity));
    INFO("No warning should have been given");
    REQUIRE(!world.headless.Logger().Contains(Testing::LogLevel::Warn, "were removed"));
}

TEST_CASE("a component whose layout changed is removed from every scene with a warning", "[script-data]")
{
    World world;
    LiteralAssembly before;
    before.components = {Component("Rules.Health")};
    before.fingerprints = {{"Rules.Health", "8/4;Current:int"}};
    REQUIRE(world.Admit(before).Succeeded());
    const auto first = world.EntityWith("Rules.Health");
    const auto second = world.EntityWith("Rules.Health");
    world.ReleaseGeneration();
    LiteralAssembly after;
    after.components = {Component("Rules.Health", 16, 8)};
    after.fingerprints = {{"Rules.Health", "16/8;Current:long"}};

    const auto result = world.Admit(after);

    INFO("The new layout should load: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("No entity should keep the old bytes");
    REQUIRE((!world.Has("Rules.Health", first) && !world.Has("Rules.Health", second)));
    INFO("The warning should say the component changed and how many were removed");
    REQUIRE(world.headless.Logger().Contains(Testing::LogLevel::Warn,
                                             "the script component 'Rules.Health' changed its layout; 2 component(s) "
                                             "were removed so nothing reads the old bytes as the new layout."));
    INFO("The new layout is what is registered");
    REQUIRE(world.registry.Components().Get(world.registry.Components().Find("Rules.Health"))->type.Size() == 16);
}

TEST_CASE("a component that no longer exists is removed from every scene with a warning", "[script-data]")
{
    World world;
    LiteralAssembly before;
    before.components = {Component("Rules.Health")};
    REQUIRE(world.Admit(before).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");
    world.ReleaseGeneration();
    LiteralAssembly after;

    const auto result = world.Admit(after);

    INFO("A script that dropped the component should load: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("The entity should not keep data nobody defines");
    REQUIRE(world.registry.Components().Find("Rules.Health") < 0);
    REQUIRE(world.CountOf("Rules.Health") == 0);
    REQUIRE(world.Entities().HasEntity(entity));
    INFO("The warning should say it no longer exists");
    REQUIRE(world.headless.Logger().Contains(Testing::LogLevel::Warn,
                                             "the script component 'Rules.Health' no longer exists; 1 component(s) "
                                             "were removed so nothing reads the old bytes as the new layout."));
    INFO("It should no longer be owned");
    REQUIRE(world.data.OwnedComponents().empty());
}

TEST_CASE("an event queue whose layout is unchanged is kept with its events", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.events = {Component("Rules.Beep", 4, 4)};
    assembly.fingerprints = {{"Rules.Beep", "4/4;Value:int"}};
    REQUIRE(world.Admit(assembly).Succeeded());
    auto* queue = world.headless.Get().FindEventQueue("Rules.Beep");
    const std::int32_t event = 5;
    REQUIRE(queue->SendRaw(&event));
    world.ReleaseGeneration();

    REQUIRE(world.Admit(assembly).Succeeded());

    INFO("The queue should be the same one");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") == queue);
    INFO("Its event should still be in it");
    REQUIRE(Testing::HeldEventCount(*queue) == 1);
}

TEST_CASE("an event that no longer exists loses its queue with a warning", "[script-data]")
{
    World world;
    LiteralAssembly before;
    before.events = {Component("Rules.Beep", 4, 4)};
    REQUIRE(world.Admit(before).Succeeded());
    world.ReleaseGeneration();
    LiteralAssembly after;

    const auto result = world.Admit(after);

    INFO("A script that dropped the event should load: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("Its queue should be gone");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") == nullptr);
    INFO("The warning should say it no longer exists");
    REQUIRE(world.headless.Logger().Contains(
        Testing::LogLevel::Warn,
        "the script event 'Rules.Beep' no longer exists; its queue and the events in it were dropped."));
}

TEST_CASE("an event queue whose layout changed is removed with its events and made again", "[script-data]")
{
    World world;
    LiteralAssembly before;
    before.events = {Component("Rules.Beep", 4, 4)};
    before.fingerprints = {{"Rules.Beep", "4/4;Value:int"}};
    REQUIRE(world.Admit(before).Succeeded());
    const std::int32_t event = 5;
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->SendRaw(&event));
    world.ReleaseGeneration();
    LiteralAssembly after;
    after.events = {Component("Rules.Beep", 8, 8)};
    after.fingerprints = {{"Rules.Beep", "8/8;Value:long"}};

    const auto result = world.Admit(after);

    INFO("The new layout should load: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    const auto* queue = world.headless.Get().FindEventQueue("Rules.Beep");
    INFO("The queue should exist with the new event size and none of the old events");
    REQUIRE(queue != nullptr);
    REQUIRE((queue->EventSize() == 8 && Testing::HeldEventCount(*queue) == 0));
    INFO("The warning should say the queue and its events were dropped");
    REQUIRE(world.headless.Logger().Contains(
        Testing::LogLevel::Warn,
        "the script event 'Rules.Beep' changed its layout; its queue and the events in it were dropped."));
}

// The native check, and its place in the order.

TEST_CASE("a failure of the native components' check changes nothing", "[script-data]")
{
    World world;
    LiteralAssembly before;
    before.components = {Component("Rules.Health")};
    before.events = {Component("Rules.Beep", 4, 4)};
    before.fingerprints = {{"Rules.Health", "8/4;Current:int"}, {"Rules.Beep", "4/4;Value:int"}};
    REQUIRE(world.Admit(before).Succeeded());
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

    const auto result = world.Admit(after);

    INFO("The check's failure should be the result, unchanged");
    REQUIRE(
        (!result.Succeeded() && result.diagnostic == "Component 'Test.BadHealth' does not match its C# struct: Max"));
    INFO("The previous generation's component data should not have been dropped, even though the layout changed");
    REQUIRE(world.CountOf("Rules.Health") == 1);
    REQUIRE(world.Entities().HasEntity(entity));
    INFO("Nor should its event queue and the event in it");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->EventSize() == 4);
    REQUIRE(Testing::HeldEventCount(*world.headless.Get().FindEventQueue("Rules.Beep")) == 1);
    INFO("Nothing of the new report should be registered, and ownership should be as it was");
    REQUIRE(
        (world.registry.Components().Find("Rules.Armour") < 0 && world.registry.Components().Find("Rules.Health") < 0));
    REQUIRE((world.data.OwnedComponents().size() == 1 && world.data.OwnedComponents().contains("Rules.Health")));
    INFO("A warning about dropping data should not have been given");
    REQUIRE(!world.headless.Logger().Contains(Testing::LogLevel::Warn, "were removed"));
}

TEST_CASE("the native components are checked only after the definitions' own rules pass", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health"), Component("Rules.Health")};
    assembly.nativeCheck = Iris::CallResult<void>{Iris::CallStatus::InvalidArgument, "the native check", false};

    const auto result = world.Admit(assembly);

    INFO("The definitions' own refusal should be the one reported");
    REQUIRE(result.diagnostic == "The script reported the component 'Rules.Health' more than once.");
    INFO("The native check should not have been asked");
    REQUIRE(assembly.nativeChecks == 0);
}

TEST_CASE("the native components are checked once for a report that passes its own rules", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4)};

    REQUIRE(world.Admit(assembly).Succeeded());

    INFO("Components and events share one native check");
    REQUIRE(assembly.nativeChecks == 1);
}

// Admission is all or nothing, and what a generation leaves behind outlives it.

TEST_CASE("an admission refused after its components were registered leaves nothing registered and the data as it was",
          "[script-data]")
{
    World world;
    LiteralAssembly before;
    before.components = {Component("Rules.Health")};
    before.events = {Component("Rules.Beep", 4, 4)};
    before.fingerprints = {{"Rules.Health", "8/4;Current:int"}, {"Rules.Beep", "4/4;Value:int"}};
    REQUIRE(world.Admit(before).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");
    const std::int32_t event = 5;
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep")->SendRaw(&event));
    world.ReleaseGeneration();
    // The report passes every check and the components register, then the events refuse to: an event with no name
    // is the one report the checks let through that the registry turns down (a real assembly cannot give one, since
    // the reporting natives refuse an empty name, which is why the checks do not look for it).
    LiteralAssembly after;
    after.components = {Component("Rules.Health"), Component("Rules.Armour")};
    after.events = {Component("Rules.Beep", 4, 4), Component("", 4, 4)};
    after.fingerprints = before.fingerprints;

    const auto result = world.Admit(after);

    INFO("The admission should be refused with the events' refusal");
    REQUIRE((!result.Succeeded() && result.diagnostic == "The script's events could not be registered together."));
    INFO("Nothing script-defined should be left registered, the components that registered first included");
    REQUIRE(world.ScriptDefinedCount() == 0);
    REQUIRE((world.registry.Components().Find("Rules.Health") < 0 &&
             world.registry.Components().Find("Rules.Armour") < 0 && world.registry.Events().Find("Rules.Beep") < 0));
    INFO("The previous generation's data should be as it was, since nothing stale was dropped");
    REQUIRE(world.CountOf("Rules.Health") == 1);
    REQUIRE(Testing::HeldEventCount(*world.headless.Get().FindEventQueue("Rules.Beep")) == 1);
    REQUIRE((world.data.OwnedComponents().contains("Rules.Health") && world.data.OwnedEvents().contains("Rules.Beep")));

    const auto restored = world.Admit(before);

    INFO("The previous report should admit again, as a restore of the previous generation does: " +
         restored.diagnostic);
    REQUIRE(restored.Succeeded());
    INFO("It should find its data and its queue where it left them");
    REQUIRE(world.Has("Rules.Health", entity));
    REQUIRE(Testing::HeldEventCount(*world.headless.Get().FindEventQueue("Rules.Beep")) == 1);
}

TEST_CASE("forgetting a generation removes its registrations and keeps its data for the next", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4)};
    REQUIRE(world.Admit(assembly).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");

    world.data.Forget();

    INFO("Nothing script-defined should be registered");
    REQUIRE((world.ScriptDefinedCount() == 0 && world.registry.Components().Find("Rules.Health") < 0));
    INFO("The engine's own components should still be registered");
    REQUIRE(world.registry.Components().Find("Fenrir.Transform") >= 0);
    INFO("The data, the queue and their layouts should stay");
    REQUIRE((world.CountOf("Rules.Health") == 1 && world.Entities().HasEntity(entity)));
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") != nullptr);
    REQUIRE((world.data.OwnedComponents().contains("Rules.Health") && world.data.OwnedEvents().contains("Rules.Beep")));
}

TEST_CASE("dropping the script data empties what is owned and registered", "[script-data]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Component("Rules.Health")};
    assembly.events = {Component("Rules.Beep", 4, 4)};
    REQUIRE(world.Admit(assembly).Succeeded());
    const auto entity = world.EntityWith("Rules.Health");

    world.data.Drop(world.headless.Get());

    INFO("The component data should be gone from the entity");
    REQUIRE((world.CountOf("Rules.Health") == 0 && world.Entities().HasEntity(entity)));
    INFO("The event queue should be gone");
    REQUIRE(world.headless.Get().FindEventQueue("Rules.Beep") == nullptr);
    INFO("Nothing should be owned or registered");
    REQUIRE((world.data.OwnedComponents().empty() && world.data.OwnedEvents().empty()));
    REQUIRE(world.ScriptDefinedCount() == 0);
}

// The descriptions admission gives the App's type registry: each script component as a fixed-layout component type,
// each struct it holds as a nested one, so the scene reader and writer reach a C# component as they reach a native one.
namespace
{
    using Fenrir::Detail::ScriptFieldDescription;

    ScriptFieldDescription Field(const char* name, Kind kind, std::size_t offset, std::size_t size,
                                 std::string target = {}, std::size_t count = 0)
    {
        ScriptFieldDescription field;
        field.name = name;
        field.kind = kind;
        field.offset = offset;
        field.size = size;
        field.target = std::move(target);
        field.count = count;
        return field;
    }

    // A struct of two floats, as a component holds one by value.
    ScriptComponentDescription Spot()
    {
        ScriptComponentDescription spot{"Rules.Spot", 8, 4};
        spot.fields = {Field("Height", Kind::F32, 0, 4), Field("Count", Kind::I32, 4, 4)};
        return spot;
    }

    // A component with a field of every kind the ticket names: an entity, a reference, a fixed string, a script
    // struct, an engine struct and a transient field, with its `new T()` bytes.
    ScriptComponentDescription Seen()
    {
        ScriptComponentDescription seen{"Rules.Seen", 72, 8};
        seen.fields = {
            Field("Target", Kind::Entity, 0, 16),
            Field("Weapon", Kind::Reference, 16, 8, "Fenrir::Mesh"),
            Field("Label", Kind::FixedString, 24, 16, {}, 16),
            Field("Spot", Kind::Struct, 40, 8, "Rules.Spot"),
            Field("Offset", Kind::Struct, 48, 12, "Fenrir::Math::Vec3"),
            Field("Frames", Kind::I32, 60, 4),
        };
        seen.fields.back().transient = true;
        ScriptFieldDescription slots = Field("Slots", Kind::Array, 64, 8, {}, 2);
        slots.elementKind = Kind::I32;
        seen.fields.push_back(slots);
        seen.defaults.resize(seen.size);
        for (std::size_t at = 0; at < seen.defaults.size(); ++at)
        {
            seen.defaults[at] = static_cast<std::byte>(at + 1);
        }
        return seen;
    }

    // The script types the registry holds: its components and nested types from FenrirScripting.
    std::size_t ScriptTypeCount(const TypeRegistry& types)
    {
        std::size_t count = 0;
        for (const Role role : {Role::Component, Role::Nested})
        {
            for (const TypeDescription* type : types.Types(role))
            {
                count += type->package == "FenrirScripting" ? 1 : 0;
            }
        }
        return count;
    }
} // namespace

TEST_CASE("admission describes each script component and the structs it holds to the App's type registry",
          "[script-data][types]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Seen()};
    assembly.structs = {Spot()};

    const auto result = world.Admit(assembly);

    INFO("The report should be admitted: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    const auto& types = world.headless.Get().GetResource<TypeRegistry>();
    const TypeDescription* seen = types.Find("Rules.Seen");
    const TypeDescription* spot = types.Find("Rules.Spot");
    INFO("The component and the struct it holds should be registered by their full names");
    REQUIRE((seen != nullptr && spot != nullptr));
    INFO("The component should be a fixed-layout component, the struct a nested type");
    REQUIRE((seen->role == Role::Component && !seen->treeBacked && seen->size == 72 && seen->alignment == 8));
    REQUIRE((spot->role == Role::Nested && spot->size == 8 && spot->fields.size() == 2));
    INFO("Each field should carry the kind, offset and target the script reported");
    REQUIRE(seen->fields.size() == 7);
    CHECK(
        (seen->fields[0].name == "Target" && seen->fields[0].type.kind == Kind::Entity && seen->fields[0].offset == 0));
    CHECK((seen->fields[1].type.kind == Kind::Reference && seen->fields[1].type.target == "Fenrir::Mesh" &&
           seen->fields[1].offset == 16));
    CHECK((seen->fields[2].type.kind == Kind::FixedString && seen->fields[2].type.count == 16 &&
           seen->fields[2].offset == 24));
    CHECK((seen->fields[3].type.kind == Kind::Struct && seen->fields[3].type.type == spot));
    CHECK((seen->fields[4].type.kind == Kind::Struct && seen->fields[4].type.type == types.Find("Fenrir::Math::Vec3")));
    CHECK((seen->fields[5].name == "Frames" && seen->fields[5].transient && !seen->fields[0].transient));
    CHECK((seen->fields[6].type.kind == Kind::Array && seen->fields[6].type.count == 2 &&
           seen->fields[6].type.element != nullptr && seen->fields[6].type.element->kind == Kind::I32));

    INFO("Its default should be the bytes of its new T()");
    std::vector<std::byte> constructed(seen->size);
    seen->Construct(constructed.data());
    REQUIRE(constructed == Seen().defaults);

    INFO("ComponentType::ForName should find the component the ECS stores");
    const auto component = ComponentType::ForName(types, "Rules.Seen");
    REQUIRE(component.has_value());
    REQUIRE((component->IsDynamic() && component->Size() == 72));
    const auto entity = world.Entities().CreateEntity().GetId();
    REQUIRE(world.Entities().Add(*component, entity));
    REQUIRE(world.Has("Rules.Seen", entity));
    INFO("NameOf should name it back, since it is the registration's own type");
    REQUIRE(ComponentType::NameOf(types, *component) == std::optional<std::string_view>("Rules.Seen"));
}

TEST_CASE("forget unregisters a generation's descriptions, and the next admission registers them again",
          "[script-data][types]")
{
    World world;
    LiteralAssembly assembly;
    assembly.components = {Seen()};
    assembly.structs = {Spot()};
    const auto& types = world.headless.Get().GetResource<TypeRegistry>();
    const std::size_t before = ScriptTypeCount(types);
    REQUIRE(world.Admit(assembly).Succeeded());
    REQUIRE(ScriptTypeCount(types) == before + 2);

    world.ReleaseGeneration();

    INFO("Forget should unregister the component and the struct");
    REQUIRE((types.Find("Rules.Seen") == nullptr && types.Find("Rules.Spot") == nullptr));
    REQUIRE(!ComponentType::ForName(types, "Rules.Seen").has_value());
    REQUIRE(ScriptTypeCount(types) == before);

    INFO("The next generation should register them again");
    const auto again = world.Admit(assembly);
    INFO(again.diagnostic);
    REQUIRE(again.Succeeded());
    REQUIRE((types.Find("Rules.Seen") != nullptr && ComponentType::ForName(types, "Rules.Seen").has_value()));
}

TEST_CASE("a component holding another component by value registers the held one first", "[script-data][types]")
{
    World world;
    LiteralAssembly assembly;
    ScriptComponentDescription outer{"Rules.Outer", 16, 4};
    outer.fields = {Field("Inner", Kind::Struct, 0, 8, "Rules.Inner"), Field("Spot", Kind::Struct, 8, 8, "Rules.Spot")};
    ScriptComponentDescription inner{"Rules.Inner", 8, 4};
    inner.fields = {Field("Spot", Kind::Struct, 0, 8, "Rules.Spot")};
    // The holder first, so the order the report gives is not the order they register in.
    assembly.components = {outer, inner};
    assembly.structs = {Spot()};

    const auto result = world.Admit(assembly);
    INFO(result.diagnostic);
    REQUIRE(result.Succeeded());
    const auto& types = world.headless.Get().GetResource<TypeRegistry>();
    REQUIRE(types.Find("Rules.Outer")->fields[0].type.type == types.Find("Rules.Inner"));

    INFO("Forget should unregister them all, holders first");
    world.ReleaseGeneration();
    REQUIRE((types.Find("Rules.Outer") == nullptr && types.Find("Rules.Inner") == nullptr &&
             types.Find("Rules.Spot") == nullptr));
}

TEST_CASE("a report whose descriptions cannot be made is refused whole, leaving the type registry unchanged",
          "[script-data][types]")
{
    World world;
    LiteralAssembly assembly;
    auto seen = Seen();
    std::string expected;
    const int refusal = GENERATE(0, 1, 2, 3, 4, 5);
    switch (refusal)
    {
    case 0:
        seen.fields[3].target = "Rules.Nowhere";
        expected = "The field 'Spot' of the script type 'Rules.Seen' holds the struct 'Rules.Nowhere', which neither "
                   "the script nor the engine describes.";
        break;
    case 1:
        seen.fields[0].size = 8;
        expected = "The field 'Target' of the script type 'Rules.Seen' takes 8 bytes, but its kind, entity, takes 16.";
        break;
    case 2:
        seen.fields[6].offset = 68;
        expected = "The field 'Slots' of the script type 'Rules.Seen' ends past the type's 72 bytes.";
        break;
    case 3:
        seen.name = "Fenrir::Transform";
        expected =
            "The script defines a component 'Fenrir::Transform', but the engine already has a type of that name, "
            "from FenrirECS.";
        break;
    case 4:
        seen.defaults.resize(4);
        expected = "The script component 'Rules.Seen' reported 4 bytes of defaults for a size of 72.";
        break;
    case 5:
        seen.fields[1].kind = Kind::String;
        expected =
            "The field 'Weapon' of the script type 'Rules.Seen' is of kind string, which a component cannot hold.";
        break;
    default:
        break;
    }
    assembly.components = {seen, Component("Rules.Plain")};
    assembly.structs = {Spot()};
    const auto& types = world.headless.Get().GetResource<TypeRegistry>();
    const std::size_t before = ScriptTypeCount(types);

    const auto result = world.Admit(assembly);

    INFO("Refusal " << refusal << " should refuse the whole report");
    REQUIRE(!result.Succeeded());
    REQUIRE(result.diagnostic == expected);
    INFO("Nothing of it should be registered, in either registry");
    REQUIRE(ScriptTypeCount(types) == before);
    REQUIRE((types.Find("Rules.Plain") == nullptr && types.Find("Rules.Spot") == nullptr));
    REQUIRE(world.ScriptDefinedCount() == 0);
}
