// Events between C++ and C#: a C++ event read in C#, a C# event read in C++, a C#-only event, each reader seeing
// each event once in both languages, a sender never seeing its own sends, lookup failures, and C++'s event users
// unchanged by named and byte queues.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"
#include "ScriptingProbe.hpp"
#include "TestExtension.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "FenrirApp/App.hpp"
#include "FenrirScripting/Scripting.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;

    ScriptingConfig ConfigWithAlert()
    {
        return ConfigFor(fixture, {}, {MakeEvent<TestNative::Alert>("Test.Alert")});
    }

    void ACppEventIsReadInCSharp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigWithAlert());
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        {
            INFO("There were events before any was sent");
            REQUIRE(EventScript<std::optional<std::string>>(scripting, "ReadAlerts").value_or("?").empty());
        }

        headless.Get().SendEvent(TestNative::Alert{5, 0.5F});
        headless.Get().SendEvent(TestNative::Alert{6, 1.0F});
        {
            INFO("C# did not read the events C++ sent");
            REQUIRE(EventScript<std::optional<std::string>>(scripting, "ReadAlerts").value_or("?") == "5,6");
        }
    }

    void ACSharpReaderMadeAfterSwapsStartsAtTheOldestEventHeld()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigWithAlert());
        // One Alert a frame, each frame ticking, so the queue swaps three times and drops Alerts 1 and 2.
        for (std::int32_t code = 1; code <= 3; ++code)
        {
            headless.Get().SendEvent(TestNative::Alert{code, 0.0F});
            headless.Get().RunFrame(HeadlessApp::FrameDelta);
        }
        headless.Get().SendEvent(TestNative::Alert{4, 0.0F});
        {
            INFO("A new C# reader did not start at the oldest Alert held, or counted ones sent before it as missed");
            REQUIRE(EventScript<std::optional<std::string>>(scripting, "ReadAlertsWithNewReader").value_or("?") ==
                    "3,4;missed=0");
        }
        headless.Get().RunExit();
    }

    void ACSharpEventIsReadInCpp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigWithAlert());
        EventReader<TestNative::Alert> alerts;
        EventScript<void>(scripting, "SendAlert", std::int32_t{9}, 2.5F);
        // Held until the script returns: nothing has reached the queue yet.
        {
            INFO("A send reached the queue before the system returned");
            REQUIRE(alerts.Read(headless.Get()).empty());
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        const auto events = alerts.Read(headless.Get());
        {
            INFO("C++ did not read the event C# sent");
            REQUIRE((events.size() == 1 && events.begin()->Code == 9 && events.begin()->Level == 2.5F));
        }
    }

    void ACSharpOnlyEventNeedsNoCpp()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The fixture did not load");
            REQUIRE(scripting.HasScripts());
        }
        EventScript<void>(scripting, "SendPing", std::int32_t{3});
        EventScript<void>(scripting, "SendPing", std::int32_t{4});
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("A C#-only event was not delivered");
            REQUIRE(EventScript<std::int32_t>(scripting, "SumPings") == 7);
        }

        // Nothing in C++ knows its type, but the queue is there by name, with the layout C# reported.
        const IEventQueue* queue = headless.Get().FindEventQueue("Fenrir.ScriptFixture.Ping");
        std::size_t seen = NewReaderCursor;
        {
            INFO("The C#-only event's queue was not there by name");
            REQUIRE((queue != nullptr && queue->EventSize() == 4 && queue->ReadRawAfter(seen).Count() == 2));
        }

        // The queue goes with the script assembly.
        scripting.Shutdown();
        {
            INFO("The queue outlived the script assembly");
            REQUIRE(headless.Get().FindEventQueue("Fenrir.ScriptFixture.Ping") == nullptr);
        }
        auto again = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("The script assembly did not load again on the same App");
            REQUIRE(again.HasScripts());
        }
        // The reloaded assembly sends and reads through a fresh queue (ids cached for the old registry are not reused).
        EventScript<void>(again, "SendPing", std::int32_t{5});
        (void)ScriptingProbe::ApplyChanges(again);
        {
            INFO("A reloaded script assembly could not use its events");
            REQUIRE(EventScript<std::int32_t>(again, "SumPings") == 5);
        }
    }

    void EachReaderSeesEachEventOnceInBothLanguages()
    {
        HeadlessApp headless;
        App& app = headless.Get();
        int frame = 0;
        // C++ sends an Alert in Update of the first frame, before C#'s Update systems run, and another in PostUpdate of
        // the second frame, after they ran (a late send).
        app.AddSequentialSystems(SchedulePriority::Update, {[&](App& running) {
                                     if (++frame == 1)
                                     {
                                         running.SendEvent(TestNative::Alert{1, 0});
                                     }
                                 }});
        app.AddSequentialSystems(SchedulePriority::PostUpdate, {[&](App& running) {
                                     if (frame == 2)
                                     {
                                         running.SendEvent(TestNative::Alert{2, 0});
                                     }
                                 }});
        Scripting* installed = nullptr;
        std::string dump;
        app.AddSequentialSystems(
            SchedulePriority::Exit, {[&](App&) {
                dump = CallIn<std::optional<std::string>>(EventProbeType, *installed, "Dump").value_or("");
            }});
        auto scripting = Scripting::Install(app, ConfigWithAlert());
        installed = &scripting;
        // After all of C#'s Update systems, with readers of its own: the Alert by type, and the C#-only Ping by name,
        // as bytes, with no C++ type.
        std::vector<std::size_t> nativeAlerts;
        std::vector<std::size_t> nativePings;
        app.AddSequentialSystems(
            SchedulePriority::Update,
            {[&, alerts = EventReader<TestNative::Alert>{}, pingsSeen = NewReaderCursor](App& running) mutable {
                nativeAlerts.push_back(alerts.Read(running).size());
                const IEventQueue* pings = running.FindEventQueue("Fenrir.ScriptFixture.Ping");
                nativePings.push_back(pings == nullptr ? 99 : pings->ReadRawAfter(pingsSeen).Count());
            }});
        CallIn<void>(EventProbeType, scripting, "Enable");

        // One tick, no tick, three ticks, one tick.
        constexpr double tick = 1.0 / 60.0;
        for (const double delta : {tick, 0.0, 3.5 * tick, tick})
        {
            app.RunFrame(delta);
        }
        app.RunExit();

        // Frame 1: the Ping is sent by the sender (which does not see its own send until it returns) and read by the
        // system after it, and the Alert is read once (the Tick reader ran before it was sent). Frame 2 (no tick): the
        // sender sees its own Ping, once; nothing else is new. Frame 3: the Tick reader sees both Alerts on its first
        // run and none on the next two; the late Alert reaches the Update reader once. Frame 4: nothing is seen again.
        const std::string expected = "Tick=0,Own=0,Later=1,Alerts=1,"
                                     "Own=1,Later=0,Alerts=0,"
                                     "Tick=2,Tick=0,Tick=0,Own=0,Later=0,Alerts=1,"
                                     "Tick=0,Own=0,Later=0,Alerts=0";
        {
            INFO("C# readers did not see each event once: " + dump);
            REQUIRE(dump == expected);
        }
        // C++ readers of the same queues are independent of the C# ones and see each event once too.
        {
            INFO("A C++ reader did not see each Alert once");
            REQUIRE((nativeAlerts == std::vector<std::size_t>{1, 0, 1, 0}));
        }
        {
            INFO("A C++ reader of the C#-only Ping did not see it once");
            REQUIRE((nativePings == std::vector<std::size_t>{1, 0, 0, 0}));
        }
    }

    void ASystemNeverSeesItsOwnSendsAndAViewStaysValid()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
        {
            INFO("A send was readable by its own sender");
            REQUIRE(EventScript<std::int32_t>(scripting, "SendThenCount", std::int32_t{1}) == 0);
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("A send was visible before its system returned, or the earlier one was lost");
            REQUIRE(EventScript<std::int32_t>(scripting, "SendThenCount", std::int32_t{2}) == 1);
        }
        (void)ScriptingProbe::ApplyChanges(scripting);
        {
            INFO("Both sends did not arrive");
            REQUIRE(EventScript<std::int32_t>(scripting, "SumPings") == 3);
        }

        // A hundred sends in one system do not move or change the view the system already holds.
        {
            INFO("A view of events changed while its system sent more");
            REQUIRE(EventScript<bool>(scripting, "ViewStaysValid"));
        }
        {
            INFO("The sends made in that system were not delivered together");
            REQUIRE(ScriptingProbe::ApplyChanges(scripting).applied == 100);
        }
    }

    void EventLookupFailuresSayWhatWentWrong()
    {
        HeadlessApp headless;
        auto scripting = Scripting::Install(headless.Get(), ConfigWithAlert());
        const auto orphan = EventScript<std::optional<std::string>>(scripting, "TryReadOrphan").value_or("");
        {
            INFO("An unknown event said: " + orphan);
            REQUIRE(orphan.find("no event registered as 'Test.NoSuchEvent'") != std::string::npos);
        }
        const auto fake = EventScript<std::optional<std::string>>(scripting, "TryReadFake").value_or("");
        {
            INFO("A script claiming a native event said: " + fake);
            REQUIRE(fake.find("different C# type") != std::string::npos);
        }
    }

    void CppEventUsersAreUnchangedByNamedAndByteQueues()
    {
        struct Plain
        {
            int value;
        };
        struct Other
        {
            int value;
        };
        struct Heavy
        {
            std::string text;
        };
        HeadlessApp headless;
        App& app = headless.Get();

        app.SendEvent(Plain{1});
        app.SendEvent(Plain{2});
        {
            INFO("Typed events changed");
            REQUIRE(HeldEvents<Plain>(app).size() == 2);
        }
        {
            INFO("A queue could not be named, or naming twice failed");
            REQUIRE((app.NameEvent<Plain>("Plain") && app.NameEvent<Plain>("Plain")));
        }
        {
            INFO("A name was given to two different queues");
            REQUIRE(!app.NameEvent<Other>("Plain"));
        }
        const IEventQueue* named = app.FindEventQueue("Plain");
        {
            INFO("The named queue is not the typed queue");
            REQUIRE((named != nullptr && named->EventSize() == sizeof(Plain) && HeldEventCount(*named) == 2));
        }
        const int third = 3;
        {
            INFO("Sending by name did not reach the typed queue");
            REQUIRE((app.FindEventQueue("Plain")->SendRaw(&third) && HeldEvents<Plain>(app).size() == 3 &&
                     HeldEvents<Plain>(app)[2].value == 3));
        }

        // An event that is not plain bytes still works as before and cannot be used by name.
        app.SendEvent(Heavy{"a string"});
        {
            INFO("A non-trivial event changed");
            REQUIRE((HeldEvents<Heavy>(app).size() == 1 && HeldEvents<Heavy>(app)[0].text == "a string"));
        }
        {
            INFO("A non-trivial event was usable as bytes");
            REQUIRE((app.NameEvent<Heavy>("Heavy") && app.FindEventQueue("Heavy")->EventSize() == 0 &&
                     !app.FindEventQueue("Heavy")->SendRaw(&third)));
        }

        // A byte queue: the same two buffers, and it rotates with the frame.
        IEventQueue* bytes = app.CreateEventQueue("Bytes", 8, 4);
        {
            INFO("A byte queue could be made twice, or over a typed one");
            REQUIRE((bytes != nullptr && app.CreateEventQueue("Bytes", 8, 4) == nullptr &&
                     app.CreateEventQueue("Plain", 8, 4) == nullptr));
        }
        {
            INFO("An unusable byte queue was made");
            REQUIRE((app.CreateEventQueue("Odd", 6, 4) == nullptr && app.CreateEventQueue("Wide", 32, 32) == nullptr &&
                     app.CreateEventQueue("", 4, 4) == nullptr));
        }
        const std::int64_t value = 42;
        {
            INFO("A byte queue did not hold its event");
            REQUIRE((bytes->SendRaw(&value) && HeldEventCount(*bytes) == 1));
        }
        {
            INFO("Removing a byte queue went wrong, or a typed queue could be removed");
            REQUIRE((app.RemoveEventQueue("Bytes") && app.FindEventQueue("Bytes") == nullptr &&
                     !app.RemoveEventQueue("Bytes") && !app.RemoveEventQueue("Plain")));
        }
    }

} // namespace

TEST_CASE("a C++ event is read in C#", "[scripting]")
{
    ACppEventIsReadInCSharp();
}

TEST_CASE("a C# reader made after its queue swapped starts at the oldest event held", "[scripting]")
{
    ACSharpReaderMadeAfterSwapsStartsAtTheOldestEventHeld();
}

TEST_CASE("a C# event is read in C++", "[scripting]")
{
    ACSharpEventIsReadInCpp();
}

TEST_CASE("a C#-only event needs no C++", "[scripting]")
{
    ACSharpOnlyEventNeedsNoCpp();
}

TEST_CASE("each reader sees each event once in both languages", "[scripting]")
{
    EachReaderSeesEachEventOnceInBothLanguages();
}

TEST_CASE("a system never sees its own sends and a view stays valid", "[scripting]")
{
    ASystemNeverSeesItsOwnSendsAndAViewStaysValid();
}

TEST_CASE("event lookup failures say what went wrong", "[scripting]")
{
    EventLookupFailuresSayWhatWentWrong();
}

TEST_CASE("C++ event users are unchanged by named and byte queues", "[scripting]")
{
    CppEventUsersAreUnchangedByNamedAndByteQueues();
}
