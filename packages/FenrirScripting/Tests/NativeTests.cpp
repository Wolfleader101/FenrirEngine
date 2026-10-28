// Native functions from an extension: C#'s arguments and the result, enums and wrappers by value, strings, the
// active scene through the call context, the extension's components and events, its state, the binding name, the
// change buffer from a native, a warning's prefix, the not-running answer, and a native called through the harness.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "NativeCallHarness.hpp"
#include "ScriptingProbe.hpp"
#include "TestExtension.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirScripting/Scripting.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    void ANativeFunctionGetsCSharpArgumentsAndItsResultReachesCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("The script assembly using the extension did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("20 + 22 through the extension's native function was not 42");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Add", std::int32_t{20}, std::int32_t{22}) == 42);
        }
        const auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity().GetId();
        {
            INFO("An entity did not cross to the native function and back unchanged");
            REQUIRE(ExtensionCall<std::uint32_t>(scripting, "EchoEntity", entity) == entity);
        }
    }

    void ANativeFunctionTakesAndReturnsAnEnumAndAWrapperByValue()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("An enum did not cross to the native function and back (East turns right to South, West to North)");
            REQUIRE(ExtensionCall<std::uint8_t>(scripting, "TurnRight", std::uint8_t{2}) == 3);
            REQUIRE(ExtensionCall<std::uint8_t>(scripting, "TurnRight", std::uint8_t{4}) == 1);
        }
        {
            INFO("A wrapper and an enum did not both arrive, or the wrapper did not come back (500 + West's 4 is 504)");
            REQUIRE(ExtensionCall<std::uint16_t>(scripting, "Retag", std::uint16_t{500}, std::uint8_t{4}) == 504);
        }
    }

    void ANativeFunctionReceivesAStringFromCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        TestExtension::labelled = "untouched";
        {
            INFO("Multi-byte text and a number did not both arrive (6 UTF-8 bytes * 10 + 3 is 63)");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Label", std::string_view("\xCF\x80 \xE2\x82\xAC"),
                                                std::int32_t{3}) == 63);
            REQUIRE(TestExtension::labelled == "\xCF\x80 \xE2\x82\xAC");
        }
        {
            INFO("Empty text did not arrive as an empty string");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Label", std::string_view(), std::int32_t{4}) == 4);
            REQUIRE(TestExtension::labelled.empty());
        }
    }

    void ALoneSurrogateInAStringArrivesAsTheReplacementCharacter()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        TestExtension::labelled = "untouched";
        {
            INFO("'a', a lone surrogate and 'b' (1 + 3 + 1 UTF-8 bytes, times 10, plus 7) did not give 57");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "LabelWithLoneSurrogate", std::int32_t{7}) == 57);
        }
        {
            INFO("The lone surrogate did not arrive as U+FFFD");
            REQUIRE(TestExtension::labelled == "a\xEF\xBF\xBD"
                                               "b");
        }
    }

    void ANativeFunctionReachesTheActiveSceneThroughItsCallContext()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        auto entity = entities.CreateEntity();
        entity.GetComponent<Transform>().pos.y = 2.5F;
        {
            INFO("The native function did not read the entity's Transform through its call context");
            REQUIRE(ExtensionCall<float>(scripting, "HeightOf", entity.GetId()) == 2.5F);
        }
        ExtensionCall<void>(scripting, "Lift", entity.GetId(), 1.0F);
        {
            INFO("The native function's write did not reach the engine's storage");
            REQUIRE(entity.GetComponent<Transform>().pos.y == 3.5F);
        }
        {
            INFO("C# did not see what the native function wrote");
            REQUIRE(ExtensionCall<float>(scripting, "ScriptHeightOf", entity.GetId()) == 3.5F);
        }
    }

    void AnExtensionsComponentsAndEventsAreRegisteredAndCheckedAgainstCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("The extension's component and event did not pass their checks against C#");
            REQUIRE(scripting.HasScripts());
        }
        const auto* charge = scripting.FindComponent("Fenrir.TestExtension.Charge");
        {
            INFO("The extension's component was not registered");
            REQUIRE(charge != nullptr);
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto entity = entities.CreateEntity().GetId();
        {
            INFO("The extension's component could not be added natively");
            REQUIRE(entities.Add(charge->type, entity));
        }
        static_cast<TestExtension::Charge*>(entities.Get(charge->type, entity))->Level = 7;
        {
            INFO("C# did not read the extension's component");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "ChargeLevel", entity) == 7);
        }
        {
            INFO("The extension's event has no named queue");
            REQUIRE(headless.Get().FindEventQueue("Fenrir.TestExtension.Ping") != nullptr);
        }
    }

    void ANativeFunctionReceivesTheStateItsExtensionWasGiven()
    {
        HeadlessApp headless;
        const auto tally = std::make_shared<TestExtension::Tally>();
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(tally)}));
        {
            INFO("The script assembly using the stateful extension did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("Adding 5 to a fresh tally through C# did not give 5");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Tally", std::int32_t{5}) == 5);
        }
        {
            INFO("The native function did not add to the object SetState was given");
            REQUIRE(tally->total == 5);
        }
        tally->total = 100;
        {
            INFO("The native function did not see a change made to the object SetState was given");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Tally", std::int32_t{1}) == 101);
        }
    }

    void AStatelessNativeFunctionOfAStatefulExtensionStillWorks()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("The script assembly using the stateful extension did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("A stateless native function of an extension carrying a state did not add 20 and 22");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "Add", std::int32_t{20}, std::int32_t{22}) == 42);
        }
    }

    void TheBindingNameOfANativeFunctionFollowsOneRule()
    {
        {
            INFO("Namespace.Type.Method did not bind to the field __native_Method of Namespace.Type");
            REQUIRE(NativeFunction::BindingNameFor("Fenrir.Physics.Raycast") == "Fenrir.Physics.__native_Raycast");
        }
        {
            INFO("A nested type's name (Outer+Inner) did not carry over");
            REQUIRE(NativeFunction::BindingNameFor("Game.Outer+Inner.Run") == "Game.Outer+Inner.__native_Run");
        }
        {
            INFO("A name with no type part had a binding name");
            REQUIRE(NativeFunction::BindingNameFor("Run").empty());
            REQUIRE(NativeFunction::BindingNameFor(".Run").empty());
            REQUIRE(NativeFunction::BindingNameFor("Game.").empty());
        }
    }

    void ANativeFunctionCreatesAnEntityThatIsUsableAtOnce()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const auto& charge = scripting.FindComponent("Fenrir.TestExtension.Charge")->type;

        const auto entity = ExtensionCall<std::uint32_t>(scripting, "SpawnCharged", std::int32_t{5});
        {
            INFO("The entity the native function created was not alive when the call returned");
            REQUIRE(entities.HasEntity(entity));
        }
        {
            INFO("The component the native function added reached the storage before playback");
            REQUIRE(!entities.Has(charge, entity));
        }
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Creating the entity and adding its Charge were not both played back");
            REQUIRE((summary.applied == 2 && summary.skipped == 0));
        }
        {
            INFO("The created entity did not get its Charge, with the native function's value, at playback");
            REQUIRE(entities.Has(charge, entity));
            REQUIRE(static_cast<TestExtension::Charge*>(entities.Get(charge, entity))->Level == 5);
        }
        {
            INFO("The created entity did not get the components every entity has");
            REQUIRE(entities.GetEntity(entity).HasComponent<Transform>());
        }
    }

    void ANativeFunctionsChangesPlayBackInOrderAfterTheCall()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        Scene& scene = headless.Get().GetActiveScene();
        auto& entities = scene.GetEntityList();
        const auto& charge = scripting.FindComponent("Fenrir.TestExtension.Charge")->type;
        const auto kept = entities.CreateEntity().GetId();
        const auto doomed = entities.CreateEntity().GetId();

        ExtensionCall<void>(scripting, "Rearrange", kept, doomed, 2.5F);
        {
            INFO("A change the native function recorded reached the storage before playback");
            REQUIRE(!entities.Has(charge, kept));
            REQUIRE(entities.HasEntity(doomed));
            REQUIRE(scene.GetResource<TestExtension::Weather>() == nullptr);
        }
        {
            INFO("The native function's six changes were not all recorded");
            REQUIRE(ScriptingProbe::PendingChanges(scripting) == 6);
        }
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("The add after the destroy was not the one change skipped");
            REQUIRE((summary.applied == 5 && summary.skipped == 1));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Warn, "skipped a change a native function recorded"));
        }
        {
            INFO("Add, remove, add did not leave the last Charge: they did not play back in order");
            REQUIRE(entities.Has(charge, kept));
            REQUIRE(static_cast<TestExtension::Charge*>(entities.Get(charge, kept))->Level == 3);
        }
        {
            INFO("The destroy did not play back");
            REQUIRE(!entities.HasEntity(doomed));
        }
        {
            INFO("The scene resource the native function inserted did not play back with its value");
            const auto* weather = scene.GetResource<TestExtension::Weather>();
            REQUIRE((weather != nullptr && weather->Wind == 2.5F && weather->Gusts == 4));
        }
    }

    void ANativeFunctionParentsAnEntityAtPlayback()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const Entity parent = entities.CreateEntity();
        const Entity older = entities.CreateEntity();
        parent.AddChild(older);

        const auto child = ExtensionCall<std::uint32_t>(scripting, "SpawnUnder", parent.GetId());
        {
            INFO("The parenting reached the hierarchy before playback");
            REQUIRE(parent.GetComponent<Relationship>().firstChild == older);
            REQUIRE(!older.GetComponent<Relationship>().nextSibling.IsValid());
        }
        const auto summary = ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Creating the entity and parenting it were not both played back");
            REQUIRE((summary.applied == 2 && summary.skipped == 0));
        }
        {
            INFO("The created entity is not the parent's last child after playback");
            REQUIRE(entities.GetEntity(child).GetComponent<Relationship>().parent == parent);
            REQUIRE(older.GetComponent<Relationship>().nextSibling.GetId() == child);
        }

        const std::uint32_t dead = entities.CreateEntity().GetId();
        entities.DestroyEntity(dead);
        const auto orphan = ExtensionCall<std::uint32_t>(scripting, "SpawnUnder", dead);
        const auto refused = ScriptingProbe::ApplyChanges(scripting);
        INFO("Parenting under an entity that is not alive was recorded, or the orphan has a parent");
        REQUIRE((refused.applied == 1 && refused.skipped == 0));
        REQUIRE(!entities.GetEntity(orphan).GetComponent<Relationship>().parent.IsValid());
    }

    // How many warnings `headless` logged that read `text`.
    std::size_t WarningsReading(const HeadlessApp& headless, const std::string& text)
    {
        const auto messages = headless.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const Testing::LoggedMessage& message) {
                return message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos;
            }));
    }

    void ANativeFunctionsWarningIsPrefixedWithItsTypeAndMethod()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("The script assembly using the extension did not load");
            REQUIRE(scripting.HasScripts());
        }
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const std::uint32_t live = entities.CreateEntity().GetId();
        const std::uint32_t dead = entities.CreateEntity().GetId();
        entities.DestroyEntity(dead);

        ExtensionCall<void>(scripting, "Nudge", live);
        {
            INFO("A live entity was not nudged up by one");
            REQUIRE(ExtensionCall<float>(scripting, "HeightOf", live) == 1.0F);
        }
        ExtensionCall<void>(scripting, "Nudge", dead);
        ExtensionCall<void>(scripting, "Nudge", dead);
        // The native gave no name: the thunk's call context carries the type-and-method part of its registered name.
        constexpr const char* Warning = "[test extension] Gadgets.Nudge: the entity is not alive, so it does nothing";
