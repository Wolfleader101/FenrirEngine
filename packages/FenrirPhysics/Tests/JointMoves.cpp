// Moving and reshaping jointed bodies through the frame seam: teleports (a Transform written, or a body reparented),
// collider edits, compound children added, removed or moved, scale changes and rebuilds, observed in world transforms,
// velocities, joint state and Joint::enabled as physics steps.

#include <algorithm>
#include <functional>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Joints.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;

namespace
{
    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    Joint& JointOf(const Entity& entity)
    {
        return entity.GetComponent<Joint>();
    }

    /** How far apart a joint's two frames are, as of the last step: 0 while it holds. */
    float Gap(PhysicsApp& test, const Entity& joint)
    {
        return Math::Magnitude(World(test).GetJointState(joint).relative.position);
    }

    /** A door 1 wide, 2 tall and of mass 1 beside a static post, hinged on its left edge about world up, the post as
     * bodyA. The hinge is on the door's own entity, and breaks at about twice the door's weight and the torque it
     * hangs with. */
    struct Door
    {
        Entity post;
        Entity door;
    };

    Door MakeDoor(PhysicsApp& test)
    {
        Door made;
        made.post = Place(test.Entities(), Math::Vec3(-0.6f, 1.0f, 0.0f));
        made.post.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.05f, 1.0f, 0.05f)}});
        made.door = AddBody(test.Entities(), Math::Vec3(0.0f, 1.0f, 0.0f), BoxShape{Math::Vec3(0.5f, 1.0f, 0.05f)});
        const Math::Quat axisUp = Math::AngleAxis(std::numbers::pi_v<float> / 2.0f, Math::Vec3(0.0f, 0.0f, 1.0f));
        const JointFrames frames =
            FrameFromWorld(made.post, made.door, JointFrame{Math::Vec3(-0.5f, 1.0f, 0.0f), axisUp});
        made.door.AddComponent<Joint>(Joint{.bodyA = made.post,
                                            .bodyB = made.door,
                                            .frameA = frames.frameA,
                                            .frameB = frames.frameB,
                                            .kind = HingeJoint{},
                                            .breakForce = 20.0f,
                                            .breakTorque = 10.0f});
        return made;
    }

    /** What a door did over `frames` ticks: its fastest speed, and the first tick after which its hinge's frames were
     * within a centimetre (0 if never). Fails if the hinge breaks. */
    struct Return
    {
        float peakSpeed = 0.0f;
        int closed = 0;
    };

    Return Watch(PhysicsApp& test, const Door& door, int frames)
    {
        Return seen;
        for (int i = 1; i <= frames; ++i)
        {
            test.app.RunFrame(tick);
            INFO("the hinge never breaks: tick " << i << " after the teleport");
            REQUIRE(JointOf(door.door).enabled);
            const RigidBody& body = door.door.GetComponent<RigidBody>();
            seen.peakSpeed =
                std::max({seen.peakSpeed, Math::Magnitude(body.linearVelocity), Math::Magnitude(body.angularVelocity)});
            if (seen.closed == 0 && Gap(test, door.door) < 0.01f)
            {
                seen.closed = i;
            }
        }
        return seen;
    }
} // namespace

