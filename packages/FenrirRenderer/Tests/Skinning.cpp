// Skinned drawing on the null backend (the spec's seam 1; ADR-0035, ADR-0041): SkinnedMesh, the palette buffer and
// the draw records, the skip cases and skinned bounds, with FenrirSkeleton installed as the skinning source.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Skinning.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssets/ShaderData.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirMath/Math.hpp"
#include "SkinningTestApp.hpp"

using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using RendererTest::SkinningTestApp;

namespace
{
    bool Near(Vec4 a, Vec4 b)
    {
        return glm::all(glm::lessThan(glm::abs(a - b), Vec4(1e-5f)));
    }

    bool Near(Vec3 a, Vec3 b)
    {
        return glm::all(glm::lessThan(glm::abs(a - b), Vec3(1e-5f)));
    }

    bool HasKey(const std::vector<std::string>& keys, const std::string& key)
    {
        return std::find(keys.begin(), keys.end(), key) != keys.end();
    }
} // namespace

TEST_CASE("A skinned item is drawn with its skeleton's world matrix, never its own entity's", "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    const RendererTest::SkinnedRig rig = test.AddRig({3.0f, 0.0f, 0.0f});
    rig.mesh.GetComponent<Fenrir::Transform>().pos = {100.0f, 0.0f, 0.0f};

    test.Frame();

    const auto draws = test.Draws();
    INFO("the skinned strip is drawn once");
    REQUIRE(draws.size() == 1);
    INFO("at the skeleton entity's place, not the mesh entity's");
    REQUIRE(Near(test.DrawPosition(draws[0]), {3.0f, 0.0f, 0.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Each skinned item's record names where its skeleton's palette starts, in bones", "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddRig({-2.0f, 0.0f, 0.0f});
    const RendererTest::SkinnedRig second = test.AddRig({2.0f, 0.0f, 0.0f});
    // The second rig's upper bone moved up a unit, so its palette tells the two apart.
    test.Frame();
    test.skeletons->EditBasePose(test.ActiveScene(), second.owner)[1].pos = {0.0f, 2.0f, 0.0f};
    test.Frame();

    const auto draws = test.Draws();
    REQUIRE(draws.size() == 2);
    std::set<std::uint32_t> offsets;
    for (const auto& draw : draws)
    {
        const RendererTest::DrawRecord record = test.Record(draw);
        offsets.insert(record.paletteOffset);
        const bool isSecond = record.world[3].x > 0.0f;
        INFO("bone 0 of each palette is the identity's rows");
        const RendererTest::PaletteRows lower = test.Palette(draw, record.paletteOffset);
        REQUIRE(Near(lower.r0, {1.0f, 0.0f, 0.0f, 0.0f}));
        REQUIRE(Near(lower.r1, {0.0f, 1.0f, 0.0f, 0.0f}));
        REQUIRE(Near(lower.r2, {0.0f, 0.0f, 1.0f, 0.0f}));
        INFO("bone 1's row 1 carries its translation in w: 0 at rest, 1 for the moved bone");
        const RendererTest::PaletteRows upper = test.Palette(draw, record.paletteOffset + 1);
        REQUIRE(Near(upper.r1, {0.0f, 1.0f, 0.0f, isSecond ? 1.0f : 0.0f}));
    }
    INFO("the two palettes sit one after the other: offsets 0 and 2");
    REQUIRE(offsets == std::set<std::uint32_t>{0, 2});
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Two meshes on one skeleton, seen by two cameras, share one palette range", "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddCamera({0.0f, 1.0f, 12.0f});
    const RendererTest::SkinnedRig rig = test.AddRig();
    test.AddSkinned(rig.owner, test.StripMesh(), test.Material());

    test.Frame();

    const auto draws = test.Draws();
    INFO("two meshes in each of two views");
    REQUIRE(draws.size() == 4);
    std::set<std::uint32_t> offsets;
    for (const auto& draw : draws)
    {
        offsets.insert(test.Record(draw).paletteOffset);
    }
    INFO("every draw reads the one palette range at 0");
    REQUIRE(offsets == std::set<std::uint32_t>{0});
}

TEST_CASE("Stats count one palette for two meshes and two cameras on one skeleton, and reset each frame",
          "[renderer][skinning][facade]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddCamera({0.0f, 1.0f, 12.0f});
    const RendererTest::SkinnedRig rig = test.AddRig();
    const Fenrir::Entity second = test.AddSkinned(rig.owner, test.StripMesh(), test.Material());
    test.AddMeshRenderer(test.Mesh(), test.Material());

    INFO("before any frame, every palette count is zero");
    REQUIRE(test.renderer.Stats().paletteBytesUploaded == 0);
    REQUIRE(test.renderer.Stats().distinctPalettesWritten == 0);
    REQUIRE(test.renderer.Stats().skinnedDraws == 0);

    // Twice, so a count kept across frames would show as doubled.
    for (int frame = 0; frame < 2; ++frame)
    {
        test.Frame();
        const Fenrir::RendererStats stats = test.renderer.Stats();
        INFO("frame " << frame);
        INFO("three items in each of two views, four of them skinned");
        REQUIRE(stats.draws == 6);
        REQUIRE(stats.skinnedDraws == 4);
        INFO("one palette for the skeleton, its two bones uploaded once");
        REQUIRE(stats.distinctPalettesWritten == 1);
        REQUIRE(stats.paletteBytesUploaded == 2 * sizeof(Fenrir::Render::GpuBone));
    }

    rig.mesh.GetComponent<Fenrir::MeshRenderer>().visible = false;
    second.GetComponent<Fenrir::MeshRenderer>().visible = false;
    test.Frame();
    const Fenrir::RendererStats hidden = test.renderer.Stats();
    INFO("with the skinned meshes hidden, the next frame writes no palette and draws only the rigid item");
    REQUIRE(hidden.draws == 2);
    REQUIRE(hidden.skinnedDraws == 0);
    REQUIRE(hidden.distinctPalettesWritten == 0);
    REQUIRE(hidden.paletteBytesUploaded == 0);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A skinned draw uses its shader's SKINNED variant, and a rigid one does not", "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddRig({-2.0f, 0.0f, 0.0f});
    test.AddMeshRenderer(test.Mesh(), test.Material()).GetComponent<Fenrir::Transform>().pos = {2.0f, 0.0f, 0.0f};

    test.Frame();

    const auto draws = test.Draws();
    REQUIRE(draws.size() == 2);
    for (const auto& draw : draws)
    {
        const bool skinnedItem = test.DrawPosition(draw).x < 0.0f;
        INFO("the strip on the skeleton draws with SKINNED, the cube without");
        REQUIRE(HasKey(Fenrir::RendererProbe::Keys(test.Device(), draw.pipeline), "SKINNED") == skinnedItem);
    }
}

TEST_CASE("Every skinned draw's palette range lies within the palette buffer", "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 30.0f});
    for (int i = 0; i < 9; ++i)
    {
        test.AddRig({static_cast<float>(i) - 4.0f, 0.0f, 0.0f});
    }

    test.Frame();
    test.Frame();

    const auto draws = test.Draws();
    REQUIRE(draws.size() == 9);
    for (const auto& draw : draws)
    {
        const std::size_t entries = test.BindingBytes(draw, 1).size() / sizeof(RendererTest::PaletteRows);
        INFO("the offset plus the strip's two bones stays inside the buffer");
        REQUIRE(test.Record(draw).paletteOffset + 2 <= entries);
    }
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A mesh with skin data and no SkinnedMesh is not drawn, logged once", "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    test.AddMeshRenderer(test.StripMesh(), test.Material());

    test.Frame();
    test.Frame();

    INFO("nothing is drawn, and one warning says why");
    REQUIRE(test.Draws().empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "has no SkinnedMesh") == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A SkinnedMesh on a mesh with no skin data is ignored: the mesh draws rigidly, logged once",
          "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    const Fenrir::Entity owner = test.AddSkeleton({5.0f, 0.0f, 0.0f});
    Fenrir::Entity cube = test.AddSkinned(owner, test.Mesh(), test.Material());
    cube.GetComponent<Fenrir::Transform>().pos = {-1.0f, 0.0f, 0.0f};

    test.Frame();
    test.Frame();

    const auto draws = test.Draws();
    INFO("the cube is drawn at its own place, not the skeleton's");
    REQUIRE(draws.size() == 1);
    REQUIRE(Near(test.DrawPosition(draws[0]), {-1.0f, 0.0f, 0.0f}));
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "drawn rigidly") == 1);
}

TEST_CASE("A SkinnedMesh naming a dead entity, one with no SkeletonPose or an Unavailable skeleton is not drawn, "
          "each logged once",
          "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    Fenrir::EntityList& entities = test.ActiveScene().GetEntityList();
    Fenrir::Entity dead = entities.CreateEntity();
    const Fenrir::Entity plain = entities.CreateEntity();
    Fenrir::Entity unavailable = entities.CreateEntity();
    unavailable.AddComponent<Fenrir::SkeletonPose>();
    test.AddSkinned(dead, test.StripMesh(), test.Material());
    test.AddSkinned(plain, test.StripMesh(), test.Material());
    test.AddSkinned(unavailable, test.StripMesh(), test.Material());
    entities.DestroyEntity(dead);

    test.Frame();
    test.Frame();
    test.Frame();

    INFO("none is drawn");
    REQUIRE(test.Draws().empty());
    INFO("one warning for each");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "skeleton entity that is not alive") == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "which has no SkeletonPose") == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "whose skeleton is Unavailable") == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A mesh needing more bones than its skeleton has is not drawn, logged once", "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    Fenrir::MeshData strip = RendererTest::Strip();
    strip.skin->boneCount = 3;
    test.AddSkinned(test.AddSkeleton(), test.Mesh(std::move(strip)), test.Material());

    test.Frame();
    test.Frame();

    REQUIRE(test.Draws().empty());
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "needs 3 bones") == 1);
}