#ifdef NDEBUG
        INFO("A release build warned");
        REQUIRE(WarningsReading(headless, Warning) == 0);
#else
        INFO("Two calls on a dead entity did not warn exactly once under '<Type>.<Method>: '");
        REQUIRE(WarningsReading(headless, Warning) == 1);
#endif
    }

    void ANativeCalledWhileNoScriptingRunsReturnsItsNotRunningValueAndLeavesItsOutputUntouched()
    {
        {
            HeadlessApp headless;
            auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
            {
                INFO("The script assembly using the extension did not load");
                REQUIRE(scripting.HasScripts());
            }
            auto& entities = headless.Get().GetActiveScene().GetEntityList();
            const Entity live = entities.CreateEntity();
            live.GetComponent<Transform>().pos.y = 2.5F;
            const std::uint32_t dead = entities.CreateEntity().GetId();
            entities.DestroyEntity(dead);
            {
                INFO("While it runs, a live entity did not give one reading of its height");
                REQUIRE(ExtensionCall<std::int32_t>(scripting, "Weigh", live.GetId()) == 1);
                REQUIRE(ExtensionCall<float>(scripting, "WeighHeight", live.GetId()) == 2.5F);
            }
            {
                INFO("While it runs, a dead entity did not give zero readings and a zeroed reading");
                REQUIRE(ExtensionCall<std::int32_t>(scripting, "Weigh", dead) == 0);
                REQUIRE(ExtensionCall<float>(scripting, "WeighHeight", dead) == 0.0F);
            }
        }
        // The Scripting is gone, so the registered thunk is on its not-running path: the value Make was given, and the
        // zeroing, which sits inside the check, does not run.
        TestExtension::Reading reading{3, 2.5F};
        // No entity: the thunk is on its not-running path and never reaches the native.
        const Entity five;
        const std::int32_t result =
            Detail::NativeFunctionTraits<&TestExtension::ZeroingWeigh::Call>::Call(&five, &reading);
        {
            INFO("A call while no Scripting runs did not return the @notrunning value");
            REQUIRE(result == -1);
        }
        {
            INFO("A call while no Scripting runs touched the output");
            REQUIRE((reading.Count == 3 && reading.Weight == 2.5F));
        }
    }

    void ANativeFunctionCalledThroughTheHarnessRecordsAChangeThatPlaysBack()
    {
        Testing::NativeCallHarness harness;
        const CallContext context = harness.Context("Gadgets.SpawnCharged");
        Entity created;
        TestExtension::SpawnCharged(context, 7, &created);
        const std::uint32_t spawned = created.GetId();
        auto& entities = harness.GetApp().GetActiveScene().GetEntityList();
        {
            INFO("The entity the native created is not alive at once");
            REQUIRE(entities.HasEntity(spawned));
        }
        {
            INFO("The Charge reached the entity before playback, or the two changes were not recorded");
            REQUIRE_FALSE(entities.GetEntity(spawned).HasComponent<TestExtension::Charge>());
            REQUIRE(harness.PendingChanges() == 2);
        }
        const auto summary = harness.ApplyChanges();
        {
            INFO("Creating the entity and adding its Charge were not both played back");
            REQUIRE((summary.applied == 2 && summary.skipped == 0));
            REQUIRE(harness.PendingChanges() == 0);
        }
        {
            INFO("The entity does not have the Charge the native recorded after playback");
            REQUIRE(entities.GetEntity(spawned).HasComponent<TestExtension::Charge>());
            REQUIRE(entities.GetEntity(spawned).GetComponent<TestExtension::Charge>().Level == 7);
        }
    }

} // namespace

