// Skinned drawing on OpenGL (the spec's seam 2; ADR-0035): the SKINNED variants of the built-in programs reading the
// palette buffer, read back from the view's SceneColor, with FenrirSkeleton as the skinning source. Where a pixel is
// predicted, the prediction comes from the CPU mirror of Fenrir/Skinning.glsl or from the rigid path.

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "Core/Skinning.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "GlScene.hpp"
#include "SkinningTestApp.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Mat4;
using Fenrir::Math::Vec3;

namespace
{
    // The Background pass's colour in a scene with no Sky, linear.
    constexpr GlProbe::Pixel Background{0.015f, 0.015f, 0.015f, 1.0f};
    constexpr GlProbe::Pixel White{1.0f, 1.0f, 1.0f, 1.0f};

    /**
     * @brief GlScene with FenrirSkeleton installed, and a camera entity at the origin looking along -Z, its bloom off
     * as GlScene::AddCamera's is.
     */
    class GlSkinScene : public GlTest::GlScene
    {
      public:
        GlSkinScene()
        {
            camera = app.GetActiveScene().GetEntityList().CreateEntity();
            camera.AddComponent<Fenrir::Camera>().bloom.intensity = 0.0f;
        }

        Fenrir::Skeletons skeletons{Fenrir::Skeletons::Install(app)};
        Fenrir::Entity camera;

