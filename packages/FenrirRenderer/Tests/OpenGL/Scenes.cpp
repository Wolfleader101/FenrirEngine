// The renderer's ECS facade on OpenGL (the spec's seam 2): an App with a hidden main window, FenrirAssets and the
// Renderer, run one frame at a time, each test reading back the view's SceneColor, the linear target the scene is drawn
// into before Output encodes it for the window.

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirWindow/Window.hpp"
#include "GlProbe.hpp"
#include "RecordingLogger.hpp"
#include "RendererProbe.hpp"
#include "RendererTestApp.hpp"

using Fenrir::GlProbe;
using Fenrir::RendererProbe;

namespace
{
    constexpr std::uint32_t Size = 64;
    // The Background pass's colour until the sky (renderer ticket 15), linear.
    constexpr GlProbe::Pixel Background{0.015f, 0.015f, 0.015f, 1.0f};

    /**
     * @brief An App with a hidden 64x64 main window, FenrirAssets and the Renderer on OpenGL with debug validation, as
     * an app installs them. Call RequireOpenGL first: without a display or a 4.6 context the test reports skipped.
     */
    class GlScene
    {
      public:
        std::shared_ptr<RendererTest::LoggedMessages> messages = std::make_shared<RendererTest::LoggedMessages>();
        Fenrir::App app{std::make_unique<RendererTest::RecordingLogger>(messages), 1};
        Fenrir::Window window{Fenrir::Window::Install(app, HiddenWindow())};
        Fenrir::Assets assets{Fenrir::Assets::Install(app)};
        Fenrir::Renderer renderer{Fenrir::Renderer::Install(app, OpenGLWithValidation())};

        /** @brief Skips the calling test, saying why, when the renderer could not start on OpenGL 4.6. */
        void RequireOpenGL() const
        {
            if (!renderer.IsValid())
            {
                SKIP("the renderer could not start on OpenGL 4.6: "
                     << (messages->errors.empty() ? std::string("no window") : messages->errors.back()));
            }
        }

        /** @brief A camera at the origin, looking along -Z. */
        void AddCamera()
        {
            app.GetActiveScene().GetEntityList().CreateEntity().AddComponent<Fenrir::Camera>();
        }

        /** @brief A square of side `size` facing the camera at distance `distance`, in a material on `shader`. */
        Fenrir::AssetHandle<Fenrir::Material> AddQuad(float size, float distance, const char* shader,
                                                      Fenrir::Colour colour)
        {
            Fenrir::AssetStore& store = assets.Store();
            const auto mesh = store.Add<Fenrir::Mesh>(RendererTest::Quad(size, -distance), Fenrir::AssetOwner::App());
            const auto material = store.CreateMaterial(
                store.Load<Fenrir::Shader>(shader, Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
            store.SetColour(material, "baseColour", colour);
            Fenrir::Entity entity = app.GetActiveScene().GetEntityList().CreateEntity();
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
            return material;
        }

        /** @brief Runs one frame and reads back its SceneColor. */
        GlProbe::Image Frame()
        {
            app.RunFrame(1.0 / 60.0);
            Fenrir::Graphics::GraphicsDevice& device = RendererProbe::Device(renderer);
            const Fenrir::Graphics::TextureHandle sceneColour = RendererProbe::SceneColour(renderer);
            REQUIRE_FALSE(sceneColour.IsEmpty());
            return GlProbe::Read(device, sceneColour);
        }

      private:
        static Fenrir::WindowConfig HiddenWindow()
        {
            Fenrir::WindowConfig config;
            config.title = "FenrirRendererGlTests";
            config.logicalSize = {static_cast<int>(Size), static_cast<int>(Size)};
            config.visible = false;
            return config;
        }

        static Fenrir::RendererConfig OpenGLWithValidation()
        {
            Fenrir::RendererConfig config;
            config.backend = Fenrir::GraphicsBackend::OpenGL;
            config.debugValidation = true;
            return config;
        }
    };
} // namespace

TEST_CASE("An unlit quad draws its base colour, with the background around it", "[renderer][gl]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const GlProbe::Pixel colour{0.2f, 0.6f, 0.4f, 1.0f};
    test.AddQuad(1.0f, 2.0f, "builtin:unlit", Fenrir::Colour{colour[0], colour[1], colour[2], colour[3]});

    const GlProbe::Image image = test.Frame();

    INFO("the centre, which the quad covers, holds its base colour times white vertex colour");
    REQUIRE(GlProbe::Near(image.At(Size / 2, Size / 2), colour));
    INFO("a corner, which nothing covers, holds the background colour");
    REQUIRE(GlProbe::Near(image.At(0, 0), Background));
    INFO("no GL error or other error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("The nearer of two quads wins under reversed-Z", "[renderer][gl]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    // The far quad is bigger, so it shows around the near one.
    test.AddQuad(1.0f, 2.0f, "builtin:unlit", Fenrir::Colour::Red);
    test.AddQuad(4.0f, 4.0f, "builtin:unlit", Fenrir::Colour::Green);

    const GlProbe::Image image = test.Frame();

    INFO("the centre, where both quads are, holds the nearer red one");
    REQUIRE(GlProbe::Near(image.At(Size / 2, Size / 2), {1.0f, 0.0f, 0.0f, 1.0f}));
    INFO("beside it, only the far green quad is drawn");
    REQUIRE(GlProbe::Near(image.At(8, Size / 2), {0.0f, 1.0f, 0.0f, 1.0f}));
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A lit face is its base colour times N.L of the light, plus ambient", "[renderer][gl]")
{
    GlScene test;
    test.RequireOpenGL();
    test.AddCamera();
    const auto material = test.AddQuad(1.0f, 2.0f, "builtin:lit", Fenrir::Colour{0.8f, 0.8f, 0.8f, 1.0f});
    // Fully rough, so GGX's specular adds under 1%.
    test.assets.Store().SetFloat(material, "roughness", 1.0f);
    // A white light travelling along its entity's -Z, turned 60 degrees about +Y: (-sin 60, 0, -cos 60), so N.L = 0.5
    // on the quad facing +Z.
    Fenrir::Entity light = test.app.GetActiveScene().GetEntityList().CreateEntity();
    light.GetComponent<Fenrir::Transform>().rot =
        glm::angleAxis(Fenrir::Math::DegToRad(60.0f), Fenrir::Math::Vec3(0.0f, 1.0f, 0.0f));
    light.AddComponent<Fenrir::DirectionalLight>();
    test.app.GetActiveScene().AddResource<Fenrir::AmbientLight>(
        Fenrir::AmbientLight{Fenrir::Colour{0.1f, 0.1f, 0.1f, 1.0f}, 1.0f});

    const GlProbe::Image image = test.Frame();

    // 0.8 x 0.5 from the light, plus 0.8 x 0.1 from ambient: 0.48. Fresnel at 30 degrees takes about 4% of the
    // diffuse and specular gives back about 1%, inside the tolerance.
    INFO("the face is 0.8 x N.L + 0.8 x ambient = 0.48 in each colour channel");
    REQUIRE(GlProbe::Near(image.At(Size / 2, Size / 2), {0.48f, 0.48f, 0.48f, 1.0f}, 0.02f));
    REQUIRE(test.messages->errors.empty());
}
