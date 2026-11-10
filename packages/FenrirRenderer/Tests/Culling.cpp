// The CPU half of culling on the null backend through App::RunFrame (ADR-0057, culling ticket 02): transparent items
// outside a camera's frustum are not drawn while opaque ones still are, each draw record carries its item's box, an
// InstancedMeshRenderer's world bounds are the box around every copy, and GetFrustum is the camera's planes. The camera
// is RendererTestApp's: at (0, 0, 5) looking along -Z, a 60-degree perspective with a near plane at 0.1, over the null
// surface's 1280 x 720.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/InstanceListData.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirMath/Frustum.hpp"
#include "RendererTestApp.hpp"

using Fenrir::Math::AABB;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using RendererTest::RecordedDraw;
using RendererTest::RendererTestApp;

namespace
{
    constexpr std::size_t RecordSize = 192;
    constexpr std::size_t BoundsOffset = 160;

    /** @brief A material on builtin:lit with `mode`. */
    Fenrir::AssetHandle<Fenrir::Material> Blended(RendererTestApp& test, Fenrir::BlendMode mode)
    {
        const auto material = test.Material();
        test.Store().SetBlendMode(material, mode);
        return material;
    }

    /** @brief A unit cube drawn with `material` at `position`, scaled by `scale`. */
    Fenrir::Entity AddCubeAt(RendererTestApp& test, Vec3 position, Fenrir::AssetHandle<Fenrir::Material> material,
                             float scale = 1.0f)
    {
        Fenrir::Entity entity = test.AddMeshRenderer(test.Mesh(), material);
        entity.GetComponent<Fenrir::Transform>().pos = position;
        entity.GetComponent<Fenrir::Transform>().scale = Vec3(scale);
        return entity;
    }

    /** @brief `count` copies along +X, a unit apart. */
    Fenrir::InstanceListData Row(std::size_t count)
    {
        Fenrir::InstanceListData list;
        for (std::size_t i = 0; i < count; ++i)
        {
            list.transforms.push_back(
                Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), {static_cast<float>(i), 0.0f, 0.0f}));
        }
        return list;
    }

    /** @brief The box in a draw's record of the draw buffer (set 3 binding 0, `Fenrir/Draw.glsl`'s boundsMin and max).
     */
    AABB RecordBounds(const RendererTestApp& test, const RecordedDraw& draw)
    {
        for (const Fenrir::Graphics::BindingGroupEntry& entry :
             Fenrir::RendererProbe::Entries(test.Device(), draw.groups.at(3)))
        {
            if (entry.binding == 0)
            {
                const std::vector<std::byte> bytes = Fenrir::RendererProbe::Bytes(test.Device(), entry.buffer);
                const std::size_t at = std::size_t{draw.draw.firstInstance} * RecordSize + BoundsOffset;
                return {{RendererTest::FloatAt(bytes, at), RendererTest::FloatAt(bytes, at + 4),
                         RendererTest::FloatAt(bytes, at + 8)},
                        {RendererTest::FloatAt(bytes, at + 16), RendererTest::FloatAt(bytes, at + 20),
                         RendererTest::FloatAt(bytes, at + 24)}};
            }
        }
        FAIL("the draw bound no draw buffer at set 3 binding 0");
        return {};
    }

    bool Near(const Vec3& a, const Vec3& b, float tolerance = 1e-4f)
    {
        return std::abs(a.x - b.x) < tolerance && std::abs(a.y - b.y) < tolerance && std::abs(a.z - b.z) < tolerance;
    }

    bool Near(const Vec4& a, const Vec4& b)
    {
        return Near(Vec3(a), Vec3(b)) && std::abs(a.w - b.w) < 1e-4f;
    }

    /** @brief Whether `inner` lies within `outer`. */
    bool Encloses(const AABB& outer, const AABB& inner)
    {
        return inner.min.x >= outer.min.x - 1e-4f && inner.min.y >= outer.min.y - 1e-4f &&
               inner.min.z >= outer.min.z - 1e-4f && inner.max.x <= outer.max.x + 1e-4f &&
               inner.max.y <= outer.max.y + 1e-4f && inner.max.z <= outer.max.z + 1e-4f;
    }
} // namespace

