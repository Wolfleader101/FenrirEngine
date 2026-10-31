#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirWindow/Window.hpp"
#include "GlProbe.hpp"
#include "GoldenImages.hpp"
#include "RecordingLogger.hpp"
#include "RendererProbe.hpp"
#include "RendererTestApp.hpp"

namespace GlTest
{
    /**
     * @brief An App with a hidden `Size` x `Size` main window, FenrirAssets and the Renderer on OpenGL with debug
     * validation, as an app installs them, run one frame at a time. Call RequireOpenGL first: without a display or a
     * 4.6 context the test reports skipped.
     */
    class GlScene
    {
      public:
        /** @brief The main window's side, in pixels. */
        static constexpr std::uint32_t Size = 64;

        explicit GlScene(std::uint32_t msaa = 1) : renderer(Fenrir::Renderer::Install(app, OpenGLWithValidation(msaa)))
        {
        }

        std::shared_ptr<RendererTest::LoggedMessages> messages = std::make_shared<RendererTest::LoggedMessages>();
        Fenrir::App app{std::make_unique<RendererTest::RecordingLogger>(messages), 1};
        Fenrir::Window window{Fenrir::Window::Install(app, HiddenWindow())};
        Fenrir::Assets assets{Fenrir::Assets::Install(app)};
        Fenrir::Renderer renderer;

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
        Fenrir::Camera& AddCamera()
        {
            return app.GetActiveScene().GetEntityList().CreateEntity().AddComponent<Fenrir::Camera>();
        }

        /** @brief A square of side `size` facing the camera at distance `distance`, drawn with `material`. */
        void AddQuad(float size, float distance, Fenrir::AssetHandle<Fenrir::Material> material)
        {
            Fenrir::AssetStore& store = assets.Store();
            const auto mesh = store.Add<Fenrir::Mesh>(RendererTest::Quad(size, -distance), Fenrir::AssetOwner::App());
            Fenrir::Entity entity = app.GetActiveScene().GetEntityList().CreateEntity();
            entity.AddComponent<Fenrir::MeshRenderer>(Fenrir::MeshRenderer{mesh, material, true});
        }

        /** @brief A square of side `size` facing the camera at distance `distance`, in a material on `shader`. */
        Fenrir::AssetHandle<Fenrir::Material> AddQuad(float size, float distance, const char* shader,
                                                      Fenrir::Colour colour)
        {
            Fenrir::AssetStore& store = assets.Store();
            const auto material = store.CreateMaterial(
                store.Load<Fenrir::Shader>(shader, Fenrir::AssetOwner::App()).handle, Fenrir::AssetOwner::App());
            store.SetColour(material, "baseColour", colour);
            AddQuad(size, distance, material);
            return material;
        }

        /** @brief Runs one frame. */
        void RunFrame()
        {
            app.RunFrame(1.0 / 60.0);
        }

        /** @brief Runs one frame and reads back its SceneColor (of `width` x `height`, or any size when 0). */
        Fenrir::GlProbe::Image Frame(std::uint32_t width = 0, std::uint32_t height = 0)
        {
            RunFrame();
            Fenrir::Graphics::GraphicsDevice& device = Fenrir::RendererProbe::Device(renderer);
            const Fenrir::Graphics::TextureHandle sceneColour =
                Fenrir::RendererProbe::SceneColour(renderer, width, height);
            REQUIRE_FALSE(sceneColour.IsEmpty());
            return Fenrir::GlProbe::Read(device, sceneColour);
        }

        /**
         * @brief Reads back the render target image `image` as it stores it, 8 bits a channel, for a golden image.
         * Requires that the renderer prepared it.
         */
        RendererTest::Rgba8Image ReadTarget(Fenrir::AssetHandle<Fenrir::Image> image)
        {
            const Fenrir::Graphics::TextureHandle texture = Fenrir::RendererProbe::ImageTexture(renderer, image);
            REQUIRE_FALSE(texture.IsEmpty());
            const Fenrir::GlProbe::Image pixels =
                Fenrir::GlProbe::Read(Fenrir::RendererProbe::Device(renderer), texture);
            RendererTest::Rgba8Image frame;
            frame.width = pixels.width;
            frame.height = pixels.height;
            frame.rgba.reserve(pixels.rgba.size());
            for (const float value : pixels.rgba)
            {
                frame.rgba.push_back(static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)));
            }
            return frame;
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

        static Fenrir::RendererConfig OpenGLWithValidation(std::uint32_t msaa)
        {
            Fenrir::RendererConfig config;
            config.backend = Fenrir::GraphicsBackend::OpenGL;
            config.debugValidation = true;
            config.msaa = msaa;
            return config;
        }
    };
} // namespace GlTest
