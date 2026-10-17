// Joint limits, motors, springs and friction through the frame seam: the settings of the hinge, slider, distance and
// swing-twist kinds, observed in their bodies' world transforms and velocities as physics steps, and in the warnings
// logged for out-of-range values.

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Joints.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    constexpr float pi = std::numbers::pi_v<float>;

    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    /** A joint of `kind` on `jointEntity` between `bodyA` and `bodyB`, its rest pose at `anchor` where the bodies are
     * now. */
    Joint& Join(Entity jointEntity, const Entity& bodyA, const Entity& bodyB, const JointFrame& anchor, JointKind kind)
    {
        const JointFrames frames = FrameFromWorld(bodyA, bodyB, anchor);
        return jointEntity.AddComponent<Joint>(
            Joint{.bodyA = bodyA, .bodyB = bodyB, .frameA = frames.frameA, .frameB = frames.frameB, .kind = kind});
    }

    /** A door 1 wide, 2 tall and of mass 1 beside a static post, hinged on its left edge about world up. The post is
     * bodyA, so the hinge angle is the door's turn about world up, by the right-hand rule. The hinge is on the door's
     * own entity. */
    struct Door
    {
        Entity post;
        Entity door;

        [[nodiscard]] HingeJoint& Hinge() const
        {
            return door.GetComponent<Joint>().kind.As<HingeJoint>();
        }

        /** The hinge angle, from the door's world rotation. */
        [[nodiscard]] float Angle() const
        {
            const Math::Quat rotation = GetWorldTransform(door).rot;
            // The rotation's sign is free: the shorter way round reads the angle in (-pi, pi].
            const float sign = rotation.w < 0.0f ? -1.0f : 1.0f;
            return 2.0f * std::atan2(sign * rotation.y, sign * rotation.w);
        }

        /** The door's angular velocity about the hinge. */
        [[nodiscard]] float Speed() const
        {
            return door.GetComponent<RigidBody>().angularVelocity.y;
        }
    };

    Door MakeDoor(PhysicsApp& test, HingeJoint hinge = {})
    {
        Door made;
        made.post = Place(test.Entities(), Math::Vec3(-0.6f, 1.0f, 0.0f));
        made.post.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.05f, 1.0f, 0.05f)}});
        made.door = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f), BoxShape{Math::Vec3(0.5f, 1.0f, 0.05f)});
        const Math::Quat axisUp = Math::AngleAxis(pi / 2.0f, Math::Vec3(0.0f, 0.0f, 1.0f));
        Join(made.door, made.post, made.door, JointFrame{Math::Vec3(-0.5f, 1.0f, 0.0f), axisUp}, hinge);
        return made;
    }

    /** Swings the door open by an angular impulse about the hinge: about 3 rad/s per unit. */
    void Push(PhysicsApp& test, const Door& door, float impulse)
    {
        World(test).AddAngularImpulse(door.door, Math::Vec3(0.0f, impulse, 0.0f));
    }

    /** The largest hinge angle over `seconds` of ticks. */
    float WidestOver(PhysicsApp& test, const Door& door, double seconds)
    {
        float widest = door.Angle();
        const int frames = static_cast<int>(seconds / tick + 0.5);
        for (int i = 0; i < frames; ++i)
        {
            test.app.RunFrame(tick);
            widest = std::max(widest, door.Angle());
        }
        return widest;
    }
} // namespace

TEST_CASE("A hard hinge limit stops the door at its maximum", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test, HingeJoint{.limits = {.enabled = true, .min = 0.0f, .max = 1.0f}});
    test.Run(0.1);

    Push(test, door, 2.0f);
    const float widest = WidestOver(test, door, 1.0);

    INFO("the door, swung at 6 rad/s, reached its limit of 1 rad and went less than a step past it; its widest angle "
         "was "
         << widest << " rad");
    REQUIRE(widest == Approx(1.0f).margin(0.05));
    INFO("and it rests at the limit or inside it, at " << door.Angle() << " rad");
    REQUIRE(door.Angle() < 1.005f);
}

TEST_CASE("A soft hinge limit lets the door past it and pulls it back", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door = MakeDoor(
        test,
        HingeJoint{
            .limits = {.enabled = true, .min = 0.0f, .max = 1.0f, .spring = {.frequency = 1.0f, .damping = 1.0f}}});
    test.Run(0.1);

    Push(test, door, 2.0f);
    const float widest = WidestOver(test, door, 3.0);

    INFO("the door, swung at 6 rad/s, went well past its soft limit of 1 rad; its widest angle was " << widest
                                                                                                     << " rad");
    REQUIRE(widest > 1.2f);
    INFO("and was pushed back within its limits, at " << door.Angle() << " rad");
    REQUIRE(door.Angle() < 1.02f);
    REQUIRE(door.Angle() > -0.1f);
}

