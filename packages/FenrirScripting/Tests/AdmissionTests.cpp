// What Install and a load refuse, whole and with a message naming the culprit: mismatched and duplicate components,
// events, shared structs and resources; a script assembly whose report the engine cannot accept; a C# field with no
// native function, or one whose signature or state differs; and names an extension cannot take.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "TestExtension.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fstream>
#include <ios>
#include <memory>
#include <string>
#include <utility>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Scripting.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;
    using TestNative::BadHealth;
    using TestNative::FloatHealth;
    using TestNative::Health;

    // Installs the fixture with `config` and checks that it failed, explaining `diagnostic` (one message, naming the
    // test's own extension).
    void ExpectInstallRefused(ScriptingConfig config, const std::string& diagnostic, const char* what)
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), std::move(config));
        {
            INFO(std::string(what) + " was accepted");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO(std::string(what) + " was not explained as: " + diagnostic);
            REQUIRE(headless.Logger().Contains(LogLevel::Error, diagnostic));
        }
    }

    void ADuplicateComponentNameFailsInstallAndNamesTheExtension()
    {
        ExpectInstallRefused(
            ConfigFor(fixture, {MakeComponent<Health>("Test.Health"), MakeComponent<Health>("Test.Health")}),
            "a component in extension '" + std::string(TestAppExtension) + "' has an empty or duplicate name",
            "A duplicate component name");
    }

    void AnEmptyComponentNameFailsInstallAndNamesTheExtension()
    {
        ExpectInstallRefused(ConfigFor(fixture, {MakeComponent<Health>("")}),
                             "a component in extension '" + std::string(TestAppExtension) +
                                 "' has an empty or duplicate name",
                             "An empty component name");
    }

    void ADuplicateEventNameFailsInstallAndNamesTheExtension()
    {
        ExpectInstallRefused(
            ConfigFor(fixture, {},
                      {MakeEvent<TestNative::Alert>("Test.Alert"), MakeEvent<TestNative::Alert>("Test.Alert")}),
            "an event in extension '" + std::string(TestAppExtension) + "' has an empty or duplicate name",
            "A duplicate event name");
    }

    void AMismatchedComponentFailsTheLoadAndNamesTheField()
    {
        HeadlessApp headless;
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<BadHealth>("Test.BadHealth")}));

        // All or nothing: the assembly is not left loaded with a component that does not match.
        {
            INFO("A mismatched component left scripts loaded");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("The mismatch did not name the component and the field");
            REQUIRE((headless.Logger().Contains(LogLevel::Error, "Test.BadHealth") &&
                     headless.Logger().Contains(LogLevel::Error, "'Current'") &&
                     headless.Logger().Contains(LogLevel::Error, "'Max'")));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running after a failed load");
            REQUIRE(headless.FramesRun() == Frames);
        }

        // The engine's own components still load once the bad one is gone.
        HeadlessApp again;
        auto fine = Scripting::Install(again.Get(), ConfigFor(fixture));
        {
            INFO("A later install without the bad component did not load");
            REQUIRE(fine.HasScripts());
        }
    }

    void AWrongFieldTypeIsCaughtToo()
    {
        HeadlessApp headless;
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<FloatHealth>("Test.FloatHealth")}));
        {
            INFO("A field of the wrong type left the script assembly loaded");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("A field of the wrong type was not named");
            REQUIRE((headless.Logger().Contains(LogLevel::Error, "Test.FloatHealth") &&
                     headless.Logger().Contains(LogLevel::Error, "'Current'")));
        }
    }

    void AMismatchedEventFailsTheLoadAndNamesTheField()
    {
        HeadlessApp headless;
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {}, {MakeEvent<TestNative::BadAlert>("Test.Alert")}));
        {
            INFO("A mismatched event left scripts loaded");
            REQUIRE((!scripting.HasScripts() && !scripting.IsRunning()));
        }
        {
            INFO("The mismatch did not name the event and the field");
            REQUIRE((headless.Logger().Contains(LogLevel::Error, "Test.Alert") &&
                     headless.Logger().Contains(LogLevel::Error, "'Code'")));
        }
        {
            INFO("A failed load left a queue behind");
            REQUIRE(headless.Get().FindEventQueue("Fenrir.ScriptFixture.Ping") == nullptr);
        }
    }

    void AFailedLoadLeavesNoComponentsAndNoPools()
    {
        HeadlessApp headless;
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const std::size_t poolsBefore = PoolCount(entities);

        // The script assembly defines components and loads fine, then a native mapping fails: the load is rolled back.
        auto scripting =
            Scripting::Install(headless.Get(), ConfigFor(fixture, {MakeComponent<BadHealth>("Test.BadHealth")}));
        {
            INFO("A mismatched component left scripts loaded");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("A failed load left script components registered");
            REQUIRE(
                (scripting.FindComponent(VelocityName) == nullptr && scripting.FindComponent(CounterName) == nullptr &&
                 scripting.FindComponent(AlignedName) == nullptr && scripting.FindComponent(TaggedName) == nullptr));
        }
        {
            INFO("A failed load disturbed the components that were registered before it");
            REQUIRE((scripting.FindComponent("Fenrir.Transform") != nullptr &&
                     scripting.FindComponent("Test.BadHealth") != nullptr));
        }
        {
            INFO("A failed load left storage behind");
            REQUIRE(PoolCount(entities) == poolsBefore);
        }

        // And a clean load afterwards registers them, with nothing carried over from the failure.
        HeadlessApp again;
        auto fine = Scripting::Install(again.Get(), ConfigFor(fixture));
        {
            INFO("A later clean load did not register the script components");
            REQUIRE(fine.FindComponent(VelocityName) != nullptr);
        }
    }

    // Tells the bad-report fixture what to report as it loads (see BadReports.cs); restored to a sound report on exit.
    // The mode goes through a file, not the environment: on Linux and macOS .NET reads the environment once, as the
    // runtime starts, so a variable set afterwards never reaches the fixture.
    struct BadReportMode
    {
        explicit BadReportMode(const char* mode)
        {
            Set(mode);
        }
        ~BadReportMode()
        {
            Set("");
        }
        BadReportMode(const BadReportMode&) = delete;
        BadReportMode& operator=(const BadReportMode&) = delete;

        static void Set(const char* mode)
        {
            std::ofstream(badReportModeFile, std::ios::trunc) << mode;
        }
    };

    void ASoundReportLoadsAndRegistersEverythingTogether()
    {
        const BadReportMode mode("");
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(badReports));
        {
            INFO("The sound bad-report fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("The script component was not registered");
            REQUIRE(scripting.FindComponent(BadAlphaName) != nullptr);
        }
        {
            INFO("The script event's queue was not made");
            REQUIRE(headless.Get().FindEventQueue(BadBeepName) != nullptr);
        }
    }

    // A script assembly that reports something the engine cannot accept is refused whole: nothing it reported stays
    // registered or stored, whichever kind the bad report was and whichever kind came before it.
    void ExpectReportRefusedWholesale(const char* reportMode, const char* explanation)
    {
        const BadReportMode mode(reportMode);
        HeadlessApp headless;
        auto& entities = headless.Get().GetActiveScene().GetEntityList();
        const std::size_t poolsBefore = PoolCount(entities);
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(badReports));
        {
            INFO(std::string("A script assembly that reported ") + reportMode + " was kept");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO(std::string("The refusal of ") + reportMode + " was not explained");
            REQUIRE(headless.Logger().Contains(LogLevel::Error, explanation));
        }
        {
            INFO(std::string(reportMode) + " left a component registered");
            REQUIRE(scripting.FindComponent(BadAlphaName) == nullptr);
        }
        {
            INFO(std::string(reportMode) + " left an event queue");
            REQUIRE(headless.Get().FindEventQueue(BadBeepName) == nullptr);
        }
        {
            INFO(std::string(reportMode) + " left storage behind");
            REQUIRE(PoolCount(entities) == poolsBefore);
        }
        {
            INFO(std::string(reportMode) + " disturbed the engine's own components");
            REQUIRE(scripting.FindComponent("Fenrir.Transform") != nullptr);
        }
    }

    void ADuplicateScriptComponentIsRefused()
    {
        ExpectReportRefusedWholesale("duplicate-component", "more than once");
    }

    void ADuplicateScriptEventIsRefused()
    {
        ExpectReportRefusedWholesale("duplicate-event", "more than once");
    }

    void ACSharpFieldWithNoNativeFunctionFailsInstallAndNamesIt()
    {
        HeadlessApp headless;
        ScriptingExtension broken;
        broken.name = "broken extension";
        broken.sharedAssemblies = {brokenExtension};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), broken}));
        {
            INFO("Scripts ran although a C# field has no native function");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the unbound field and say the app runs without scripts");
            REQUIRE((headless.Logger().Contains(Testing::LogLevel::Error, "Broken.__native_Unregistered") &&
                     headless.Logger().Contains(Testing::LogLevel::Error, "running without scripts")));
        }
    }

    void ANativeFunctionWhoseSignatureDiffersFromItsFieldFailsInstallAndNamesIt()
    {
        HeadlessApp headless;
        ScriptingExtension broken;
        broken.name = "broken extension";
        broken.sharedAssemblies = {brokenExtension};
        broken.functions = {
            NativeFunction::Make<&TestExtension::Mismatched>("Fenrir.BrokenExtension.Broken.Mismatched")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), broken}));
        {
            INFO("Scripts ran although a native function's signature differs from its C# field");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the mismatched field and say the signatures differ");
            REQUIRE((headless.Logger().Contains(Testing::LogLevel::Error, "Broken.__native_Mismatched") &&
                     headless.Logger().Contains(Testing::LogLevel::Error, "does not match")));
        }
    }

    void TwoExtensionsDeclaringOneNativeFunctionFailInstallAndNameBoth()
    {
        HeadlessApp headless;
        ScriptingExtension copy;
        copy.name = "copying extension";
        copy.functions = {NativeFunction::Make<&TestExtension::Add>("Fenrir.TestExtension.Gadgets.Add")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), copy}));
        {
            INFO("Scripts ran although two extensions declare one native function");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the function and both extensions");
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Fenrir.TestExtension.Gadgets.Add"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'test extension'"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'copying extension'"));
        }
    }

    void AnExtensionCannotTakeAnEngineComponentsName()
    {
        HeadlessApp headless;
        ScriptingExtension shadowing;
        shadowing.name = "shadowing extension";
        shadowing.components = {MakeComponent<TestExtension::Charge>("Fenrir.Transform")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), shadowing}));
        {
            INFO("Scripts ran although an extension took an engine component's name");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the extension and say the app runs without scripts");
            REQUIRE((headless.Logger().Contains(Testing::LogLevel::Error, "'shadowing extension'") &&
                     headless.Logger().Contains(Testing::LogLevel::Error, "running without scripts")));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running after the failed install");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void ANativeFunctionTakingAStateItsExtensionLacksFailsInstallAndNamesIt()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(nullptr)}));
        {
            INFO("Scripts ran although a native function takes a state its extension does not carry");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the function and its state type, say the extension carries none and say the "
                 "app runs without scripts");
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'Fenrir.TestExtension.Gadgets.Tally'"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Tally'"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "carries none"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "running without scripts"));
        }
    }

    void ANativeFunctionTakingAStateOfAnotherTypeFailsInstallAndNamesBothTypes()
    {
        HeadlessApp headless;
        auto extension = TheTestExtension(nullptr);
        extension.SetState(std::make_shared<TestExtension::Other>());
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({std::move(extension)}));
        {
            INFO("Scripts ran although a native function takes a state of another type than its extension's");
            REQUIRE(!scripting.HasScripts());
        }
        {
            // The state types are spelled by type_index::name(), which GCC and Clang mangle ("...5OtherE"), so only
            // the type's own name is looked for.
            INFO("The failure did not name the function and both state types");
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "'Fenrir.TestExtension.Gadgets.Tally'"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Tally"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Other"));
        }
    }

    void ASharedStructWhoseCSharpMirrorDriftedFailsTheLoadAndNamesIt()
    {
        HeadlessApp headless;
        auto extension = TheTestExtension();
        extension.sharedStructs = {MakeSharedStruct<TestExtension::DriftedReading>("Fenrir.TestExtension.Reading")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({std::move(extension)}));
        {
            INFO("Scripts ran although a shared struct's C# mirror does not match its mapping");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the shared struct and the drifted field, and say the app runs without "
                 "scripts");
            REQUIRE(
                headless.Logger().Contains(Testing::LogLevel::Error, "Shared struct 'Fenrir.TestExtension.Reading'"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "Amount"));
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error, "running without scripts"));
        }
        headless.Run(Frames);
        {
            INFO("The app did not keep running after the failed load");
            REQUIRE(headless.FramesRun() == Frames);
        }
    }

    void ASharedStructCannotTakeAComponentsName()
    {
        HeadlessApp headless;
        auto extension = TheTestExtension();
        extension.sharedStructs = {MakeSharedStruct<TestExtension::Reading>("Fenrir.TestExtension.Charge")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({std::move(extension)}));
        {
            INFO("Scripts ran although a shared struct took a component's name");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not say a shared struct of the extension has a duplicate name");
            REQUIRE(headless.Logger().Contains(
                Testing::LogLevel::Error,
                "a shared struct in extension 'test extension' has an empty or duplicate name"));
        }
    }

    void AResourceWhoseLayoutDiffersFromItsCSharpStructFailsTheLoad()
    {
        HeadlessApp headless;
        auto extension = TheTestExtension();
        extension.resources[0] = MakeSceneResource<TestExtension::BadWeather>("Fenrir.TestExtension.Weather");
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({extension}));
        {
            INFO("Scripts loaded although a resource's mapping differs from its C# struct");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the resource and say its C# struct does not match");
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Error,
                                               "Resource 'Fenrir.TestExtension.Weather' does not match its C# struct"));
        }
    }

    void ADuplicateResourceNameFailsInstallAndNamesTheExtension()
    {
        HeadlessApp headless;
        ScriptingExtension copy;
        copy.name = "copying extension";
        copy.resources = {MakeAppResource<TestExtension::Settings>("Fenrir.TestExtension.Weather")};
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension(), copy}));
        {
            INFO("Scripts ran although two extensions registered one resource name");
            REQUIRE(!scripting.HasScripts());
        }
        {
            INFO("The failure did not name the second extension");
            REQUIRE(headless.Logger().Contains(
                Testing::LogLevel::Error,
                "a resource in extension 'copying extension' has an empty or duplicate name"));
        }
    }

} // namespace

