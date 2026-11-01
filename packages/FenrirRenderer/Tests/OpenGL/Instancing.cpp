// Automatic instancing on OpenGL (ADR-0051), read back from the view's SceneColor: each instance of a merged draw
// reads its own record of the draw buffer, at the draw's base instance plus its instance index, which GL's SPIR-V
// counts from 0 (Fenrir/Draw.glsl). So merged items land where their own records put them, and merged skinned items
// skin from their own palettes.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "FenrirSkeleton/Skeletons.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;

namespace
{
    // The Background pass's colour in a scene with no Sky, linear.
    constexpr GlProbe::Pixel Background{0.015f, 0.015f, 0.015f, 1.0f};
    constexpr GlProbe::Pixel White{1.0f, 1.0f, 1.0f, 1.0f};

    /**
     * @brief GlScene with FenrirSkeleton installed, a camera entity at the origin looking along -Z with its bloom off,
     * and a white builtin:unlit material drawing both faces.
     */
    class GlInstanceScene : public GlTest::GlScene
    {
      public:
        GlInstanceScene()
        {
            camera = app.GetActiveScene().GetEntityList().CreateEntity();
            camera.AddComponent<Fenrir::Camera>().bloom.intensity = 0.0f;
            Fenrir::AssetStore& store = assets.Store();
            white = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle,
                                         Fenrir::AssetOwner::App());
            store.SetColour(white, "baseColour", Fenrir::Colour::White);
            store.SetDoubleSided(white, true);
        }

        Fenrir::Skeletons skeletons{Fenrir::Skeletons::Install(app)};
        Fenrir::Entity camera;
        Fenrir::AssetHandle<Fenrir::Material> white;

        [[nodiscard]] Fenrir::AssetHandle<Fenrir::Mesh> Mesh(Fenrir::MeshData data)
        {
            return assets.Store().Add<Fenrir::Mesh>(std::move(data), Fenrir::AssetOwner::App());
        }

        /** @brief `mesh` in the white material, its entity at `position`. */
        void AddAt(Fenrir::AssetHandle<Fenrir::Mesh> mesh, Vec3 position)
        {
            Fenrir::Entity entity = app.GetActiveScene().GetEntityList().CreateEntity();
            entity.GetComponent<Fenrir::Transform>().pos = position;
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, white, true});
        }

        /**
         * @brief A skeleton entity at `position` posing one bone at rest at `bone` from it (its inverse bind the
         * identity), and `mesh`, in the white material, skinned by it.
         */
        void AddRig(Fenrir::AssetHandle<Fenrir::Mesh> mesh, Vec3 position, Vec3 bone)
        {
            Fenrir::AssetStore& store = assets.Store();
            Fenrir::EntityList& entities = app.GetActiveScene().GetEntityList();
            Fenrir::SkeletonData skeleton;
            skeleton.bones.resize(1);
            skeleton.bones[0].sourceName = "bone";
            skeleton.bones[0].rest.pos = bone;
            Fenrir::Entity owner = entities.CreateEntity();
            owner.GetComponent<Fenrir::Transform>().pos = position;
            owner.AddComponent<Fenrir::SkeletonPose>(
                Fenrir::SkeletonPose{store.Add<Fenrir::Skeleton>(std::move(skeleton), Fenrir::AssetOwner::App())});
            Fenrir::Entity drawn = entities.CreateEntity();
            drawn.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, white, true});
            drawn.AddComponent<Fenrir::SkinnedMesh>(Fenrir::SkinnedMesh{owner});
        }

        /** @brief The pixel of `image` that `point` (world space) appears at. */
        [[nodiscard]] GlProbe::Pixel At(const GlProbe::Image& image, Vec3 point) const
        {
            const std::optional<Fenrir::Math::Vec2> screen = renderer.WorldToScreenPoint(camera, point);
            REQUIRE(screen.has_value());
            return image.At(static_cast<std::uint32_t>(screen->x), static_cast<std::uint32_t>(screen->y));
        }
    };

    /** @brief A square of side `size` at the origin in the XY plane, facing +Z, weighted wholly to bone 0. */
    Fenrir::MeshData BoneSquare(float size)
    {
        Fenrir::MeshData mesh = RendererTest::Quad(size, 0.0f);
        Fenrir::MeshSkin skin;
        for (std::size_t v = 0; v < mesh.positions.size(); ++v)
        {
            skin.joints.push_back({0, 0, 0, 0});
            skin.weights.push_back({65535, 0, 0, 0});
        }
        mesh.skin = std::move(skin);
        return mesh;
    }
} // namespace

