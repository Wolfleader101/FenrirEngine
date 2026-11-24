#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"

namespace
{
    // Keeps the errors it is given; a second ConsoleLogger in one process throws.
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<std::vector<std::string>> errors) : m_errors(std::move(errors))
        {
        }
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
        void ErrorImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }
        void FatalImpl(std::string_view) override
        {
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_errors;
    };

    struct Health
    {
        int value = 0;
    };
} // namespace

using Fenrir::App;
using Fenrir::Scene;

TEST_CASE("Creating scenes leaves other scenes and their entities where they were", "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors));
    Scene& first = app.CreateScene("First");
    Fenrir::Entity entity = first.GetEntityList().CreateEntity();
    entity.AddComponent<Health>(Health{42});

    for (int i = 0; i < 100; ++i)
    {
        app.CreateScene("Scene" + std::to_string(i));
    }

    INFO("creating more scenes moved the first scene");
    REQUIRE(&app.GetScene("First") == &first);
    INFO("the first scene lost its name when more scenes were created");
    REQUIRE(first.GetName() == "First");
    INFO("the first scene's entity lost its component when more scenes were created");
    REQUIRE(entity.HasComponent<Health>());
    INFO("the first scene's entity's component changed when more scenes were created");
    REQUIRE(entity.GetComponent<Health>().value == 42);
    INFO("creating scenes logged an error");
    REQUIRE(errors->empty());
}

TEST_CASE("Destroying a scene before the active one keeps the same scene active", "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors));
    app.CreateScene("First");
    app.CreateScene("Second");
    Scene& third = app.CreateScene("Third");
    app.ChangeActiveScene("Third");

    app.DestroyScene("First");

    INFO("destroying an earlier scene changed which scene object is active");
    REQUIRE(&app.GetActiveScene() == &third);
    INFO("destroying an earlier scene made another scene active");
    REQUIRE(app.GetActiveScene().GetName() == "Third");
    INFO("destroying an inactive scene logged an error");
    REQUIRE(errors->empty());
}

TEST_CASE("Destroying the active scene is refused with a logged error", "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors));
    Scene& level = app.CreateScene("Level");
    Fenrir::Entity entity = level.GetEntityList().CreateEntity();
    entity.AddComponent<Health>(Health{7});
    app.ChangeActiveScene("Level");

    app.DestroyScene("Level");

    INFO("the active scene stopped being active when destroying it was refused");
    REQUIRE(&app.GetActiveScene() == &level);
    INFO("the active scene was removed although destroying it was refused");
    REQUIRE(&app.GetScene("Level") == &level);
    INFO("the active scene's entity changed although destroying it was refused");
    REQUIRE(entity.GetComponent<Health>().value == 7);
    INFO("refusing to destroy the active scene did not log exactly one error");
    REQUIRE(errors->size() == 1);
}

namespace
{
    std::vector<std::string> SceneNames(App& app)
    {
        std::vector<std::string> names;
        app.ForEachScene([&names](Scene& scene) { names.push_back(scene.GetName()); });
        return names;
    }
} // namespace

TEST_CASE("Scene requests made during a frame take effect after its last phase, in the order made", "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors));
    std::vector<std::string> seenAtRender;
    std::string activeAtRender;
    bool requested = false;
    app.AddSequentialSystem(Fenrir::SchedulePriority::Update, [&requested](App& running) {
        if (requested)
            return;
        requested = true;
        // Activating before creating fails at its turn: the requests are applied in the order made.
        running.RequestChangeActiveScene("Title");
        running.RequestCreateScene("Title");
        running.RequestCreateScene("Level");
        running.RequestChangeActiveScene("Level");
    });
    app.AddSequentialSystem(Fenrir::SchedulePriority::Render, [&](App& running) {
        if (seenAtRender.empty())
        {
            seenAtRender = SceneNames(running);
            activeAtRender = running.GetActiveScene().GetName();
        }
    });

    app.RunFrame(0.0);

    INFO("a create request took effect before the frame's last phase");
    REQUIRE(seenAtRender == std::vector<std::string>{"Default"});
    INFO("an activate request took effect before the frame's last phase");
    REQUIRE(activeAtRender == "Default");
    INFO("the requested scenes were not made at the end of the frame, in the order asked");
    REQUIRE(SceneNames(app) == std::vector<std::string>{"Default", "Title", "Level"});
    INFO("the last activate request did not leave its scene active");
    REQUIRE(app.GetActiveScene().GetName() == "Level");
    INFO("activating a scene before the request that creates it did not log exactly one error");
    REQUIRE(errors->size() == 1);
}

