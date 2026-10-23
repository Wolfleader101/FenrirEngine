#pragma once

#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "Core/DebugFeature.hpp"
#include "Core/RendererCore.hpp"
#include "Core/SceneRenderer.hpp"
#include "Device/GraphicsSurface.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirRenderer/Renderer.hpp"

namespace Fenrir
{
    class App;
}

namespace Fenrir::Detail
{
    /**
     * @brief The main window's graphics surface for the OpenGL backend, through FenrirWindow's surface functions
     * (ADR-0022). A minimised window reads as an empty size, so nothing is drawn to it.
     */
    class WindowSurface final : public Graphics::GraphicsSurface
    {
      public:
        explicit WindowSurface(App& app) : m_app(&app)
        {
        }

        [[nodiscard]] Graphics::PixelSize GetPixelSize() const override;
        [[nodiscard]] void* GetProcAddress(const char* name) override;
        bool MakeCurrent() override;
        void Present() override;
        bool SetSwapInterval(int interval) override;

      private:
        App* m_app;
    };

    /**
     * @brief The surface the null backend draws into: the main window's pixel size while it exists (empty while it is
     * minimised), else a fixed size. It presents nothing.
     */
    class NullSurface final : public Graphics::GraphicsSurface
    {
      public:
        /** @brief The size with no window, the default main window's. */
        static constexpr Graphics::PixelSize FixedSize{1280, 720};

        explicit NullSurface(App& app) : m_app(&app)
        {
        }

        /** @brief A size RendererProbe sets to stand in for a resized or minimised window; none by default. */
        std::optional<Graphics::PixelSize> sizeOverride;

        [[nodiscard]] Graphics::PixelSize GetPixelSize() const override;
        [[nodiscard]] void* GetProcAddress(const char*) override
        {
            return nullptr;
        }
        bool MakeCurrent() override
        {
            return true;
        }
        void Present() override
        {
        }
        bool SetSwapInterval(int) override
        {
            return true;
        }

      private:
        App* m_app;
    };

    /** @brief What Renderer::Install set up, shared by the owner and held weakly by the Render system. */
    struct RendererState
    {
        App* app = nullptr;
        RendererConfig config;
        /** @brief Whether the main window's surface was requested, and so is released at teardown. */
        bool holdsSurface = false;
        std::unique_ptr<Graphics::GraphicsSurface> surface;
        std::unique_ptr<Render::RendererCore> core;
        /** @brief The core's scene render feature, which the core owns. */
        Render::SceneRenderer* scene = nullptr;
        /** @brief The core's debug render feature, which the core owns. */
        Render::DebugFeature* debug = nullptr;
        RendererStats stats;
        /** @brief Seconds of frame time since the first Render. */
        double time = 0.0;

        /** @name Logged once
         * @{
         */
        StaleHandleLog staleMeshes;
        StaleHandleLog fallbackMaterials;
        bool loggedNoCamera = false;
        bool loggedLights = false;
        /** @brief Cameras that draw nothing, by entity id and why. */
        std::set<std::pair<std::uint32_t, std::uint8_t>> loggedCameras;
        /** @brief Cameras of equal order into one target, by their entity ids. */
        std::set<std::pair<std::uint32_t, std::uint32_t>> loggedTies;
        /** @brief Images materials name that cannot be sampled as a 2D texture, by index and generation. */
        std::set<std::pair<std::uint32_t, std::uint32_t>> loggedImages;
        /** @brief Sky cubemaps that are empty, stale or not cubemaps with pixels. */
        StaleHandleLog skyCubemaps;
        /** @} */

        RendererState() = default;
        RendererState(const RendererState&) = delete;
        RendererState& operator=(const RendererState&) = delete;
        RendererState(RendererState&&) = delete;
        RendererState& operator=(RendererState&&) = delete;
        /** @brief Frees the GPU side, then releases the main window's surface. */
        ~RendererState();
    };
} // namespace Fenrir::Detail
