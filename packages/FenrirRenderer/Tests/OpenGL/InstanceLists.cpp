// InstancedMeshRenderer on OpenGL (ADR-0051), read back from the view's SceneColor: each copy of an instance list
// lands at the entity's world matrix times its own transform, read through Fenrir/Draw.glsl from the instance-list
// buffer, and its colour tints the material's base colour.

#include <cstdint>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/InstanceListData.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Vec3;

namespace
{
    // The Background pass's colour in a scene with no Sky, linear.
    constexpr GlProbe::Pixel Background{0.015f, 0.015f, 0.015f, 1.0f};

    /**
     * @brief GlScene with a camera entity at the origin looking along -Z, its bloom off, and a white builtin:unlit
     * material drawing both faces.
     */
    class GlListScene : public GlTest::GlScene
    {
      public:
        GlListScene()
        {
            camera = app.GetActiveScene().GetEntityList().CreateEntity();
            camera.AddComponent<Fenrir::Camera>().look.bloom.intensity = 0.0f;
            Fenrir::AssetStore& store = assets.Store();
            white = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle,
                                         Fenrir::AssetOwner::App());
            store.SetColour(white, "baseColour", Fenrir::Colour::White);
            store.SetDoubleSided(white, true);
            square = store.Add<Fenrir::Mesh>(RendererTest::Quad(0.4f, 0.0f), Fenrir::AssetOwner::App());
        }

        Fenrir::Entity camera;
        Fenrir::AssetHandle<Fenrir::Material> white;
        Fenrir::AssetHandle<Fenrir::Mesh> square;

        /** @brief An entity drawing `list` with the square in the white material. */
        Fenrir::Entity AddList(Fenrir::InstanceListData list)
        {
            Fenrir::Entity entity = app.GetActiveScene().GetEntityList().CreateEntity();
            const auto handle = assets.Store().Add<Fenrir::InstanceList>(std::move(list), Fenrir::AssetOwner::App());
            entity.AddComponent<Fenrir::InstancedMeshRenderer>(
                Fenrir::InstancedMeshRenderer{square, white, handle, true, true});
            return entity;
        }

        /** @brief The pixel of `image` that `point` (world space) appears at. */
        [[nodiscard]] GlProbe::Pixel At(const GlProbe::Image& image, Vec3 point) const
        {
            const std::optional<Fenrir::Math::Vec2> screen = renderer.WorldToScreenPoint(camera, point);
            REQUIRE(screen.has_value());
            return image.At(static_cast<std::uint32_t>(screen->x), static_cast<std::uint32_t>(screen->y));
        }
    };

    Fenrir::Math::Mat4 At(Vec3 offset)
    {
        return Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), offset);
    }
} // namespace