TEST_CASE("A hinge limit with min == max holds the door shut", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test, HingeJoint{.limits = {.enabled = true, .min = 0.0f, .max = 0.0f}});
    test.Run(0.1);

    Push(test, door, 1.0f);
    const float widest = WidestOver(test, door, 1.0);

    INFO("pushed open, the door stays shut; its widest angle was " << widest << " rad");
    REQUIRE(std::abs(widest) < 0.05f);
    REQUIRE(std::abs(door.Angle()) < 0.005f);
}

TEST_CASE("A hinge limit excluding the rest pose is widened to take it in, with one warning",
          "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    // From 0.3 to 1: the door, made shut, would start outside it.
    const Door door = MakeDoor(test, HingeJoint{.limits = {.enabled = true, .min = 0.3f, .max = 1.0f}});
    test.Run(0.1);

    INFO("the door stays where it was made, shut, rather than being thrown to 0.3 rad; it is at " << door.Angle());
    REQUIRE(std::abs(door.Angle()) < 0.005f);

    Push(test, door, -1.0f);
    test.Run(1.0);
    INFO("pushed the other way, it stops at the rest pose, the widened limit's minimum; it is at " << door.Angle());
    REQUIRE(door.Angle() > -0.05f);

    Push(test, door, 2.0f);
    const float widest = WidestOver(test, door, 1.0);
    INFO("and its maximum is kept: it opens no wider than 1 rad; its widest angle was " << widest);
    REQUIRE(widest < 1.05f);

    INFO("one warning was logged for the joint, however many ticks it was applied");
    REQUIRE(test.warnings.size() == 1);
}

TEST_CASE("A hinge limit with min above max is widened to take in both and the rest pose",
          "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test, HingeJoint{.limits = {.enabled = true, .min = 1.0f, .max = 0.5f}});
    test.Run(0.1);

    Push(test, door, 2.0f);
    const float widest = WidestOver(test, door, 1.0);
    INFO("the door opens up to 1 rad, the larger of the two, and no further; its widest angle was " << widest);
    REQUIRE(widest == Approx(1.0f).margin(0.05));
    Push(test, door, -4.0f);
    test.Run(1.0);
    INFO("and shuts as far as the rest pose; it is at " << door.Angle());
    REQUIRE(door.Angle() > -0.05f);
    INFO("with one warning");
    REQUIRE(test.warnings.size() == 1);
}

namespace
{
    const Math::Vec3 rig(0.0f, 5.0f, 0.0f);

    /** A unit box beside a static rail along world Z at `rig`, joined to it by a slider. The rail is bodyA, so the
     * slider's position is the box's offset along world Z. Gravity is across the slider, so it does not move the
     * box. */
    struct Slide
    {
        Entity rail;
        Entity box;

        [[nodiscard]] SliderJoint& Slider() const
        {
            return box.GetComponent<Joint>().kind.As<SliderJoint>();
        }

        [[nodiscard]] float Position() const
        {
            return PositionOf(box).z - rig.z;
        }

        [[nodiscard]] float Speed() const
        {
            return box.GetComponent<RigidBody>().linearVelocity.z;
        }
    };

    Slide MakeSlide(PhysicsApp& test, SliderJoint slider = {})
    {
        Slide made;
        made.rail = Place(test.Entities(), rig);
        made.rail.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.05f, 0.05f, 2.0f)}});
        made.box = AddBody(test.Entities(), rig);
        const Math::Quat alongZ = Math::AngleAxis(-pi / 2.0f, Math::Vec3(0.0f, 1.0f, 0.0f));
        Join(made.box, made.rail, made.box, JointFrame{rig, alongZ}, slider);
        return made;
    }

    /** The furthest the box slides along Z over `seconds` of ticks. */
    float FurthestOver(PhysicsApp& test, const Slide& slide, double seconds)
    {
        float furthest = slide.Position();
        const int frames = static_cast<int>(seconds / tick + 0.5);
        for (int i = 0; i < frames; ++i)
        {
            test.app.RunFrame(tick);
            furthest = std::max(furthest, slide.Position());
        }
        return furthest;
    }
} // namespace

TEST_CASE("A hard slider limit stops the box at its maximum", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Slide slide = MakeSlide(test, SliderJoint{.limits = {.enabled = true, .min = -0.5f, .max = 0.5f}});
    test.Run(0.1);

    World(test).AddImpulse(slide.box, Math::Vec3(0.0f, 0.0f, 3.0f));
    const float furthest = FurthestOver(test, slide, 1.0);

    INFO("the box, slid at 3 m/s, stopped at its limit of 0.5 m; it went as far as " << furthest);
    REQUIRE(furthest == Approx(0.5f).margin(0.05));
    REQUIRE(slide.Position() < 0.505f);
}

TEST_CASE("A soft slider limit lets the box past it and pulls it back", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Slide slide = MakeSlide(
        test,
        SliderJoint{
            .limits = {.enabled = true, .min = -0.5f, .max = 0.5f, .spring = {.frequency = 1.0f, .damping = 1.0f}}});
    test.Run(0.1);

    World(test).AddImpulse(slide.box, Math::Vec3(0.0f, 0.0f, 3.0f));
    const float furthest = FurthestOver(test, slide, 3.0);

    INFO("the box, slid at 3 m/s, went well past its soft limit of 0.5 m; it went as far as " << furthest);
    REQUIRE(furthest > 0.6f);
    INFO("and was pushed back within its limits, at " << slide.Position());
    REQUIRE(std::abs(slide.Position()) < 0.52f);
}

