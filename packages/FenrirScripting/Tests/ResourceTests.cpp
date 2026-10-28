// Native resources from an extension (ADR-0029): read and written from C# through Ref, TryGet and Get when absent,
// a scene resource following the active scene, inserts and removals at playback in order, and lookup failures.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"
#include "TestExtension.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    // Native resources and the change buffer from an extension (ADR-0029), through the test extension.

    void AnExtensionsResourcesAreReadAndWrittenFromCSharpThroughRef()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("The extension's resources did not pass their checks against C#");
            REQUIRE(scripting.HasScripts());
        }
        auto& weather = headless.Get().GetActiveScene().AddResource<TestExtension::Weather>(2.0F, 1);
        auto& settings = headless.Get().AddResource<TestExtension::Settings>(5);
        headless.Get().AddResource<TestExtension::Clock>(12.5);
        {
            INFO("C# did not read the scene resource's value");
            REQUIRE(ExtensionCall<float>(scripting, "WeatherWind") == 2.0F);
        }
        ExtensionCall<void>(scripting, "SetWeather", 7.0F, std::int32_t{3});
        {
            INFO("C#'s writes through Ref<Weather> did not reach the scene resource in place");
            REQUIRE((weather.Wind == 7.0F && weather.Gusts == 3));
        }
        {
            INFO("C# did not read the app resource's value");
            REQUIRE(ExtensionCall<std::int32_t>(scripting, "SettingsVolume") == 5);
        }
        ExtensionCall<void>(scripting, "SetVolume", std::int32_t{9});
        {
            INFO("C#'s write through Ref<Settings> did not reach the app resource in place");
            REQUIRE(settings.Volume == 9);
        }
        {
            INFO("C# did not read the read-only app resource through its ReadOnlyRef");
            REQUIRE(ExtensionCall<double>(scripting, "ClockSeconds") == 12.5);
        }
    }

    void TryGetIsFalseWhenAbsentAndGetLogsAndReturnsAnEmptyRef()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        {
            INFO("TryGet said there is a scene resource nobody inserted");
            REQUIRE(!ExtensionCall<bool>(scripting, "HasWeather"));
        }
        {
            INFO("TryGetReadOnly said there is an app resource nobody inserted");
            REQUIRE(!ExtensionCall<bool>(scripting, "HasClock"));
        }
        {
            INFO("Get on an absent resource did not return an empty Ref");
            REQUIRE(ExtensionCall<bool>(scripting, "MissingWeatherIsEmpty"));
        }
        {
            INFO("Get on an absent resource did not log a warning naming it");
            REQUIRE(headless.Logger().Contains(Testing::LogLevel::Warn, "no Fenrir.TestExtension.Weather resource"));
        }
        headless.Get().GetActiveScene().AddResource<TestExtension::Weather>(1.5F, 0);
        {
            INFO("TryGet did not give a view of a resource that is there");
            REQUIRE(ExtensionCall<float>(scripting, "TryGetWind") == 1.5F);
        }
    }

    void ASceneResourceFollowsTheActiveScene()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        App& app = headless.Get();
        const std::string first = app.GetActiveScene().GetName();
        auto& firstWeather = app.GetActiveScene().AddResource<TestExtension::Weather>(2.0F, 0);
        auto& secondWeather = app.CreateScene("second").AddResource<TestExtension::Weather>(9.0F, 0);

        app.ChangeActiveScene("second");
        {
            INFO("C# did not read the newly active scene's resource");
            REQUIRE(ExtensionCall<float>(scripting, "WeatherWind") == 9.0F);
        }
        ExtensionCall<void>(scripting, "SetWeather", 4.0F, std::int32_t{0});
        {
            INFO("C#'s write did not land in the active scene's resource alone");
            REQUIRE((secondWeather.Wind == 4.0F && firstWeather.Wind == 2.0F));
        }
        app.ChangeActiveScene(first);
        {
            INFO("C# did not go back to the first scene's resource");
            REQUIRE(ExtensionCall<float>(scripting, "WeatherWind") == 2.0F);
        }
    }

    void InsertingAndRemovingAResourceFromCSharpTakeEffectAtPlaybackInOrder()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        App& app = headless.Get();
        Scene& scene = app.GetActiveScene();

        {
            INFO("An inserted resource was there before playback");
            REQUIRE(!ExtensionCall<bool>(scripting, "InsertWeather", 3.0F));
            REQUIRE(scene.GetResource<TestExtension::Weather>() == nullptr);
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("The insert did not play back with C#'s value");
            const auto* weather = scene.GetResource<TestExtension::Weather>();
            REQUIRE((weather != nullptr && weather->Wind == 3.0F && weather->Gusts == 2));
        }
        {
            INFO("A removed resource was gone before playback");
            REQUIRE(ExtensionCall<bool>(scripting, "RemoveWeather"));
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("The removal did not play back");
            REQUIRE(scene.GetResource<TestExtension::Weather>() == nullptr);
        }

        ExtensionCall<void>(scripting, "RemoveThenInsertWeather", 6.0F);
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Remove then insert did not leave the inserted resource: they did not play back in order");
            const auto* weather = scene.GetResource<TestExtension::Weather>();
            REQUIRE((weather != nullptr && weather->Wind == 6.0F));
        }
        ExtensionCall<void>(scripting, "InsertThenRemoveSettings", std::int32_t{4});
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Insert then remove left an app resource: they did not play back in order");
            REQUIRE(app.TryGetResource<TestExtension::Settings>() == nullptr);
        }
        ExtensionCall<void>(scripting, "InsertDefaultSettings");
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("An insert with no value did not make the C++ default (Volume 11)");
            const auto* settings = app.TryGetResource<TestExtension::Settings>();
            REQUIRE((settings != nullptr && settings->Volume == 11));
        }
        {
            INFO("A Ref taken before an insert and a removal was not good until the call returned");
            REQUIRE(ExtensionCall<float>(scripting, "RefSurvivesInsertAndRemove", 8.0F) == 8.0F);
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("The insert recorded after the removal is not the resource after playback");
            const auto* weather = scene.GetResource<TestExtension::Weather>();
            REQUIRE((weather != nullptr && weather->Wind == 100.0F));
        }
    }

    void ResourceLookupFailuresSayWhatWentWrong()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ExtensionConfig({TheTestExtension()}));
        const auto entity = headless.Get().GetActiveScene().GetEntityList().CreateEntity().GetId();
        const auto asComponent =
            ExtensionCall<std::optional<std::string>>(scripting, "TryAddWeatherAsComponent", entity).value_or("");
        {
            INFO("Adding a resource to an entity said: " + asComponent);
            REQUIRE(asComponent.find("is a resource, not a component") != std::string::npos);
        }
        const auto unregistered =
            ExtensionCall<std::optional<std::string>>(scripting, "TryGetUnregistered").value_or("");
        {
            INFO("Getting a resource no extension registered said: " + unregistered);
            REQUIRE(unregistered.find("no resource registered as 'Fenrir.TestExtension.Unregistered'") !=
                    std::string::npos);
        }
        const auto claimed = ExtensionCall<std::optional<std::string>>(scripting, "TryClaimClock").value_or("");
        {
            INFO("Claiming a resource with a struct of its own said: " + claimed);
            REQUIRE(claimed.find("registered for a different C# type") != std::string::npos);
        }
    }

} // namespace

TEST_CASE("an extension's resources are read and written from C# through Ref", "[scripting]")
{
    AnExtensionsResourcesAreReadAndWrittenFromCSharpThroughRef();
}

TEST_CASE("TryGet is false when a resource is absent, and Get logs and returns an empty Ref", "[scripting]")
{
    TryGetIsFalseWhenAbsentAndGetLogsAndReturnsAnEmptyRef();
}

TEST_CASE("a scene resource follows the active scene", "[scripting]")
{
    ASceneResourceFollowsTheActiveScene();
}

TEST_CASE("inserting and removing a resource from C# take effect at playback, in order", "[scripting]")
{
    InsertingAndRemovingAResourceFromCSharpTakeEffectAtPlaybackInOrder();
}

TEST_CASE("resource lookup failures say what went wrong", "[scripting]")
{
    ResourceLookupFailuresSayWhatWentWrong();
}
