// Powered (ADR-0054): each joint whose child body is powered gets its motor in Position mode, aimed at the child bone's
// base-pose rotation relative to its parent bone in the joint's frames (a hinge's angle for elbows and knees), with
// the torque and spring the strength gives; Off again on any other mode, its other fields left. A powered ragdoll holds
// its pose against gravity at strength 1 and sags at 0, recovers from a shove, is drawn through the overrides as a
// simulating one is, and leaves the root undriven.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/WorldTransform.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirRagdoll/Ragdolls.hpp"
#include "RagdollTest.hpp"

using Fenrir::Entity;
using Fenrir::HingeJoint;
using Fenrir::JointMotorMode;
using Fenrir::RagdollBodyMode;
using Fenrir::RagdollMode;
using Fenrir::Skeletons;
using Fenrir::SwingTwistJoint;
using Fenrir::Math::Quat;
using Fenrir::Math::Vec3;
using RagdollTest::BodyOf;
using RagdollTest::Character;
using RagdollTest::Decomposed;
using RagdollTest::Near;
using RagdollTest::Text;

namespace
{
    constexpr float Degree = std::numbers::pi_v<float> / 180.0f;

    Fenrir::Joint& JointOf(const Entity& joint)
    {
        return joint.GetComponent<Fenrir::Joint>();
    }

    std::string NameOfJoint(const Entity& joint)
    {
        return Text(JointOf(joint).bodyB.GetComponent<Fenrir::RagdollBody>().bone);
    }

    JointMotorMode MotorMode(const Entity& joint)
    {
        const Fenrir::JointKind& kind = JointOf(joint).kind;
        return kind.Is<HingeJoint>() ? kind.As<HingeJoint>().motor.mode : kind.As<SwingTwistJoint>().motor.mode;
    }

    // Bends the base pose in Animate, each listed bone's local rotation set (the test rigs' rest rotations are the
    // identity): the pose a powered ragdoll drives towards.
    void Pose(Character& character, std::vector<std::pair<std::string, Quat>> bones)
    {
        std::vector<std::pair<std::int32_t, Quat>> indexed;
        for (const auto& [name, rotation] : bones)
        {
            const std::int32_t bone = character.Bone(name);
            INFO("the pose names a bone of the rig: " << name);
            REQUIRE(bone != Fenrir::NoBone);
            indexed.emplace_back(bone, rotation);
        }
        character.test.skeletons.AddAnimate("pose", 0, [indexed](Fenrir::AnimateContext& context) {
            for (const Entity& instance : context.Instances())
            {
                std::span<Fenrir::Transform> locals = Skeletons::EditBasePose(context.GetScene(), instance);
                for (const auto& [bone, rotation] : indexed)
                {
                    locals[static_cast<std::size_t>(bone)].rot = rotation;
                }
            }
        });
    }

    Quat About(float degrees, const Vec3& axis)
    {
        return Fenrir::Math::AngleAxis(degrees * Degree, axis);
    }

    // A bent pose within every joint's limits: the spine through its unmapped middle bone, the neck under the head
    // (unmapped too), a shoulder swung out, both elbows forward, a knee back, a hip forward and a wrist turned.
    void Bend(Character& character)
    {
        const Vec3 x(1.0f, 0.0f, 0.0f);
        const Vec3 y(0.0f, 1.0f, 0.0f);
        const Vec3 z(0.0f, 0.0f, 1.0f);
        Pose(character, {{"spine_01", About(8.0f, x)},
                         {"spine_02", About(10.0f, x)},
                         {"spine_03", About(-6.0f, y)},
                         {"neck_01", About(15.0f, y)},
                         {"Head", About(10.0f, x)},
                         {"upperarm_l", About(30.0f, z)},
                         {"lowerarm_l", About(-70.0f, x)},
                         {"lowerarm_r", About(-50.0f, x)},
                         {"hand_r", About(20.0f, z)},
                         {"thigh_r", About(-20.0f, x)},
                         {"calf_l", About(40.0f, x)}});
    }

    // The base pose's model matrix for `body`'s bone, in the simulation pose.
    Fenrir::Math::Mat4 BaseModel(Character& character, const Entity& body)
    {
        return *Skeletons::GetBaseModel(character.test.level, character.skeleton, character.BoneOf(body),
                                        Fenrir::PoseKind::Simulation);
    }