TEST_CASE("A slider limit with min == max holds the box at its rest pose", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Slide slide = MakeSlide(test, SliderJoint{.limits = {.enabled = true, .min = 0.0f, .max = 0.0f}});
    test.Run(0.1);

    World(test).AddImpulse(slide.box, Math::Vec3(0.0f, 0.0f, 1.0f));
    const float furthest = FurthestOver(test, slide, 1.0);

    INFO("pushed along the slider, the box stays put; it went as far as " << furthest);
    REQUIRE(std::abs(furthest) < 0.03f);
    REQUIRE(std::abs(slide.Position()) < 0.005f);
}

TEST_CASE("A Velocity motor drives a hinge and a slider at its target velocity", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    // The position target is ignored in Velocity mode.
    const Door door = MakeDoor(
        test, HingeJoint{.motor = {.mode = JointMotorMode::Velocity, .targetVelocity = 1.5f, .targetPosition = 0.2f}});
    const Slide slide = MakeSlide(
        test,
        SliderJoint{.motor = {.mode = JointMotorMode::Velocity, .targetVelocity = -0.5f, .targetPosition = 0.2f}});

    test.Run(0.5);

    INFO("the door turns at its motor's 1.5 rad/s; it turns at " << door.Speed());
    REQUIRE(door.Speed() == Approx(1.5f).margin(0.01));
    INFO("the box slides at its motor's -0.5 m/s; it slides at " << slide.Speed());
    REQUIRE(slide.Speed() == Approx(-0.5f).margin(0.01));
}

TEST_CASE("A Position motor drives a hinge and a slider to its target position", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    // The velocity target is ignored in Position mode.
    const Door door = MakeDoor(
        test, HingeJoint{.motor = {.mode = JointMotorMode::Position, .targetVelocity = 5.0f, .targetPosition = 0.8f}});
    const Slide slide = MakeSlide(
        test,
        SliderJoint{.motor = {.mode = JointMotorMode::Position, .targetVelocity = 5.0f, .targetPosition = -0.3f}});

    test.Run(3.0);

    INFO("the door turns to its motor's 0.8 rad and stays; it is at " << door.Angle() << ", turning at "
                                                                      << door.Speed());
    REQUIRE(door.Angle() == Approx(0.8f).margin(0.005));
    REQUIRE(std::abs(door.Speed()) < 0.01f);
    INFO("the box slides to its motor's -0.3 m and stays; it is at " << slide.Position());
    REQUIRE(slide.Position() == Approx(-0.3f).margin(0.005));
}

TEST_CASE("A Position motor's target outside an enabled limit is clamped into it, silently",
          "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test, HingeJoint{.limits = {.enabled = true, .min = -0.5f, .max = 0.5f},
                                                .motor = {.mode = JointMotorMode::Position, .targetPosition = 2.0f}});
    const Slide slide =
        MakeSlide(test, SliderJoint{.limits = {.enabled = true, .min = -0.2f, .max = 0.2f},
                                    .motor = {.mode = JointMotorMode::Position, .targetPosition = -1.0f}});

    test.Run(3.0);

    INFO("the door is driven to its limit of 0.5 rad and rests there; it is at " << door.Angle());
    REQUIRE(door.Angle() == Approx(0.5f).margin(0.01));
    INFO("the box is driven to its limit of -0.2 m; it is at " << slide.Position());
    REQUIRE(slide.Position() == Approx(-0.2f).margin(0.01));
    INFO("and nothing was warned about");
    REQUIRE(test.warnings.empty());
}

TEST_CASE("An unbounded Velocity motor is held at a hard hinge or slider limit", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test, HingeJoint{.limits = {.enabled = true, .min = 0.0f, .max = 1.0f},
                                                .motor = {.mode = JointMotorMode::Velocity, .targetVelocity = 2.0f}});
    const Slide slide =
        MakeSlide(test, SliderJoint{.limits = {.enabled = true, .min = -0.4f, .max = 0.4f},
                                    .motor = {.mode = JointMotorMode::Velocity, .targetVelocity = -2.0f}});

    const float widest = WidestOver(test, door, 2.0);

    INFO("the door, driven open at 2 rad/s with no limit on the motor's torque, never passed its limit of 1 rad; its "
         "widest angle was "
         << widest << ", and it rests at " << door.Angle());
    REQUIRE(widest < 1.01f);
    REQUIRE(door.Angle() == Approx(1.0f).margin(0.01));
    INFO("the box, driven at -2 m/s, rests at its limit of -0.4 m; it is at " << slide.Position());
    REQUIRE(slide.Position() == Approx(-0.4f).margin(0.01));
}

