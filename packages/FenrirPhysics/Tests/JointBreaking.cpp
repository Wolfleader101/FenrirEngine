// Breaking joints through the frame seam: thresholds on Joint, JointBroken read through EventReader<T> after whole
// ticks, as game code reads it.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    const Math::Vec3 hook(0.0f, 10.0f, 0.0f);
    // Jolt's default gravity, which PhysicsConfig keeps.
    constexpr float g = 9.81f;
    constexpr float linkHalf = 0.25f;

    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    Joint& JointOf(const Entity& entity)
    {
        return entity.GetComponent<Joint>();
    }

    /** `count` unit-mass links hanging straight down from `hook`, each joined to the one above by a ball joint on its
     * own joint entity, the top one to the world (bodyB). Joint i carries the weight of the count - i links below it.
     */
    struct Chain
    {
        std::vector<Entity> links;
        std::vector<Entity> joints;
    };

    Chain MakeChain(EntityList& entities, int count)
    {
        Chain chain;
        for (int i = 0; i < count; ++i)
        {
            const Math::Vec3 top = hook - Math::Vec3(0.0f, 2.0f * linkHalf * static_cast<float>(i), 0.0f);
            Entity link =
                AddBody(entities, top - Math::Vec3(0.0f, linkHalf, 0.0f), BoxShape{Math::Vec3(0.1f, linkHalf, 0.1f)});
            const Entity above = i == 0 ? Entity() : chain.links.back();
            Entity joint = entities.CreateEntity();
            const JointFrames frames = FrameFromWorld(link, above, JointFrame{top});
            joint.AddComponent<Joint>(Joint{
                .bodyA = link, .bodyB = above, .frameA = frames.frameA, .frameB = frames.frameB, .kind = BallJoint{}});
            chain.links.push_back(link);
            chain.joints.push_back(joint);
        }
        return chain;
    }

    /** Every JointBroken sent while it runs frames, read after each frame as a system would. */
    struct Breaks
    {
        EventReader<JointBroken> reader;
        std::vector<JointBroken> events;

        void Run(PhysicsApp& test, double seconds)
        {
            const int frames = static_cast<int>(seconds / tick + 0.5);
            for (int i = 0; i < frames; ++i)
            {
                test.app.RunFrame(tick);
                for (const JointBroken& event : reader.Read(test.app))
                {
                    events.push_back(event);
                }
            }
        }
    };

    /** Steps one frame at a time, for up to `seconds`, until a frame sends a JointBroken. Before that, reads
     * `watched`'s force after each frame into `mostBeforeBreak`: the largest load the joint carried without breaking.
     */
    void RunUntilBreak(PhysicsApp& test, Breaks& breaks, const Entity& watched, double seconds, float& mostBeforeBreak)
    {
        const std::size_t before = breaks.events.size();
        const int frames = static_cast<int>(seconds / tick + 0.5);
        for (int i = 0; i < frames && breaks.events.size() == before; ++i)
        {
            breaks.Run(test, tick);
            if (breaks.events.size() == before)
            {
                mostBeforeBreak = std::max(mostBeforeBreak, Math::Magnitude(World(test).GetJointState(watched).force));
            }
        }
    }

    void RunUntilBreak(PhysicsApp& test, Breaks& breaks, const Entity& watched, double seconds)
    {
        float mostBeforeBreak = 0.0f;
        RunUntilBreak(test, breaks, watched, seconds, mostBeforeBreak);
    }
} // namespace

TEST_CASE("The first step over its break force breaks a joint, with one event naming it and its load",
          "[physics][joints][breaking]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 5);
    Breaks breaks;
    breaks.Run(test, 1.0);
    REQUIRE(breaks.events.empty());

    // The top joint holds five links, 49.05 N; the one below it four, 39.2 N.
    JointOf(chain.joints[0]).breakForce = 45.0f;
    float mostBeforeBreak = 0.0f;
    RunUntilBreak(test, breaks, chain.joints[0], 1.0, mostBeforeBreak);

    INFO("the top joint, over 45 N, breaks, and only it");
    REQUIRE(breaks.events.size() == 1);
    const JointBroken& broken = breaks.events[0];
    REQUIRE(broken.joint == chain.joints[0]);
    INFO("no step over 45 N passed without breaking it: the most it carried unbroken was " << mostBeforeBreak);
    REQUIRE(mostBeforeBreak <= 45.0f);
    INFO("it names the top link as bodyA and the world as an empty bodyB");
    REQUIRE(broken.bodyA == chain.links[0]);
    REQUIRE(broken.bodyB == Entity());
    INFO("with the load that broke it, over 45 N, about five links' weight pulling the world down: " << broken.force.y);
    REQUIRE(-broken.force.y > 45.0f);
    REQUIRE(broken.force.y == Approx(-5.0f * g).margin(5.0));
    INFO("a ball joint carries no torque");
    REQUIRE(Math::Magnitude(broken.torque) == Approx(0.0f).margin(1e-3));

    INFO("physics disabled it: the component says so, and it carries nothing now");
    REQUIRE_FALSE(JointOf(chain.joints[0]).enabled);
    REQUIRE(JointOf(chain.joints[1]).enabled);
    REQUIRE(Math::Magnitude(World(test).GetJointState(chain.joints[0]).force) == Approx(0.0f).margin(1e-6));

    breaks.Run(test, 1.0);
    INFO("a broken joint stays broken, with no more events, and the chain falls: its top link is at "
         << chain.links[0].GetComponent<Transform>().pos.y);
    REQUIRE(breaks.events.size() == 1);
    REQUIRE(chain.links[0].GetComponent<Transform>().pos.y < hook.y - 4.0f);
}

