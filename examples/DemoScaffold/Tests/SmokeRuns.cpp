#include <map>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DemoTestApp.hpp"
#include "FenrirInput/Devices.hpp"

using namespace DemoScaffoldTests;

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

TEST_CASE("A smoke run naming a scene the app does not have fails", "[demo][smoke]")
{
    FenrirDemo::DemoRunOptions run = Smoke(5);
    run.scene = "Missing";
    DemoTestApp test({EmptyScene("Only")}, run);

    const int exitCode = test.demo.Run();

    INFO("a misspelt FENRIR_DEMO_SCENE is an error, so it cannot pass by running nothing");
    REQUIRE(exitCode != 0);
}