TEST_CASE("A Velocity motor pushes past a soft hinge limit", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door =
        MakeDoor(test, HingeJoint{.limits = {.enabled = true, .min = 0.0f, .max = 1.0f, .spring = {.frequency = 2.0f}},
                                  .motor = {.mode = JointMotorMode::Velocity, .targetVelocity = 2.0f}});

    test.Run(1.5);

    INFO("a soft limit is a spring, which an unbounded motor pushes past: the door is at " << door.Angle());
    REQUIRE(door.Angle() > 1.5f);
}

TEST_CASE("Friction keeps a hinge where it is left", "[physics][joints][joint-settings]")
{
    // A box hinged by one edge about world Z, held out level: its weight turns it about the hinge by 4.9 N m.
    auto hingedBox = [](PhysicsApp& test, float friction, float z) {
        Entity box = AddBody(test.Entities(), Math::Vec3(0.5f, 5.0f, z));
        const Math::Quat alongZ = Math::AngleAxis(-pi / 2.0f, Math::Vec3(0.0f, 1.0f, 0.0f));
        Join(test.Entities().CreateEntity(), box, Entity(), JointFrame{Math::Vec3(0.0f, 5.0f, z), alongZ},
             HingeJoint{.friction = friction});
        return box;
    };
    PhysicsApp test;
    Entity free = hingedBox(test, 0.0f, 0.0f);
    Entity stiff = hingedBox(test, 10.0f, 3.0f);

    float lowest = PositionOf(free).y;
    for (int i = 0; i < 60; ++i)
    {
        test.app.RunFrame(tick);
        lowest = std::min(lowest, PositionOf(free).y);
    }

    INFO("without friction the box swings down: its centre fell as low as y = " << lowest);
    REQUIRE(lowest < 4.6f);
    INFO("with 10 N m of friction it stays level where it was left: its centre is at y = " << PositionOf(stiff).y);
    REQUIRE(PositionOf(stiff).y == Approx(5.0f).margin(0.005));
}

TEST_CASE("A distance joint with a spring bobs about its length", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Math::Vec3 top(0.0f, 10.0f, 0.0f);
    // A 1 m rod made soft: 1 Hz, lightly damped. The ball's weight would stretch it about 0.25 m at rest.
    Entity ball = AddBody(test.Entities(), top - Math::Vec3(0.0f, 1.0f, 0.0f), SphereShape{0.2f});
    ball.AddComponent<Joint>(Joint{
        .bodyA = ball,
        .bodyB = Entity(),
        .frameB = JointFrame{top},
        .kind =
            DistanceJoint{.minDistance = 1.0f, .maxDistance = 1.0f, .spring = {.frequency = 1.0f, .damping = 0.1f}}});

    float longest = 1.0f;
    float shortestAfter = 10.0f;
    for (int i = 0; i < 120; ++i)
    {
        test.app.RunFrame(tick);
        const float length = Math::Magnitude(top - PositionOf(ball));
        longest = std::max(longest, length);
        if (i > 30)
        {
            shortestAfter = std::min(shortestAfter, length);
        }
    }

    INFO("the soft rod stretched under the ball's weight, to " << longest << " m at its longest");
    REQUIRE(longest > 1.3f);
    INFO("and sprang back, to " << shortestAfter << " m at its shortest after half a second");
    REQUIRE(shortestAfter < longest - 0.2f);
}

namespace
{
    /** A unit box centred on a static anchor `along` metres along world X from `rig`, joined to it at its centre by a
     * swing-twist joint with the frames' axes along the world's. Its weight does not turn it, and frameB's rotation
     * relative to frameA is the box's world rotation. Limbs 2 m apart do not touch. */
    struct Limb
    {
        Entity anchor;
        Entity box;

        [[nodiscard]] SwingTwistJoint& SwingTwist() const
        {
            return box.GetComponent<Joint>().kind.As<SwingTwistJoint>();
        }

        [[nodiscard]] Math::Quat Rotation() const
        {
            return GetWorldTransform(box).rot;
        }
    };

    Limb MakeLimb(PhysicsApp& test, SwingTwistJoint swingTwist = {}, float along = 0.0f)
    {
        const Math::Vec3 at = rig + Math::Vec3(along, 0.0f, 0.0f);
        Limb made;
        made.anchor = Place(test.Entities(), at);
        made.anchor.AddComponent<Collider>(Collider{SphereShape{0.05f}});
        made.box = AddBody(test.Entities(), at);
        Join(made.box, made.anchor, made.box, JointFrame{at}, swingTwist);
        return made;
    }

    /** A rotation's swing (the angle of its turn away from X) and its twist (about X), in radians. */
    struct SwingAndTwist
    {
        float swing;
        float twist;
    };

    SwingAndTwist Decompose(const Math::Quat& rotation)
    {
        const float twist = 2.0f * std::atan2(rotation.x, rotation.w);
        const Math::Quat aboutX = Math::AngleAxis(twist, Math::Vec3(1.0f, 0.0f, 0.0f));
        const Math::Quat swing = rotation * Math::Conjugate(aboutX);
        return SwingAndTwist{2.0f * std::acos(std::min(1.0f, std::abs(swing.w))), std::remainder(twist, 2.0f * pi)};
    }

