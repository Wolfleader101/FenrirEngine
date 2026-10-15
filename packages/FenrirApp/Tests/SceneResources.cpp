#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"

namespace
{
    // A logger that drops everything: a second ConsoleLogger in one process throws.
    class SilentLogger : public Fenrir::ILogger
    {
      public:
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view) override
        {
        }
        void ErrorImpl(std::string_view) override
        {
        }
        void FatalImpl(std::string_view) override
        {
        }
    };

    std::unique_ptr<Fenrir::ILogger> Silent()
    {
        return std::make_unique<SilentLogger>();
    }

    constexpr double sixtieth = 1.0 / 60.0;

    // A scene resource that writes its name to a shared record when it is destroyed.
    struct Recorder
    {
        Recorder(std::vector<std::string>& destroyed, std::string name) : destroyed(&destroyed), name(std::move(name))
        {
        }
        Recorder(const Recorder&) = delete;
        Recorder& operator=(const Recorder&) = delete;
        ~Recorder()
        {
            destroyed->push_back(name);
        }

        std::vector<std::string>* destroyed;
        std::string name;
    };

    struct Gravity
    {
        float y = -9.81f;
    };
} // namespace

using Fenrir::App;
using Fenrir::Scene;

TEST_CASE("A scene has no resource of a type until one is added, then gives back that one", "[app][scene-resources]")
{
    App app(Silent());
    Scene& scene = app.GetActiveScene();

    INFO("a scene starts with no resources");
    REQUIRE(scene.GetResource<Gravity>() == nullptr);

    Gravity& added = scene.AddResource<Gravity>(-3.0f);
    app.RunFrame(sixtieth);

    Gravity* found = scene.GetResource<Gravity>();
    INFO("GetResource gives back the resource AddResource made");
    REQUIRE(found == &added);
    INFO("AddResource constructs the resource from its arguments");
    REQUIRE(found->y == -3.0f);
}

TEST_CASE("Adding a resource of a type the scene already has replaces it and destroys the old one",
          "[app][scene-resources]")
{
    std::vector<std::string> destroyed;
    App app(Silent());
    Scene& scene = app.GetActiveScene();

    scene.AddResource<Recorder>(destroyed, "first");
    app.RunFrame(sixtieth);
    Recorder& second = scene.AddResource<Recorder>(destroyed, "second");

    INFO("the replaced resource is destroyed when it is replaced");
    REQUIRE(destroyed == std::vector<std::string>{"first"});
    INFO("the scene now gives back the replacement");
    REQUIRE(scene.GetResource<Recorder>() == &second);
    REQUIRE(scene.GetResource<Recorder>()->name == "second");
}

TEST_CASE("Removing a resource destroys it and leaves the scene without one", "[app][scene-resources]")
{
    std::vector<std::string> destroyed;
    App app(Silent());
    Scene& scene = app.GetActiveScene();
    scene.AddResource<Recorder>(destroyed, "world");
    scene.AddResource<Gravity>();
    app.RunFrame(sixtieth);

    scene.RemoveResource<Recorder>();

    INFO("RemoveResource destroys the resource");
    REQUIRE(destroyed == std::vector<std::string>{"world"});
    INFO("the scene has no resource of the removed type");
    REQUIRE(scene.GetResource<Recorder>() == nullptr);
    INFO("resources of other types are untouched");
    REQUIRE(scene.GetResource<Gravity>() != nullptr);

    scene.RemoveResource<Recorder>();
    INFO("removing a resource the scene does not have does nothing");
    REQUIRE(destroyed == std::vector<std::string>{"world"});
}

TEST_CASE("A scene's resources are destroyed when the scene is destroyed", "[app][scene-resources]")
{
    std::vector<std::string> destroyed;
    App app(Silent());
    app.CreateScene("Level").AddResource<Recorder>(destroyed, "level world");
    app.RunFrame(sixtieth);

    app.DestroyScene("Level");

    INFO("DestroyScene ends the scene's resources with it");
    REQUIRE(destroyed == std::vector<std::string>{"level world"});
}

TEST_CASE("Every scene's resources are destroyed when the App ends", "[app][scene-resources]")
{
    std::vector<std::string> destroyed;
    {
        App app(Silent());
        app.GetActiveScene().AddResource<Recorder>(destroyed, "default world");
        app.CreateScene("Level").AddResource<Recorder>(destroyed, "level world");
        app.RunFrame(sixtieth);
        app.RunExit();

        INFO("resources outlive the frames and the Exit phase while the App exists");
        REQUIRE(destroyed.empty());
    }

    INFO("ending the App ends every scene's resources, active or not");
    REQUIRE(destroyed.size() == 2);
    REQUIRE(std::find(destroyed.begin(), destroyed.end(), "default world") != destroyed.end());
    REQUIRE(std::find(destroyed.begin(), destroyed.end(), "level world") != destroyed.end());
}

TEST_CASE("Two scenes hold independent resources of the same type", "[app][scene-resources]")
{
    App app(Silent());
    app.GetActiveScene().AddResource<Gravity>(-1.0f);
    app.CreateScene("Level").AddResource<Gravity>(-2.0f);
    app.RunFrame(sixtieth);

    Scene& defaultScene = app.GetScene("Default");
    Scene& level = app.GetScene("Level");
    INFO("each scene keeps its own value of the same resource type");
    REQUIRE(defaultScene.GetResource<Gravity>()->y == -1.0f);
    REQUIRE(level.GetResource<Gravity>()->y == -2.0f);

    level.GetResource<Gravity>()->y = -5.0f;
    INFO("changing one scene's resource leaves the other scene's alone");
    REQUIRE(app.GetScene("Default").GetResource<Gravity>()->y == -1.0f);

    app.GetScene("Level").RemoveResource<Gravity>();
    INFO("removing one scene's resource leaves the other scene's in place");
    REQUIRE(app.GetScene("Default").GetResource<Gravity>() != nullptr);
    REQUIRE(app.GetScene("Level").GetResource<Gravity>() == nullptr);
}