    // The base pose's model rotation for `body`'s bone × its offset: where the body would turn to in the base pose,
    // relative to the skeleton entity.
    Quat BaseBody(Character& character, const Entity& body)
    {
        return Decomposed(BaseModel(character, body) *
                          Fenrir::TransformToMat4(body.GetComponent<Fenrir::RagdollBody>().offset))
            .rot;
    }

    // frameB's rotation relative to frameA with both bodies turned as the base pose turns them: the swing-twist motor's
    // target, from both bones' model matrices.
    Quat ExpectedRotation(Character& character, const Fenrir::Joint& joint)
    {
        return Fenrir::Math::Normalized(Fenrir::Math::Conjugate(joint.frameA.rotation) *
                                        Fenrir::Math::Conjugate(BaseBody(character, joint.bodyA)) *
                                        BaseBody(character, joint.bodyB) * joint.frameB.rotation);
    }

    // The turn of `rotation` about X, in radians: a hinge's angle.
    float HingeAngle(Quat rotation)
    {
        rotation = rotation.w < 0.0f ? -rotation : rotation;
        return 2.0f * std::atan2(rotation.x, rotation.w);
    }

    // Degrees between two rotations.
    float AngleBetween(const Quat& a, const Quat& b)
    {
        const float dot = std::min(1.0f, std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w));
        return 2.0f * std::acos(dot) / Degree;
    }

    // How far, in degrees, the bodies have turned `joint`'s child bone relative to its parent bone, from where the base
    // pose turns it.
    float Deviation(Character& character, const Fenrir::Joint& joint)
    {
        const Quat parent = RagdollTest::BoneFromBody(joint.bodyA).rot;
        const Quat child = RagdollTest::BoneFromBody(joint.bodyB).rot;
        const Quat parentBase = Decomposed(BaseModel(character, joint.bodyA)).rot;
        const Quat childBase = Decomposed(BaseModel(character, joint.bodyB)).rot;
        return AngleBetween(Fenrir::Math::Conjugate(parent) * child, Fenrir::Math::Conjugate(parentBase) * childBase);
    }

    // The largest deviation over every joint of the ragdoll, and the joint's bone.
    std::pair<float, std::string> WorstDeviation(Character& character)
    {
        std::pair<float, std::string> worst{0.0f, ""};
        for (const Entity& joint : character.spawned.joints)
        {
            const float deviation = Deviation(character, JointOf(joint));
            if (deviation > worst.first)
            {
                worst = {deviation, NameOfJoint(joint)};
            }
        }
        return worst;
    }

    // The skeleton entity laid on its back on the ground, its head towards -Z: the humanoid faces up, its bodies just
    // clear of the floor. A ragdoll's root is undriven, so a powered one standing up would topple, and its ankles,
    // driven at 30 N m per kilogram of foot, cannot carry the body's weight on the foot's toe end.
    Fenrir::Transform OnItsBack()
    {
        return Fenrir::Transform(Vec3(0.0f, 0.11f, 0.85f), About(-90.0f, Vec3(1.0f, 0.0f, 0.0f)), Vec3(1.0f));
    }

    // Both arms raised 60 degrees forward at the shoulder and bent 45 degrees at the elbow: lying on its back, a pose
    // gravity pulls down towards the floor.
    void ArmsRaised(Character& character)
    {
        const Vec3 x(1.0f, 0.0f, 0.0f);
        Pose(character, {{"upperarm_l", About(-60.0f, x)},
                         {"upperarm_r", About(-60.0f, x)},
                         {"lowerarm_l", About(-45.0f, x)},
                         {"lowerarm_r", About(-45.0f, x)}});
    }

    // A character on its back with its arms raised, powered from the start at `strength`.
    void PoweredOnItsBack(Character& character, float strength)
    {
        ArmsRaised(character);
        character.test.Run(2);
        character.Ragdoll().mode = RagdollMode::Powered;
        character.Ragdoll().strength = strength;
    }
} // namespace

