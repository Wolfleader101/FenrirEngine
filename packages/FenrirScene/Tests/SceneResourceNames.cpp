// A scene's resource reached by the name a file spells (ADR-0074): the hook a generated registration calls for each
// `scene-resource` type hangs type-erased find, add and remove doors on it, and Scene's named doors open them. The
// descriptions here are hand-written, standing in for a package's generated registration.
#include <catch2/catch_test_macros.hpp>

#include "FenrirScene/Scene.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

using Fenrir::Role;
using Fenrir::Scene;
using Fenrir::TypeDescription;
using Fenrir::TypeRegistry;

namespace
{
    // A scene resource whose default is not all zero, so default construction can be told from zeroing.
    struct Weather
    {
        float rain = 0.25F;
    };

    // A component: registered, but not a scene resource.
    struct Footprint
    {
        int depth = 1;
    };

    constexpr std::string_view WeatherFormerly[] = {"SceneTest::Climate"};

    const TypeDescription WeatherDescription{
        .name = "SceneTest::Weather",
        .package = "FenrirSceneTests",
        .role = Role::SceneResource,
        .formerly = WeatherFormerly,
        .size = sizeof(Weather),
        .alignment = alignof(Weather),
        .construct = Fenrir::ConstructValue<Weather>,
        .destroy = Fenrir::DestroyValue<Weather>,
    };

    const TypeDescription FootprintDescription{
        .name = "SceneTest::Footprint",
        .package = "FenrirSceneTests",
        .role = Role::Component,
        .size = sizeof(Footprint),
        .alignment = alignof(Footprint),
        .construct = Fenrir::ConstructValue<Footprint>,
        .destroy = Fenrir::DestroyValue<Footprint>,
    };

    // A registry as a registration leaves it: Weather registered and handed to FenrirScene's hook, Footprint
    // registered with no scene doors. Its diagnostics go to `messages`.
    struct Registered
    {
        std::shared_ptr<std::vector<std::string>> messages = std::make_shared<std::vector<std::string>>();
        TypeRegistry registry{[messages = messages](std::string_view message) { messages->emplace_back(message); }};

        Registered()
        {
            INFO("setup: Weather registers with its scene doors, and Footprint registers without any");
            REQUIRE(registry.Register(WeatherDescription));
            REQUIRE(Fenrir::AttachSceneResource<Weather>(registry, WeatherDescription));
            REQUIRE(registry.Register(FootprintDescription));
        }
    };
} // namespace

TEST_CASE("AddResource by name makes a default resource the C++ type finds", "[scene][types]")
{
    Registered types;
    Scene scene("Test");

    void* added = scene.AddResource(types.registry, "SceneTest::Weather");

    INFO("adding by a scene resource's name should give the new resource");
    REQUIRE(added != nullptr);
    INFO("GetResource<Weather> should find the resource added by name");
    REQUIRE(scene.GetResource<Weather>() == added);
    INFO("and it is default-constructed");
    REQUIRE(scene.GetResource<Weather>()->rain == 0.25F);
}

TEST_CASE("FindResource by name finds the resource the C++ type added", "[scene][types]")
{
    Registered types;
    Scene scene("Test");

    INFO("before anything is added the scene has no Weather to find");
    REQUIRE(scene.FindResource(types.registry, "SceneTest::Weather") == nullptr);

    Weather& weather = scene.AddResource<Weather>();
    INFO("FindResource by name should give the Weather AddResource<Weather> made");
    REQUIRE(scene.FindResource(types.registry, "SceneTest::Weather") == &weather);
}

TEST_CASE("RemoveResource by name removes the resource", "[scene][types]")
{
    Registered types;
    Scene scene("Test");
    scene.AddResource<Weather>();

    scene.RemoveResource(types.registry, "SceneTest::Weather");

    INFO("the scene should have no Weather once it is removed by name");
    REQUIRE(scene.GetResource<Weather>() == nullptr);
}

TEST_CASE("A scene resource's former name opens its doors", "[scene][types]")
{
    Registered types;
    Scene scene("Test");

    void* added = scene.AddResource(types.registry, "SceneTest::Climate");

    INFO("SceneTest::Climate is Weather's former name, so adding by it should make a Weather");
    REQUIRE(added != nullptr);
    INFO("the one GetResource<Weather> finds");
    REQUIRE(scene.GetResource<Weather>() == added);
}

TEST_CASE("A name that is not a scene resource's opens no door, with no diagnostic", "[scene][types]")
{
    Registered types;
    Scene scene("Test");

    for (const std::string_view name : {"SceneTest::Footprint", "SceneTest::NoSuchType"})
    {
        INFO(name << " is not a scene resource, so adding it gives nothing");
        REQUIRE(scene.AddResource(types.registry, name) == nullptr);
        INFO(name << " is not a scene resource, so finding it gives nothing");
        REQUIRE(scene.FindResource(types.registry, name) == nullptr);
        scene.RemoveResource(types.registry, name);
    }

    INFO("the registry should report nothing about names that open no door");
    REQUIRE(types.messages->empty());
    INFO("and the scene gained no resource");
    REQUIRE(scene.GetResource<Footprint>() == nullptr);
}
