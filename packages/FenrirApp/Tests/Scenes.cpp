#include <memory>
#include <string>
#include <string_view>
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