TEST_CASE("Powered aims each joint's motor at the base pose's child-relative rotation in the joint's frames",
          "[ragdoll][powered]")
{
    Character character(RagdollTest::Weightless());
    Bend(character);
    character.test.Run(2);
    for (const Entity& joint : character.spawned.joints)
    {
        INFO(NameOfJoint(joint) << "'s joint is off while following");
        REQUIRE(MotorMode(joint) == JointMotorMode::Off);
    }

    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(1);
    int hinges = 0;
    for (const Entity& joint : character.spawned.joints)
    {
        const Fenrir::Joint& settings = JointOf(joint);
        const Quat expected = ExpectedRotation(character, settings);
        INFO(NameOfJoint(joint) << "'s joint");
        REQUIRE(MotorMode(joint) == JointMotorMode::Position);
        if (settings.kind.Is<HingeJoint>())
        {
            ++hinges;
            const Fenrir::JointMotor& motor = settings.kind.As<HingeJoint>().motor;
            INFO("a hinge's target is the angle of the bone's turn about the hinge axis: " << motor.targetPosition);
            REQUIRE(Near(motor.targetPosition, HingeAngle(expected), 1e-4f));
            REQUIRE(motor.targetVelocity == 0.0f);
        }
        else
        {
            const Fenrir::SwingTwistMotor& motor = settings.kind.As<SwingTwistJoint>().motor;
            INFO("a swing-twist joint's target is frameB's rotation relative to frameA in the base pose");
            REQUIRE(Near(motor.targetRotation, expected, 1e-5f));
            REQUIRE(motor.targetAngularVelocity == Vec3(0.0f));
        }
    }
    {
        INFO("the elbows and knees are the hinges");
        REQUIRE(hinges == 4);
    }

    // The hinge angles are the elbows' and knee's bends, signed as each hinge's limits run.
    const auto hingeOf = [&character](const char* bone) {
        const Fenrir::Joint& joint = *RagdollTest::JointTo(character.spawned, BodyOf(character.spawned, bone));
        return std::pair(joint.kind.As<HingeJoint>().motor.targetPosition, joint.kind.As<HingeJoint>().limits);
    };
    for (const auto& [bone, bend] : {std::pair("lowerarm_l", 70.0f), std::pair("lowerarm_r", 50.0f),
                                     std::pair("calf_l", 40.0f), std::pair("calf_r", 0.0f)})
    {
        const auto [target, limits] = hingeOf(bone);
        INFO(bone << "'s hinge target " << target / Degree << " degrees, its limits " << limits.min / Degree << " to "
                  << limits.max / Degree);
        REQUIRE(Near(std::abs(target), bend * Degree, 1e-4f));
        REQUIRE(target >= limits.min - 1e-5f);
        REQUIRE(target <= limits.max + 1e-5f);
    }
}

TEST_CASE("At the rest pose every powered joint's target is its rest", "[ragdoll][powered]")
{
    Character character(RagdollTest::Weightless());
    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(1);
    for (const Entity& joint : character.spawned.joints)
    {
        const Fenrir::JointKind& kind = JointOf(joint).kind;
        INFO(NameOfJoint(joint) << "'s joint aims where its frames coincide, as the builder put them");
        if (kind.Is<HingeJoint>())
        {
            REQUIRE(Near(kind.As<HingeJoint>().motor.targetPosition, 0.0f, 1e-5f));
        }
        else
        {
            REQUIRE(Near(kind.As<SwingTwistJoint>().motor.targetRotation, Quat(1.0f, 0.0f, 0.0f, 0.0f), 1e-6f));
        }
    }
}

TEST_CASE("Every joint's motor is off again once its child is not powered", "[ragdoll][powered]")
{
    Character character(RagdollTest::Weightless());
    Bend(character);
    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(2);
    for (const RagdollMode mode : {RagdollMode::Simulating, RagdollMode::Following})
    {
        character.Ragdoll().mode = mode;
        character.test.Run(1);
        for (const Entity& joint : character.spawned.joints)
        {
            INFO(NameOfJoint(joint) << "'s joint is off again once the ragdoll is not powered");
            REQUIRE(MotorMode(joint) == JointMotorMode::Off);
        }
    }
}