    /** The largest swing the limb reaches over `seconds` of ticks. */
    float WidestSwingOver(PhysicsApp& test, const Limb& limb, double seconds)
    {
        float widest = 0.0f;
        const int frames = static_cast<int>(seconds / tick + 0.5);
        for (int i = 0; i < frames; ++i)
        {
            test.app.RunFrame(tick);
            widest = std::max(widest, Decompose(limb.Rotation()).swing);
        }
        return widest;
    }
} // namespace

TEST_CASE("A swing-twist joint holds its cone and twist limits", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    // Narrow about the axis each is swung about, wide about the other.
    const Limb swungY = MakeLimb(test, SwingTwistJoint{.swingYLimit = 0.3f, .swingZLimit = 0.8f}, 0.0f);
    const Limb swungZ = MakeLimb(test, SwingTwistJoint{.swingYLimit = 0.8f, .swingZLimit = 0.3f}, 2.0f);
    const Limb twisted = MakeLimb(test, SwingTwistJoint{.twistMin = -0.2f, .twistMax = 0.2f}, 4.0f);
    test.Run(0.1);

    World(test).AddAngularImpulse(swungY.box, Math::Vec3(0.0f, 0.3f, 0.0f));
    World(test).AddAngularImpulse(swungZ.box, Math::Vec3(0.0f, 0.0f, 0.3f));
    World(test).AddAngularImpulse(twisted.box, Math::Vec3(0.3f, 0.0f, 0.0f));
    const float widestY = WidestSwingOver(test, swungY, 1.0);
    const float widestZ = Decompose(swungZ.Rotation()).swing;
    const float twist = Decompose(twisted.Rotation()).twist;

    INFO("swung about Y, a box swings to its 0.3 rad swingYLimit and no further; it reached " << widestY);
    REQUIRE(widestY == Approx(0.3f).margin(0.03));
    INFO("swung about Z, another rests at its 0.3 rad swingZLimit; it is at " << widestZ);
    REQUIRE(widestZ == Approx(0.3f).margin(0.02));
    INFO("twisted about X, a third rests at its 0.2 rad twist limit; it is at " << twist);
    REQUIRE(twist == Approx(0.2f).margin(0.02));
}

TEST_CASE("A swing-twist Velocity motor turns the body at its target angular velocity",
          "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Limb limb =
        MakeLimb(test, SwingTwistJoint{.swingYLimit = pi,
                                       .swingZLimit = pi,
                                       .twistMin = -pi,
                                       .twistMax = pi,
                                       .motor = {.mode = JointMotorMode::Velocity,
                                                 .targetRotation = Math::AngleAxis(0.3f, Math::Vec3(1.0f, 0.0f, 0.0f)),
                                                 .targetAngularVelocity = Math::Vec3(0.0f, 0.0f, 1.0f)}});

    test.Run(0.3);

    const Math::Vec3 spin = limb.box.GetComponent<RigidBody>().angularVelocity;
    INFO("the box turns about frameA's Z at 1 rad/s and about nothing else; it turns at " << spin.x << ", " << spin.y
                                                                                          << ", " << spin.z);
    REQUIRE(Math::Magnitude(spin - Math::Vec3(0.0f, 0.0f, 1.0f)) < 0.02f);
}

TEST_CASE("A swing-twist Position motor turns the body to its target rotation", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Math::Quat target =
        Math::AngleAxis(0.5f, Math::Vec3(0.3f, 1.0f, 0.5f) / Math::Magnitude(Math::Vec3(0.3f, 1.0f, 0.5f)));
    const Limb limb = MakeLimb(test, SwingTwistJoint{.motor = {.mode = JointMotorMode::Position,
                                                               .targetRotation = target,
                                                               .targetAngularVelocity = Math::Vec3(5.0f)}});

    test.Run(3.0);

    const Math::Quat at = limb.Rotation();
    const float apart =
        2.0f *
        std::acos(std::min(1.0f, std::abs(at.w * target.w + at.x * target.x + at.y * target.y + at.z * target.z)));
    INFO("the box turned to its motor's target, inside the cone and twist; it is " << apart << " rad from it");
    REQUIRE(apart < 0.01f);
}

TEST_CASE("An unbounded swing-twist Velocity motor is held at the cone and twist limits",
          "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Limb swinging =
        MakeLimb(test, SwingTwistJoint{.swingYLimit = 0.5f,
                                       .swingZLimit = 0.3f,
                                       .motor = {.mode = JointMotorMode::Velocity,
                                                 .targetAngularVelocity = Math::Vec3(0.0f, 2.0f, 1.0f)}});
    const Limb twisting = MakeLimb(test,
                                   SwingTwistJoint{.twistMin = -0.4f,
                                                   .twistMax = 0.4f,
                                                   .motor = {.mode = JointMotorMode::Velocity,
                                                             .targetAngularVelocity = Math::Vec3(-2.0f, 0.0f, 0.0f)}},
                                   2.0f);

    const float widest = WidestSwingOver(test, swinging, 2.0);

    INFO("the box driven to swing with no limit on the motor's torque never left its cone, at most 0.5 rad wide; it "
         "reached "
         << widest);
    REQUIRE(widest < 0.52f);
    const float twist = Decompose(twisting.Rotation()).twist;
    INFO("the box driven to twist rests at its -0.4 rad limit; it is at " << twist);
    REQUIRE(twist == Approx(-0.4f).margin(0.01));
}

