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
} // namespace

using Fenrir::App;
using Fenrir::SchedulePriority;

TEST_CASE("A frame runs Tick once for each whole tick its delta and what was owed cover", "[app][frames]")
{
    App app(Silent());
    REQUIRE(app.GetTime().tickRate == sixtieth);
    int ticks = 0;
    app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) { ++ticks; }});

    app.RunFrame(sixtieth);
    CHECK(ticks == 1);

    app.RunFrame(0.5 * sixtieth);
    CHECK(ticks == 1);

    app.RunFrame(0.5 * sixtieth);
    CHECK(ticks == 2);
}

TEST_CASE("The first frame runs the init phases once, before the frame; later frames do not", "[app][frames]")
{
    App app(Silent());
    std::vector<std::string> phases;
    const auto record = [&](SchedulePriority priority, std::string name) {
        // AddSystem: the scheduler runs the init phases' systems only when they were added this way.
        app.AddSystem(priority, [&phases, name](App&) { phases.push_back(name); });
    };
    record(SchedulePriority::PreInit, "PreInit");
    record(SchedulePriority::Init, "Init");
    record(SchedulePriority::PostInit, "PostInit");
    record(SchedulePriority::PreUpdate, "PreUpdate");

    app.RunFrame(sixtieth);
    app.RunFrame(sixtieth);

    CHECK(phases == std::vector<std::string>{"PreInit", "Init", "PostInit", "PreUpdate", "PreUpdate"});
}

TEST_CASE("Run runs the init phases, then frames until stopped, then Exit", "[app][frames]")
{
    App app(Silent());
    std::vector<std::string> phases;
    const auto record = [&](SchedulePriority priority, std::string name) {
        app.AddSystem(priority, [&phases, name](App&) { phases.push_back(name); });
    };
    record(SchedulePriority::PreInit, "PreInit");
    record(SchedulePriority::Init, "Init");
    record(SchedulePriority::PostInit, "PostInit");
    record(SchedulePriority::PreUpdate, "PreUpdate");
    record(SchedulePriority::Update, "Update");
    record(SchedulePriority::PostUpdate, "PostUpdate");
    record(SchedulePriority::Exit, "Exit");
    // Ticks are left out: how many run depends on the wall clock.
    int frames = 0;
    app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App& running) {
                                 if (++frames == 2)
                                 {
                                     running.Stop();
                                 }
                             }});

    app.Run();

    CHECK(phases == std::vector<std::string>{"PreInit", "Init", "PostInit", "PreUpdate", "Update", "PostUpdate",
                                             "PreUpdate", "Update", "PostUpdate", "Exit"});
}

TEST_CASE("RunExit runs the Exit phase for an app driven by RunFrame", "[app][frames]")
{
    App app(Silent());
    int exits = 0;
    app.AddSystem(SchedulePriority::Exit, [&](App&) { ++exits; });

    app.RunFrame(sixtieth);
    CHECK(exits == 0);

    app.RunExit();
    CHECK(exits == 1);
}

TEST_CASE("A frame's delta is the one it was given", "[app][frames]")
{
    App app(Silent());
    double seen = -1.0;
    app.AddSequentialSystems(SchedulePriority::Update, {[&](App& running) { seen = running.GetTime().deltaTime; }});

    app.RunFrame(0.25);

    CHECK(seen == 0.25);
}