TEST_CASE("Two skeletons with the same bone count but other bind signatures are told apart: the mesh baked for one "
          "is not drawn on the other, logged once",
          "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    // The same two bones, renamed: the same count, another signature.
    Fenrir::SkeletonData renamed = RendererTest::TwoBones();
    renamed.bones[0].sourceName = "hip";
    renamed.bones[1].sourceName = "spine";
    const auto other = test.Store().Add<Fenrir::Skeleton>(std::move(renamed), Fenrir::AssetOwner::App());
    Fenrir::MeshData baked = RendererTest::Strip();
    baked.skin->expectedSignature = test.Store().TryGet(test.Skeleton())->BindSignature();
    const auto mesh = test.Mesh(std::move(baked));

    test.AddSkinned(test.AddSkeleton({-2.0f, 0.0f, 0.0f}), mesh, test.Material());
    Fenrir::Entity wrong = test.ActiveScene().GetEntityList().CreateEntity();
    wrong.AddComponent<Fenrir::SkeletonPose>(Fenrir::SkeletonPose{other});
    test.AddSkinned(wrong, mesh, test.Material());

    test.Frame();
    test.Frame();

    const auto draws = test.Draws();
    INFO("only the mesh on the skeleton it was baked for is drawn");
    REQUIRE(draws.size() == 1);
    REQUIRE(Near(test.DrawPosition(draws[0]), {-2.0f, 0.0f, 0.0f}));
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "bind signature differs") == 1);
}

