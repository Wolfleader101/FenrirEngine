// Joint edge cases through the frame seam: inactive joints (naming something that is not a body of their scene),
// idle joints (whose bodies cannot move), motion-type changes, destroyed bodies and inactive scenes, observed in world
// transforms, warnings, joint state and debug lines as physics steps.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/DebugLines.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    const Math::Vec3 hook(0.0f, 10.0f, 0.0f);
    constexpr float linkHalf = 0.25f;

    PhysicsWorld& World(PhysicsApp& test)
    {
        return *PhysicsWorld::Of(test.app.GetActiveScene());
    }

    Joint& JointOf(const Entity& entity)
    {
        return entity.GetComponent<Joint>();
    }

    /** A ball joint on a new joint entity between `bodyA` and `bodyB` (empty: the world), its rest pose at `anchor`
     * where the bodies are now. */
    Entity Join(EntityList& entities, const Entity& bodyA, const Entity& bodyB, const Math::Vec3& anchor)
    {
        const JointFrames frames = FrameFromWorld(bodyA, bodyB, JointFrame{anchor});
        Entity joint = entities.CreateEntity();
        joint.AddComponent<Joint>(Joint{
            .bodyA = bodyA, .bodyB = bodyB, .frameA = frames.frameA, .frameB = frames.frameB, .kind = BallJoint{}});
        return joint;
    }

    /** `count` unit-mass links hanging straight down from `hook`, each joined to the one above by a ball joint on its
     * own joint entity, the top one to the world. */
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
            chain.joints.push_back(Join(entities, link, above, top));
            chain.links.push_back(link);
        }
        return chain;
    }

    /** Where link `i` of a chain hangs at rest. */
    float RestingY(std::size_t i)
    {
        return hook.y - linkHalf * static_cast<float>(2 * i + 1);
    }

    /** A static post at `hook` with a unit box hanging beneath it from a ball joint, the post as bodyA. */
    struct Hanging
    {
        Entity post;
        Entity box;
        Entity joint;
    };

    Hanging MakeHanging(PhysicsApp& test)
    {
        Hanging hanging;
        hanging.post = Place(test.Entities(), hook + Math::Vec3(0.0f, 0.5f, 0.0f));
        hanging.post.AddComponent<Collider>();
        hanging.box = AddBody(test.Entities(), hook - Math::Vec3(0.0f, 0.5f, 0.0f));
        hanging.joint = Join(test.Entities(), hanging.post, hanging.box, hook);
        return hanging;
    }
} // namespace

TEST_CASE("Each kind of inactive joint warns once when it goes inactive, and works again once its bodies are valid",
          "[physics][joints][inactive]")
{
    struct Case
    {
        const char* what;
        // Makes the joint inactive; the joint's last valid value is restored to make it valid again.
        std::function<void(PhysicsApp&, Hanging&)> invalidate;
    };
    const Case cases[] = {
        {"bodyA emptied", [](PhysicsApp&, Hanging& h) { JointOf(h.joint).bodyA = Entity(); }},
        {"bodyB the same as bodyA", [](PhysicsApp&, Hanging& h) { JointOf(h.joint).bodyB = h.post; }},
        {"bodyB an entity with no body",
         [](PhysicsApp& test, Hanging& h) { JointOf(h.joint).bodyB = test.Entities().CreateEntity(); }},
        {"bodyB a collider of a compound body",
         [](PhysicsApp& test, Hanging& h) {
             Entity child = Place(test.Entities(), Math::Vec3(0.0f));
             h.box.AddChild(child);
             child.AddComponent<Collider>(Collider{SphereShape{0.05f}});
             JointOf(h.joint).bodyB = child;
         }},
        {"bodyB a trigger's entity",
         [](PhysicsApp& test, Hanging& h) {
             Entity trigger = Place(test.Entities(), Math::Vec3(5.0f, 0.0f, 0.0f));
             trigger.AddComponent<Collider>();
             trigger.AddComponent<Trigger>();
             JointOf(h.joint).bodyB = trigger;
         }},
        {"bodyB a body in another scene",
         [](PhysicsApp& test, Hanging& h) {
             static int scenes = 0;
             const std::string name = "Other " + std::to_string(++scenes);
             Entity other = AddBody(test.app.CreateScene(name).GetEntityList(), Math::Vec3(0.0f));
             JointOf(h.joint).bodyB = other;
         }},
        {"bodyB's RigidBody and Collider removed",
         [](PhysicsApp&, Hanging& h) {
             h.box.RemoveComponent<RigidBody>();
             h.box.RemoveComponent<Collider>();
         }},
    };
    for (const Case& c : cases)
    {
        PhysicsApp test;
        Hanging hanging = MakeHanging(test);
        test.Run(0.5);
        const Joint valid = JointOf(hanging.joint);
        REQUIRE(test.warnings.empty());

        c.invalidate(test, hanging);
        test.Run(0.25);
        INFO(c.what << ": the joint gave one warning on going inactive, and no more while it stayed inactive");
        REQUIRE(test.warnings.size() == 1);
        INFO("the warning names the joint entity: " << test.warnings.front());
        REQUIRE(test.warnings.front().find("entity " + std::to_string(hanging.joint.GetId())) != std::string::npos);

        if (!hanging.box.HasComponent<RigidBody>())
        {
            hanging.box.AddComponent<RigidBody>();
            hanging.box.AddComponent<Collider>();
        }
        else
        {
            INFO("the box was let go, and fell; it is at y = " << PositionOf(hanging.box).y);
            REQUIRE(PositionOf(hanging.box).y < hook.y - 0.6f);
        }
        JointOf(hanging.joint) = valid;
        test.Run(3.0);
        INFO("valid again, the joint pulled the box back beneath the post, with no further warning; it is at y = "
             << PositionOf(hanging.box).y);
        REQUIRE(PositionOf(hanging.box).y == Approx(hook.y - 0.5f).margin(0.02));
        REQUIRE(test.warnings.size() == 1);

        c.invalidate(test, hanging);
        test.Run(tick);
        INFO("going inactive again warns again");
        REQUIRE(test.warnings.size() == 2);
    }
}