TEST_CASE("A jointed body teleported alone is pulled back without a jolt, and never breaks its joint",
          "[physics][joints][teleport]")
{
    struct Case
    {
        const char* what;
        std::function<void(PhysicsApp&, const Door&)> teleport;
    };
    const Case cases[] = {{"its Transform moved 0.7 m",
                           [](PhysicsApp&, const Door& d) { d.door.GetComponent<Transform>().pos.x += 0.7f; }},
                          {"its Transform moved and turned",
                           [](PhysicsApp&, const Door& d) {
                               Transform& at = d.door.GetComponent<Transform>();
                               at.pos += Math::Vec3(0.3f, 0.4f, -0.3f);
                               at.rot = Math::AngleAxis(0.6f, Math::Normalized(Math::Vec3(1.0f, 0.0f, 1.0f))) * at.rot;
                           }},
                          {"reparented under an entity 0.7 m away", [](PhysicsApp& test, const Door& d) {
                               Entity parent = Place(test.Entities(), Math::Vec3(0.7f, 0.0f, 0.0f));
                               parent.AddChild(d.door);
                           }}};
    for (const Case& c : cases)
    {
        PhysicsApp test;
        const Door door = MakeDoor(test);
        test.Run(1.0);
        INFO(c.what);
        REQUIRE(JointOf(door.door).enabled);
        REQUIRE(Gap(test, door.door) < 0.01f);

        c.teleport(test, door);
        const Return seen = Watch(test, door, 60);

        INFO("the door gained no speed: its fastest was " << seen.peakSpeed);
        REQUIRE(seen.peakSpeed < 0.01f);
        INFO("it was back on its hinge within half a second: after " << seen.closed << " ticks");
        REQUIRE(seen.closed > 0);
        REQUIRE(seen.closed <= 30);
        REQUIRE(test.warnings.empty());
        REQUIRE(test.errors.empty());
    }
}

TEST_CASE("A jointed body and its partner teleported together line up at once", "[physics][joints][teleport]")
{
    PhysicsApp test;
    const Door door = MakeDoor(test);
    test.Run(1.0);

    for (const Entity& moved : {door.post, door.door})
    {
        moved.GetComponent<Transform>().pos += Math::Vec3(3.0f, 0.5f, -2.0f);
    }
    const Return seen = Watch(test, door, 1);

    INFO("the hinge's frames are together after one tick: " << Gap(test, door.door) << " apart");
    REQUIRE(Gap(test, door.door) < 1.0e-3f);
    INFO("and the door gained no speed: " << seen.peakSpeed);
    REQUIRE(seen.peakSpeed < 0.01f);
}

namespace
{
    // Jolt's default gravity, which PhysicsConfig keeps.
    constexpr float g = 9.81f;
    constexpr float beamMass = 20.0f;

    /** A 20 kg beam 2 long sticking out sideways from a static post on a fixed joint. */
    struct Cantilever
    {
        Entity post;
        Entity beam;
        Entity joint;
    };

    Cantilever MakeCantilever(PhysicsApp& test)
    {
        Cantilever made;
        made.post = Place(test.Entities(), Math::Vec3(0.0f, 5.0f, 0.0f));
        made.post.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.1f)}});
        made.beam = AddBody(test.Entities(), Math::Vec3(1.1f, 5.0f, 0.0f), BoxShape{Math::Vec3(1.0f, 0.1f, 0.1f)});
        made.beam.GetComponent<RigidBody>().mass = beamMass;
        const JointFrames frames = FrameFromWorld(made.post, made.beam, JointFrame{Math::Vec3(0.1f, 5.0f, 0.0f)});
        made.joint = test.Entities().CreateEntity();
        made.joint.AddComponent<Joint>(Joint{.bodyA = made.post,
                                             .bodyB = made.beam,
                                             .frameA = frames.frameA,
                                             .frameB = frames.frameB,
                                             .kind = FixedJoint{}});
        return made;
    }
} // namespace