TEST_CASE("a native function from an extension gets C#'s arguments and its result reaches C#", "[scripting]")
{
    ANativeFunctionGetsCSharpArgumentsAndItsResultReachesCSharp();
}

TEST_CASE("a native function takes and returns an enum and a wrapper by value", "[scripting]")
{
    ANativeFunctionTakesAndReturnsAnEnumAndAWrapperByValue();
}

TEST_CASE("a native function receives a string from C#", "[scripting]")
{
    ANativeFunctionReceivesAStringFromCSharp();
}

TEST_CASE("a lone surrogate in a string arrives as the replacement character", "[scripting]")
{
    ALoneSurrogateInAStringArrivesAsTheReplacementCharacter();
}

TEST_CASE("a native function reaches the active scene through its call context", "[scripting]")
{
    ANativeFunctionReachesTheActiveSceneThroughItsCallContext();
}

TEST_CASE("an extension's components and events are registered and checked against C#", "[scripting]")
{
    AnExtensionsComponentsAndEventsAreRegisteredAndCheckedAgainstCSharp();
}

TEST_CASE("a native function receives the state its extension was given", "[scripting]")
{
    ANativeFunctionReceivesTheStateItsExtensionWasGiven();
}

TEST_CASE("a stateless native function of a stateful extension still works", "[scripting]")
{
    AStatelessNativeFunctionOfAStatefulExtensionStillWorks();
}

TEST_CASE("the binding name of a native function follows one rule", "[scripting]")
{
    TheBindingNameOfANativeFunctionFollowsOneRule();
}

TEST_CASE("a native function creates an entity that is usable at once", "[scripting]")
{
    ANativeFunctionCreatesAnEntityThatIsUsableAtOnce();
}

TEST_CASE("a native function's changes play back in order after the call", "[scripting]")
{
    ANativeFunctionsChangesPlayBackInOrderAfterTheCall();
}

TEST_CASE("a native function parents an entity under another at playback", "[scripting]")
{
    ANativeFunctionParentsAnEntityAtPlayback();
}

TEST_CASE("a native function's warning is prefixed with its type and method", "[scripting]")
{
    ANativeFunctionsWarningIsPrefixedWithItsTypeAndMethod();
}

TEST_CASE("a native called while no Scripting runs returns its @notrunning value and leaves its output untouched",
          "[scripting]")
{
    ANativeCalledWhileNoScriptingRunsReturnsItsNotRunningValueAndLeavesItsOutputUntouched();
}

TEST_CASE("a native function called through the harness records a change that plays back", "[scripting]")
{
    ANativeFunctionCalledThroughTheHarnessRecordsAChangeThatPlaysBack();
}