TEST_CASE("A body whose RigidBody is removed but whose Collider stays is a static body, and keeps its joints",
          "[physics][joints][inactive]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 2);
    test.Run(1.0);

    chain.links[0].RemoveComponent<RigidBody>();
    test.Run(2.0);

    INFO("no joint went inactive");
    REQUIRE(test.warnings.empty());
    INFO("the bottom link still hangs from the top one, now static, at y = " << RestingY(1) << "; it is at "
                                                                             << PositionOf(chain.links[1]).y);
    REQUIRE(PositionOf(chain.links[1]).y == Approx(RestingY(1)).margin(0.02));
}

TEST_CASE("A sleeping body whose joint goes inactive wakes and falls", "[physics][joints][inactive]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 2);
    test.Run(3.0);
    REQUIRE(World(test).IsSleeping(chain.links[1]));

    chain.links[0].RemoveComponent<RigidBody>();
    chain.links[0].RemoveComponent<Collider>();
    test.app.RunFrame(tick);

    INFO("the bottom link, its joint to the top one inactive, was woken");
    REQUIRE_FALSE(World(test).IsSleeping(chain.links[1]));
    test.Run(1.0);
    INFO("and fell; it is at y = " << PositionOf(chain.links[1]).y);
    REQUIRE(PositionOf(chain.links[1]).y < RestingY(1) - 3.0f);
    INFO("both joints naming the top link warned once: " << test.warnings.size());
    REQUIRE(test.warnings.size() == 2);
}

TEST_CASE("Destroying a jointed body mid-swing warns once for each joint naming it, and it never comes back",
          "[physics][joints][inactive]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 6);
    test.Run(0.5);
    World(test).AddImpulse(chain.links.back(), Math::Vec3(5.0f, 0.0f, 0.0f));
    test.Run(0.3);

    test.Entities().DestroyEntity(chain.links[2]);
    test.Run(0.5);

    INFO("nothing went wrong in the backend");
    REQUIRE(test.errors.empty());
    INFO("the joints above and below the destroyed link warned once each");
    REQUIRE(test.warnings.size() == 2);

    // New bodies, one of which may take the destroyed link's id under a newer version.
    for (int i = 0; i < 4; ++i)
    {
        AddBody(test.Entities(), Math::Vec3(10.0f + 2.0f * static_cast<float>(i), 0.0f, 0.0f));
    }
    test.Run(2.0);
    INFO("the joints naming the destroyed link stay inactive: no new body is it");
    REQUIRE(test.warnings.size() == 2);
    INFO("the links below it fell; the one beneath is at y = " << PositionOf(chain.links[3]).y);
    REQUIRE(PositionOf(chain.links[3]).y < hook.y - 10.0f);
    REQUIRE(test.errors.empty());
}

