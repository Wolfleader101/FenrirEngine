#include <chrono>
#include <cstddef>
#include <cstring>
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

    struct Ping
    {
        int value = 0;
    };

    using Reads = std::vector<std::vector<int>>;

    // A system that reads Pings through its own reader and records the values each run saw.
    Fenrir::SystemFunc RecordingReader(Reads& reads)
    {
        return [&reads, reader = Fenrir::EventReader<Ping>{}](Fenrir::App& app) mutable {
            std::vector<int>& run = reads.emplace_back();
            for (const Ping& ping : reader.Read(app))
            {
                run.push_back(ping.value);
            }
        };
    }
} // namespace

using Fenrir::App;
using Fenrir::SchedulePriority;

TEST_CASE("An Update reader sees each event once, and one sent after it ran the next frame", "[app][events]")
{
    App app(Silent());
    int frame = 0;
    Reads reads;
    app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App& running) {
                                 if (++frame == 1)
                                 {
                                     running.SendEvent(Ping{1});
                                 }
                             }});
    app.AddSequentialSystems(SchedulePriority::Update, {RecordingReader(reads)});
    app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App& running) {
                                 if (frame == 1)
                                 {
                                     running.SendEvent(Ping{2});
                                 }
                             }});

    for (int run = 0; run < 4; ++run)
    {
        app.RunFrame(sixtieth);
    }

    INFO("the reader should see the early send in frame 1 and the late send in frame 2, each once");
    REQUIRE(reads == Reads{{1}, {2}, {}, {}});
}

TEST_CASE("A Tick reader that runs three times in a frame sees each event on its first run after the send",
          "[app][events]")
{
    App app(Silent());
    int ticks = 0;
    Reads reads;
    app.AddSequentialSystems(SchedulePriority::PreUpdate, {[&](App& running) {
                                 if (ticks == 0)
                                 {
                                     running.SendEvent(Ping{1});
                                 }
                             }});
    app.AddSequentialSystems(SchedulePriority::Tick, {RecordingReader(reads), [&](App& running) {
                                                          if (++ticks == 1)
                                                          {
                                                              running.SendEvent(Ping{2});
                                                          }
                                                      }});

    // A little over three ticks a frame, so floating-point rounding cannot drop the third.
    app.RunFrame(3.5 * sixtieth);
    app.RunFrame(3.0 * sixtieth);

    INFO("the reader should see the frame's send on tick 1, the first tick's send on tick 2, and nothing again");
    REQUIRE(reads == Reads{{1}, {2}, {}, {}, {}, {}});
}

TEST_CASE("A Tick reader sees an event sent in frames that ran no tick", "[app][events]")
{
    App app(Silent());
    int frame = 0;
    Reads reads;
    app.AddSequentialSystems(SchedulePriority::Update, {[&](App& running) {
                                 if (++frame == 1)
                                 {
                                     running.SendEvent(Ping{1});
                                 }
                             }});
    app.AddSequentialSystems(SchedulePriority::Tick, {RecordingReader(reads)});

    // Two frames with no tick, then a frame whose delta completes one.
    app.RunFrame(0.5 * sixtieth);
    app.RunFrame(0.4 * sixtieth);
    app.RunFrame(0.5 * sixtieth);

    INFO("the event sent before two tickless frames should reach the first tick after them");
    REQUIRE(reads == Reads{{1}});
}

TEST_CASE("A reader made after its queue has swapped starts at the oldest event held and has missed nothing",
          "[app][events]")
{
    App app(Silent());
    int frame = 0;
    std::vector<int> values;
    std::vector<std::size_t> missed;
    app.AddSequentialSystems(SchedulePriority::Update, {[&](App& running) { running.SendEvent(Ping{++frame}); }});
    app.AddSequentialSystems(SchedulePriority::Update, {[&](App& running) {
                                 if (frame == 4)
                                 {
                                     Fenrir::EventReader<Ping> reader;
                                     const auto read = reader.Read(running);
                                     missed.push_back(read.Missed());
                                     for (const Ping& ping : read)
                                     {
                                         values.push_back(ping.value);
                                     }
                                 }
                             }});

    // One tick a frame, so the queue swaps three times before the reader exists.
    for (int run = 0; run < 4; ++run)
    {
        app.RunFrame(sixtieth);
    }

    INFO("a new reader should yield the events the queue still holds (frames 3 and 4) and report no misses");
    REQUIRE(values == std::vector<int>{3, 4});
    REQUIRE(missed == std::vector<std::size_t>{0});
}

TEST_CASE("A reader that goes two swaps without reading reports how many events it missed", "[app][events]")
{
    App app(Silent());
    int frame = 0;
    std::vector<int> values;
    std::vector<std::size_t> missed;
    app.AddSequentialSystems(SchedulePriority::Update, {[&](App& running) {
                                 ++frame;
                                 if (frame == 2)
                                 {
                                     running.SendEvent(Ping{2});
                                     running.SendEvent(Ping{3});
                                 }
                                 if (frame == 3)
                                 {
                                     running.SendEvent(Ping{4});
                                 }
                             }});
    app.AddSequentialSystems(SchedulePriority::Update,
                             {[&, reader = Fenrir::EventReader<Ping>{}](App& running) mutable {
                                 if (frame == 1 || frame == 4)
                                 {
                                     const auto read = reader.Read(running);
                                     missed.push_back(read.Missed());
                                     for (const Ping& ping : read)
                                     {
                                         values.push_back(ping.value);
                                     }
                                 }
                             }});

    // One tick a frame, so the queue swaps at the end of each.
    for (int run = 0; run < 4; ++run)
    {
        app.RunFrame(sixtieth);
    }

    INFO("the reader should miss frame 2's two events, dropped after two swaps, and still see frame 3's");
    REQUIRE(values == std::vector<int>{4});
    REQUIRE(missed == std::vector<std::size_t>{0, 2});
}

