// Bodies in the entity hierarchy through the frame seam: compound bodies built from child colliders, and bodies under
// moved and rotated parents.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;
using Catch::Approx;

namespace
{
    /** A child of `parent` at local `position`, with a unit box collider. */
    Entity AddChildBox(EntityList& entities, Entity parent, const Math::Vec3& position)
    {
        Entity child = Place(entities, position);
        child.AddComponent<Collider>(Collider{BoxShape{Math::Vec3(0.5f)}});
        parent.AddChild(child);
        return child;
    }
} // namespace

TEST_CASE("Colliders on two child entities form one compound body with their parent's RigidBody",
          "[physics][hierarchy]")
{
    PhysicsApp test;
    AddGround(test.Entities());
    Entity body = Place(test.Entities(), Math::Vec3(0.0f, 4.0f, 0.0f));
    body.AddComponent<RigidBody>(RigidBody{.motionType = MotionType::Dynamic});
    Entity left = AddChildBox(test.Entities(), body, Math::Vec3(-1.0f, 0.0f, 0.0f));
    Entity right = AddChildBox(test.Entities(), body, Math::Vec3(1.0f, 0.0f, 0.0f));

    test.Run(3.0);

    INFO("the parent, which has no collider of its own, fell with its children's boxes and rests on them, half a unit "
         "above the ground; it is at "
         << PositionOf(body).y);
    REQUIRE(PositionOf(body).y == Approx(0.5f).margin(restSlop));
    INFO("the children kept their places beside it: one body, not three");
    REQUIRE(PositionOf(left).x == Approx(-1.0f).margin(1e-3));
    REQUIRE(PositionOf(right).x == Approx(1.0f).margin(1e-3));
    REQUIRE(PositionOf(left).y == Approx(0.5f).margin(restSlop));
    REQUIRE(PositionOf(right).y == Approx(0.5f).margin(restSlop));
}