TEST_CASE("Without a skinning source, skinned meshes are skipped with one warning and rigid meshes still draw",
          "[renderer][skinning]")
{
    SkinningTestApp test(false);
    test.AddCamera({0.0f, 1.0f, 10.0f});
    const Fenrir::Entity owner = test.ActiveScene().GetEntityList().CreateEntity();
    test.AddSkinned(owner, test.StripMesh(), test.Material());
    test.AddSkinned(owner, test.StripMesh(), test.Material());
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    test.Frame();

    INFO("the cube is drawn, the two skinned strips are not");
    REQUIRE(test.Draws().size() == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "no skinning source") == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Rigid drawing is unchanged with no FenrirSkeleton installed", "[renderer][skinning]")
{
    SkinningTestApp test(false);
    test.AddCamera({0.0f, 0.0f, 10.0f});
    test.AddMeshRenderer(test.Mesh(), test.Material()).GetComponent<Fenrir::Transform>().pos = {1.0f, 2.0f, 3.0f};

    test.Frame();

    const auto draws = test.Draws();
    REQUIRE(draws.size() == 1);
    const RendererTest::DrawRecord record = test.Record(draws[0]);
    INFO("the cube's record holds its own world matrix, palette offset 0 and a positive sign");
    REQUIRE(Near(Vec3(record.world[3]), {1.0f, 2.0f, 3.0f}));
    REQUIRE(record.paletteOffset == 0);
    REQUIRE(record.worldSign == 1.0f);
    INFO("the variant drawn is the rigid one");
    REQUIRE_FALSE(HasKey(Fenrir::RendererProbe::Keys(test.Device(), draws[0].pipeline), "SKINNED"));
    INFO("nothing about skinning is logged");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "skin") == 0);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A draw record's sign is -1 for a mirroring world matrix", "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    const RendererTest::SkinnedRig rig = test.AddRig();
    rig.owner.GetComponent<Fenrir::Transform>().scale = {-1.0f, 1.0f, 1.0f};

    test.Frame();

    const auto draws = test.Draws();
    REQUIRE(draws.size() == 1);
    REQUIRE(test.Record(draws[0]).worldSign == -1.0f);
}