TEST_CASE("A transparent item outside the camera's frustum is not drawn, while one straddling its edge and every "
          "opaque item are",
          "[renderer][culling]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto blended = Blended(test, Fenrir::BlendMode::AlphaBlend);
    const auto opaque = Blended(test, Fenrir::BlendMode::Opaque);
    // At depth 5 (z = 0) the view is 5 tan 30 degrees times 16 / 9, 5.13, from the axis to its right edge: a cube at
    // x = 5.3 spans 4.8 to 5.8 across it, one at x = 7 is wholly past it, and one at z = 10 is behind the camera.
    for (const auto material : {blended, opaque})
    {
        AddCubeAt(test, {0.0f, 0.0f, 10.0f}, material);
        AddCubeAt(test, {7.0f, 0.0f, 0.0f}, material);
        AddCubeAt(test, {5.3f, 0.0f, 0.0f}, material);
        AddCubeAt(test, {0.0f, 0.0f, 0.0f}, material);
    }

    test.Frame();
    const std::vector<RecordedDraw> transparent = test.Draws("Transparent");
    INFO("Transparent draws the straddling cube and the one in the middle, not the two outside");
    REQUIRE(transparent.size() == 2);
    for (const RecordedDraw& draw : transparent)
    {
        const Vec3 position = test.DrawPosition(draw);
        INFO("the transparent item drawn at (" << position.x << ", " << position.y << ", " << position.z
                                               << ") is inside the view");
        REQUIRE((Near(position, {5.3f, 0.0f, 0.0f}) || Near(position, Vec3(0.0f))));
    }
    INFO("Opaque draws all four opaque cubes: nothing culls them on the CPU");
    std::uint32_t opaqueInstances = 0;
    for (const RecordedDraw& draw : test.Draws("Opaque"))
    {
        opaqueInstances += draw.draw.instanceCount;
    }
    REQUIRE(opaqueInstances == 4);
    INFO("the stats count the four opaque items and the two transparent ones drawn");
    REQUIRE(test.renderer.Stats().instances == 6);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Transparent items that survive the cull are still drawn back to front", "[renderer][culling]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto blended = Blended(test, Fenrir::BlendMode::AlphaBlend);
    AddCubeAt(test, {0.0f, 0.0f, 2.0f}, blended);
    AddCubeAt(test, {0.0f, 0.0f, 10.0f}, blended);
    AddCubeAt(test, {0.0f, 0.0f, -4.0f}, blended);
    AddCubeAt(test, {0.0f, 0.0f, -1.0f}, blended);

    test.Frame();
    const std::vector<RecordedDraw> transparent = test.Draws("Transparent");
    REQUIRE(transparent.size() == 3);
    INFO("the farthest first: z = -4, then -1, then 2");
    REQUIRE(Near(test.DrawPosition(transparent[0]), {0.0f, 0.0f, -4.0f}));
    REQUIRE(Near(test.DrawPosition(transparent[1]), {0.0f, 0.0f, -1.0f}));
    REQUIRE(Near(test.DrawPosition(transparent[2]), {0.0f, 0.0f, 2.0f}));
}

TEST_CASE("An item's draw record carries its mesh's box under its world matrix, and an instance list's its mesh's "
          "local box",
          "[renderer][culling]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto material = test.Material();
    AddCubeAt(test, {1.0f, 2.0f, 3.0f}, material, 2.0f);
    const auto list = test.Store().Add<Fenrir::InstanceList>(Row(3), Fenrir::AssetOwner::App());
    Fenrir::Entity instanced = test.ActiveScene().GetEntityList().CreateEntity();
    instanced.AddComponent<Fenrir::InstancedMeshRenderer>(
        Fenrir::InstancedMeshRenderer{test.Mesh(), material, list, true, true});
    instanced.GetComponent<Fenrir::Transform>().pos = {10.0f, 0.0f, 0.0f};

    test.Frame();
    const std::vector<RecordedDraw> draws = test.Draws("Opaque");
    REQUIRE(draws.size() == 2);
    for (const RecordedDraw& draw : draws)
    {
        const AABB bounds = RecordBounds(test, draw);
        if (draw.draw.instanceCount == 3)
        {
            INFO("the instance list's record holds the cube's own box, -0.5 to 0.5, not moved by the entity");
            REQUIRE(Near(bounds.min, Vec3(-0.5f)));
            REQUIRE(Near(bounds.max, Vec3(0.5f)));
        }
        else
        {
            INFO("the cube at (1, 2, 3) scaled by 2 has the box (0, 1, 2) to (2, 3, 4) in its record");
            REQUIRE(Near(bounds.min, {0.0f, 1.0f, 2.0f}));
            REQUIRE(Near(bounds.max, {2.0f, 3.0f, 4.0f}));
        }
    }
}