TEST_CASE("A joint twisted past its break torque breaks", "[physics][joints][breaking]")
{
    PhysicsApp test;
    // A 1 kg bar held level by a fixed joint to the world at its left end, 0.5 from its centre: about 4.9 N m.
    Entity bar = AddBody(test.Entities(), Math::Vec3(0.5f, 10.0f, 0.0f), BoxShape{Math::Vec3(0.5f, 0.1f, 0.1f)});
    Entity joint = test.Entities().CreateEntity();
    const JointFrames frames = FrameFromWorld(bar, Entity(), JointFrame{Math::Vec3(0.0f, 10.0f, 0.0f)});
    joint.AddComponent<Joint>(Joint{.bodyA = bar, .bodyB = Entity(), .frameA = frames.frameA, .frameB = frames.frameB});
    Breaks breaks;
    breaks.Run(test, 0.5);
    REQUIRE(breaks.events.empty());

    JointOf(joint).breakTorque = 4.0f;
    RunUntilBreak(test, breaks, joint, 1.0);

    INFO("the fixed joint, holding about 4.9 N m against a 4 N m break torque, breaks once");
    REQUIRE(breaks.events.size() == 1);
    INFO("with the torque that broke it: " << Math::Magnitude(breaks.events[0].torque));
    REQUIRE(Math::Magnitude(breaks.events[0].torque) > 4.0f);
    REQUIRE_FALSE(JointOf(joint).enabled);
}

TEST_CASE("Joints breaking in the same step are sent in joint entity order", "[physics][joints][breaking]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 5);
    // Set out of order, each far below what its joint holds.
    for (const std::size_t i : {std::size_t{3}, std::size_t{0}, std::size_t{1}})
    {
        JointOf(chain.joints[i]).breakForce = 1.0f;
    }
    Breaks breaks;
    RunUntilBreak(test, breaks, chain.joints[0], 1.0);

    INFO("the three joints break in one step, sent by joint entity: 0, 1, then 3");
    REQUIRE(breaks.events.size() == 3);
    REQUIRE(breaks.events[0].joint == chain.joints[0]);
    REQUIRE(breaks.events[1].joint == chain.joints[1]);
    REQUIRE(breaks.events[2].joint == chain.joints[3]);
    INFO("a joint between two links names both, the lower link as bodyA");
    REQUIRE(breaks.events[1].bodyA == chain.links[1]);
    REQUIRE(breaks.events[1].bodyB == chain.links[0]);
}

TEST_CASE("An infinite threshold never breaks a joint", "[physics][joints][breaking]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 5);
    Breaks breaks;
    breaks.Run(test, 0.2);
    // A hard kick to the bottom link: the chain whips about.
    World(test).AddImpulse(chain.links[4], Math::Vec3(40.0f, 0.0f, 0.0f));
    float most = 0.0f;
    for (int i = 0; i < 120; ++i)
    {
        breaks.Run(test, tick);
        most = std::max(most, Math::Magnitude(World(test).GetJointState(chain.joints[0]).force));
    }

    INFO("the top joint carried up to " << most << " N, far over its weight, and nothing broke");
    REQUIRE(most > 100.0f);
    REQUIRE(breaks.events.empty());
    for (const Entity& joint : chain.joints)
    {
        REQUIRE(JointOf(joint).enabled);
    }
}

TEST_CASE("Disabling a joint from game code sends no JointBroken", "[physics][joints][breaking]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 5);
    Breaks breaks;
    breaks.Run(test, 0.5);

    // In the same tick: a threshold the joint would be over, and disabled.
    JointOf(chain.joints[0]).breakForce = 45.0f;
    JointOf(chain.joints[0]).enabled = false;
    breaks.Run(test, 1.0);

    INFO("a game disable is not a break: no event, and a disabled joint is never checked");
    REQUIRE(breaks.events.empty());
    REQUIRE_FALSE(JointOf(chain.joints[0]).enabled);
}

