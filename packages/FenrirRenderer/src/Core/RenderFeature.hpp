#pragma once

#include <cstdint>

#include "Core/RenderGraph.hpp"
#include "Device/GraphicsDevice.hpp"
#include "Device/GraphicsSurface.hpp"

namespace Fenrir::Render
{
    /** @brief What a render feature is given each frame to add its passes with. */
    struct FrameContext
    {
        /** @brief The device, to make the resources its passes record with. */
        Graphics::GraphicsDevice& device;
        /** @brief The frame being built; per-frame buffers ring by it. */
        std::uint64_t frameIndex = 0;
        /** @brief The surface's size in pixels this frame; never empty, since an empty one builds no graph. */
        Graphics::PixelSize size;
        /** @brief The surface's backbuffer, imported into the graph. */
        GraphResource backbuffer;
    };

    /**
     * @brief A part of the renderer core that adds render passes to each frame's render graph, such as the background
     * or debug drawing (ADR-0019). The interface is the core's own: game code and other packages add none.
     */
    class RenderFeature
    {
      public:
        virtual ~RenderFeature() = default;

        /** @brief Adds this frame's passes, and any transients they use, to `graph`. */
        virtual void AddPasses(RenderGraph& graph, const FrameContext& frame) = 0;

      protected:
        RenderFeature() = default;
        RenderFeature(const RenderFeature&) = default;
        RenderFeature& operator=(const RenderFeature&) = default;
        RenderFeature(RenderFeature&&) = default;
        RenderFeature& operator=(RenderFeature&&) = default;
    };
} // namespace Fenrir::Render
