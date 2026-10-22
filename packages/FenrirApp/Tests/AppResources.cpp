#include <memory>
#include <string>
#include <string_view>
#include <utility>
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

    // An app resource that writes its name to a shared record when it is destroyed.
    struct Recorder
    {
        Recorder(std::vector<std::string>& record, std::string recordedName)
            : destroyed(&record), name(std::move(recordedName))
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

    struct Counter
    {
        int value = 0;
    };
} // namespace

using Fenrir::App;
using Fenrir::SchedulePriority;

TEST_CASE("An app with no resource of a type gives none for it", "[app][resources]")
{
    App app(Silent());

    INFO("TryGetResource found a resource that was never added");
    REQUIRE(app.TryGetResource<Counter>() == nullptr);
    const App& constApp = app;
    INFO("TryGetResource on a const App found a resource that was never added");
    REQUIRE(constApp.TryGetResource<Counter>() == nullptr);
}

TEST_CASE("An app resource is the same value from every phase, init, frame and exit", "[app][resources]")
{
    App app(Silent());
    Counter& added = app.AddResource<Counter>();
    const SchedulePriority phases[] = {
        SchedulePriority::PreInit,   SchedulePriority::Init,        SchedulePriority::PostInit,
        SchedulePriority::PreUpdate, SchedulePriority::BeforeTicks, SchedulePriority::PreTick,
        SchedulePriority::Tick,      SchedulePriority::PostTick,    SchedulePriority::AfterTicks,
        SchedulePriority::Update,    SchedulePriority::PostUpdate,  SchedulePriority::Render,
        SchedulePriority::Exit,
    };
    int notFound = 0;
    for (const SchedulePriority phase : phases)
    {
        // AddSystem: the init phases run only systems added this way. Each reaches the resource both ways.
        app.AddSystem(phase, [&](App& running) {
            Counter* found = running.TryGetResource<Counter>();
            if (found != &added || &running.GetResource<Counter>() != &added)
            {
                ++notFound;
                return;
            }
            ++found->value;
        });
    }

    // One tick, so the tick phases run once.
    app.RunFrame(sixtieth);
    app.RunExit();

    INFO("a phase did not reach the resource that was added");
    REQUIRE(notFound == 0);
    INFO("not every one of the 13 phases wrote to the one resource");
    REQUIRE(added.value == 13);
}

TEST_CASE("A second AddResource of a type replaces the first and destroys it", "[app][resources]")
{
    std::vector<std::string> destroyed;
    App app(Silent());
    app.AddResource<Recorder>(destroyed, "first");

    Recorder& second = app.AddResource<Recorder>(destroyed, "second");

    INFO("replacing the resource did not destroy the first one, and only it");
    REQUIRE(destroyed == std::vector<std::string>{"first"});
    INFO("the app does not give the second resource");
    REQUIRE(&app.GetResource<Recorder>() == &second);
    REQUIRE(app.GetResource<Recorder>().name == "second");
}

TEST_CASE("RemoveResource destroys the resource of that type and leaves the others", "[app][resources]")
{
    std::vector<std::string> destroyed;
    App app(Silent());
    app.AddResource<Recorder>(destroyed, "recorder");
    app.AddResource<Counter>();

    app.RemoveResource<Recorder>();

    INFO("RemoveResource did not destroy the resource");
    REQUIRE(destroyed == std::vector<std::string>{"recorder"});
    INFO("TryGetResource still found the removed resource");
    REQUIRE(app.TryGetResource<Recorder>() == nullptr);
    INFO("removing one type's resource removed another type's");
    REQUIRE(app.TryGetResource<Counter>() != nullptr);

    app.RemoveResource<Recorder>();
    INFO("removing a resource the app no longer has destroyed something");
    REQUIRE(destroyed.size() == 1);
}

TEST_CASE("App resources are destroyed with the App", "[app][resources]")
{
    std::vector<std::string> destroyed;
    {
        App app(Silent());
        app.AddResource<Recorder>(destroyed, "recorder");
        app.RunFrame(sixtieth);
        app.RunExit();
        INFO("the resource was destroyed before the App was");
        REQUIRE(destroyed.empty());
    }
    INFO("the resource was not destroyed, once, with the App");
    REQUIRE(destroyed == std::vector<std::string>{"recorder"});
}

TEST_CASE("An app resource and a scene resource of the same type are separate values", "[app][resources]")
{
    App app(Silent());
    app.AddResource<Counter>().value = 1;
    app.GetActiveScene().AddResource<Counter>().value = 2;

    INFO("the app's resource was changed by the scene's");
    REQUIRE(app.GetResource<Counter>().value == 1);
    INFO("the scene's resource was changed by the app's");
    REQUIRE(app.GetActiveScene().GetResource<Counter>()->value == 2);
}