TEST_CASE("a mismatched component fails the load and names the field", "[scripting]")
{
    AMismatchedComponentFailsTheLoadAndNamesTheField();
}

TEST_CASE("a duplicate component name fails install and names the extension", "[scripting]")
{
    ADuplicateComponentNameFailsInstallAndNamesTheExtension();
}

TEST_CASE("an empty component name fails install and names the extension", "[scripting]")
{
    AnEmptyComponentNameFailsInstallAndNamesTheExtension();
}

TEST_CASE("a duplicate event name fails install and names the extension", "[scripting]")
{
    ADuplicateEventNameFailsInstallAndNamesTheExtension();
}

TEST_CASE("a mismatched event fails the load and names the field", "[scripting]")
{
    AMismatchedEventFailsTheLoadAndNamesTheField();
}

TEST_CASE("a failed load leaves no components and no pools", "[scripting]")
{
    AFailedLoadLeavesNoComponentsAndNoPools();
}

TEST_CASE("a sound report loads and registers everything together", "[scripting]")
{
    ASoundReportLoadsAndRegistersEverythingTogether();
}

TEST_CASE("a duplicate script component is refused and leaves nothing behind", "[scripting]")
{
    ADuplicateScriptComponentIsRefused();
}

TEST_CASE("a duplicate script event is refused and leaves nothing behind", "[scripting]")
{
    ADuplicateScriptEventIsRefused();
}