        /** @brief A material on `shader` with base colour `colour`, drawing both faces. */
        Fenrir::AssetHandle<Fenrir::Material> Material(const char* shader, Fenrir::Colour colour)
        {
            Fenrir::AssetStore& store = assets.Store();
            const auto material = store.CreateMaterial(
                store.Load<Fenrir::Shader>(shader, Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
            store.SetColour(material, "baseColour", colour);
            store.SetDoubleSided(material, true);
            return material;
        }

        /** @brief A skeleton entity posing `skeleton` at `transform`, and `mesh` skinned by it in `material`. */
        Fenrir::Entity AddRig(Fenrir::SkeletonData skeleton, Fenrir::MeshData mesh,
                              Fenrir::AssetHandle<Fenrir::Material> material, const Fenrir::Transform& transform)
        {
            Fenrir::AssetStore& store = assets.Store();
            Fenrir::EntityList& entities = app.GetActiveScene().GetEntityList();
            Fenrir::Entity owner = entities.CreateEntity();
            owner.GetComponent<Fenrir::Transform>() = transform;
            owner.AddComponent<Fenrir::SkeletonPose>(
                Fenrir::SkeletonPose{store.Add<Fenrir::Skeleton>(std::move(skeleton), Fenrir::AssetOwner::App())});
            Fenrir::Entity drawn = entities.CreateEntity();
            drawn.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{
                store.Add<Fenrir::Mesh>(std::move(mesh), Fenrir::AssetOwner::App()), material, true});
            drawn.AddComponent<Fenrir::SkinnedMesh>(Fenrir::SkinnedMesh{owner});
            return owner;
        }

        /** @brief The pixel `point` (world space) appears at in the 64-pixel window. */
        [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> PixelOf(Vec3 point) const
        {
            const std::optional<Fenrir::Math::Vec2> screen = renderer.WorldToScreenPoint(camera, point);
            REQUIRE(screen.has_value());
            return {static_cast<std::uint32_t>(screen->x), static_cast<std::uint32_t>(screen->y)};
        }

        [[nodiscard]] static GlProbe::Pixel At(const GlProbe::Image& image, std::pair<std::uint32_t, std::uint32_t> at)
        {
            return image.At(at.first, at.second);
        }
    };

    Fenrir::Transform At(Vec3 position)
    {
        Fenrir::Transform transform;
        transform.pos = position;
        return transform;
    }

    /** @brief A square of side `size` around `centre` in the XY plane, facing +Z, weighted wholly to bone 0. */
    Fenrir::MeshData BoneSquare(Vec3 centre, float size)
    {
        Fenrir::MeshData mesh = RendererTest::Quad(size, centre.z);
        Fenrir::MeshSkin skin;
        for (Vec3& position : mesh.positions)
        {
            position += Vec3(centre.x, centre.y, 0.0f);
            skin.joints.push_back({0, 0, 0, 0});
            skin.weights.push_back({65535, 0, 0, 0});
        }
        mesh.skin = std::move(skin);
        return mesh;
    }

    /** @brief One bone at the skeleton entity's origin, at rest at `rest`, its inverse bind the identity. */
    Fenrir::SkeletonData OneBone(const Fenrir::Transform& rest)
    {
        Fenrir::SkeletonData skeleton;
        skeleton.bones.resize(1);
        skeleton.bones[0].sourceName = "bone";
        skeleton.bones[0].rest = rest;
        return skeleton;
    }
} // namespace

TEST_CASE("A two-bone strip with its upper bone turned 90 degrees draws its upper half swung aside",
          "[renderer][gl][skinning]")
{
    GlSkinScene test;
    test.RequireOpenGL();
    const Fenrir::Entity owner = test.AddRig(RendererTest::TwoBones(), RendererTest::Strip(),
                                             test.Material("builtin:unlit", Fenrir::Colour::White), At({0, -1, -4}));
    // In bind space, (0, 1.75) is on the strip's upper half and (-0.8, 1) beside it; turning the upper bone 90 degrees
    // about +Z about its joint at (0, 1) swings the top row to x = -1, which covers (-0.8, 1) and uncovers (0, 1.75).
    const auto upper = test.PixelOf(Vec3(0.0f, 0.75f, -4.0f));
    const auto beside = test.PixelOf(Vec3(-0.8f, 0.0f, -4.0f));
    const auto lower = test.PixelOf(Vec3(0.0f, -0.5f, -4.0f));

    const GlProbe::Image rest = test.Frame();
    INFO("at rest the upper half is drawn and the point beside it is not");
    REQUIRE(GlProbe::Near(GlSkinScene::At(rest, upper), White));
    REQUIRE(GlProbe::Near(GlSkinScene::At(rest, beside), Background));

    test.skeletons.EditBasePose(test.app.GetActiveScene(), owner)[1].rot =
        Fenrir::Math::AngleAxis(glm::radians(90.0f), Vec3(0.0f, 0.0f, 1.0f));
    const GlProbe::Image bent = test.Frame();

    INFO("bent, the upper half has swung to cover the point beside it, and the lower half has not moved");
    REQUIRE(GlProbe::Near(GlSkinScene::At(bent, upper), Background));
    REQUIRE(GlProbe::Near(GlSkinScene::At(bent, beside), White));
    REQUIRE(GlProbe::Near(GlSkinScene::At(bent, lower), White));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A bone with translation, rotation and non-uniform scale moves a vertex where the CPU mirror puts it",
          "[renderer][gl][skinning]")
{
    GlSkinScene test;
    test.RequireOpenGL();
    Fenrir::Transform rest;
    rest.pos = {0.4f, -0.3f, 0.2f};
    rest.rot = Fenrir::Math::AngleAxis(glm::radians(35.0f), glm::normalize(Vec3(0.3f, 0.2f, 1.0f)));
    rest.scale = {2.0f, 0.5f, 1.5f};
    // The square, 0.4 on a side, lands about (-0.6, -0.6) in the world, scaled to 0.8 by 0.2: well away from its
    // unskinned place.
    const Vec3 bindCentre(-0.5f, 0.6f, 0.0f);
    const Fenrir::Entity owner = test.AddRig(OneBone(rest), BoneSquare(bindCentre, 0.4f),
                                             test.Material("builtin:unlit", Fenrir::Colour::White), At({0, 0, -4}));

    // Run once so the pose is built, then predict the square's centre with the palette and the CPU mirror.
    test.RunFrame();
    const std::span<const Mat4> palette = Fenrir::Skeletons::GetPalette(test.app.GetActiveScene(), owner);
    REQUIRE(palette.size() == 1);
    const Fenrir::Render::GpuBone bone = Fenrir::Render::GpuBone::FromMatrix(palette[0]);
    const Vec3 predicted = Fenrir::Render::SkinPosition(bone, Fenrir::GetWorldMatrix(owner), bindCentre);
    INFO("the CPU mirror agrees with the bone's own matrix");
    REQUIRE(glm::length(predicted - Vec3(Fenrir::GetWorldMatrix(owner) * palette[0] *
                                         Fenrir::Math::Vec4(bindCentre, 1.0f))) < 1e-5f);

    const GlProbe::Image image = test.Frame();

    INFO("the GPU drew the square's centre where the CPU predicted, and not at its unskinned place");
    REQUIRE(GlProbe::Near(GlSkinScene::At(image, test.PixelOf(predicted)), White));
    REQUIRE(GlProbe::Near(GlSkinScene::At(image, test.PixelOf(bindCentre + Vec3(0.0f, 0.0f, -4.0f))), Background));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A non-uniformly scaled bone's normal lights builtin:lit as the rigid path lights the same surface",
          "[renderer][gl][skinning]")
{
    GlSkinScene test;
    test.RequireOpenGL();
    // A square turned 45 degrees about +Y, its normal (0.71, 0, 0.71), on a bone scaled 3 along Z: the inverse
    // transpose tilts the normal to (0.95, 0, 0.32), while A x n would give (0.32, 0, 0.95).
    Fenrir::MeshData square = BoneSquare(Vec3(0.0f), 0.4f);
    const Fenrir::Math::Quat turn = Fenrir::Math::AngleAxis(glm::radians(45.0f), Vec3(0.0f, 1.0f, 0.0f));
    for (std::size_t v = 0; v < square.positions.size(); ++v)
    {
        square.positions[v] = Fenrir::Math::Rotate(turn, square.positions[v]);
        square.normals[v] = Fenrir::Math::Rotate(turn, square.normals[v]);
    }
    Fenrir::Transform stretch;
    stretch.scale = {1.0f, 1.0f, 3.0f};
    const auto material = test.Material("builtin:lit", Fenrir::Colour{0.8f, 0.8f, 0.8f, 1.0f});
    test.assets.Store().SetFloat(material, "roughness", 1.0f);
    test.AddRig(OneBone(stretch), square, material, At({-0.6f, 0.0f, -3.0f}));

    // The same surface drawn rigidly: the square without skin data, its entity at the same place and scale.
    Fenrir::MeshData rigidSquare = square;
    rigidSquare.skin.reset();
    Fenrir::Entity rigid = test.app.GetActiveScene().GetEntityList().CreateEntity();
    rigid.GetComponent<Fenrir::Transform>() =
        Fenrir::Transform({0.6f, 0.0f, -3.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, stretch.scale);
    rigid.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{
        test.assets.Store().Add<Fenrir::Mesh>(std::move(rigidSquare), Fenrir::AssetOwner::App()), material, true});

    // A white light travelling along (-sin 60, 0, -cos 60), as the foundation's lit test has it.
    Fenrir::Entity light = test.app.GetActiveScene().GetEntityList().CreateEntity();
    light.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(glm::radians(60.0f), Vec3(0.0f, 1.0f, 0.0f));
    light.AddComponent<Fenrir::DirectionalLight>();

    const GlProbe::Image image = test.Frame();
    const GlProbe::Pixel skinned = GlSkinScene::At(image, test.PixelOf({-0.6f, 0.0f, -3.0f}));
    const GlProbe::Pixel reference = GlSkinScene::At(image, test.PixelOf({0.6f, 0.0f, -3.0f}));

    INFO("the skinned face is as bright as the rigid one: 0.8 x N.L with N.L about 0.98, not about 0.75");
    REQUIRE(GlProbe::Near(skinned, reference, 0.03f));
    REQUIRE(skinned[0] > 0.7f);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A skinned mesh follows its skeleton entity's world transform, rotation and scale included",
          "[renderer][gl][skinning]")
{
    GlSkinScene test;
    test.RequireOpenGL();
    // The owner turned 90 degrees about +Z and halved: the strip's upper half, (0, 1.5) in bind space, lands at
    // (-0.75, 0) from the owner.
    const Fenrir::Transform owner({0.5f, -0.2f, -4.0f}, Fenrir::Math::AngleAxis(glm::radians(90.0f), Vec3(0, 0, 1)),
                                  Vec3(0.5f));
    const Fenrir::Entity skeleton = test.AddRig(RendererTest::TwoBones(), RendererTest::Strip(),
                                                test.Material("builtin:unlit", Fenrir::Colour::White), owner);
    test.RunFrame();
    const Vec3 moved(Fenrir::GetWorldMatrix(skeleton) * Fenrir::Math::Vec4(0.0f, 1.5f, 0.0f, 1.0f));
    REQUIRE(glm::length(moved - Vec3(-0.25f, -0.2f, -4.0f)) < 1e-5f);

    const GlProbe::Image image = test.Frame();

    INFO("drawn where the owner's world matrix puts it, and not where the unmoved owner would");
    REQUIRE(GlProbe::Near(GlSkinScene::At(image, test.PixelOf(moved)), White));
    REQUIRE(GlProbe::Near(GlSkinScene::At(image, test.PixelOf({0.5f, 1.3f, -4.0f})), Background));
    REQUIRE(test.messages->errors.empty());
}
