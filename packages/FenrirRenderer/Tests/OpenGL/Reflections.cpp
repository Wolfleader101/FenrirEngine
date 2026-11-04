// Screen-space reflections on OpenGL (ADR-0056, lighting ticket 30): pixel probes of SceneColor where a mirror floor
// reflects what is on screen. The camera, at the origin looking along -Z with a 90-degree field of view, sees a white
// metal floor of roughness 0 one unit below it, and an emissive red board 2 units tall standing on it 6 units away,
// whose centre the floor mirrors at row 42 (where the line to its mirror image, 2 below it, crosses the floor). The
// reflections show last frame's colour, so each probe runs a few frames first. Then a mirror floor under emissive
// strips and a rect light, against its golden image; and the camera's two reflection debug views (lighting ticket 31),
// read from an image target against the hit target's confidence at the probed pixel.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirMath/Math.hpp"
#include "GlScene.hpp"

using Fenrir::GlProbe;
using Fenrir::Math::Quat;
using Fenrir::Math::Vec3;
using GlTest::GlScene;

namespace
{
    constexpr std::uint32_t Size = GlScene::Size;
    /** @brief The pixel where the floor mirrors the board's centre, and one on the board itself. */
    constexpr std::uint32_t MirrorX = 32;
    constexpr std::uint32_t MirrorY = 42;
    constexpr std::uint32_t BoardY = 31;
    constexpr float HalfTurn = 3.14159265f;
    /** @brief The board's colour: unlit, so its pixels hold it as it is. */
    constexpr Fenrir::Colour BoardColour{2.0f, 0.0f, 0.0f, 1.0f};
    /** @brief How many frames each probe runs: the first has no history to reflect. */
    constexpr int Frames = 3;

    /** @brief A quad's turn from facing the camera to facing up. */
    Quat FacingUp()
    {
        return Fenrir::Math::AngleAxis(-HalfTurn * 0.5f, Vec3(1.0f, 0.0f, 0.0f));
    }