TEST_CASE("A shader without SKINNED draws a skinned mesh with the fallback material's SKINNED variant, logged once",
          "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    const Fenrir::Entity owner = test.AddSkeleton();
    // SampledPicture is built with no variant keys at all.
    test.AddSkinned(owner, test.StripMesh(), test.SampledMaterial({}));
    test.AddSkinned(owner, test.StripMesh(), test.SampledMaterial({}));

    test.Frame();
    test.Frame();

    const auto draws = test.Draws();
    REQUIRE(draws.size() == 2);
    for (const auto& draw : draws)
    {
        INFO("drawn by the fallback program, skinned, with no material group");
        REQUIRE(Fenrir::RendererProbe::Label(test.Device(), draw.pipeline) == "BuiltinFallback");
        REQUIRE(HasKey(Fenrir::RendererProbe::Keys(test.Device(), draw.pipeline), "SKINNED"));
        REQUIRE(draw.groups.at(2).IsEmpty());
    }
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "has no SKINNED variant") == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A transparent skinned item is sorted by the centre of its skinned bounds, not its skeleton's origin",
          "[renderer][skinning]")
{
    SkinningTestApp test;
    test.AddCamera({0.0f, 1.0f, 10.0f});
    const auto glass = test.Material();
    REQUIRE(test.Store().SetBlendMode(glass, Fenrir::BlendMode::AlphaBlend));
    // Far's skeleton entity is the farthest, at z = -10, but its root bone carries the whole strip 8 forward, to
    // z = -2; Near's stays at its skeleton entity, z = -5.
    const Fenrir::Entity far = test.AddSkeleton({0.0f, 0.0f, -10.0f});
    test.AddSkinned(far, test.StripMesh(), glass);
    const Fenrir::Entity near = test.AddSkeleton({0.5f, 0.0f, -5.0f});
    test.AddSkinned(near, test.StripMesh(), glass);
    test.Frame();
    test.skeletons->EditBasePose(test.ActiveScene(), far)[0].pos = {0.0f, 0.0f, 8.0f};

    test.Frame();

    const auto draws = test.Draws("Transparent");
    REQUIRE(draws.size() == 2);
    INFO("back to front by the bounds' centres: Near's (z = -5) before Far's (z = -2)");
    REQUIRE(test.Record(draws[0]).world[3].z == -5.0f);
    REQUIRE(test.Record(draws[1]).world[3].z == -10.0f);
}

