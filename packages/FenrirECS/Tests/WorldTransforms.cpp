#include <catch2/catch_test_macros.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"

#include <cmath>
#include <string>

using Fenrir::Entity;
using Fenrir::EntityList;
using Fenrir::Transform;
namespace Math = Fenrir::Math;

namespace
{
    constexpr float Tolerance = 1e-5F;

    bool Near(float left, float right)
    {
        return std::abs(left - right) <= Tolerance;
    }

    bool Near(const Math::Vec3& left, const Math::Vec3& right)
    {
        return Near(left.x, right.x) && Near(left.y, right.y) && Near(left.z, right.z);
    }

    // The same rotation: q and -q are one rotation.
    bool SameRotation(const Math::Quat& left, const Math::Quat& right)
    {
        const float dot = left.w * right.w + left.x * right.x + left.y * right.y + left.z * right.z;
        return Near(std::abs(dot), 1.0F);
    }

    std::string Text(const Math::Vec3& vector)
    {
        return "(" + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ", " + std::to_string(vector.z) + ")";
    }

    Math::Quat AboutY(float degrees)
    {
        return Math::AngleAxis(Math::DegToRad(degrees), Math::Vec3(0.0F, 1.0F, 0.0F));
    }

    Math::Quat AboutZ(float degrees)
    {
        return Math::AngleAxis(Math::DegToRad(degrees), Math::Vec3(0.0F, 0.0F, 1.0F));
    }

    Entity Make(EntityList& entities, const Transform& local)
    {
        Entity entity = entities.CreateEntity();
        entity.GetComponent<Transform>() = local;
        return entity;
    }
} // namespace

TEST_CASE("an entity with no parent has its local transform as its world transform", "[world transform]")
{
    EntityList entities;
    const Transform local(Math::Vec3(1.0F, 2.0F, 3.0F), AboutY(30.0F), Math::Vec3(2.0F, 3.0F, 4.0F));
    const Entity entity = Make(entities, local);

    const Transform world = Fenrir::GetWorldTransform(entity);

    INFO("world position " + Text(world.pos));
    REQUIRE(Near(world.pos, local.pos));
    REQUIRE(SameRotation(world.rot, local.rot));
    REQUIRE(Near(world.scale, local.scale));
}

TEST_CASE("a rotated parent rotates its child's offset", "[world transform]")
{
    EntityList entities;
    Entity parent = Make(entities, Transform(Math::Vec3(10.0F, 0.0F, 0.0F), AboutY(90.0F), Math::Vec3(1.0F)));
    const Entity child = Make(entities, Transform(Math::Vec3(1.0F, 0.0F, 0.0F), AboutY(0.0F), Math::Vec3(1.0F)));
    parent.AddChild(child);

    const Transform world = Fenrir::GetWorldTransform(child);

    // A quarter turn about +Y takes +X to -Z.
    INFO("world position " + Text(world.pos));
    REQUIRE(Near(world.pos, Math::Vec3(10.0F, 0.0F, -1.0F)));
    REQUIRE(SameRotation(world.rot, AboutY(90.0F)));
}

TEST_CASE("a deep chain composes through every ancestor", "[world transform]")
{
    EntityList entities;
    Entity grandparent = Make(entities, Transform(Math::Vec3(0.0F, 5.0F, 0.0F), AboutZ(90.0F), Math::Vec3(1.0F)));
    Entity parent = Make(entities, Transform(Math::Vec3(1.0F, 0.0F, 0.0F), AboutZ(0.0F), Math::Vec3(2.0F)));
    const Entity child =
        Make(entities, Transform(Math::Vec3(1.0F, 0.0F, 0.0F), AboutZ(0.0F), Math::Vec3(1.0F, 1.0F, 3.0F)));
    grandparent.AddChild(parent);
    parent.AddChild(child);

    const Transform world = Fenrir::GetWorldTransform(child);

    // A quarter turn about +Z takes +X to +Y: the parent sits at (0, 6, 0), and the child's offset is doubled by the
    // parent's scale and turned by the grandparent's rotation.
    INFO("world position " + Text(world.pos) + ", scale " + Text(world.scale));
    REQUIRE(Near(world.pos, Math::Vec3(0.0F, 8.0F, 0.0F)));
    REQUIRE(SameRotation(world.rot, AboutZ(90.0F)));
    REQUIRE(Near(world.scale, Math::Vec3(2.0F, 2.0F, 6.0F)));
}

TEST_CASE("non-uniform scale composes component-wise", "[world transform]")
{
    EntityList entities;
    Entity parent = Make(entities, Transform(Math::Vec3(0.0F), AboutY(0.0F), Math::Vec3(2.0F, 3.0F, 4.0F)));
    const Entity child =
        Make(entities, Transform(Math::Vec3(1.0F, 1.0F, 1.0F), AboutY(0.0F), Math::Vec3(5.0F, 6.0F, 7.0F)));
    parent.AddChild(child);

    const Transform world = Fenrir::GetWorldTransform(child);

    INFO("world position " + Text(world.pos) + ", scale " + Text(world.scale));
    REQUIRE(Near(world.pos, Math::Vec3(2.0F, 3.0F, 4.0F)));
    REQUIRE(Near(world.scale, Math::Vec3(10.0F, 18.0F, 28.0F)));
}

