// What a loaded script assembly may define, with no .NET: reconcile decides, from the assembly's report, what is
// refused and what is kept. The assembly side is a small interface with two adapters, one over a loaded Iris component
// and, here, one that returns literals, so every refusal can be checked without an assembly that lies.
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "../HeadlessApp.hpp"

#include "ComponentRegistry.hpp"
#include "ScriptSchema.hpp"

#include "FenrirECS/DefaultComponents.hpp"

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
            REQUIRE(Fenrir::Detail::RegisterEngineComponents(registry));
        }

        EntityList& Entities()
        {
            return headless.Get().GetActiveScene().GetEntityList();
        }

        Iris::CallResult<void> Reconcile(ScriptAssembly& assembly)
        {
            return Fenrir::Detail::Reconcile(headless.Get(), registry, assembly, owned);
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
