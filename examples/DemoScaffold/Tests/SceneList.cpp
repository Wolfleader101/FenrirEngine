#include <algorithm>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoTestApp.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirScene/Scene.hpp"

using namespace DemoScaffoldTests;

namespace
{
    std::vector<std::string> SceneNames(Fenrir::App& app)
    {
        std::vector<std::string> names;
        app.ForEachScene([&](Fenrir::Scene& scene) { names.push_back(scene.GetName()); });
        return names;
    }

    // A scene that spawns a camera of its own, and keeps it in `camera`.
    FenrirDemo::DemoScene SceneWithCamera(Fenrir::Entity& camera)
    {
        FenrirDemo::DemoScene scene = EmptyScene("Chase");
        scene.spawn = [&camera](Fenrir::App&, Fenrir::Scene& spawned) {
            camera = spawned.GetEntityList().CreateEntity();
            camera.AddComponent<Fenrir::Transform>();
            camera.AddComponent<Fenrir::Camera>();
        };
        return scene;
    }
} // namespace

TEST_CASE("The first frame enters the first scene", "[demo][scenes]")
{
    DemoTestApp test({EmptyScene("One"), EmptyScene("Two")});

    test.Frames();

    INFO("the active scene is the first in the list");
    REQUIRE(test.ActiveScene() == "One");
}

TEST_CASE("FENRIR_DEMO_SCENE names the scene the app starts in", "[demo][scenes]")
{
    FenrirDemo::DemoRunOptions run = NullRun();
    run.scene = "Two";
    DemoTestApp test({EmptyScene("One"), EmptyScene("Two")}, run);

    test.Frames();

    INFO("the app starts in the named scene");
    REQUIRE(test.ActiveScene() == "Two");
}

TEST_CASE("PageDown and PageUp step through the scene list, wrapping at both ends", "[demo][scenes]")
{
    DemoTestApp test({EmptyScene("One"), EmptyScene("Two"), EmptyScene("Three")});
    test.Frames();

    test.Tap(Fenrir::Key::PageDown);
    INFO("PageDown steps to the next scene");
    REQUIRE(test.ActiveScene() == "Two");

    test.Tap(Fenrir::Key::PageDown);
    test.Tap(Fenrir::Key::PageDown);
    INFO("PageDown on the last scene wraps to the first");
    REQUIRE(test.ActiveScene() == "One");

    test.Tap(Fenrir::Key::PageUp);
    INFO("PageUp on the first scene wraps to the last");
    REQUIRE(test.ActiveScene() == "Three");
}

TEST_CASE("Each scene is spawned into a FenrirScene of its own, destroyed when it is left", "[demo][scenes]")
{
    int spawns = 0;
    FenrirDemo::DemoScene counted = EmptyScene("Counted");
    counted.spawn = [&spawns](Fenrir::App&, Fenrir::Scene& scene) {
        ++spawns;
        scene.GetEntityList().CreateEntity().AddComponent<Fenrir::Transform>();
    };
    DemoTestApp test({counted, EmptyScene("Other")});
    test.Frames();
    const std::vector<std::string> before = SceneNames(test.app);

    test.Tap(Fenrir::Key::PageDown);

    const std::vector<std::string> after = SceneNames(test.app);
    INFO("the left scene's FenrirScene is gone, and the entered one's is there");
    REQUIRE(std::find(after.begin(), after.end(), "Counted") == after.end());
    REQUIRE(std::find(after.begin(), after.end(), "Other") != after.end());
    INFO("leaving one scene and entering another leaves as many FenrirScenes as before");
    REQUIRE(after.size() == before.size());

    test.Tap(Fenrir::Key::PageDown);
    INFO("a scene entered again is spawned again");
    REQUIRE(spawns == 2);
}

TEST_CASE("A scene with a camera of its own turns the free-fly camera off until F3", "[demo][scenes][camera]")
{
    Fenrir::Entity sceneCamera;
    DemoTestApp test({SceneWithCamera(sceneCamera)});
    test.Frames();

    INFO("the scene's camera draws, and the free-fly camera does not");
    REQUIRE(sceneCamera.GetComponent<Fenrir::Camera>().enabled);
    REQUIRE_FALSE(test.demo.FreeFlyCamera().GetComponent<Fenrir::Camera>().enabled);
    REQUIRE_FALSE(test.demo.FreeFlyCameraOn());

    test.Tap(Fenrir::Key::F3);
    INFO("F3 turns the free-fly camera on and the scene's camera off");
    REQUIRE(test.demo.FreeFlyCamera().GetComponent<Fenrir::Camera>().enabled);
    REQUIRE_FALSE(sceneCamera.GetComponent<Fenrir::Camera>().enabled);

    test.Tap(Fenrir::Key::F3);
    INFO("F3 again goes back to the scene's camera");
    REQUIRE(sceneCamera.GetComponent<Fenrir::Camera>().enabled);
    REQUIRE_FALSE(test.demo.FreeFlyCamera().GetComponent<Fenrir::Camera>().enabled);
}

TEST_CASE("A scene with no camera of its own draws through the free-fly camera", "[demo][scenes][camera]")
{
    DemoTestApp test({EmptyScene("Empty")});
    test.Frames();

    INFO("the free-fly camera is on and enabled");
    REQUIRE(test.demo.FreeFlyCameraOn());
    REQUIRE(test.demo.FreeFlyCamera().GetComponent<Fenrir::Camera>().enabled);
}

TEST_CASE("F1 logs the help: the app, the scene and the scene's keys", "[demo][help]")
{
    FenrirDemo::DemoScene scene = EmptyScene("Lit cube");
    scene.keyHelp = "L rotates the light";
    DemoTestApp test({scene});
    test.Frames();

    test.Tap(Fenrir::Key::F1);

    INFO("the help names the app and the scene");
    REQUIRE(test.logged->InfoContains("TestDemo: Lit cube (1/1)"));
    INFO("the help lists the scene's keys");
    REQUIRE(test.logged->InfoContains("L rotates the light"));
    INFO("the help lists the scaffold's keys");
    REQUIRE(test.logged->InfoContains("PageDown/PageUp"));
}