TEST_CASE("A teleport never breaks a joint, even one set to break just over the load it holds, and adds no velocity",
          "[physics][joints][teleport]")
{
    struct Case
    {
        const char* what;
        Math::Quat turn;
    };
    const Case cases[] = {
        {"moved and turned about world up", Math::AngleAxis(1.2f, Math::Vec3(0.0f, 1.0f, 0.0f))},
        {"moved and turned upside down", Math::AngleAxis(3.0f, Math::Vec3(1.0f, 0.0f, 0.0f))},
        {"moved and turned to hang down", Math::AngleAxis(-1.5f, Math::Vec3(0.0f, 0.0f, 1.0f))},
    };
    for (const Case& c : cases)
    {
        PhysicsApp test;
        const Cantilever cantilever = MakeCantilever(test);
        test.Run(1.0);
        // Once it has settled (a new joint overshoots in its first steps): 3 % over the weight it holds, and the
        // torque it holds it out with, a metre from its centre.
        const float weight = beamMass * g;
        JointOf(cantilever.joint).breakForce = 1.03f * weight;
        JointOf(cantilever.joint).breakTorque = 1.03f * weight * 1.0f;
        test.Run(0.5);
        INFO(c.what);
        REQUIRE(JointOf(cantilever.joint).enabled);

        Transform& at = cantilever.beam.GetComponent<Transform>();
        at.rot = c.turn * at.rot;
        at.pos += Math::Vec3(0.2f, 0.3f, 0.5f);
        float peakSpeed = 0.0f;
        for (int i = 1; i <= 60; ++i)
        {
            test.app.RunFrame(tick);
            INFO("the joint holds: tick " << i << " after the teleport, it read "
                                          << Math::Magnitude(World(test).GetJointState(cantilever.joint).force)
                                          << " N against its " << JointOf(cantilever.joint).breakForce
                                          << " N threshold");
            REQUIRE(JointOf(cantilever.joint).enabled);
            const RigidBody& body = cantilever.beam.GetComponent<RigidBody>();
            peakSpeed =
                std::max({peakSpeed, Math::Magnitude(body.linearVelocity), Math::Magnitude(body.angularVelocity)});
        }
        INFO("the beam gained no speed: its fastest was " << peakSpeed);
        REQUIRE(peakSpeed < 1.0e-3f);
        INFO("and it is back on the post: its frames are " << Gap(test, cantilever.joint) << " apart");
        REQUIRE(Gap(test, cantilever.joint) < 0.01f);
    }
}

namespace
{
    const Math::Vec3 hook(0.0f, 10.0f, 0.0f);
    constexpr float linkHalf = 0.25f;

    /** Two unit-mass links hanging from `hook`, each on a ball joint at its top: link 0 to the world (link 0 as
     * bodyA), link 1 to link 0 (link 0 as bodyB). */
    struct Chain
    {
        Entity links[2];
        Entity joints[2];
    };

    Chain MakeChain(EntityList& entities)
    {
        Chain chain;
        for (int i = 0; i < 2; ++i)
        {
            const Math::Vec3 top = hook - Math::Vec3(0.0f, 2.0f * linkHalf * static_cast<float>(i), 0.0f);
            chain.links[i] =
                AddBody(entities, top - Math::Vec3(0.0f, linkHalf, 0.0f), BoxShape{Math::Vec3(0.1f, linkHalf, 0.1f)});
            const Entity above = i == 0 ? Entity() : chain.links[0];
            const JointFrames frames = FrameFromWorld(chain.links[i], above, JointFrame{top});
            chain.joints[i] = entities.CreateEntity();
            chain.joints[i].AddComponent<Joint>(Joint{.bodyA = chain.links[i],
                                                      .bodyB = above,
                                                      .frameA = frames.frameA,
                                                      .frameB = frames.frameB,
                                                      .kind = BallJoint{}});
        }
        return chain;
    }

    /** A sphere collider on a new child of `body`, at `position` in its parent. */
    Entity AddLump(EntityList& entities, const Entity& body, const Math::Vec3& position, float radius)
    {
        Entity lump = Place(entities, position);
        lump.AddComponent<Collider>(Collider{SphereShape{radius}});
        body.AddChild(lump);
        return lump;
    }

    /** Where a point `local` to a body's origin is in the world, as of the last step: on a scaled body, `local` is
     * before scale. */
    Math::Vec3 OnBody(const Entity& body, const Math::Vec3& local)
    {
        return Math::MultiplyPoint(local, GetWorldMatrix(body));
    }
} // namespace