namespace
{
    /** An out-of-range value in a joint, and another of the same kind to edit it to. */
    struct OutOfRange
    {
        const char* what;
        Joint (*made)();
        void (*editedAgain)(Joint&);
    };

    template <typename Kind>
    Joint Holding(Kind kind)
    {
        return Joint{.kind = kind};
    }

    const OutOfRange outOfRange[] = {
        {"a negative hinge friction", [] { return Holding(HingeJoint{.friction = -1.0f}); },
         [](Joint& joint) { joint.kind.As<HingeJoint>().friction = -2.0f; }},
        {"a negative motor force", [] { return Holding(HingeJoint{.motor = {.maxForce = -5.0f}}); },
         [](Joint& joint) { joint.kind.As<HingeJoint>().motor.maxForce = -6.0f; }},
        {"a negative limit spring frequency",
         [] { return Holding(HingeJoint{.limits = {.spring = {.frequency = -1.0f}}}); },
         [](Joint& joint) { joint.kind.As<HingeJoint>().limits.spring.frequency = -2.0f; }},
        {"a negative damping ratio", [] { return Holding(HingeJoint{.motor = {.spring = {.damping = -1.0f}}}); },
         [](Joint& joint) { joint.kind.As<HingeJoint>().motor.spring.damping = -2.0f; }},
        {"a spring frequency above half the tick rate",
         [] { return Holding(HingeJoint{.motor = {.spring = {.frequency = 1000.0f}}}); },
         [](Joint& joint) { joint.kind.As<HingeJoint>().motor.spring.frequency = 2000.0f; }},
        {"hinge limits excluding the rest pose",
         [] { return Holding(HingeJoint{.limits = {.enabled = true, .min = 0.2f, .max = 1.0f}}); },
         [](Joint& joint) { joint.kind.As<HingeJoint>().limits.min = 0.3f; }},
        {"hinge limits beyond a half turn",
         [] { return Holding(HingeJoint{.limits = {.enabled = true, .min = -4.0f, .max = 1.0f}}); },
         [](Joint& joint) { joint.kind.As<HingeJoint>().limits.max = 5.0f; }},
        {"a negative slider friction", [] { return Holding(SliderJoint{.friction = -1.0f}); },
         [](Joint& joint) { joint.kind.As<SliderJoint>().friction = -2.0f; }},
        {"slider limits with min above max",
         [] { return Holding(SliderJoint{.limits = {.enabled = true, .min = 0.5f, .max = 0.2f}}); },
         [](Joint& joint) { joint.kind.As<SliderJoint>().limits.min = 0.6f; }},
        {"a negative minimum distance", [] { return Holding(DistanceJoint{.minDistance = -1.0f}); },
         [](Joint& joint) { joint.kind.As<DistanceJoint>().minDistance = -2.0f; }},
        {"a maximum distance below the minimum",
         [] { return Holding(DistanceJoint{.minDistance = 2.0f, .maxDistance = 1.0f}); },
         [](Joint& joint) { joint.kind.As<DistanceJoint>().maxDistance = 0.5f; }},
        {"a negative distance spring frequency", [] { return Holding(DistanceJoint{.spring = {.frequency = -1.0f}}); },
         [](Joint& joint) { joint.kind.As<DistanceJoint>().spring.frequency = -2.0f; }},
        {"a negative swing limit", [] { return Holding(SwingTwistJoint{.swingYLimit = -0.1f}); },
         [](Joint& joint) { joint.kind.As<SwingTwistJoint>().swingZLimit = -0.2f; }},
        {"a swing limit beyond a half turn", [] { return Holding(SwingTwistJoint{.swingZLimit = 4.0f}); },
         [](Joint& joint) { joint.kind.As<SwingTwistJoint>().swingYLimit = 5.0f; }},
        {"a twist range excluding the rest pose",
         [] { return Holding(SwingTwistJoint{.twistMin = 0.2f, .twistMax = 0.5f}); },
         [](Joint& joint) { joint.kind.As<SwingTwistJoint>().twistMin = 0.3f; }},
        {"twist limits beyond a half turn", [] { return Holding(SwingTwistJoint{.twistMin = -4.0f}); },
         [](Joint& joint) { joint.kind.As<SwingTwistJoint>().twistMax = 5.0f; }},
        {"a negative swing-twist friction", [] { return Holding(SwingTwistJoint{.friction = -1.0f}); },
         [](Joint& joint) { joint.kind.As<SwingTwistJoint>().friction = -2.0f; }},
        {"a negative motor torque", [] { return Holding(SwingTwistJoint{.motor = {.maxTorque = -1.0f}}); },
         [](Joint& joint) { joint.kind.As<SwingTwistJoint>().motor.maxTorque = -2.0f; }},
        {"a negative break force", [] { return Joint{.kind = BallJoint{}, .breakForce = -1.0f}; },
         [](Joint& joint) { joint.breakTorque = -1.0f; }},
    };
} // namespace

