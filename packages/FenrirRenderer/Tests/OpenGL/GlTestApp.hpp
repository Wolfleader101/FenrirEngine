#pragma once

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Backend/OpenGL/OpenGLDevice.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirWindow/Window.hpp"
#include "RecordingLogger.hpp"

namespace GlTest
{
    /**
     * @brief The main window's graphics surface as the OpenGL backend sees it, through FenrirWindow's surface functions
     * (ADR-0022), as the renderer's ECS facade adapts it. It can refuse one swap interval, as a driver without that
     * mode does.
     */
    class WindowSurface final : public Fenrir::Graphics::GraphicsSurface
    {
      public:
        WindowSurface(Fenrir::App& app, Fenrir::WindowId window) : m_app(&app), m_window(window)
        {
        }

        /** @brief A swap interval SetSwapInterval refuses without asking the window; none by default. */
        std::optional<int> refusedInterval;

        [[nodiscard]] Fenrir::Graphics::PixelSize GetPixelSize() const override
        {
            const Fenrir::WindowSize size = Fenrir::Window::PixelSize(*m_app, m_window);
            return {static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height)};
        }

        [[nodiscard]] void* GetProcAddress(const char* name) override
        {
            return Fenrir::Window::GetProcAddress(*m_app, m_window, name);
        }

        bool MakeCurrent() override
        {
            return Fenrir::Window::MakeCurrent(*m_app, m_window);
        }

        void Present() override
        {
            Fenrir::Window::Present(*m_app, m_window);
        }

        bool SetSwapInterval(int interval) override
        {
            return interval != refusedInterval && Fenrir::Window::SetSwapInterval(*m_app, m_window, interval);
        }

      private:
        Fenrir::App* m_app;
        Fenrir::WindowId m_window;
    };

    /**
     * @brief The OpenGL backend's logger in the GL tests. By default an error, which with debug validation includes
     * every GL error and high-severity debug message, fails the running test, the one whose GL call caused it, since
     * the callback is synchronous. A test about the messages themselves records errors instead.
     */
    class GlLogger final : public RendererTest::RecordingLogger
    {
      public:
        explicit GlLogger(std::shared_ptr<RendererTest::LoggedMessages> messages) : RecordingLogger(std::move(messages))
        {
        }

        /** @brief Whether an error fails the running test (true) or is only recorded. */
        bool failOnError = true;

      protected:
        void InfoImpl(std::string_view message) override
        {
            // The OpenGL version and renderer the backend started on, printed once, so a log shows what was tested.
            static bool printed = false;
            if (!printed && message.starts_with("OpenGL backend started"))
            {
                printed = true;
                std::cout << message << '\n';
            }
        }

        void ErrorImpl(std::string_view message) override
        {
            if (failOnError)
            {
                FAIL_CHECK("the OpenGL backend logged an error: " << message);
            }
            RecordingLogger::ErrorImpl(message);
        }
    };

    /** @brief The side of the GL tests' offscreen targets, in pixels. */
    inline constexpr std::uint32_t TargetSize = 64;

    /**
     * @brief An App with FenrirWindow and a hidden main window holding an OpenGL 4.6 core context, made as the renderer
     * makes it (spec: seam 2), and an OpenGL device on it with debug validation. Nothing is presented. Call
     * RequireOpenGL first: without a display or a 4.6 context the test reports skipped.
     */
    class GlTestApp
    {
      public:
        explicit GlTestApp(std::optional<int> refusedInterval = std::nullopt)
        {
            if (!window.IsValid())
            {
                m_whyNot = "SDL's video could not start (no display)";
                return;
            }
            Fenrir::SurfaceRequest request;
            request.debugContext = true;
            std::string error;
            if (!Fenrir::Window::RequestSurface(app, Fenrir::Window::MainWindow(), request, error))
            {
                m_whyNot = "no OpenGL 4.6 core context: " + error;
                return;
            }
            surface.refusedInterval = refusedInterval;
            Fenrir::Graphics::DeviceConfig config;
            config.debugValidation = true;
            Fenrir::Graphics::OpenGLDeviceResult result = Fenrir::Graphics::CreateOpenGLDevice(surface, config, logger);
            device = std::move(result.device);
            m_whyNot = std::move(result.diagnostic);
        }

        ~GlTestApp()
        {
            device.reset();
            Fenrir::Window::ReleaseSurface(app, Fenrir::Window::MainWindow());
        }

        GlTestApp(const GlTestApp&) = delete;
        GlTestApp& operator=(const GlTestApp&) = delete;
        GlTestApp(GlTestApp&&) = delete;
        GlTestApp& operator=(GlTestApp&&) = delete;

        /** @brief Skips the calling test, saying why, when there is no OpenGL 4.6 core device to test. */
        void RequireOpenGL() const
        {
            if (device == nullptr)
            {
                SKIP("no OpenGL 4.6 core device: " << m_whyNot);
            }
        }

        /** @brief A new TargetSize-square RGBA8 offscreen target. */
        [[nodiscard]] Fenrir::Graphics::TextureHandle MakeTarget()
        {
            Fenrir::Graphics::TextureDesc desc;
            desc.format = Fenrir::Graphics::TextureFormat::Rgba8Unorm;
            desc.width = TargetSize;
            desc.height = TargetSize;
            desc.usage = Fenrir::Graphics::TextureUsage::ColourAttachment | Fenrir::Graphics::TextureUsage::Sampled;
            return device->CreateTexture(desc, "GL test target");
        }

        std::shared_ptr<RendererTest::LoggedMessages> appMessages = std::make_shared<RendererTest::LoggedMessages>();
        Fenrir::App app{std::make_unique<RendererTest::RecordingLogger>(appMessages), 1};
        Fenrir::Window window{Fenrir::Window::Install(app, HiddenWindow())};
        WindowSurface surface{app, Fenrir::Window::MainWindow()};
        std::shared_ptr<RendererTest::LoggedMessages> messages = std::make_shared<RendererTest::LoggedMessages>();
        GlLogger logger{messages};
        std::unique_ptr<Fenrir::Graphics::GraphicsDevice> device;

      private:
        static Fenrir::WindowConfig HiddenWindow()
        {
            Fenrir::WindowConfig config;
            config.title = "FenrirRendererGlTests";
            config.logicalSize = {TargetSize, TargetSize};
            config.visible = false;
            return config;
        }

        std::string m_whyNot;
    };

    /** @brief A shader container embedded by fenrir_add_shaders, read back; throws if it does not read. */
    inline Fenrir::ShaderContainer ReadContainer(std::span<const std::uint8_t> embedded)
    {
        Fenrir::ShaderContainerResult read = Fenrir::ReadShaderContainer(std::as_bytes(embedded));
        if (!read.Ok())
        {
            throw std::runtime_error("the fixture container does not read: " + read.diagnostic);
        }
        return std::move(*read.container);
    }

    /** @brief A value's bytes, for a buffer's initial contents. */
    template <typename T>
    std::span<const std::byte> BytesOf(const T& value)
    {
        return std::as_bytes(std::span(&value, 1));
    }
} // namespace GlTest