TEST_CASE("The built-in programs' SKINNED variants read the palette at set 3, binding 1, in GpuBone's 48-byte layout",
          "[renderer][skinning]")
{
    SkinningTestApp test;
    for (const char* name : {"builtin:unlit", "builtin:lit", "builtin:fallback"})
    {
        const Fenrir::ShaderData* data = test.Store().TryGet(test.Builtin(name));
        REQUIRE(data != nullptr);
        const Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(data->bytes);
        REQUIRE(read.Ok());
        const std::array<std::string_view, 1> skinned{"SKINNED"};
        const Fenrir::ShaderVariant* variant = read.container->FindVariant(skinned);
        INFO(name << " has a SKINNED variant");
        REQUIRE(HasKey(read.container->keys, "SKINNED"));
        REQUIRE(variant != nullptr);
        const auto palette = std::find_if(variant->bindings.begin(), variant->bindings.end(),
                                          [](const Fenrir::ShaderBinding& b) { return b.set == 3 && b.binding == 1; });
        REQUIRE(palette != variant->bindings.end());
        INFO("a storage block whose reflected size is " << palette->size);
        REQUIRE(palette->kind == Fenrir::ShaderBindingKind::StorageBuffer);
        REQUIRE(palette->size == sizeof(Fenrir::Render::GpuBone));
    }
}

TEST_CASE("GetWorldBounds of a skinned mesh follows a moved bone and holds every skinned vertex",
          "[renderer][skinning]")
{
    SkinningTestApp test;
    const RendererTest::SkinnedRig rig = test.AddRig({3.0f, 0.0f, 0.0f});
    rig.mesh.GetComponent<Fenrir::Transform>().pos = {-50.0f, 0.0f, 0.0f};
    test.Frame();

    const auto rest = test.renderer.GetWorldBounds(rig.mesh);
    INFO("at rest: the strip, one wide and two tall, at the skeleton entity");
    REQUIRE(rest.has_value());
    REQUIRE(Near(rest->min, {2.5f, 0.0f, 0.0f}));
    REQUIRE(Near(rest->max, {3.5f, 2.0f, 0.0f}));

    // The upper bone turned 90 degrees about +Z: the top row, (+-0.5, 2) in bind space, swings to (-1, 1 -+ 0.5).
    test.skeletons->EditBasePose(test.ActiveScene(), rig.owner)[1].rot =
        Fenrir::Math::AngleAxis(glm::radians(90.0f), {0.0f, 0.0f, 1.0f});
    test.Frame();

    const auto bent = test.renderer.GetWorldBounds(rig.mesh);
    REQUIRE(bent.has_value());
    const std::span<const Fenrir::Math::Mat4> palette = Fenrir::Skeletons::GetPalette(test.ActiveScene(), rig.owner);
    const Fenrir::Math::Mat4 world = Fenrir::GetWorldMatrix(rig.owner);
    const Fenrir::MeshData strip = RendererTest::Strip();
    for (std::size_t v = 0; v < strip.positions.size(); ++v)
    {
        // Each vertex is weighted wholly to its first joint.
        const Vec3 skinned(world * palette[strip.skin->joints[v][0]] * Vec4(strip.positions[v], 1.0f));
        INFO("the bounds hold vertex " << v << ", skinned on the CPU");
        REQUIRE(glm::all(glm::lessThanEqual(bent->min - Vec3(1e-4f), skinned)));
        REQUIRE(glm::all(glm::lessThanEqual(skinned, bent->max + Vec3(1e-4f))));
    }
    INFO("the swung top row reaches x = 2 in the world");
    REQUIRE(bent->min.x <= 2.0f + 1e-4f);
}

TEST_CASE("GetWorldBounds of a skinned mesh is empty without a skinning source", "[renderer][skinning]")
{
    SkinningTestApp test(false);
    const Fenrir::Entity owner = test.ActiveScene().GetEntityList().CreateEntity();
    const Fenrir::Entity mesh = test.AddSkinned(owner, test.StripMesh(), test.Material());
    test.Frame();

    REQUIRE_FALSE(test.renderer.GetWorldBounds(mesh).has_value());
}

TEST_CASE("GetWorldBounds of a skinned mesh is empty while its skeleton is not Ready", "[renderer][skinning]")
{
    SkinningTestApp test;
    Fenrir::Entity unavailable = test.ActiveScene().GetEntityList().CreateEntity();
    unavailable.AddComponent<Fenrir::SkeletonPose>();
    const Fenrir::Entity mesh = test.AddSkinned(unavailable, test.StripMesh(), test.Material());
    test.Frame();

    REQUIRE_FALSE(test.renderer.GetWorldBounds(mesh).has_value());
}
