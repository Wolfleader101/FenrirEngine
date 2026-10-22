#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Core/RenderFeature.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/TransientPool.hpp"
#include "Device/GraphicsDevice.hpp"
#include "Device/GraphicsSurface.hpp"

namespace Fenrir
{
    struct RendererProbe;
}

namespace Fenrir::Render
{
    /** @brief What one frame did. */
    struct FrameReport
    {
        /** @brief The frame's index; the next frame's is one more, drawn or not. */
        std::uint64_t frameIndex = 0;
        /** @brief Whether anything was built and submitted: false for an empty surface or a graph with errors. */
        bool drawn = false;
        /** @brief How many render passes ran and how many were culled. */
        std::uint32_t passesRun = 0;
        std::uint32_t passesCulled = 0;
        /** @brief The render graph's errors, one line each; with any, nothing was submitted. */
        std::vector<std::string> errors;
    };

    /**
     * @brief The renderer core (ADR-0019): the middle layer, over the graphics device and under the ECS facade. Each
     * frame it reads the surface's size, has its render features add passes to a new render graph, compiles it, records
     * each pass into its own CommandList and submits them in order, then presents. It knows nothing of the app, scenes
     * or entities, so it runs with a fake surface and the null backend.
     */
    class RendererCore
    {
      public:
        /** @brief Draws through `device` into `surface`, which must outlive the core. */
        RendererCore(std::unique_ptr<Graphics::GraphicsDevice> device, Graphics::GraphicsSurface& surface);

        /** @brief Waits for the GPU, then destroys the features and every resource the core made. */
        ~RendererCore();

        RendererCore(const RendererCore&) = delete;
        RendererCore& operator=(const RendererCore&) = delete;
        RendererCore(RendererCore&&) = delete;
        RendererCore& operator=(RendererCore&&) = delete;

        /** @brief The device everything is drawn through. */
        [[nodiscard]] Graphics::GraphicsDevice& Device() noexcept
        {
            return *m_device;
        }

        /** @brief Adds a render feature; features add their passes in the order they were added. */
        void AddFeature(std::unique_ptr<RenderFeature> feature);

        /**
         * @brief Builds, records, submits and presents one frame. An empty surface (a minimised window) builds,
         * submits and presents nothing, but the frame index still advances, so deferred destruction keeps going.
         */
        FrameReport RenderFrame();

        /** @brief The index the next frame will have. */
        [[nodiscard]] std::uint64_t FrameIndex() const noexcept
        {
            return m_frameIndex;
        }

      private:
        friend struct Fenrir::RendererProbe;

        std::unique_ptr<Graphics::GraphicsDevice> m_device;
        Graphics::GraphicsSurface& m_surface;
        TransientPool m_transients;
        std::vector<std::unique_ptr<RenderFeature>> m_features;
        std::uint64_t m_frameIndex = 0;
    };
} // namespace Fenrir::Render