TEST_CASE("Each out-of-range joint setting is corrected with one warning per joint",
          "[physics][joints][joint-settings]")
{
    for (const OutOfRange& value : outOfRange)
    {
        PhysicsApp test;
        // Two joints holding it, on two boxes hung from the world at their centres, far apart.
        std::vector<Entity> boxes;
        for (const float x : {0.0f, 5.0f})
        {
            const Math::Vec3 at(x, 5.0f, 0.0f);
            Entity box = AddBody(test.Entities(), at);
            Joint joint = value.made();
            joint.bodyA = box;
            joint.frameB = JointFrame{at};
            box.AddComponent<Joint>(joint);
            boxes.push_back(box);
        }

        test.Run(0.5);
        INFO(value.what << " gives one warning for each of the two joints holding it, however many ticks it is "
                           "applied; the warnings were:");
        for (const std::string& warning : test.warnings)
        {
            INFO("  " << warning);
        }
        REQUIRE(test.warnings.size() == 2);

        value.editedAgain(boxes[0].GetComponent<Joint>());
        test.Run(0.5);
        INFO("edited to another value of the same kind, the joint is not warned about again");
        REQUIRE(test.warnings.size() == 2);
        INFO("and nothing failed in the backend");
        REQUIRE(test.errors.empty());
    }
}

TEST_CASE("A spring frequency above half the tick rate is lowered to it", "[physics][joints][joint-settings]")
{
    // At 60 ticks a second, a 1000 Hz Position motor drives as a 30 Hz one: still to its target, without blowing up.
    PhysicsApp test;
    const Door door = MakeDoor(test, HingeJoint{.motor = {.mode = JointMotorMode::Position,
                                                          .targetPosition = 0.5f,
                                                          .spring = {.frequency = 1000.0f}}});

    test.Run(1.0);

    INFO("the door reaches its target, 0.5 rad, and rests there; it is at " << door.Angle() << ", turning at "
                                                                            << door.Speed());
    REQUIRE(door.Angle() == Approx(0.5f).margin(0.005));
    REQUIRE(std::abs(door.Speed()) < 0.01f);
}

TEST_CASE("A negative motor force is treated as 0: the motor drives nothing", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door = MakeDoor(
        test, HingeJoint{.motor = {.mode = JointMotorMode::Velocity, .targetVelocity = 2.0f, .maxForce = -5.0f}});

    test.Run(1.0);

    INFO("the door stays shut and still; it is at " << door.Angle() << ", turning at " << door.Speed());
    REQUIRE(std::abs(door.Angle()) < 0.005f);
    REQUIRE(std::abs(door.Speed()) < 0.005f);
}

namespace
{
    /** An edit to a joint's settings, made to a joint of `kind`. */
    struct SettingEdit
    {
        const char* what;
        JointKind kind;
        void (*apply)(JointKind&);
    };

    const SettingEdit settingEdits[] = {
        {"enabling hinge limits", HingeJoint{},
         [](JointKind& kind) { kind.As<HingeJoint>().limits = {.enabled = true, .min = -1.0f, .max = 1.0f}; }},
        {"softening hinge limits", HingeJoint{.limits = {.enabled = true, .min = -1.0f, .max = 1.0f}},
         [](JointKind& kind) { kind.As<HingeJoint>().limits.spring.frequency = 2.0f; }},
        {"changing a hinge motor's mode", HingeJoint{},
         [](JointKind& kind) { kind.As<HingeJoint>().motor.mode = JointMotorMode::Position; }},
        {"changing a hinge motor's target", HingeJoint{.motor = {.mode = JointMotorMode::Position}},
         [](JointKind& kind) { kind.As<HingeJoint>().motor.targetPosition = 0.5f; }},
        {"changing a hinge motor's force", HingeJoint{},
         [](JointKind& kind) { kind.As<HingeJoint>().motor.maxForce = 10.0f; }},
        {"changing a hinge motor's spring", HingeJoint{},
         [](JointKind& kind) { kind.As<HingeJoint>().motor.spring.damping = 0.5f; }},
        {"changing hinge friction", HingeJoint{}, [](JointKind& kind) { kind.As<HingeJoint>().friction = 1.0f; }},
        {"enabling slider limits", SliderJoint{},
         [](JointKind& kind) { kind.As<SliderJoint>().limits = {.enabled = true, .min = -1.0f, .max = 1.0f}; }},
        {"changing a slider motor's velocity", SliderJoint{.motor = {.mode = JointMotorMode::Velocity}},
         [](JointKind& kind) { kind.As<SliderJoint>().motor.targetVelocity = 0.1f; }},
        {"changing slider friction", SliderJoint{}, [](JointKind& kind) { kind.As<SliderJoint>().friction = 1.0f; }},
        {"softening a distance joint", DistanceJoint{.maxDistance = 0.0f},
         [](JointKind& kind) { kind.As<DistanceJoint>().spring.frequency = 2.0f; }},
        {"narrowing a swing cone", SwingTwistJoint{},
         [](JointKind& kind) { kind.As<SwingTwistJoint>().swingYLimit = 0.2f; }},
        {"narrowing a twist range", SwingTwistJoint{},
         [](JointKind& kind) { kind.As<SwingTwistJoint>().twistMax = 0.2f; }},
        {"changing swing-twist friction", SwingTwistJoint{},
         [](JointKind& kind) { kind.As<SwingTwistJoint>().friction = 1.0f; }},
        {"changing a swing-twist motor's target", SwingTwistJoint{.motor = {.mode = JointMotorMode::Position}},
         [](JointKind& kind) {
             kind.As<SwingTwistJoint>().motor.targetRotation = Math::AngleAxis(0.2f, Math::Vec3(0.0f, 1.0f, 0.0f));
         }},
        {"changing a swing-twist motor's torque", SwingTwistJoint{},
         [](JointKind& kind) { kind.As<SwingTwistJoint>().motor.maxTorque = 5.0f; }},
    };
} // namespace