TEST_CASE("A body's own Powered drives only its joint to its parent", "[ragdoll][powered]")
{
    Character character(RagdollTest::Weightless());
    Bend(character);
    const Entity forearm = BodyOf(character.spawned, "lowerarm_l");
    forearm.GetComponent<Fenrir::RagdollBody>().mode = RagdollBodyMode::Powered;
    character.test.Run(2);
    for (const Entity& joint : character.spawned.joints)
    {
        const bool driven = JointOf(joint).bodyB == forearm;
        INFO(NameOfJoint(joint) << "'s joint is " << (driven ? "driven" : "off"));
        REQUIRE(MotorMode(joint) == (driven ? JointMotorMode::Position : JointMotorMode::Off));
    }
    {
        const Fenrir::Joint& elbow = *RagdollTest::JointTo(character.spawned, forearm);
        INFO("aimed at the base pose's bend under a following upper arm");
        REQUIRE(Near(elbow.kind.As<HingeJoint>().motor.targetPosition, HingeAngle(ExpectedRotation(character, elbow)),
                     1e-4f));
    }

    forearm.GetComponent<Fenrir::RagdollBody>().mode = RagdollBodyMode::Inherit;
    character.test.Run(1);
    INFO("Inherit takes the ragdoll's Following: off again");
    REQUIRE(RagdollTest::JointTo(character.spawned, forearm)->kind.As<HingeJoint>().motor.mode == JointMotorMode::Off);
}

TEST_CASE("A powered body that is skipped has its joint turned off", "[ragdoll][powered]")
{
    Character character(RagdollTest::Weightless());
    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(1);
    const Entity forearm = BodyOf(character.spawned, "lowerarm_l");
    const Fenrir::Joint& elbow = *RagdollTest::JointTo(character.spawned, forearm);
    {
        INFO("powered, the elbow is driven");
        REQUIRE(elbow.kind.As<HingeJoint>().motor.mode == JointMotorMode::Position);
    }

    forearm.GetComponent<Fenrir::RagdollBody>().bone = RagdollTest::NameOf("no_such_bone");
    character.test.Run(1);
    INFO("a body whose bone resolves to nothing drives nothing: its joint's motor is off");
    REQUIRE(elbow.kind.As<HingeJoint>().motor.mode == JointMotorMode::Off);
}

TEST_CASE("A powered motor's torque and spring come from strength and strengthScale", "[ragdoll][powered]")
{
    Character character(RagdollTest::Weightless());
    character.Ragdoll().mode = RagdollMode::Powered;
    BodyOf(character.spawned, "spine_03").GetComponent<Fenrir::RagdollBody>().strengthScale = 0.5f;
    BodyOf(character.spawned, "calf_r").GetComponent<Fenrir::RagdollBody>().strengthScale = 0.25f;
    BodyOf(character.spawned, "foot_l").GetComponent<Fenrir::RagdollBody>().strengthScale = 0.0f;

    for (const float strength : {1.0f, 0.5f, 0.0f})
    {
        character.Ragdoll().strength = strength;
        character.test.Run(1);
        for (const Entity& joint : character.spawned.joints)
        {
            const Fenrir::Joint& settings = JointOf(joint);
            const Fenrir::RagdollBody& child = settings.bodyB.GetComponent<Fenrir::RagdollBody>();
            const float mass = settings.bodyB.GetComponent<Fenrir::RigidBody>().mass;
            const float torque = strength * child.strengthScale * 30.0f * mass;
            const float frequency = 1.0f + 9.0f * strength;
            INFO("strength " << strength << ", " << NameOfJoint(joint) << "'s joint (strength scale "
                             << child.strengthScale << ", child " << mass << " kg)");
            const Fenrir::JointSpring spring = settings.kind.Is<HingeJoint>()
                                                   ? settings.kind.As<HingeJoint>().motor.spring
                                                   : settings.kind.As<SwingTwistJoint>().motor.spring;
            const float limit = settings.kind.Is<HingeJoint>() ? settings.kind.As<HingeJoint>().motor.maxForce
                                                               : settings.kind.As<SwingTwistJoint>().motor.maxTorque;
            INFO("30 N m per kilogram of the child body at full strength: " << limit << ", wanted " << torque);
            REQUIRE(Near(limit, torque, 1e-4f * std::max(1.0f, torque)));
            INFO("the spring runs from 1 Hz at strength 0 to 10 Hz at 1, critically damped");
            REQUIRE(Near(spring.frequency, frequency, 1e-5f));
            REQUIRE(spring.damping == 1.0f);
            if (child.strengthScale == 0.0f)
            {
                INFO("a strength scale of 0 leaves the motor without torque");
                REQUIRE(limit == 0.0f);
            }
        }
    }
}

