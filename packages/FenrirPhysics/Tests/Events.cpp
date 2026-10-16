// Contact and trigger events through the frame seam, read as game code reads them: through EventReader<T>.

#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Events.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    template <typename TEvent>
    struct Recorded
    {
        EventReader<TEvent> reader;
        std::vector<TEvent> events;

        void Collect(const App& app)
        {
            for (const TEvent& event : reader.Read(app))
            {
                events.push_back(event);
            }
        }
    };

    /** Every physics event sent while it runs frames, read after each frame as a system would. */
    struct EventLog
    {
        Recorded<ContactBegin> begins;
        Recorded<ContactEnd> ends;
        Recorded<TriggerEnter> enters;
        Recorded<TriggerExit> exits;

        void Run(PhysicsApp& test, double seconds)
        {
            const int frames = static_cast<int>(seconds / tick + 0.5);
            for (int i = 0; i < frames; ++i)
            {
                test.app.RunFrame(tick);
                begins.Collect(test.app);
                ends.Collect(test.app);
                enters.Collect(test.app);
                exits.Collect(test.app);
            }
        }
    };
} // namespace

TEST_CASE("A box with ContactEvents landing on the ground sends one ContactBegin", "[physics][events]")
{
    PhysicsApp test;
    EventLog log;
    Entity ground = AddGround(test.Entities());
    Entity box = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f));
    box.AddComponent<ContactEvents>();

    log.Run(test, 2.0);

    INFO("the box lands once and stays, so one contact begins, got " << log.begins.events.size());
    REQUIRE(log.begins.events.size() == 1);
    const ContactBegin& begin = log.begins.events[0];
    INFO("A is the ground (the lower entity id) and B the box, as body and as collider");
    REQUIRE(begin.bodyA == ground);
    REQUIRE(begin.bodyB == box);
    REQUIRE(begin.colliderA == ground);
    REQUIRE(begin.colliderB == box);
    INFO("the normal points from the ground up to the box, it is (" << begin.normal.x << ", " << begin.normal.y << ", "
                                                                    << begin.normal.z << ")");
    REQUIRE(begin.normal.y == Approx(1.0f).margin(1e-3));
    INFO("the box falls 1.5 m before it touches, closing at about sqrt(2 x 9.81 x 1.5) = 5.4 m/s, got "
         << begin.relativeSpeed);
    REQUIRE(begin.relativeSpeed == Approx(5.4f).margin(0.5f));
    INFO("it touches on the ground's top face (y = 0) under the box, at y = " << begin.point.y);
    REQUIRE(begin.point.y == Approx(0.0f).margin(0.05f));
    REQUIRE(std::abs(begin.point.x) <= 0.5f + 1e-3f);
    INFO("nothing ended while the box rests");
    REQUIRE(log.ends.events.empty());
}