TEST_CASE("Merged squares each land where their own record puts them", "[renderer][gl][instancing]")
{
    GlInstanceScene test;
    test.RequireOpenGL();
    // Squares of two meshes in one material across the view, with gaps between them: two runs, so whichever draws
    // second starts past record 0, where GL's instance index alone would read the first run's records.
    const auto large = test.Mesh(RendererTest::Quad(0.5f, 0.0f));
    const auto small = test.Mesh(RendererTest::Quad(0.4f, 0.0f));
    const Vec3 largePlaces[] = {{-1.2f, 0.6f, -4.0f}, {0.0f, 0.6f, -4.0f}, {1.2f, 0.6f, -4.0f}};
    const Vec3 smallPlaces[] = {{-0.6f, -0.6f, -4.0f}, {0.6f, -0.6f, -4.0f}};
    for (const Vec3 place : largePlaces)
    {
        test.AddAt(large, place);
    }
    for (const Vec3 place : smallPlaces)
    {
        test.AddAt(small, place);
    }

    const GlProbe::Image image = test.Frame();

    INFO("they were two draws of five instances");
    REQUIRE(test.renderer.Stats().draws == 2);
    REQUIRE(test.renderer.Stats().instances == 5);
    for (const auto& places : {std::span<const Vec3>(largePlaces), std::span<const Vec3>(smallPlaces)})
    {
        for (const Vec3 place : places)
        {
            INFO("the square at (" << place.x << ", " << place.y << ") is drawn there");
            REQUIRE(GlProbe::Near(test.At(image, place), White));
        }
    }
    INFO("and nothing is drawn beside them, where no record puts a square");
    REQUIRE(GlProbe::Near(test.At(image, {-1.2f, -0.6f, -4.0f}), Background));
    REQUIRE(GlProbe::Near(test.At(image, {0.0f, -0.6f, -4.0f}), Background));
    REQUIRE(GlProbe::Near(test.At(image, {-0.6f, 0.6f, -4.0f}), Background));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Merged skinned items each skin from their own palette", "[renderer][gl][instancing][skinning]")
{
    GlInstanceScene test;
    test.RequireOpenGL();
    // Pairs of skeletons, one pair a row, whose one bone rests 0.8 to either side of the pair's place: the row's
    // square lands on the bone, so each instance lands to its own side only through its own palette. Two meshes make
    // two runs, so one starts past record 0.
    const float rows[] = {0.6f, -0.6f};
    for (const float row : rows)
    {
        const auto square = test.Mesh(BoneSquare(row > 0.0f ? 0.4f : 0.3f));
        test.AddRig(square, {0.0f, row, -4.0f}, {-0.8f, 0.0f, 0.0f});
        test.AddRig(square, {0.0f, row, -4.0f}, {0.8f, 0.0f, 0.0f});
    }

    const GlProbe::Image image = test.Frame();

    INFO("they were two draws of two skinned instances each");
    REQUIRE(test.renderer.Stats().draws == 2);
    REQUIRE(test.renderer.Stats().instances == 4);
    REQUIRE(test.renderer.Stats().skinnedDraws == 4);
    for (const float row : rows)
    {
        INFO("in the row at " << row << ", each square is drawn on its own skeleton's bone");
        REQUIRE(GlProbe::Near(test.At(image, {-0.8f, row, -4.0f}), White));
        REQUIRE(GlProbe::Near(test.At(image, {0.8f, row, -4.0f}), White));
        INFO("and neither at the unskinned place between them");
        REQUIRE(GlProbe::Near(test.At(image, {0.0f, row, -4.0f}), Background));
    }
    REQUIRE(test.messages->errors.empty());
}