TEST_CASE("GetWorldBounds of an InstancedMeshRenderer encloses every copy under the moved entity, before and after "
          "its first frame",
          "[renderer][culling]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto mesh = test.Mesh();
    const auto list = test.Store().Add<Fenrir::InstanceList>(Row(3), Fenrir::AssetOwner::App());
    Fenrir::Entity entity = test.ActiveScene().GetEntityList().CreateEntity();
    entity.AddComponent<Fenrir::InstancedMeshRenderer>(
        Fenrir::InstancedMeshRenderer{mesh, test.Material(), list, true, true});
    entity.GetComponent<Fenrir::Transform>().pos = {10.0f, 0.0f, 0.0f};
    entity.GetComponent<Fenrir::Transform>().scale = Vec3(2.0f);

    const auto check = [&](const char* when) {
        const std::optional<AABB> bounds = test.renderer.GetWorldBounds(entity);
        INFO(when << ", the entity has world bounds");
        REQUIRE(bounds.has_value());
        INFO(when << ", three unit cubes a unit apart, scaled by 2 at x = 10, span (9, -1, -1) to (15, 1, 1)");
        REQUIRE(Near(bounds->min, {9.0f, -1.0f, -1.0f}));
        REQUIRE(Near(bounds->max, {15.0f, 1.0f, 1.0f}));
        const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(entity);
        for (const Fenrir::Math::Mat4& copy : test.Store().TryGet(list)->transforms)
        {
            INFO(when << ", the box encloses the copy's cube");
            REQUIRE(Encloses(*bounds, Fenrir::Math::Moved({Vec3(-0.5f), Vec3(0.5f)}, world * copy)));
        }
    };
    check("before the first frame");
    test.Frame();
    check("after the first frame");
    INFO("the static answers as the owner does");
    REQUIRE(Fenrir::Renderer::GetWorldBounds(test.app, entity).has_value());
}

TEST_CASE("GetFrustum is the camera's five planes under its infinite far plane", "[renderer][culling]")
{
    RendererTestApp test;
    const Fenrir::Entity camera = test.AddCamera();
    test.Frame();
    const std::optional<Fenrir::Math::Frustum> frustum = test.renderer.GetFrustum(camera);
    REQUIRE(frustum.has_value());
    INFO("a perspective camera with an infinite far plane has five planes");
    REQUIRE(frustum->Planes().size() == 5);

    // The camera is at (0, 0, 5): its near plane is at z = 4.9; its top plane passes through it 30 degrees above -Z,
    // and its left plane at the angle whose tangent is tan 30 degrees times 16 / 9.
    const float half = std::numbers::pi_v<float> / 6.0f;
    const float side = std::atan(std::tan(half) * 1280.0f / 720.0f);
    const Vec4 left(std::cos(side), 0.0f, -std::sin(side), 5.0f * std::sin(side));
    const Vec4 top(0.0f, -std::cos(half), -std::sin(half), 5.0f * std::sin(half));
    const Vec4 near(0.0f, 0.0f, -1.0f, 4.9f);
    INFO("the left plane leans in by the horizontal half-angle");
    REQUIRE(Near(frustum->Planes()[0], left));
    INFO("the top plane leans down by 30 degrees");
    REQUIRE(Near(frustum->Planes()[3], top));
    INFO("the near plane faces -Z at z = 4.9");
    REQUIRE(Near(frustum->Planes()[4], near));
    INFO("a point in front of the camera is inside, one behind it is not");
    REQUIRE(frustum->Intersects(Fenrir::Math::Sphere{{0.0f, 0.0f, 0.0f}, 0.5f}));
    REQUIRE_FALSE(frustum->Intersects(Fenrir::Math::Sphere{{0.0f, 0.0f, 10.0f}, 0.5f}));

    INFO("an entity with no Camera has no frustum, from the owner or the static");
    const Fenrir::Entity plain = test.ActiveScene().GetEntityList().CreateEntity();
    REQUIRE_FALSE(test.renderer.GetFrustum(plain).has_value());
    REQUIRE(Fenrir::Renderer::GetFrustum(test.app, camera).has_value());
}