TEST_CASE("A restart made of create, activate and destroy in one frame leaves only the new scene, active",
          "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors));
    app.CreateScene("Level");
    app.ChangeActiveScene("Level");
    app.DestroyScene("Default");

    app.RequestCreateScene("Level Again");
    app.RequestChangeActiveScene("Level Again");
    app.RequestDestroyScene("Level");
    app.RunFrame(0.0);

    INFO("the restart did not leave only the new scene");
    REQUIRE(SceneNames(app) == std::vector<std::string>{"Level Again"});
    INFO("the restart did not leave the new scene active");
    REQUIRE(app.GetActiveScene().GetName() == "Level Again");
    INFO("the restart logged an error");
    REQUIRE(errors->empty());
}

TEST_CASE("A request to destroy the active scene is refused with a logged error at its turn", "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors));
    Scene& level = app.CreateScene("Level");
    Fenrir::Entity entity = level.GetEntityList().CreateEntity();
    entity.AddComponent<Health>(Health{7});
    app.ChangeActiveScene("Level");

    app.RequestDestroyScene("Level");
    app.RunFrame(0.0);

    INFO("the active scene stopped being active when the request to destroy it was refused");
    REQUIRE(&app.GetActiveScene() == &level);
    INFO("the active scene's entity changed although the request to destroy it was refused");
    REQUIRE(entity.GetComponent<Health>().value == 7);
    INFO("refusing the request to destroy the active scene did not log exactly one error");
    REQUIRE(errors->size() == 1);
}

TEST_CASE("Scene requests naming a scene that exists to create, or one that does not to activate or destroy, are "
          "logged errors",
          "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors));
    Scene& level = app.CreateScene("Level");

    app.RequestCreateScene("Level");
    app.RequestChangeActiveScene("Missing");
    app.RequestDestroyScene("Missing");
    app.RunFrame(0.0);

    INFO("a request to create a scene that exists made a second scene of that name");
    REQUIRE(SceneNames(app) == std::vector<std::string>{"Default", "Level"});
    INFO("a request to create a scene that exists replaced it");
    REQUIRE(&app.GetScene("Level") == &level);
    INFO("a request to activate a scene that does not exist changed the active scene");
    REQUIRE(app.GetActiveScene().GetName() == "Default");
    INFO("the three bad requests did not log exactly one error each");
    REQUIRE(errors->size() == 3);
}

TEST_CASE("The immediate scene calls take effect at once while scene requests wait for the frame's end",
          "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors));
    std::vector<std::string> seenInPostUpdate;
    app.AddSequentialSystem(Fenrir::SchedulePriority::Update, [](App& running) {
        running.CreateScene("Now");
        running.RequestCreateScene("Later");
    });
    app.AddSequentialSystem(Fenrir::SchedulePriority::PostUpdate,
                            [&seenInPostUpdate](App& running) { seenInPostUpdate = SceneNames(running); });

    app.RunFrame(0.0);

    INFO("the immediate create did not take effect at once, or the request did not wait");
    REQUIRE(seenInPostUpdate == std::vector<std::string>{"Default", "Now"});
    INFO("the request did not take effect at the frame's end");
    REQUIRE(SceneNames(app) == std::vector<std::string>{"Default", "Now", "Later"});
    INFO("the immediate and requested creates logged an error");
    REQUIRE(errors->empty());
}

TEST_CASE("Scene requests from systems run in parallel take effect in the order the systems were registered",
          "[app][scenes]")
{
    auto errors = std::make_shared<std::vector<std::string>>();
    App app(std::make_unique<RecordingLogger>(errors), 2);
    app.AddSystems(Fenrir::SchedulePriority::Update, {[](App& running) {
                                                          // Late, so without the hold its request would come second.
                                                          std::this_thread::sleep_for(std::chrono::milliseconds(50));
                                                          running.RequestCreateScene("First");
                                                      },
                                                      [](App& running) { running.RequestCreateScene("Second"); }});

    app.RunFrame(0.0);

    INFO("parallel systems' requests did not take effect in the order the systems were registered");
    REQUIRE(SceneNames(app) == std::vector<std::string>{"Default", "First", "Second"});
    INFO("the parallel systems' requests logged an error");
    REQUIRE(errors->empty());
}
