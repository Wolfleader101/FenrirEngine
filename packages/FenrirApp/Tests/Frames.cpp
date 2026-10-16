#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"

namespace
{
    // A logger that prints nothing (a second ConsoleLogger in one process throws), and keeps the warnings for the
    // test when given somewhere to put them.
    class SilentLogger : public Fenrir::ILogger
    {
      public:
        explicit SilentLogger(std::vector<std::string>* warnings = nullptr) : m_warnings(warnings)
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
        void WarnImpl(std::string_view message) override
        {
            if (m_warnings != nullptr)
            {
                m_warnings->emplace_back(message);
            }
        }
        void ErrorImpl(std::string_view) override
        {
        }
        void FatalImpl(std::string_view) override
        {
        }

      private:
        std::vector<std::string>* m_warnings;
    };

    std::unique_ptr<Fenrir::ILogger> Silent()
    {
        return std::make_unique<SilentLogger>();
    }

    std::unique_ptr<Fenrir::ILogger> KeepingWarnings(std::vector<std::string>& warnings)
    {
        return std::make_unique<SilentLogger>(&warnings);
    }

    constexpr double sixtieth = 1.0 / 60.0;
} // namespace

using Fenrir::App;
using Fenrir::SchedulePriority;

TEST_CASE("A frame runs Tick once for each whole tick its delta and what was owed cover", "[app][frames]")
{
    App app(Silent());
    INFO("the default tick rate is not 60 Hz, so the deltas below are not whole ticks");
    REQUIRE(app.GetTime().tickRate == sixtieth);
    int ticks = 0;
    app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) { ++ticks; }});

    app.RunFrame(sixtieth);
    INFO("a frame covering one whole tick did not run Tick exactly once");
    REQUIRE(ticks == 1);

    app.RunFrame(0.5 * sixtieth);
    INFO("a frame covering half a tick ran Tick");
    REQUIRE(ticks == 1);

    app.RunFrame(0.5 * sixtieth);
    INFO("the half tick owed plus this frame's half did not run Tick once");
    REQUIRE(ticks == 2);
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

    INFO("the init phases did not run once, in order, before the first frame's PreUpdate only");
    REQUIRE(phases == std::vector<std::string>{"PreInit", "Init", "PostInit", "PreUpdate", "PreUpdate"});
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

    INFO("Run did not run the init phases, two frames and then Exit, in that order");
    REQUIRE(phases == std::vector<std::string>{"PreInit", "Init", "PostInit", "PreUpdate", "Update", "PostUpdate",
                                               "PreUpdate", "Update", "PostUpdate", "Exit"});
}

TEST_CASE("RunExit runs the Exit phase for an app driven by RunFrame", "[app][frames]")
{
    App app(Silent());
    int exits = 0;
    app.AddSystem(SchedulePriority::Exit, [&](App&) { ++exits; });

    app.RunFrame(sixtieth);
    INFO("a frame ran the Exit phase");
    REQUIRE(exits == 0);

    app.RunExit();
    INFO("RunExit did not run the Exit phase once");
    REQUIRE(exits == 1);
}

namespace
{
    // Records the name of each frame phase as it runs.
    void RecordFramePhases(App& app, std::vector<std::string>& phases)
    {
        const std::pair<SchedulePriority, const char*> framePhases[] = {
            {SchedulePriority::PreUpdate, "PreUpdate"}, {SchedulePriority::BeforeTicks, "BeforeTicks"},
            {SchedulePriority::PreTick, "PreTick"},     {SchedulePriority::Tick, "Tick"},
            {SchedulePriority::PostTick, "PostTick"},   {SchedulePriority::AfterTicks, "AfterTicks"},
            {SchedulePriority::Update, "Update"},       {SchedulePriority::PostUpdate, "PostUpdate"},
        };
        for (const auto& [priority, name] : framePhases)
        {
            app.AddSequentialSystems(priority,
                                     {[&phases, phase = std::string(name)](App&) { phases.push_back(phase); }});
        }
    }
} // namespace