TEST_CASE("Instance list copies land at their transforms under a moved entity", "[renderer][gl][instancing]")
{
    GlListScene test;
    test.RequireOpenGL();
    // Copies one and two units along the list's +X, under an entity moved to (-0.5, -0.8, -4) and turned a quarter
    // about +Z: the list's +X is the world's +Y, so the copies stand above the entity, not beside it.
    Fenrir::InstanceListData list;
    list.transforms = {At({0.0f, 0.0f, 0.0f}), At({0.8f, 0.0f, 0.0f}), At({1.6f, 0.0f, 0.0f})};
    Fenrir::Entity entity = test.AddList(std::move(list));
    Fenrir::Transform& transform = entity.GetComponent<Fenrir::Transform>();
    transform.pos = {-0.5f, -0.8f, -4.0f};
    transform.rot = Fenrir::Math::AngleAxis(1.5707964f, Vec3(0.0f, 0.0f, 1.0f));

    const GlProbe::Image image = test.Frame();

    INFO("one draw of three copies");
    REQUIRE(test.renderer.Stats().draws == 1);
    REQUIRE(test.renderer.Stats().instances == 3);
    for (const float up : {0.0f, 0.8f, 1.6f})
    {
        INFO("the copy " << up << " along the list's +X is drawn that far above the entity");
        REQUIRE(GlProbe::Near(test.At(image, {-0.5f, -0.8f + up, -4.0f}), GlProbe::Pixel{1.0f, 1.0f, 1.0f, 1.0f}));
    }
    INFO("and none where the list's +X would be unturned, or at the world origin's offsets");
    REQUIRE(GlProbe::Near(test.At(image, {0.3f, -0.8f, -4.0f}), Background));
    REQUIRE(GlProbe::Near(test.At(image, {1.1f, -0.8f, -4.0f}), Background));
    REQUIRE(GlProbe::Near(test.At(image, {0.8f, 0.0f, -4.0f}), Background));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Each copy's colour tints the material's base colour", "[renderer][gl][instancing]")
{
    GlListScene test;
    test.RequireOpenGL();
    test.assets.Store().SetColour(test.white, "baseColour", Fenrir::Colour{0.5f, 1.0f, 1.0f, 1.0f});
    Fenrir::InstanceListData list;
    list.transforms = {At({-0.8f, 0.0f, 0.0f}), At({0.0f, 0.0f, 0.0f}), At({0.8f, 0.0f, 0.0f})};
    list.colours = {Fenrir::Colour{1.0f, 0.0f, 0.0f, 1.0f}, Fenrir::Colour{0.0f, 1.0f, 0.0f, 1.0f},
                    Fenrir::Colour{1.0f, 1.0f, 0.25f, 1.0f}};
    test.AddList(std::move(list)).GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -4.0f};

    const GlProbe::Image image = test.Frame();

    INFO("each copy is the base colour times its own: red halved, green, and white with blue at a quarter, halved red");
    REQUIRE(GlProbe::Near(test.At(image, {-0.8f, 0.0f, -4.0f}), GlProbe::Pixel{0.5f, 0.0f, 0.0f, 1.0f}));
    REQUIRE(GlProbe::Near(test.At(image, {0.0f, 0.0f, -4.0f}), GlProbe::Pixel{0.0f, 1.0f, 0.0f, 1.0f}));
    REQUIRE(GlProbe::Near(test.At(image, {0.8f, 0.0f, -4.0f}), GlProbe::Pixel{0.5f, 1.0f, 0.25f, 1.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Each copy's colour tints builtin:lit's base colour too", "[renderer][gl][instancing]")
{
    GlListScene test;
    test.RequireOpenGL();
    // Lit by a white sky alone, a rough white dielectric shows its base colour, so each copy shows its own tint; the
    // little specular the sky adds is white, the same in every channel.
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{Fenrir::Colour::White});
    Fenrir::AssetStore& store = test.assets.Store();
    const auto lit = store.CreateMaterial(store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle,
                                          Fenrir::AssetOwner::App());
    store.SetColour(lit, "baseColour", Fenrir::Colour::White);
    store.SetFloat(lit, "roughness", 1.0f);
    store.SetFloat(lit, "metallic", 0.0f);
    Fenrir::InstanceListData list;
    list.transforms = {At({-0.6f, 0.0f, 0.0f}), At({0.6f, 0.0f, 0.0f})};
    list.colours = {Fenrir::Colour{1.0f, 0.0f, 0.0f, 1.0f}, Fenrir::Colour{0.0f, 1.0f, 0.0f, 1.0f}};
    Fenrir::Entity entity = test.AddList(std::move(list));
    entity.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -4.0f};
    entity.GetComponent<Fenrir::InstancedMeshRenderer>().material = lit;

    const GlProbe::Image image = test.Frame();

    const GlProbe::Pixel red = test.At(image, {-0.6f, 0.0f, -4.0f});
    const GlProbe::Pixel green = test.At(image, {0.6f, 0.0f, -4.0f});
    INFO("red (" << red[0] << ", " << red[1] << ", " << red[2] << "), green (" << green[0] << ", " << green[1] << ", "
                 << green[2] << ")");
    REQUIRE(red[0] > 4.0f * red[1]);
    REQUIRE(red[0] > 4.0f * red[2]);
    REQUIRE(green[1] > 4.0f * green[0]);
    REQUIRE(green[1] > 4.0f * green[2]);
    REQUIRE(test.messages->errors.empty());
}
