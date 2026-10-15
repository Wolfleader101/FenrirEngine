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

    CHECK(&app.GetScene("First") == &first);
    CHECK(first.GetName() == "First");
    REQUIRE(entity.HasComponent<Health>());
    CHECK(entity.GetComponent<Health>().value == 42);
    CHECK(errors->empty());
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

    CHECK(&app.GetActiveScene() == &third);
    CHECK(app.GetActiveScene().GetName() == "Third");
    CHECK(errors->empty());
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

    CHECK(&app.GetActiveScene() == &level);
    CHECK(&app.GetScene("Level") == &level);
    CHECK(entity.GetComponent<Health>().value == 7);
    CHECK(errors->size() == 1);
}