TEST_CASE("A frame with no tick still runs BeforeTicks and AfterTicks once, and no tick phase", "[app][frames]")
{
    App app(Silent());
    std::vector<std::string> phases;
    RecordFramePhases(app, phases);

    app.RunFrame(0.5 * sixtieth);

    INFO("a frame with no tick did not run BeforeTicks and AfterTicks once each with no tick phase between");
    REQUIRE(phases == std::vector<std::string>{"PreUpdate", "BeforeTicks", "AfterTicks", "Update", "PostUpdate"});
}

TEST_CASE("A frame with three ticks runs PreTick, Tick and PostTick together three times, between BeforeTicks and "
          "AfterTicks",
          "[app][frames]")
{
    App app(Silent());
    std::vector<std::string> phases;
    RecordFramePhases(app, phases);

    // A little over three ticks, so floating-point rounding cannot drop the third.
    app.RunFrame(3.5 * sixtieth);

    INFO("three ticks did not each run PreTick, Tick and PostTick together, between BeforeTicks and AfterTicks");
    REQUIRE(phases == std::vector<std::string>{"PreUpdate", "BeforeTicks", "PreTick", "Tick", "PostTick", "PreTick",
                                               "Tick", "PostTick", "PreTick", "Tick", "PostTick", "AfterTicks",
                                               "Update", "PostUpdate"});
}

TEST_CASE("A frame owing more ticks than the cap runs the cap, drops the rest and warns once", "[app][frames]")
{
    std::vector<std::string> warnings;
    App app(KeepingWarnings(warnings));
    INFO("the default cap is not five ticks, so the frame below does not owe more than the cap");
    REQUIRE(app.GetTime().maxTicksPerFrame == 5);
    int ticks = 0;
    app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) { ++ticks; }});

    // Eight and a half ticks owed: five run, and three and a half ticks (0.058 s) are dropped.
    app.RunFrame(8.5 * sixtieth);

    INFO("a frame owing eight and a half ticks did not run the cap of five");
    REQUIRE(ticks == 5);
    INFO("the ticks over the cap were kept as owed rather than dropped");
    REQUIRE(app.GetTime().accumulator == 0.0);
    INFO("dropping ticks did not warn exactly once");
    REQUIRE(warnings.size() == 1);
    INFO("the warning does not say how much time was dropped (0.058 s)");
    REQUIRE(warnings.front().find("0.058") != std::string::npos);

    // Nothing is still owed: the next frame runs only what its own delta covers, and does not warn.
    app.RunFrame(1.5 * sixtieth);
    INFO("the next frame did not run only the one tick its own delta covers");
    REQUIRE(ticks == 6);
    INFO("a frame within the cap warned");
    REQUIRE(warnings.size() == 1);
}

TEST_CASE("A frame owing exactly the cap runs every tick and does not warn", "[app][frames]")
{
    std::vector<std::string> warnings;
    App app(KeepingWarnings(warnings));
    int ticks = 0;
    app.AddSequentialSystems(SchedulePriority::Tick, {[&](App&) { ++ticks; }});

    app.RunFrame(5.5 * sixtieth);

    INFO("a frame owing exactly the cap did not run all five ticks");
    REQUIRE(ticks == 5);
    INFO("a frame owing exactly the cap warned");
    REQUIRE(warnings.empty());
}

TEST_CASE("A frame's delta is the one it was given", "[app][frames]")
{
    App app(Silent());
    double seen = -1.0;
    app.AddSequentialSystems(SchedulePriority::Update, {[&](App& running) { seen = running.GetTime().deltaTime; }});

    app.RunFrame(0.25);

    INFO("Update did not see the delta the frame was given");
    REQUIRE(seen == 0.25);
}

TEST_CASE("The App's worker threads number what it was made with, or one per hardware thread", "[app][workers]")
{
    App two(Silent(), 2);
    INFO("an App made with two worker threads does not have two");
    REQUIRE(two.GetThreadPool().ThreadCount() == 2);

    App one(Silent(), 1);
    INFO("an App made with one worker thread does not have one");
    REQUIRE(one.GetThreadPool().ThreadCount() == 1);

    App byDefault(Silent());
    const std::size_t hardware = std::thread::hardware_concurrency();
    INFO("an App made without a thread count does not have one worker thread per hardware thread");
    REQUIRE(byDefault.GetThreadPool().ThreadCount() == (hardware == 0 ? 1 : hardware));
}
