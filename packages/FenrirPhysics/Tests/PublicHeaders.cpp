// FenrirPhysics hides its physics backend at compile time (ADR-0010). This file includes the public headers before
// anything else, so if one of them ever reaches for Jolt, or Jolt's include path or definitions reach a user of
// FenrirPhysics (this test target is one), the build fails here.
#include "FenrirPhysics/CollisionLayers.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/DebugLines.hpp"
#include "FenrirPhysics/Events.hpp"
#include "FenrirPhysics/Joints.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirPhysics/PhysicsWorld.hpp"
#include "FenrirPhysics/Queries.hpp"

#ifdef JPH_VERSION_MAJOR
#error "A FenrirPhysics public header includes Jolt."
#endif

#if __has_include(<Jolt/Jolt.h>)
#error "Jolt's include path reaches a target outside FenrirPhysics: it must be linked PRIVATE."
#endif

#if defined(JPH_DEBUG_RENDERER) || defined(JPH_PROFILE_ENABLED) || defined(JPH_FLOATING_POINT_EXCEPTIONS_ENABLED) || \
    defined(JPH_USE_AVX2) || defined(JPH_OBJECT_STREAM)
#error "Jolt's compile definitions reach a target outside FenrirPhysics: it must be linked PRIVATE."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The public headers compile without the physics backend", "[physics][seam]")
{
    Fenrir::Collider collider;

    INFO("a collider made with no arguments holds a box");
    REQUIRE(collider.shape.Is<Fenrir::BoxShape>());
}

TEST_CASE("A collision shape takes any kind by assignment and is read through a checked view", "[physics][shapes]")
{
    Fenrir::Collider collider;
    collider.shape = Fenrir::SphereShape{2.0f};

    INFO("assigning a sphere makes the shape a sphere");
    REQUIRE(collider.shape.Type() == Fenrir::ShapeType::Sphere);
    REQUIRE(collider.shape.Is<Fenrir::SphereShape>());
    REQUIRE_FALSE(collider.shape.Is<Fenrir::BoxShape>());
    INFO("its radius is the one assigned");
    REQUIRE(collider.shape.As<Fenrir::SphereShape>().radius == 2.0f);

    collider.shape = Fenrir::BoxShape{};
    collider.shape.As<Fenrir::BoxShape>().halfExtents.y = 3.0f;
    INFO("a box's dimensions are edited in place through the view");
    REQUIRE(collider.shape.As<Fenrir::BoxShape>().halfExtents.y == 3.0f);
}