TEST_CASE("Every limit, motor, spring and friction edit wakes both of a sleeping joint's bodies",
          "[physics][joints][joint-settings]")
{
    for (const SettingEdit& edit : settingEdits)
    {
        PhysicsApp test;
        // Two boxes at rest on the ground, side by side, joined where they meet.
        AddGround(test.Entities());
        Entity left = AddBody(test.Entities(), Math::Vec3(-0.5f, 0.5f, 0.0f));
        Entity right = AddBody(test.Entities(), Math::Vec3(0.5f, 0.5f, 0.0f));
        Join(right, left, right, JointFrame{Math::Vec3(0.0f, 0.5f, 0.0f)}, edit.kind);
        test.Run(3.0);
        REQUIRE(World(test).IsSleeping(left));
        REQUIRE(World(test).IsSleeping(right));

        edit.apply(right.GetComponent<Joint>().kind);
        test.app.RunFrame(tick);

        INFO(edit.what << " wakes both bodies");
        REQUIRE_FALSE(World(test).IsSleeping(left));
        REQUIRE_FALSE(World(test).IsSleeping(right));
    }
}

TEST_CASE("A motor edit reaches physics on the next tick and wakes a sleeping door",
          "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test);
    test.Run(3.0);
    REQUIRE(World(test).IsSleeping(door.door));

    door.Hinge().motor = {.mode = JointMotorMode::Velocity, .targetVelocity = 1.0f};
    test.app.RunFrame(tick);

    INFO("one tick after its motor was set, the door is awake and turns at nearly its 1 rad/s (a motor's first step "
         "starts cold); it turns at "
         << door.Speed());
    REQUIRE_FALSE(World(test).IsSleeping(door.door));
    REQUIRE(door.Speed() == Approx(1.0f).margin(0.1));
}

TEST_CASE("A limit edit reaches physics on the next tick", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Slide slide =
        MakeSlide(test, SliderJoint{.motor = {.mode = JointMotorMode::Velocity, .targetVelocity = 1.0f}});
    test.Run(0.5);
    REQUIRE(slide.Position() > 0.4f);

    // Its maximum moved to where the box is now: the motor's push is cut there from the next step.
    slide.Slider().limits = {.enabled = true, .min = -1.0f, .max = slide.Position()};
    const float limit = slide.Slider().limits.max;
    test.app.RunFrame(tick);

    INFO("one tick after the limit was set at " << limit << " m, the box has stopped there; it is at "
                                                << slide.Position() << ", sliding at " << slide.Speed());
    REQUIRE(slide.Position() == Approx(limit).margin(0.002));
    REQUIRE(std::abs(slide.Speed()) < 0.05f);
}

TEST_CASE("A swing-twist motor edit reaches physics on the next tick", "[physics][joints][joint-settings]")
{
    PhysicsApp test;
    const Limb limb = MakeLimb(test);
    test.Run(0.5);

    limb.SwingTwist().motor = {.mode = JointMotorMode::Velocity, .targetAngularVelocity = Math::Vec3(0.5f, 0.0f, 0.0f)};
    test.app.RunFrame(tick);

    const Math::Vec3 spin = limb.box.GetComponent<RigidBody>().angularVelocity;
    INFO("one tick after its motor was set, the box twists at 0.5 rad/s; it turns at " << spin.x << ", " << spin.y
                                                                                       << ", " << spin.z);
    REQUIRE(Math::Magnitude(spin - Math::Vec3(0.5f, 0.0f, 0.0f)) < 0.01f);
}