TEST_CASE("A joint's motor fields are game code's except while its child is powered", "[ragdoll][powered]")
{
    Character character(RagdollTest::Weightless());
    const Entity upperArm = BodyOf(character.spawned, "upperarm_l");
    Fenrir::Joint& shoulder = *RagdollTest::JointTo(character.spawned, upperArm);
    const Quat written = About(20.0f, Vec3(0.0f, 0.0f, 1.0f));
    {
        Fenrir::SwingTwistMotor& motor = shoulder.kind.As<SwingTwistJoint>().motor;
        motor.mode = JointMotorMode::Velocity;
        motor.targetRotation = written;
        motor.targetAngularVelocity = Vec3(0.0f, 1.0f, 0.0f);
        motor.maxTorque = 7.0f;
        motor.spring = {.frequency = 3.0f, .damping = 0.5f};
    }
    character.test.Run(1);
    {
        const Fenrir::SwingTwistMotor& motor = shoulder.kind.As<SwingTwistJoint>().motor;
        INFO("following: the mode is set Off and every other field left as written");
        REQUIRE(motor.mode == JointMotorMode::Off);
        REQUIRE(motor.targetRotation == written);
        REQUIRE(motor.targetAngularVelocity == Vec3(0.0f, 1.0f, 0.0f));
        REQUIRE(motor.maxTorque == 7.0f);
        REQUIRE(motor.spring.frequency == 3.0f);
        REQUIRE(motor.spring.damping == 0.5f);
    }

    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(1);
    const Fenrir::SwingTwistMotor powered = shoulder.kind.As<SwingTwistJoint>().motor;
    {
        INFO("powered: the target, torque and spring are the glue's");
        REQUIRE(powered.mode == JointMotorMode::Position);
        REQUIRE(Near(powered.targetRotation, ExpectedRotation(character, shoulder), 1e-5f));
        REQUIRE(powered.maxTorque != 7.0f);
        REQUIRE(powered.spring.frequency == 10.0f);
    }

    character.Ragdoll().mode = RagdollMode::Simulating;
    character.test.Run(1);
    {
        const Fenrir::SwingTwistMotor& motor = shoulder.kind.As<SwingTwistJoint>().motor;
        INFO("simulating: Off again, the rest left as the glue last wrote it");
        REQUIRE(motor.mode == JointMotorMode::Off);
        REQUIRE(motor.targetRotation == powered.targetRotation);
        REQUIRE(motor.maxTorque == powered.maxTorque);
        REQUIRE(motor.spring.frequency == powered.spring.frequency);
    }
}

TEST_CASE("A powered ragdoll holds its pose against gravity at strength 1 and sags at 0", "[ragdoll][powered]")
{
    for (const float strength : {1.0f, 0.0f})
    {
        Character character(RagdollTest::StepPosesOnly(), 0.0f, OnItsBack());
        PoweredOnItsBack(character, strength);
        character.test.Run(120);
        const auto [deviation, bone] = WorstDeviation(character);
        INFO("strength " << strength << ": after 120 ticks the worst joint, " << bone << "'s, is " << deviation
                         << " degrees from the base pose");
        if (strength == 1.0f)
        {
            // A motor is a spring: a held arm settles a few degrees under its weight (5.4 at the shoulder).
            REQUIRE(deviation < 8.0f);
        }
        else
        {
            REQUIRE(deviation > 30.0f);
        }
    }
}

