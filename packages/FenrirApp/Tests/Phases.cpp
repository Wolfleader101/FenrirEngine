#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
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

    using Fenrir::SchedulePriority;

    // Every phase a frame runs, then Exit, by name: the init phases are left out (they run only systems added with
    // AddSystem, so they cannot have a sequential system).
    const std::pair<SchedulePriority, const char*> framePhases[] = {
        {SchedulePriority::PreUpdate, "PreUpdate"}, {SchedulePriority::BeforeTicks, "BeforeTicks"},
        {SchedulePriority::PreTick, "PreTick"},     {SchedulePriority::Tick, "Tick"},
        {SchedulePriority::PostTick, "PostTick"},   {SchedulePriority::AfterTicks, "AfterTicks"},
        {SchedulePriority::Update, "Update"},       {SchedulePriority::PostUpdate, "PostUpdate"},
        {SchedulePriority::Render, "Render"},       {SchedulePriority::Exit, "Exit"},
    };
    const std::pair<SchedulePriority, const char*> initPhases[] = {
        {SchedulePriority::PreInit, "PreInit"},
        {SchedulePriority::Init, "Init"},
        {SchedulePriority::PostInit, "PostInit"},
    };
} // namespace

using Fenrir::App;

TEST_CASE("CurrentPhase names the running phase to its parallel and sequential systems alike", "[app][phases]")
{
    App app(Silent(), 2);
    std::mutex mutex;
    std::vector<std::string> mismatches;
    int seen = 0;
    const auto check = [&](SchedulePriority expected, std::string what) {
        return [&, expected, what](App& running) {
            const std::optional<SchedulePriority> phase = running.CurrentPhase();
            const std::lock_guard<std::mutex> lock(mutex);
            ++seen;
            if (phase != expected)
            {
                mismatches.push_back(what);
            }
        };
    };
    for (const auto& [priority, name] : initPhases)
    {
        app.AddSystem(priority, check(priority, std::string(name) + " (init)"));
    }
    for (const auto& [priority, name] : framePhases)
    {
        app.AddSystem(priority, check(priority, std::string(name) + " (parallel)"));
        app.AddSequentialSystem(priority, check(priority, std::string(name) + " (sequential)"));
    }

    // One whole tick, so the tick phases run once.
    app.RunFrame(sixtieth);
    app.RunExit();

    INFO("these systems did not see their own phase as current: " << [&] {
        std::string list;
        for (const std::string& mismatch : mismatches)
        {
            list += mismatch + "; ";
        }
        return list;
    }());
    REQUIRE(mismatches.empty());
    INFO("not every system ran once (3 init, and a parallel and a sequential one in each of 10 other phases)");
    REQUIRE(seen == 23);
}

TEST_CASE("CurrentPhase is empty before the first frame, between frames and after Exit", "[app][phases]")
{
    App app(Silent());

    INFO("a new app reported a current phase");
    REQUIRE_FALSE(app.CurrentPhase().has_value());

    app.RunFrame(sixtieth);
    INFO("an app between frames reported a current phase");
    REQUIRE_FALSE(app.CurrentPhase().has_value());

    app.RunExit();
    INFO("an app after RunExit reported a current phase");
    REQUIRE_FALSE(app.CurrentPhase().has_value());
}

TEST_CASE("frameCount is 0 before the first frame and numbers each frame from 1 in every phase of it",
          "[app][phases][counts]")
{
    App app(Silent());
    std::vector<std::uint64_t> init;
    std::vector<std::uint64_t> inFrame;
    app.AddSystem(SchedulePriority::Init, [&](App& running) { init.push_back(running.GetTime().frameCount); });
    for (const SchedulePriority phase :
         {SchedulePriority::PreUpdate, SchedulePriority::Update, SchedulePriority::Render})
    {
        app.AddSequentialSystem(phase, [&](App& running) { inFrame.push_back(running.GetTime().frameCount); });
    }

    INFO("a new app's frameCount is not 0");
    REQUIRE(app.GetTime().frameCount == 0);

    app.RunFrame(0.5 * sixtieth);
    app.RunFrame(0.5 * sixtieth);

    INFO("frameCount in the init phases, before the first frame, is not 0");
    REQUIRE(init == std::vector<std::uint64_t>{0});
    INFO("PreUpdate, Update and Render did not see frame 1, then frame 2");
    REQUIRE(inFrame == std::vector<std::uint64_t>{1, 1, 1, 2, 2, 2});
    INFO("frameCount after two frames is not 2");
    REQUIRE(app.GetTime().frameCount == 2);
}

TEST_CASE("tickCount numbers each fixed step from 1 through its PreTick, Tick and PostTick", "[app][phases][counts]")
{
    App app(Silent());
    std::vector<std::uint64_t> seen;
    for (const SchedulePriority phase : {SchedulePriority::PreTick, SchedulePriority::Tick, SchedulePriority::PostTick})
    {
        app.AddSequentialSystem(phase, [&](App& running) { seen.push_back(running.GetTime().tickCount); });
    }

    INFO("a new app's tickCount is not 0");
    REQUIRE(app.GetTime().tickCount == 0);

    // A little over two ticks, so floating-point rounding cannot drop the second.
    app.RunFrame(2.5 * sixtieth);

    INFO("the two ticks' PreTick, Tick and PostTick did not each see their own step's number");
    REQUIRE(seen == std::vector<std::uint64_t>{1, 1, 1, 2, 2, 2});
}

TEST_CASE("tickCount keeps the last step's number through a frame with no tick, and grows by each frame's ticks",
          "[app][phases][counts]")
{
    App app(Silent());
    std::vector<std::uint64_t> afterTicks;
    app.AddSequentialSystem(SchedulePriority::AfterTicks,
                            [&](App& running) { afterTicks.push_back(running.GetTime().tickCount); });

    app.RunFrame(1.25 * sixtieth); // one tick; a quarter tick owed
    app.RunFrame(0.5 * sixtieth);  // no tick; three quarters owed
    app.RunFrame(3.0 * sixtieth);  // three ticks; three quarters owed

    INFO("AfterTicks did not see 1 after one tick, still 1 after no tick, then 4 after three more");
    REQUIRE(afterTicks == std::vector<std::uint64_t>{1, 1, 4});
}