    /**
     * @brief The camera, with reflections as `reflect` says, under a sky of `sky`, over a white metal floor of
     * `roughness`.
     */
    Fenrir::Camera& AddRoom(GlScene& test, bool reflect, float roughness, Fenrir::Colour sky)
    {
        Fenrir::Camera& camera = test.AddCamera();
        camera.projection = Fenrir::Perspective{HalfTurn * 0.5f, 0.1f};
        camera.reflections.enabled = reflect;
        test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{sky});
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", Fenrir::Colour::White);
        store.SetFloat(material, "metallic", 1.0f);
        store.SetFloat(material, "roughness", roughness);
        const auto quad = store.Add<Fenrir::Mesh>(RendererTest::Quad(1.0f), Fenrir::AssetOwner::App());
        Fenrir::Entity floor = test.app.GetActiveScene().GetEntityList().CreateEntity();
        Fenrir::Transform& transform = floor.GetComponent<Fenrir::Transform>();
        transform.pos = {0.0f, -1.0f, -10.0f};
        transform.rot = FacingUp();
        transform.scale = {20.0f, 20.0f, 1.0f};
        floor.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{quad, material});
        return camera;
    }

    /** @brief The emissive board, 2 units square, standing on the floor `distance` in front of the camera, or behind
     * it when `behind`. */
    void AddBoard(GlScene& test, float distance, bool behind = false)
    {
        Fenrir::AssetStore& store = test.assets.Store();
        const auto material = store.CreateMaterial(
            store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", BoardColour);
        store.SetDoubleSided(material, true);
        const auto quad = store.Add<Fenrir::Mesh>(RendererTest::Quad(2.0f), Fenrir::AssetOwner::App());
        Fenrir::Entity board = test.app.GetActiveScene().GetEntityList().CreateEntity();
        board.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, behind ? distance : -distance};
        board.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{quad, material});
    }

    /** @brief A rect light where the board is, the board's size and radiance, facing the camera. */
    void AddBoardLight(GlScene& test, bool visibleSurface)
    {
        Fenrir::Entity light = test.app.GetActiveScene().GetEntityList().CreateEntity();
        light.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.0f, -6.0f};
        light.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(HalfTurn, Vec3(0.0f, 1.0f, 0.0f));
        Fenrir::RectLight rect{{1.0f, 0.0f, 0.0f, 1.0f}, BoardColour.r, 2.0f, 2.0f, false, 30.0f};
        rect.visibleSurface = visibleSurface;
        light.AddComponent<Fenrir::RectLight>(rect);
    }

    /** @brief Runs the probes' frames and reads back the last one's SceneColor. */
    GlProbe::Image Settle(GlScene& test)
    {
        for (int frame = 1; frame < Frames; ++frame)
        {
            test.RunFrame();
        }
        return test.Frame(Size, Size);
    }

    bool Close(float a, float b, float tolerance)
    {
        return std::abs(a - b) <= tolerance;
    }

    /** @brief The linear value of an sRGB-encoded one. */
    float Linear(float encoded)
    {
        return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    }

    /** @brief What a camera's debug view drew: its image target, linear, and the hit target of its last frame. */
    struct Shown
    {
        GlProbe::Image image;
        GlProbe::Image hits;

        [[nodiscard]] GlProbe::Pixel At(std::uint32_t x, std::uint32_t y) const
        {
            GlProbe::Pixel pixel = image.At(x, y);
            for (int channel = 0; channel < 3; ++channel)
            {
                pixel[channel] = Linear(pixel[channel]);
            }
            return pixel;
        }
    };

    /** @brief Points `camera` at a `Size` square image showing `view`, runs the probes' frames, and reads both back. */
    Shown ShowDebugView(GlScene& test, Fenrir::Camera& camera, Fenrir::CameraDebugView view)
    {
        Fenrir::ImageData data;
        data.width = Size;
        data.height = Size;
        const auto target = test.assets.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
        camera.target = Fenrir::ImageTarget{target};
        camera.debugView = view;
        for (int frame = 0; frame < Frames; ++frame)
        {
            test.RunFrame();
        }
        Fenrir::Graphics::GraphicsDevice& device = Fenrir::RendererProbe::Device(test.renderer);
        Shown shown{GlProbe::Read(device, Fenrir::RendererProbe::ImageTexture(test.renderer, target)), {}};
        const auto hits = Fenrir::RendererProbe::ReflectionHits(test.renderer);
        if (!hits.empty())
        {
            shown.hits = GlProbe::Read(device, hits.back());
        }
        return shown;
    }

    /** @brief The brightest channel of any pixel `shown` drew. */
    float Brightest(const Shown& shown)
    {
        float brightest = 0.0f;
        for (std::uint32_t y = 0; y < Size; ++y)
        {
            for (std::uint32_t x = 0; x < Size; ++x)
            {
                const GlProbe::Pixel pixel = shown.At(x, y);
                brightest = std::max({brightest, pixel[0], pixel[1], pixel[2]});
            }
        }
        return brightest;
    }
} // namespace