TEST_CASE("a wrong field type is caught too", "[scripting]")
{
    AWrongFieldTypeIsCaughtToo();
}

TEST_CASE("a C# field with no native function fails Install and names it", "[scripting]")
{
    ACSharpFieldWithNoNativeFunctionFailsInstallAndNamesIt();
}

TEST_CASE("a native function whose signature differs from its C# field fails Install and names it", "[scripting]")
{
    ANativeFunctionWhoseSignatureDiffersFromItsFieldFailsInstallAndNamesIt();
}

TEST_CASE("two extensions declaring one native function fail Install and name both", "[scripting]")
{
    TwoExtensionsDeclaringOneNativeFunctionFailInstallAndNameBoth();
}

TEST_CASE("a native function taking a state its extension lacks fails Install and names it", "[scripting]")
{
    ANativeFunctionTakingAStateItsExtensionLacksFailsInstallAndNamesIt();
}

TEST_CASE("a native function taking a state of another type fails Install and names both types", "[scripting]")
{
    ANativeFunctionTakingAStateOfAnotherTypeFailsInstallAndNamesBothTypes();
}

TEST_CASE("a shared struct whose C# mirror drifted fails the load and names it", "[scripting]")
{
    ASharedStructWhoseCSharpMirrorDriftedFailsTheLoadAndNamesIt();
}

TEST_CASE("a shared struct cannot take a component's name", "[scripting]")
{
    ASharedStructCannotTakeAComponentsName();
}

TEST_CASE("an extension cannot take an engine component's name", "[scripting]")
{
    AnExtensionCannotTakeAnEngineComponentsName();
}

TEST_CASE("a resource whose layout differs from its C# struct fails the load", "[scripting]")
{
    AResourceWhoseLayoutDiffersFromItsCSharpStructFailsTheLoad();
}

TEST_CASE("a duplicate resource name fails install and names the extension", "[scripting]")
{
    ADuplicateResourceNameFailsInstallAndNamesTheExtension();
}