TEST_CASE("Reshaping or rescaling a jointed body keeps its joints' anchors where they are on it",
          "[physics][joints][shapes]")
{
    struct Case
    {
        const char* what;
        // Changes link 0, which has a lump on a child at (0.2, 0, 0); returns where its top, the world joint's
        // anchor, is now relative to its origin, before scale.
        std::function<Math::Vec3(PhysicsApp&, Chain&, Entity& lump)> change;
    };
    const Math::Vec3 top(0.0f, linkHalf, 0.0f);
    const Case cases[] = {
        {"its lump moved to the other side",
         [top](PhysicsApp&, Chain&, Entity& lump) {
             lump.GetComponent<Transform>().pos.x = -0.2f;
             return top;
         }},
        {"its lump's Collider made larger",
         [top](PhysicsApp&, Chain&, Entity& lump) {
             lump.GetComponent<Collider>().shape = SphereShape{0.15f};
             return top;
         }},
        {"its own Collider made longer",
         [](PhysicsApp&, Chain& chain, Entity&) {
             chain.links[0].GetComponent<Collider>().shape = BoxShape{Math::Vec3(0.1f, 0.4f, 0.1f)};
             return Math::Vec3(0.0f, linkHalf, 0.0f);
         }},
        {"a second lump added",
         [top](PhysicsApp& test, Chain& chain, Entity&) {
             AddLump(test.Entities(), chain.links[0], Math::Vec3(0.0f, -0.2f, 0.15f), 0.1f);
             return top;
         }},
        {"its lump removed",
         [top](PhysicsApp&, Chain&, Entity& lump) {
             lump.RemoveComponent<Collider>();
             return top;
         }},
        {"scaled to one and a half times its size",
         [top](PhysicsApp&, Chain& chain, Entity&) {
             chain.links[0].GetComponent<Transform>().scale = Math::Vec3(1.5f);
             return top;
         }},
        {"rebuilt, its RigidBody removed and added again",
         [top](PhysicsApp&, Chain& chain, Entity&) {
             chain.links[0].RemoveComponent<RigidBody>();
             chain.links[0].AddComponent<RigidBody>();
             return top;
         }},
    };
    for (const Case& c : cases)
    {
        PhysicsApp test;
        Chain chain = MakeChain(test.Entities());
        Entity lump = AddLump(test.Entities(), chain.links[0], Math::Vec3(0.2f, 0.0f, 0.0f), 0.1f);
        test.Run(2.0);
        INFO(c.what);
        REQUIRE(Gap(test, chain.joints[0]) < 0.01f);
        REQUIRE(Gap(test, chain.joints[1]) < 0.01f);

        const Math::Vec3 anchor = c.change(test, chain, lump);
        test.Run(3.0);

        INFO("both joints still hold, their frames together: " << Gap(test, chain.joints[0]) << " and "
                                                               << Gap(test, chain.joints[1]) << " apart");
        REQUIRE(Gap(test, chain.joints[0]) < 0.01f);
        REQUIRE(Gap(test, chain.joints[1]) < 0.01f);
        const Math::Vec3 at = OnBody(chain.links[0], anchor);
        INFO("link 0 hangs from the hook by its top: its top is at (" << at.x << ", " << at.y << ", " << at.z << ")");
        REQUIRE(Math::Magnitude(at - hook) < 0.01f);
        const Math::Vec3 below = OnBody(chain.links[0], -anchor);
        const Math::Vec3 link1Top = OnBody(chain.links[1], Math::Vec3(0.0f, linkHalf, 0.0f));
        INFO("link 1 hangs from link 0's bottom: they are " << Math::Magnitude(link1Top - below) << " apart");
        REQUIRE(Math::Magnitude(link1Top - below) < 0.01f);
        REQUIRE(test.warnings.empty());
        REQUIRE(test.errors.empty());
    }
}