TEST_CASE("A joint that goes inactive reads a zeroed state", "[physics][joints][inactive][joint-state]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 2);
    test.Run(1.0);
    REQUIRE(World(test).GetJointState(chain.joints[1]).force != Math::Vec3(0.0f));

    test.Entities().DestroyEntity(chain.links[0]);
    test.Run(tick);

    const JointState state = World(test).GetJointState(chain.joints[1]);
    INFO("no load and no pose");
    REQUIRE(state.force == Math::Vec3(0.0f));
    REQUIRE(state.torque == Math::Vec3(0.0f));
    REQUIRE(state.position == Approx(0.0f).margin(0.0));
    REQUIRE(state.relative.position == Math::Vec3(0.0f));
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

TEST_CASE("An inactive joint excludes no collisions", "[physics][joints][inactive][collide]")
{
    PhysicsApp test;
    // A static post and a box overlapping it, held where they are by a fixed joint, which excludes their contact.
    Entity post = Place(test.Entities(), Math::Vec3(0.0f));
    post.AddComponent<Collider>();
    Entity box = AddBody(test.Entities(), Math::Vec3(0.8f, 0.0f, 0.0f));
    const JointFrames frames = FrameFromWorld(post, box, JointFrame{Math::Vec3(0.4f, 0.0f, 0.0f)});
    Entity joint = test.Entities().CreateEntity();
    joint.AddComponent<Joint>(
        Joint{.bodyA = post, .bodyB = box, .frameA = frames.frameA, .frameB = frames.frameB, .kind = FixedJoint{}});
    test.Run(0.2);
    REQUIRE_FALSE(Touching(test, box, post));

    JointOf(joint).bodyB = test.Entities().CreateEntity();
    test.app.RunFrame(tick);

    INFO("with the joint inactive, the box and the post it overlaps touch");
    REQUIRE(Touching(test, box, post));
}

namespace
{
    std::size_t CountColoured(std::span<const DrawnLine> lines, const Colour& colour)
    {
        return static_cast<std::size_t>(
            std::ranges::count_if(lines, [colour](const DrawnLine& line) { return SameColour(line.colour, colour); }));
    }

    /** The lines of `lines` that start within a millimetre of `from`. */
    std::vector<DrawnLine> LinesFrom(std::span<const DrawnLine> lines, const Math::Vec3& from)
    {
        std::vector<DrawnLine> found;
        std::ranges::copy_if(lines, std::back_inserter(found),
                             [&from](const DrawnLine& line) { return Math::Magnitude(line.from - from) < 1.0e-3f; });
        return found;
    }

    /** The middle of a link's top or bottom face, as of the last step: where the joint above or below it is. */
    Math::Vec3 FaceOf(const Entity& link, float side)
    {
        const Transform at = GetWorldTransform(link);
        return at.pos + Math::Rotate(at.rot, Math::Vec3(0.0f, side * linkHalf, 0.0f));
    }
} // namespace

TEST_CASE("An inactive joint draws only the frames on its surviving bodies, in grey",
          "[physics][joints][inactive][debug]")
{
    PhysicsApp test;
    const Chain chain = MakeChain(test.Entities(), 3);
    test.Run(0.5);
    World(test).SetDebugDraw(PhysicsDebugCategories::Joints);

    test.Entities().DestroyEntity(chain.links[1]);
    test.Run(0.2);

    const std::vector<DrawnLine> lines = DrawnLines(test.app);
    INFO("the top joint draws its two frames in colour, and each joint naming the destroyed link one frame in grey");
    REQUIRE(lines.size() == 12);
    REQUIRE(CountColoured(lines, PhysicsDebugColour::InactiveJoint) == 6);
    for (const auto& [where, at] : {std::pair("the top link's bottom", FaceOf(chain.links[0], -1.0f)),
                                    std::pair("the bottom link's top", FaceOf(chain.links[2], 1.0f))})
    {
        const std::vector<DrawnLine> frame = LinesFrom(lines, at);
        INFO("a grey frame is drawn on " << where << ", where the destroyed link was joined");
        REQUIRE(frame.size() == 3);
        for (const DrawnLine& line : frame)
        {
            REQUIRE(SameColour(line.colour, PhysicsDebugColour::InactiveJoint));
        }
    }
}

TEST_CASE("An idle joint, whose bodies cannot move, is silent, and holds once a body becomes dynamic",
          "[physics][joints][idle]")
{
    struct Case
    {
        const char* what;
        MotionType motionType;
        bool toPost;
    };
    const Case cases[] = {
        {"a kinematic box joined to a static post", MotionType::Kinematic, true},
        {"a static box joined to a static post", MotionType::Static, true},
        {"a static box joined to the world", MotionType::Static, false},
        {"a kinematic box joined to the world", MotionType::Kinematic, false},
    };
    for (const Case& c : cases)
    {
        PhysicsApp test;
        Entity post = Place(test.Entities(), hook + Math::Vec3(0.0f, 0.5f, 0.0f));
        post.AddComponent<Collider>();
        Entity box = AddBody(test.Entities(), hook - Math::Vec3(0.0f, 0.5f, 0.0f), BoxShape{}, c.motionType);
        Join(test.Entities(), box, c.toPost ? post : Entity(), hook);
        test.Run(1.0);
        INFO(c.what << ": nothing moved, and nothing was logged");
        REQUIRE(test.warnings.empty());
        REQUIRE(test.errors.empty());
        REQUIRE(PositionOf(box).y == Approx(hook.y - 0.5f).margin(1e-4));

        box.GetComponent<RigidBody>().motionType = MotionType::Dynamic;
        test.Run(2.0);
        INFO("made dynamic, the box hangs from its joint; it is at y = " << PositionOf(box).y);
        REQUIRE(PositionOf(box).y == Approx(hook.y - 0.5f).margin(0.01));
        REQUIRE(test.warnings.empty());
    }
}

TEST_CASE("A kinematic end drags its dynamic partner without being pushed, and motion-type switches keep the joint",
          "[physics][joints][idle]")
{
    PhysicsApp test;
    Entity post = AddBody(test.Entities(), hook + Math::Vec3(0.0f, 0.5f, 0.0f), BoxShape{}, MotionType::Kinematic);
    Entity box = AddBody(test.Entities(), hook - Math::Vec3(0.0f, 0.5f, 0.0f));
    Join(test.Entities(), post, box, hook);
    test.Run(0.5);

    // The post moves 2 m along x over a second.
    for (int i = 1; i <= 60; ++i)
    {
        const float x = 2.0f * static_cast<float>(i) / 60.0f;
        post.GetComponent<Transform>().pos.x = x;
        test.app.RunFrame(tick);
        INFO("the post is never pushed: it is where it was moved to, x = " << x << "; it is at " << PositionOf(post).x);
        REQUIRE(PositionOf(post).x == Approx(x).margin(1e-4));
        REQUIRE(PositionOf(post).y == Approx(hook.y + 0.5f).margin(1e-4));
    }
    // The hook is the middle of the post's bottom face; the box's centre swings half a unit below it.
    const auto fromHook = [&post, &box] {
        return Math::Magnitude(PositionOf(box) - (PositionOf(post) - Math::Vec3(0.0f, 0.5f, 0.0f)));
    };
    INFO("the box was dragged along, and swings from the post; it is at x = " << PositionOf(box).x << ", " << fromHook()
                                                                              << " from the hook");
    REQUIRE(PositionOf(box).x > 1.0f);
    REQUIRE(fromHook() == Approx(0.5f).margin(0.02));

    box.GetComponent<RigidBody>().motionType = MotionType::Kinematic;
    test.Run(0.5);
    box.GetComponent<RigidBody>().motionType = MotionType::Dynamic;
    post.GetComponent<RigidBody>().motionType = MotionType::Static;
    test.Run(1.0);
    post.GetComponent<RigidBody>().motionType = MotionType::Kinematic;
    test.Run(1.0);
    INFO("after switching both bodies' motion types, the box still swings from the post, " << fromHook()
                                                                                           << " from the hook");
    REQUIRE(fromHook() == Approx(0.5f).margin(0.02));
    INFO("and nothing was logged");
    REQUIRE(test.warnings.empty());
    REQUIRE(test.errors.empty());
}

TEST_CASE("An inactive scene's joints do nothing until it resumes, then take the net effect of its changes",
          "[physics][joints][inactive][worlds]")
{
    PhysicsApp test;
    const std::string first = test.app.GetActiveScene().GetName();
    EntityList& entities = test.Entities();
    Chain chain = MakeChain(entities, 4);
    test.Run(1.0);
    const float paused = PositionOf(chain.links[3]).y;

    test.app.CreateScene("Other");
    test.app.ChangeActiveScene("Other");
    // Link 1 rebuilt with no change, and link 2 destroyed, while the scene is inactive.
    chain.links[1].RemoveComponent<RigidBody>();
    chain.links[1].AddComponent<RigidBody>();
    entities.DestroyEntity(chain.links[2]);
    test.Run(1.0);
    INFO("while the scene is inactive, nothing moves and nothing is logged");
    REQUIRE(PositionOf(chain.links[3]).y == Approx(paused).margin(0.0));
    REQUIRE(test.warnings.empty());

    test.app.ChangeActiveScene(first);
    test.Run(2.0);
    INFO("on resume, the two joints naming link 2 went inactive, with one warning each");
    REQUIRE(test.warnings.size() == 2);
    INFO("the bottom link fell; it is at y = " << PositionOf(chain.links[3]).y);
    REQUIRE(PositionOf(chain.links[3]).y < paused - 5.0f);
    INFO("link 1, rebuilt, still hangs from link 0; it is at y = " << PositionOf(chain.links[1]).y);
    REQUIRE(PositionOf(chain.links[1]).y == Approx(RestingY(1)).margin(0.02));
    REQUIRE(test.errors.empty());
}