TEST_CASE("A powered ragdoll's root is undriven", "[ragdoll][powered]")
{
    Character character(RagdollTest::Weightless());
    const Entity root = character.Ragdoll().rootBody;
    for (const Entity& joint : character.spawned.joints)
    {
        INFO("no joint of the ragdoll drives the root body");
        REQUIRE(JointOf(joint).bodyB != root);
    }
    // Game code's own joint to the root body, from a static post: not one of the ragdoll's joints, so left alone.
    Entity post = character.test.Entities().CreateEntity();
    post.GetComponent<Fenrir::Transform>().pos = Vec3(0.0f, 1.0f, -1.0f);
    post.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(0.1f)}});
    Entity tether = character.test.Entities().CreateEntity();
    SwingTwistJoint loose;
    loose.swingYLimit = std::numbers::pi_v<float>;
    loose.swingZLimit = std::numbers::pi_v<float>;
    loose.motor.mode = JointMotorMode::Velocity;
    loose.motor.maxTorque = 0.0f;
    const Fenrir::JointFrames frames =
        Fenrir::FrameFromWorld(post, root, Fenrir::JointFrame{Fenrir::GetWorldTransform(root).pos});
    tether.AddComponent<Fenrir::Joint>(
        Fenrir::Joint{.bodyA = post, .bodyB = root, .frameA = frames.frameA, .frameB = frames.frameB, .kind = loose});

    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(3);
    const Fenrir::SwingTwistMotor& motor = tether.GetComponent<Fenrir::Joint>().kind.As<SwingTwistJoint>().motor;
    INFO("game code's joint keeps its motor");
    REQUIRE(motor.mode == JointMotorMode::Velocity);
    REQUIRE(motor.maxTorque == 0.0f);
    REQUIRE(motor.targetRotation == Quat(1.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("A powered ragdoll recovers from a shove to its chest", "[ragdoll][powered]")
{
    // Weightless and a metre clear of the floor: the shove bends the joints, and nothing but the motors bends them
    // back (with gravity, the undriven root lets a shoved body roll over, and the floor holds limbs off their pose).
    Character character(RagdollTest::Weightless(), 0.0f,
                        Fenrir::Transform(Vec3(0.0f, 1.0f, 0.0f), Quat(1.0f, 0.0f, 0.0f, 0.0f), Vec3(1.0f)));
    Bend(character);
    character.test.Run(2);
    character.Ragdoll().mode = RagdollMode::Powered;
    character.test.Run(30);
    {
        const auto [before, bone] = WorstDeviation(character);
        INFO("powered, the ragdoll is in its base pose before the shove: " << bone << "'s joint is " << before
                                                                           << " degrees off");
        REQUIRE(before < 1.0f);
    }
    Fenrir::PhysicsWorld* world = Fenrir::PhysicsWorld::Of(character.test.level);
    INFO("the scene has a physics world to shove in");
    REQUIRE(world != nullptr);
    world->AddImpulse(BodyOf(character.spawned, "spine_03"), Vec3(40.0f, 0.0f, -40.0f));

    float worst = 0.0f;
    std::string worstBone;
    int recovered = -1;
    for (int tick = 1; tick <= 120 && recovered < 0; ++tick)
    {
        character.test.Run(1);
        const auto [deviation, bone] = WorstDeviation(character);
        if (deviation > worst)
        {
            worst = deviation;
            worstBone = bone;
        }
        if (worst > 5.0f && deviation < 2.0f)
        {
            recovered = tick;
        }
    }
    INFO("the shove bent " << worstBone << "'s joint " << worst << " degrees");
    REQUIRE(worst > 5.0f);
    INFO("and the motors brought every joint back within 2 degrees after " << recovered << " ticks, within 1.5 s");
    REQUIRE(recovered > 0);
    REQUIRE(recovered <= 90);
}

TEST_CASE("A powered ragdoll's bones are drawn where its bodies are, through the overrides", "[ragdoll][powered]")
{
    Character character(RagdollTest::StepPosesOnly(), 0.0f, OnItsBack());
    PoweredOnItsBack(character, 1.0f);
    for (int frame = 0; frame < 20; ++frame)
    {
        character.test.Run(1);
        for (const Entity& body : character.spawned.bodies)
        {
            const std::optional<Fenrir::Target> entry = character.OverrideOf(body);
            INFO("frame " << frame << ", " << Text(body.GetComponent<Fenrir::RagdollBody>().bone));
            REQUIRE(entry.has_value());
            REQUIRE(entry->space == Fenrir::PoseSpace::World);
            REQUIRE(entry->weight == 1.0f);
            const Fenrir::Transform expected = RagdollTest::BoneFromBody(body);
            REQUIRE(Near(entry->trs.pos, expected.pos, 1e-4f));
            REQUIRE(Near(entry->trs.rot, expected.rot, 1e-4f));
        }
    }
}
