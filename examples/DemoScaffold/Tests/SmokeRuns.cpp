#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoRoundTrip.hpp"
#include "DemoTestApp.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/SceneComponents.hpp"
#include "FenrirInput/Devices.hpp"
#include "FenrirScene/Scene.hpp"

using namespace DemoScaffoldTests;
namespace Detail = FenrirDemo::Detail;

namespace
{
    // A scene that counts the frames it is active and the keys it sees pressed.
    struct Watched
    {
        int frames = 0;
        std::vector<Fenrir::Key> pressed;
    };

    FenrirDemo::DemoScene WatchedScene(std::string name, std::vector<Fenrir::Key> keys, Watched& watched)
    {
        FenrirDemo::DemoScene scene = EmptyScene(std::move(name));
        scene.keys = keys;
        scene.update = [&watched, keys](Fenrir::App& app, Fenrir::Scene&) {
            ++watched.frames;
            const auto& keyboard = app.GetResource<Fenrir::Keyboard>();
            for (const Fenrir::Key key : keys)
            {
                if (keyboard.JustPressed(key))
                {
                    watched.pressed.push_back(key);
                }
            }
        };
        return scene;
    }

    FenrirDemo::DemoRunOptions Smoke(std::uint32_t frames)
    {
        FenrirDemo::DemoRunOptions run = NullRun();
        run.smokeFrames = frames;
        return run;
    }
} // namespace

TEST_CASE("A smoke run runs every scene its frames with the scene's keys pressed, then exits with 0", "[demo][smoke]")
{
    Watched first;
    Watched second;
    DemoTestApp test({WatchedScene("First", {Fenrir::Key::R, Fenrir::Key::T}, first),
                      WatchedScene("Second", {Fenrir::Key::Space}, second)},
                     Smoke(30));

    const int exitCode = test.demo.Run();

    INFO("a smoke run with no error logged exits with 0");
    REQUIRE(exitCode == 0);
    INFO("each scene is active for the smoke run's 30 frames");
    REQUIRE(first.frames == 30);
    REQUIRE(second.frames == 30);
    INFO("each scene sees each of its keys pressed once, in order");
    REQUIRE(first.pressed == std::vector<Fenrir::Key>{Fenrir::Key::R, Fenrir::Key::T});
    REQUIRE(second.pressed == std::vector<Fenrir::Key>{Fenrir::Key::Space});
}

TEST_CASE("A smoke run of a named scene runs that scene only", "[demo][smoke]")
{
    Watched first;
    Watched second;
    FenrirDemo::DemoRunOptions run = Smoke(10);
    run.scene = "Second";
    DemoTestApp test({WatchedScene("First", {}, first), WatchedScene("Second", {}, second)}, run);

    const int exitCode = test.demo.Run();

    INFO("the smoke run passes");
    REQUIRE(exitCode == 0);
    INFO("the scene FENRIR_DEMO_SCENE does not name never runs");
    REQUIRE(first.frames == 0);
    INFO("the named scene runs its frames");
    REQUIRE(second.frames == 10);
}

TEST_CASE("A smoke run in which an error is logged exits non-zero", "[demo][smoke]")
{
    FenrirDemo::DemoScene failing = EmptyScene("Failing");
    failing.update = [](Fenrir::App& app, Fenrir::Scene&) { app.Logger()->Error("a deliberate error"); };
    DemoTestApp test({EmptyScene("Clean"), failing}, Smoke(5));

    const int exitCode = test.demo.Run();

    INFO("one logged error fails the smoke run");
    REQUIRE(exitCode != 0);
}

TEST_CASE("A smoke run saves each scene and loads it back whole, without the free-fly camera", "[demo][smoke]")
{
    FenrirDemo::DemoScene built = EmptyScene("Built");
    built.spawn = [](Fenrir::App&, Fenrir::Scene& scene) {
        Fenrir::EntityList& entities = scene.GetEntityList();
        Fenrir::Entity parent = entities.CreateEntity();
        parent.GetComponent<Fenrir::Transform>().pos = Fenrir::Math::Vec3(1.0f, 2.0f, 3.0f);
        Fenrir::Entity child = entities.CreateEntity();
        parent.AddChild(child);
        // A debug entity a scene made, with a child of its own: neither is the scene's.
        Fenrir::Entity debug = entities.CreateEntity();
        debug.AddComponent<Fenrir::Transient>();
        debug.AddChild(entities.CreateEntity());
    };
    DemoTestApp test({EmptyScene("Empty"), built}, Smoke(3));

    const int exitCode = test.demo.Run();

    INFO("the round trips log no error, so the smoke run passes");
    REQUIRE(test.logged->errors.empty());
    REQUIRE(exitCode == 0);
    INFO("each scene is saved and loaded back, and the free-fly camera and the transient entities are in neither");
    REQUIRE(test.logged->InfoContains("Empty saved and loaded back whole: 0 entities"));
    REQUIRE(test.logged->InfoContains("Built saved and loaded back whole: 2 entities"));
}

TEST_CASE("A smoke run's round trip leaves no scene file in the asset root", "[demo][smoke]")
{
    FenrirDemo::DemoScene built = EmptyScene("Built");
    built.spawn = [](Fenrir::App&, Fenrir::Scene& scene) { scene.GetEntityList().CreateEntity(); };
    DemoTestApp test({built}, Smoke(3));
    const std::filesystem::path root = test.app.GetResource<Fenrir::AssetStore>().Root();

    REQUIRE(test.demo.Run() == 0);

    INFO("the temporary folder of the round trip's scene files is gone");
    bool leftOver = false;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(root, error))
    {
        leftOver = leftOver || entry.path().filename().string().starts_with(".demo-smoke-");
    }
    REQUIRE_FALSE(leftOver);
}

TEST_CASE("A component lost on the way back fails the round trip, naming the scene", "[demo][smoke]")
{
    const Detail::SceneRoundTripCounts saved{3, 7};

    INFO("equal counts are no mismatch");
    REQUIRE_FALSE(Detail::RoundTripMismatch("Courtyard", saved, {3, 7}));

    INFO("a component dropped on load is a mismatch, and the error names the scene");
    const std::optional<std::string> dropped = Detail::RoundTripMismatch("Courtyard", saved, {3, 6});
    REQUIRE(dropped);
    REQUIRE(dropped->find("\"Courtyard\"") != std::string::npos);
}

TEST_CASE("An entity lost on the way back fails the round trip", "[demo][smoke]")
{
    INFO("one entity fewer loaded back is a mismatch");
    REQUIRE(Detail::RoundTripMismatch("Courtyard", {3, 7}, {2, 7}));
}

TEST_CASE("A smoke run naming a scene the app does not have fails", "[demo][smoke]")
{
    FenrirDemo::DemoRunOptions run = Smoke(5);
    run.scene = "Missing";
    DemoTestApp test({EmptyScene("Only")}, run);

    const int exitCode = test.demo.Run();

    INFO("a misspelt FENRIR_DEMO_SCENE is an error, so it cannot pass by running nothing");
    REQUIRE(exitCode != 0);
}