TEST_CASE("the world matrix places points where the world transform does", "[world transform]")
{
    EntityList entities;
    Entity parent = Make(entities, Transform(Math::Vec3(10.0F, 0.0F, 0.0F), AboutY(90.0F), Math::Vec3(2.0F)));
    const Entity child =
        Make(entities, Transform(Math::Vec3(1.0F, 0.0F, 0.0F), AboutY(90.0F), Math::Vec3(1.0F, 3.0F, 1.0F)));
    parent.AddChild(child);

    const Math::Mat4 matrix = Fenrir::GetWorldMatrix(child);

    // The child's origin is 2 along the parent's turned +X, which is -Z; the child's own +X is turned twice (to -X) and
    // scaled by 2; its +Y is scaled by 2 * 3.
    const Math::Vec4 origin = matrix * Math::Vec4(0.0F, 0.0F, 0.0F, 1.0F);
    const Math::Vec4 alongX = matrix * Math::Vec4(1.0F, 0.0F, 0.0F, 1.0F);
    const Math::Vec4 alongY = matrix * Math::Vec4(0.0F, 1.0F, 0.0F, 1.0F);
    INFO("origin " + Text(Math::Vec3(origin)) + ", +X " + Text(Math::Vec3(alongX)) + ", +Y " +
         Text(Math::Vec3(alongY)));
    REQUIRE(Near(Math::Vec3(origin), Math::Vec3(10.0F, 0.0F, -2.0F)));
    REQUIRE(Near(Math::Vec3(alongX), Math::Vec3(8.0F, 0.0F, -2.0F)));
    REQUIRE(Near(Math::Vec3(alongY), Math::Vec3(10.0F, 6.0F, -2.0F)));

    INFO("with no shear, the matrix is the decomposed form's matrix");
    const Math::Mat4 decomposed = Fenrir::TransformToMat4(Fenrir::GetWorldTransform(child));
    for (int column = 0; column < 4; ++column)
    {
        REQUIRE(Near(Math::Vec3(matrix[column]), Math::Vec3(decomposed[column])));
        REQUIRE(Near(matrix[column].w, decomposed[column].w));
    }
}

TEST_CASE("under shear the matrix is exact and the decomposed form is approximate", "[world transform]")
{
    // A parent stretched along Y with a child turned 45 degrees about Z: the child's axes come out skewed, which
    // position, rotation and scale cannot hold. This is the documented case where they disagree.
    EntityList entities;
    Entity parent = Make(entities, Transform(Math::Vec3(0.0F), AboutZ(0.0F), Math::Vec3(1.0F, 2.0F, 1.0F)));
    const Entity child = Make(entities, Transform(Math::Vec3(0.0F), AboutZ(45.0F), Math::Vec3(1.0F)));
    parent.AddChild(child);

    const Math::Mat4 matrix = Fenrir::GetWorldMatrix(child);
    const Math::Vec4 alongX = matrix * Math::Vec4(1.0F, 0.0F, 0.0F, 1.0F);
    const Math::Vec4 alongY = matrix * Math::Vec4(0.0F, 1.0F, 0.0F, 1.0F);
    const float half = std::sqrt(0.5F);

    INFO("the matrix keeps the shear: +X goes to (h, 2h, 0) and +Y to (-h, 2h, 0)");
    REQUIRE(Near(Math::Vec3(alongX), Math::Vec3(half, 2.0F * half, 0.0F)));
    REQUIRE(Near(Math::Vec3(alongY), Math::Vec3(-half, 2.0F * half, 0.0F)));

    INFO("the decomposed form keeps the rotation and the component-wise scale, so it places +X elsewhere");
    const Transform world = Fenrir::GetWorldTransform(child);
    REQUIRE(Near(world.scale, Math::Vec3(1.0F, 2.0F, 1.0F)));
    const Math::Vec4 decomposedX = Fenrir::TransformToMat4(world) * Math::Vec4(1.0F, 0.0F, 0.0F, 1.0F);
    REQUIRE(!Near(Math::Vec3(decomposedX), Math::Vec3(alongX)));
}

TEST_CASE("setting a world transform under rotated, scaled parents reads back the same", "[world transform]")
{
    EntityList entities;
    Entity grandparent =
        Make(entities, Transform(Math::Vec3(3.0F, -1.0F, 2.0F), AboutY(30.0F), Math::Vec3(2.0F, 2.0F, 2.0F)));
    Entity parent =
        Make(entities, Transform(Math::Vec3(1.0F, 4.0F, 0.0F), AboutZ(-60.0F), Math::Vec3(1.0F, 3.0F, 0.5F)));
    const Entity child = Make(entities, Transform());
    grandparent.AddChild(parent);
    parent.AddChild(child);
    const Transform wanted(Math::Vec3(-5.0F, 7.0F, 1.5F), AboutY(-20.0F) * AboutZ(70.0F), Math::Vec3(4.0F, 1.0F, 2.0F));

    Fenrir::SetWorldTransform(child, wanted);
    const Transform world = Fenrir::GetWorldTransform(child);

    INFO("world position " + Text(world.pos) + ", scale " + Text(world.scale));
    REQUIRE(Near(world.pos, wanted.pos));
    REQUIRE(SameRotation(world.rot, wanted.rot));
    REQUIRE(Near(world.scale, wanted.scale));
}

TEST_CASE("setting the world transform of an entity with no parent sets its Transform", "[world transform]")
{
    EntityList entities;
    const Entity entity = Make(entities, Transform());
    const Transform wanted(Math::Vec3(1.0F, 2.0F, 3.0F), AboutZ(45.0F), Math::Vec3(2.0F, 1.0F, 1.0F));

    Fenrir::SetWorldTransform(entity, wanted);

    const Transform& local = entity.GetComponent<Transform>();
    REQUIRE(Near(local.pos, wanted.pos));
    REQUIRE(SameRotation(local.rot, wanted.rot));
    REQUIRE(Near(local.scale, wanted.scale));
}