TEST_CASE("GL: a mirror floor shows the emissive board in front of it at the reflected pixel", "[renderer][gl][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    AddRoom(test, true, 0.0f, Fenrir::Colour::Black);
    AddBoard(test, 6.0f);

    const GlProbe::Image image = Settle(test);
    INFO("the board is where it stands");
    REQUIRE(Close(image.At(MirrorX, BoardY)[0], BoardColour.r, 1e-2f));
    const GlProbe::Pixel mirrored = image.At(MirrorX, MirrorY);
    INFO("and the floor below it shows its red: " << mirrored[0] << ", " << mirrored[1] << ", " << mirrored[2]);
    REQUIRE(mirrored[0] > 0.8f * BoardColour.r);
    REQUIRE(mirrored[0] < 1.1f * BoardColour.r);
    REQUIRE(mirrored[1] < 0.05f);
    REQUIRE(mirrored[2] < 0.05f);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: with reflections off, the mirror floor shows only the sky", "[renderer][gl][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    AddRoom(test, false, 0.0f, Fenrir::Colour::Black);
    AddBoard(test, 6.0f);

    const GlProbe::Image image = Settle(test);
    INFO("black under a black sky");
    REQUIRE(image.At(MirrorX, MirrorY)[0] < 1e-3f);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: with the board behind the camera, the mirror floor shows only the sky", "[renderer][gl][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddRoom(test, true, 0.0f, {0.0f, 0.0f, 0.5f, 1.0f});
    AddBoard(test, 6.0f, true);

    const GlProbe::Pixel on = Settle(test).At(MirrorX, MirrorY);
    camera.reflections.enabled = false;
    const GlProbe::Pixel off = Settle(test).At(MirrorX, MirrorY);
    INFO("the sky's blue, no red, as with reflections off: " << on[0] << ", " << on[2] << " against " << off[2]);
    REQUIRE(on[0] < 1e-3f);
    REQUIRE(off[2] > 0.25f);
    REQUIRE(Close(on[2], off[2], 1e-3f));
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: a floor rougher than the cutoff looks as it does with reflections off", "[renderer][gl][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddRoom(test, true, 0.8f, {0.0f, 0.0f, 0.5f, 1.0f});
    AddBoard(test, 6.0f);

    const GlProbe::Image on = Settle(test);
    camera.reflections.enabled = false;
    const GlProbe::Image off = Settle(test);
    for (const std::uint32_t y : {MirrorY, MirrorY + 8u, Size - 2u})
    {
        INFO("row " << y << " matches: " << on.At(MirrorX, y)[0] << " against " << off.At(MirrorX, y)[0]);
        for (int channel = 0; channel < 3; ++channel)
        {
            REQUIRE(Close(on.At(MirrorX, y)[channel], off.At(MirrorX, y)[channel], 1e-3f));
        }
    }
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: a rect light whose board is drawn is not reflected twice", "[renderer][gl][ssr][rect]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddRoom(test, true, 0.0f, Fenrir::Colour::Black);
    AddBoard(test, 6.0f);
    AddBoardLight(test, true);

    const float on = Settle(test).At(MirrorX, MirrorY)[0];
    camera.reflections.enabled = false;
    const float off = Settle(test).At(MirrorX, MirrorY)[0];
    INFO("off, the light's specular shows the board; on, the reflection does instead, not as well: "
         << on << " against " << off);
    REQUIRE(off > 0.5f * BoardColour.r);
    REQUIRE(on < 1.3f * off);
    REQUIRE(on > 0.7f * off);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: a rect light whose surface is not drawn keeps its specular under reflections",
          "[renderer][gl][ssr][rect]")
{
    // The board is drawn, but the light says it is not: both the light's specular and the reflection show it.
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddRoom(test, true, 0.0f, Fenrir::Colour::Black);
    AddBoard(test, 6.0f);
    AddBoardLight(test, false);

    const float on = Settle(test).At(MirrorX, MirrorY)[0];
    camera.reflections.enabled = false;
    const float off = Settle(test).At(MirrorX, MirrorY)[0];
    INFO("its specular and the reflection add: " << on << " against " << off);
    REQUIRE(on > 1.6f * off);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A mirror floor under emissive strips and a rect light, reflections on, matches its golden image",
          "[renderer][gl][golden][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::AssetStore& store = test.assets.Store();
    Fenrir::EntityList& entities = test.app.GetActiveScene().GetEntityList();
    Fenrir::ImageData data;
    data.width = 160;
    data.height = 96;
    const auto target = store.Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    // A metre and a half up, looking down the room a little.
    Fenrir::Entity camera = entities.CreateEntity();
    camera.GetComponent<Fenrir::Transform>().pos = {0.0f, 1.5f, 0.0f};
    camera.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(-0.25f, Vec3(1.0f, 0.0f, 0.0f));
    Fenrir::Camera& view = camera.AddComponent<Fenrir::Camera>();
    view.target = Fenrir::ImageTarget{target};
    view.bloom.intensity = 0.0f;
    view.reflections.enabled = true;
    test.app.GetActiveScene().AddResource<Fenrir::Sky>(Fenrir::ColourSky{{0.01f, 0.01f, 0.02f, 1.0f}});

    const auto quad = store.Add<Fenrir::Mesh>(RendererTest::Quad(1.0f), Fenrir::AssetOwner::App());
    const auto lit = store.Load<Fenrir::Shader>("builtin:lit", Fenrir::AssetOwner::App()).handle;
    const auto unlit = store.Load<Fenrir::Shader>("builtin:unlit", Fenrir::AssetOwner::App()).handle;
    const auto place = [&](Fenrir::AssetHandle<Fenrir::Material> material, Vec3 position, Quat rotation, Vec3 scale) {
        Fenrir::Entity entity = entities.CreateEntity();
        Fenrir::Transform& transform = entity.GetComponent<Fenrir::Transform>();
        transform.pos = position;
        transform.rot = rotation;
        transform.scale = scale;
        entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{quad, material});
    };

    // The floor: dark polished stone on the left, which blurs what it reflects, and a silver mirror on the right.
    for (int strip = 0; strip < 2; ++strip)
    {
        const auto material = store.CreateMaterial(lit, Fenrir::AssetOwner::App());
        const float base = strip == 0 ? 0.05f : 0.9f;
        store.SetColour(material, "baseColour", {base, base, base, 1.0f});
        store.SetFloat(material, "metallic", static_cast<float>(strip));
        store.SetFloat(material, "roughness", strip == 0 ? 0.25f : 0.02f);
        place(material, {-1.5f + 3.0f * static_cast<float>(strip), 0.0f, -7.0f}, FacingUp(), {3.0f, 14.0f, 1.0f});
    }
    // Emissive strips standing across the back of the room, cyan and orange, and a green bar hung across the middle.
    const auto glow = [&](Fenrir::Colour colour) {
        const auto material = store.CreateMaterial(unlit, Fenrir::AssetOwner::App());
        store.SetColour(material, "baseColour", colour);
        store.SetDoubleSided(material, true);
        return material;
    };
    place(glow({0.2f, 2.0f, 3.0f, 1.0f}), {-1.2f, 1.0f, -9.0f}, Quat(1.0f, 0.0f, 0.0f, 0.0f), {0.2f, 2.0f, 1.0f});
    place(glow({3.0f, 1.0f, 0.1f, 1.0f}), {1.4f, 0.8f, -8.0f}, Quat(1.0f, 0.0f, 0.0f, 0.0f), {0.3f, 1.6f, 1.0f});
    place(glow({0.2f, 3.0f, 0.4f, 1.0f}), {0.0f, 1.3f, -7.0f}, Quat(1.0f, 0.0f, 0.0f, 0.0f), {3.0f, 0.1f, 1.0f});
    // A warm rect light with its board, low across the back wall, facing the camera: reflected once, by the screen.
    const Fenrir::RectLight panel{{1.0f, 0.85f, 0.7f, 1.0f}, 2.5f, 2.0f, 0.5f, false, 30.0f};
    Fenrir::Entity light = entities.CreateEntity();
    light.GetComponent<Fenrir::Transform>().pos = {0.0f, 0.4f, -10.0f};
    light.GetComponent<Fenrir::Transform>().rot = Fenrir::Math::AngleAxis(HalfTurn, Vec3(0.0f, 1.0f, 0.0f));
    light.AddComponent<Fenrir::RectLight>(panel);
    place(glow({panel.colour.r * panel.intensity, panel.colour.g * panel.intensity, panel.colour.b * panel.intensity,
                1.0f}),
          {0.0f, 0.4f, -10.0f}, Quat(1.0f, 0.0f, 0.0f, 0.0f), {panel.width, panel.height, 1.0f});

    for (int frame = 0; frame < Frames; ++frame)
    {
        test.RunFrame();
    }
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
    RendererTest::RequireGolden("mirror-floor-reflections", test.ReadTarget(target), FENRIR_TEST_GOLDENS,
                                FENRIR_TEST_GOLDEN_FAILURES);
}

TEST_CASE("GL: the Reflections debug view shows the reflected colour times its confidence, exposed and tonemapped",
          "[renderer][gl][ssr]")
{
    const std::uint32_t msaa = GENERATE(1u, 4u);
    GlScene test(msaa);
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddRoom(test, true, 0.0f, {0.0f, 0.0f, 0.5f, 1.0f});
    AddBoard(test, 6.0f);
    // Exposed by a quarter and not tonemapped, the board's red 2 shows as 0.5 where the floor is sure of it.
    camera.exposure = -2.0f;
    camera.tonemapper = Fenrir::Tonemapper::None;

    const Shown shown = ShowDebugView(test, camera, Fenrir::CameraDebugView::Reflections);
    REQUIRE(shown.hits.width == Size);
    const float confidence = shown.hits.At(MirrorX, MirrorY)[2];
    const GlProbe::Pixel mirrored = shown.At(MirrorX, MirrorY);
    INFO(msaa << " samples: the floor below the board shows its red by the confidence " << confidence << ": "
              << mirrored[0] << ", " << mirrored[1] << ", " << mirrored[2]);
    REQUIRE(confidence > 0.5f);
    REQUIRE(Close(mirrored[0], 0.25f * BoardColour.r * confidence, 2e-2f));
    REQUIRE(mirrored[1] < 1e-2f);
    REQUIRE(mirrored[2] < 1e-2f);
    INFO("the board itself reflects nothing, nor does the sky: black, not the sky's blue");
    REQUIRE(Close(shown.hits.At(MirrorX, BoardY)[2], 0.0f, 1e-3f));
    for (const std::uint32_t y : {BoardY, 2u})
    {
        const GlProbe::Pixel pixel = shown.At(MirrorX, y);
        REQUIRE(std::max({pixel[0], pixel[1], pixel[2]}) < 1e-2f);
    }
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: the ReflectionConfidence debug view shows the hit's confidence as grey", "[renderer][gl][ssr]")
{
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddRoom(test, true, 0.0f, Fenrir::Colour::Black);
    AddBoard(test, 6.0f);

    const Shown shown = ShowDebugView(test, camera, Fenrir::CameraDebugView::ReflectionConfidence);
    REQUIRE(shown.hits.width == Size);
    for (const std::uint32_t y : {MirrorY, BoardY, MirrorY + 6u})
    {
        const float confidence = shown.hits.At(MirrorX, y)[2];
        const GlProbe::Pixel pixel = shown.At(MirrorX, y);
        INFO("row " << y << ": grey " << pixel[0] << ", " << pixel[1] << ", " << pixel[2] << " for confidence "
                    << confidence);
        REQUIRE(Close(pixel[0], confidence, 1e-2f));
        REQUIRE(Close(pixel[1], pixel[0], 1e-3f));
        REQUIRE(Close(pixel[2], pixel[0], 1e-3f));
    }
    INFO("sure below the board, and nothing on the board itself");
    REQUIRE(shown.At(MirrorX, MirrorY)[0] > 0.5f);
    REQUIRE(shown.At(MirrorX, BoardY)[0] < 1e-2f);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GL: with reflections off, both reflection debug views are black", "[renderer][gl][ssr]")
{
    const Fenrir::CameraDebugView view =
        GENERATE(Fenrir::CameraDebugView::Reflections, Fenrir::CameraDebugView::ReflectionConfidence);
    GlScene test;
    test.RequireOpenGL();
    Fenrir::Camera& camera = AddRoom(test, false, 0.0f, {0.0f, 0.0f, 0.5f, 1.0f});
    AddBoard(test, 6.0f);

    const Shown shown = ShowDebugView(test, camera, view);
    INFO("view " << static_cast<int>(view) << " is black everywhere");
    REQUIRE(Brightest(shown) < 1e-3f);
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}