TEST_CASE("Repairing a joint still overloaded breaks it again, with another event", "[physics][joints][breaking]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 5);
    Breaks breaks;
    breaks.Run(test, 0.5);
    JointOf(chain.joints[0]).breakForce = 45.0f;
    RunUntilBreak(test, breaks, chain.joints[0], 1.0);
    REQUIRE(breaks.events.size() == 1);

    JointOf(chain.joints[0]).enabled = true;
    RunUntilBreak(test, breaks, chain.joints[0], 1.0);

    INFO("repair is enabled = true, with no cooldown: still holding five links, it breaks again");
    REQUIRE(breaks.events.size() == 2);
    REQUIRE(breaks.events[1].joint == chain.joints[0]);
    REQUIRE_FALSE(JointOf(chain.joints[0]).enabled);
}

namespace
{
    /** Whether `a`'s body touches `b`'s now, as of the last step. */
    bool Touching(PhysicsApp& test, const Entity& a, const Entity& b)
    {
        bool touching = false;
        World(test).ForEachContact(a, [&](const Contact& contact) { touching = touching || contact.otherBody == b; });
        return touching;
    }
} // namespace

TEST_CASE("A broken joint's bodies collide again from the next step", "[physics][joints][breaking][collide]")
{
    PhysicsApp test;
    // A static unit box at the origin, and a 1 kg unit box overlapping it by 0.2, held there by a fixed joint.
    Entity post = Place(test.Entities(), Math::Vec3(0.0f));
    post.AddComponent<Collider>();
    Entity box = AddBody(test.Entities(), Math::Vec3(0.8f, 0.0f, 0.0f));
    Entity joint = test.Entities().CreateEntity();
    const JointFrames frames = FrameFromWorld(post, box, JointFrame{Math::Vec3(0.4f, 0.0f, 0.0f)});
    joint.AddComponent<Joint>(
        Joint{.bodyA = post, .bodyB = box, .frameA = frames.frameA, .frameB = frames.frameB, .kind = FixedJoint{}});
    Breaks breaks;
    breaks.Run(test, 0.2);
    REQUIRE_FALSE(Touching(test, post, box));

    // The joint holds the box's weight, about 9.8 N.
    JointOf(joint).breakForce = 1.0f;
    RunUntilBreak(test, breaks, joint, 1.0);
    REQUIRE(breaks.events.size() == 1);

    breaks.Run(test, tick);
    INFO("one step after the break, the overlapping bodies touch");
    REQUIRE(Touching(test, post, box));
}

TEST_CASE("Lowering a sleeping joint's threshold wakes its bodies, so it can break", "[physics][joints][breaking]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 2);
    Breaks breaks;
    breaks.Run(test, 3.0);
    REQUIRE(World(test).IsSleeping(chain.links[1]));

    // The lower joint holds one link, about 9.8 N.
    JointOf(chain.joints[1]).breakForce = 5.0f;
    breaks.Run(test, 0.5);

    INFO("the edit woke the pair, and the joint broke once");
    REQUIRE(breaks.events.size() == 1);
    REQUIRE(breaks.events[0].joint == chain.joints[1]);
}

namespace
{
    std::size_t CountColoured(std::span<const DebugLine> lines, std::uint32_t colour)
    {
        return static_cast<std::size_t>(std::count_if(
            lines.begin(), lines.end(), [colour](const DebugLine& line) { return line.colour == colour; }));
    }
} // namespace

TEST_CASE("A broken joint's axes are drawn dimmed", "[physics][joints][breaking][debug]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 2);
    JointOf(chain.joints[1]).breakForce = 1.0f;
    Breaks breaks;
    breaks.Run(test, tick);
    World(test).SetDebugDraw(DebugDraw::Joints);
    RunUntilBreak(test, breaks, chain.joints[1], 1.0);
    REQUIRE(breaks.events.size() == 1);

    const std::span<const DebugLine> lines = World(test).DebugLines();
    const std::array<std::uint32_t, 6> colours = {
        DebugColour::JointAX, DebugColour::JointAY, DebugColour::JointAZ,
        DebugColour::JointBX, DebugColour::JointBY, DebugColour::JointBZ,
    };
    INFO("two joints, two frames each, three axes a frame");
    REQUIRE(lines.size() == 12);
    for (const std::uint32_t colour : colours)
    {
        INFO("one axis in colour " << std::hex << colour << " for the whole joint, and one dimmed for the broken one");
        REQUIRE(CountColoured(lines, colour) == 1);
        REQUIRE(CountColoured(lines, DebugColour::Dimmed(colour)) == 1);
    }
}