namespace
{
    // A system that sends three Pings numbered from `first`. With `slow`, it first waits a little, so it would finish
    // after a quick system it runs beside if nothing put their sends in order.
    Fenrir::SystemFunc SendsThree(int first, bool slow)
    {
        return [first, slow](App& app) {
            if (slow)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            for (int offset = 0; offset < 3; ++offset)
            {
                app.SendEvent(Ping{first + offset});
            }
        };
    }
} // namespace

TEST_CASE("Events sent by systems running in parallel arrive in the order the systems were registered, every run",
          "[app][events]")
{
    App app(Silent());
    Reads reads;
    app.AddSystems(SchedulePriority::Update, {SendsThree(10, true), SendsThree(20, false)});
    app.AddSequentialSystems(SchedulePriority::PostUpdate, {RecordingReader(reads)});

    constexpr int frames = 50;
    for (int run = 0; run < frames; ++run)
    {
        app.RunFrame(sixtieth);
    }

    INFO("each frame's sends should arrive first system first, each system's in the order it sent them");
    REQUIRE(reads == Reads(frames, {10, 11, 12, 20, 21, 22}));
}

TEST_CASE("Readers and senders running in parallel each see every event once, from the queue's first use",
          "[app][events]")
{
    // Several fresh apps, so the queue is first looked up by parallel systems each time.
    constexpr int apps = 20;
    constexpr int frames = 20;
    constexpr int readers = 4;
    for (int attempt = 0; attempt < apps; ++attempt)
    {
        App app(Silent());
        std::vector<Reads> reads(readers);
        app.AddSystems(SchedulePriority::Update,
                       {SendsThree(10, false), RecordingReader(reads[0]), RecordingReader(reads[1]),
                        SendsThree(20, false), RecordingReader(reads[2]), RecordingReader(reads[3])});

        for (int run = 0; run < frames; ++run)
        {
            app.RunFrame(sixtieth);
        }

        // A batch's sends arrive after it, so each frame's readers see the previous frame's sends.
        Reads expected(frames, {10, 11, 12, 20, 21, 22});
        expected.front().clear();
        for (const Reads& read : reads)
        {
            INFO("a parallel reader should see each frame's parallel sends once, in registration order, the next "
                 "frame");
            REQUIRE(read == expected);
        }
    }
}

namespace
{
    // SendsThree, by name and as bytes: what scripting does.
    Fenrir::SystemFunc SendsThreeByName(std::string name, int first, bool slow)
    {
        return [name = std::move(name), first, slow](App& app) {
            if (slow)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            for (int offset = 0; offset < 3; ++offset)
            {
                const int value = first + offset;
                (void)app.FindEventQueue(name)->SendRaw(&value);
            }
        };
    }

    // RecordingReader, by name and as bytes, with a cursor of its own.
    Fenrir::SystemFunc RecordingRawReader(std::string name, Reads& reads)
    {
        return [name = std::move(name), &reads, seen = Fenrir::NewReaderCursor](App& app) mutable {
            std::vector<int>& run = reads.emplace_back();
            const Fenrir::RawEventRange events = app.FindEventQueue(name)->ReadRawAfter(seen);
            for (const auto& [data, count] :
                 {std::pair{events.older, events.olderCount}, std::pair{events.newer, events.newerCount}})
            {
                for (std::size_t index = 0; index < count; ++index)
                {
                    int value = 0;
                    std::memcpy(&value, static_cast<const std::byte*>(data) + index * sizeof(int), sizeof(int));
                    run.push_back(value);
                }
            }
        };
    }
} // namespace

TEST_CASE("Sends and reads by name, as bytes, follow the rules typed ones do", "[app][events]")
{
    // A named typed queue and a queue with no C++ type: both are reached only by name here.
    for (const bool typed : {true, false})
    {
        App app(Silent());
        const std::string name = "Ping";
        if (typed)
        {
            REQUIRE(app.NameEvent<Ping>(name));
        }
        else
        {
            REQUIRE(app.CreateEventQueue(name, sizeof(int), alignof(int)) != nullptr);
        }
        std::vector<Reads> reads(2);
        app.AddSystems(SchedulePriority::Update,
                       {SendsThreeByName(name, 10, true), RecordingRawReader(name, reads[0]),
                        RecordingRawReader(name, reads[1]), SendsThreeByName(name, 20, false)});

        constexpr int frames = 20;
        for (int run = 0; run < frames; ++run)
        {
            app.RunFrame(sixtieth);
        }

        Reads expected(frames, {10, 11, 12, 20, 21, 22});
        expected.front().clear();
        for (const Reads& read : reads)
        {
            INFO((typed ? "typed queue: " : "byte queue: ")
                 << "parallel sends by name should arrive after the batch in registration order, and each raw reader "
                    "should see them once");
            REQUIRE(read == expected);
        }
    }
}
