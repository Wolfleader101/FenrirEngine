// Physics materials through the frame seam: friction and restitution on a collider change how its body moves.

#include <algorithm>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    // The highest a ball dropped from 2 m onto the ground climbs after its first bounce.
    float BounceHeight(const PhysicsMaterial& material)
    {
        PhysicsApp test;
        AddGround(test.Entities());
        Entity ball = AddBody(test.Entities(), Math::Vec3(0.0f, 2.0f, 0.0f), SphereShape{0.25f});
        ball.AddComponent<PhysicsMaterial>(material);

        // Its centre falls 1.75 m before it touches: about 0.6 s.
        test.Run(0.7);
        float highest = PositionOf(ball).y;
        for (int i = 0; i < 60; ++i)
        {
            test.Run(tick);
            highest = std::max(highest, PositionOf(ball).y);
        }
        return highest;
    }

    // How far a box let go on a 30 degree slope slides in two seconds, along the slope.
    float SlideDistance(const PhysicsMaterial& material)
    {
        PhysicsApp test;
        const Math::Quat tilt = glm::angleAxis(glm::radians(30.0f), Math::Vec3(0.0f, 0.0f, 1.0f));
        Entity slope = Place(test.Entities(), Math::Vec3(0.0f));
        slope.GetComponent<Transform>().rot = tilt;
        slope.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(20.0f, 0.5f, 5.0f)}});
        slope.AddComponent<PhysicsMaterial>(material);

        // Resting on the slope's top face, its own faces parallel to it.
        const Math::Vec3 start = tilt * Math::Vec3(0.0f, 1.0f, 0.0f);
        Entity box = AddBody(test.Entities(), start);
        box.GetComponent<Transform>().rot = tilt;
        box.AddComponent<PhysicsMaterial>(material);

        test.Run(2.0);
        return glm::length(PositionOf(box) - start);
    }
} // namespace

TEST_CASE("A bouncy material bounces higher than a dead one", "[physics][materials]")
{
    const float bouncy = BounceHeight(PhysicsMaterial{.restitution = 0.9f});
    const float dead = BounceHeight(PhysicsMaterial{.restitution = 0.0f});

    INFO("with restitution 0.9 the ball climbs back well over halfway (about 0.25 + 1.75 x 0.81 = 1.67), it reached "
         << bouncy);
    REQUIRE(bouncy > 1.2f);
    INFO("with restitution 0 it stays on the ground (centre at 0.25), it reached " << dead);
    REQUIRE(dead < 0.25f + restSlop + 0.05f);
}

TEST_CASE("A low-friction slope lets a box slide further than a high-friction one", "[physics][materials]")
{
    const float icy = SlideDistance(PhysicsMaterial{.friction = 0.05f});
    const float rough = SlideDistance(PhysicsMaterial{.friction = 1.0f});

    INFO("on ice (friction 0.05, well under tan 30 = 0.58) the box slides metres down the slope, it slid " << icy);
    REQUIRE(icy > 3.0f);
    INFO("with friction 1 (over tan 30) it holds still, it slid " << rough);
    REQUIRE(rough < 0.05f);
}
