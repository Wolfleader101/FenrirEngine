// An app resource reached by the name a file spells (ADR-0074): the hook a generated registration calls for each
// `app-resource` type hangs type-erased find, add and remove doors on it, and App's named doors open them. The
// description here is hand-written, standing in for a package's generated registration, and is registered in the
// App's own registry.
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "RecordingLogger.hpp"

using Fenrir::Role;
using Fenrir::TypeDescription;
using Fenrir::TypeRegistry;

namespace
{
    // An app resource whose default is not all zero, so default construction can be told from zeroing.
    struct Clock
    {
        double scale = 1.5;
    };

    // A scene resource: registered, but not an app resource.
    struct Weather
    {
        float rain = 0.25F;
    };

    const TypeDescription ClockDescription{
        .name = "AppTest::Clock",
        .package = "FenrirAppTests",
        .role = Role::AppResource,
        .size = sizeof(Clock),
        .alignment = alignof(Clock),
        .construct = Fenrir::ConstructValue<Clock>,
        .destroy = Fenrir::DestroyValue<Clock>,
    };

    const TypeDescription WeatherDescription{
        .name = "AppTest::Weather",
        .package = "FenrirAppTests",
        .role = Role::SceneResource,
        .size = sizeof(Weather),
        .alignment = alignof(Weather),
        .construct = Fenrir::ConstructValue<Weather>,
        .destroy = Fenrir::DestroyValue<Weather>,
    };

    // A headless App whose registry holds Clock, handed to FenrirApp's hook as a registration would, and Weather,
    // handed to FenrirScene's.
    struct RegisteredApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<AppTest::RecordingLogger>(errors), 1};

        RegisteredApp()
        {
            TypeRegistry& registry = Registry();
            INFO("setup: Clock registers as an app resource and Weather as a scene resource, each with its doors");
            REQUIRE(registry.Register(ClockDescription));
            REQUIRE(Fenrir::AttachAppResource<Clock>(registry, ClockDescription));
            REQUIRE(registry.Register(WeatherDescription));
            REQUIRE(Fenrir::AttachSceneResource<Weather>(registry, WeatherDescription));
        }

        [[nodiscard]] TypeRegistry& Registry()
        {
            return app.GetResource<TypeRegistry>();
        }
    };
} // namespace

TEST_CASE("App AddResource by name makes a default resource the C++ type finds", "[app][types]")
{
    RegisteredApp registered;

    void* added = registered.app.AddResource(registered.Registry(), "AppTest::Clock");

    INFO("adding by an app resource's name should give the new resource");
    REQUIRE(added != nullptr);
    INFO("TryGetResource<Clock> should find the resource added by name");
    REQUIRE(registered.app.TryGetResource<Clock>() == added);
    INFO("and it is default-constructed");
    REQUIRE(registered.app.GetResource<Clock>().scale == 1.5);
}

TEST_CASE("App FindResource by name finds the resource the C++ type added", "[app][types]")
{
    RegisteredApp registered;

    INFO("before anything is added the app has no Clock to find");
    REQUIRE(registered.app.FindResource(registered.Registry(), "AppTest::Clock") == nullptr);

    Clock& clock = registered.app.AddResource<Clock>();
    INFO("FindResource by name should give the Clock AddResource<Clock> made");
    REQUIRE(registered.app.FindResource(registered.Registry(), "AppTest::Clock") == &clock);
}

TEST_CASE("App RemoveResource by name removes the resource", "[app][types]")
{
    RegisteredApp registered;
    registered.app.AddResource<Clock>();

    registered.app.RemoveResource(registered.Registry(), "AppTest::Clock");

    INFO("the app should have no Clock once it is removed by name");
    REQUIRE(registered.app.TryGetResource<Clock>() == nullptr);
}

TEST_CASE("A name that is not an app resource's opens no App door, with no diagnostic", "[app][types]")
{
    RegisteredApp registered;
    TypeRegistry& registry = registered.Registry();

    for (const std::string_view name : {"AppTest::Weather", "AppTest::NoSuchType"})
    {
        INFO(name << " is not an app resource, so adding it gives nothing");
        REQUIRE(registered.app.AddResource(registry, name) == nullptr);
        INFO(name << " is not an app resource, so finding it gives nothing");
        REQUIRE(registered.app.FindResource(registry, name) == nullptr);
        registered.app.RemoveResource(registry, name);
    }

    INFO("nothing should be reported about names that open no door");
    REQUIRE(registered.errors->empty());
    INFO("and the app gained no Weather");
    REQUIRE(registered.app.TryGetResource<Weather>() == nullptr);
}
