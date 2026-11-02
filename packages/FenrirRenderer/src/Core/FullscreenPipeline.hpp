#pragma once

#include <string_view>

#include "Device/GraphicsDevice.hpp"

namespace Fenrir
{
    class ILogger;
} // namespace Fenrir

namespace Fenrir::Render
{
    /**
     * @brief A built-in pipeline that draws a fullscreen triangle: `desc` with no culling and no depth attachment, so
     * the caller sets only its shader, sets, colour formats, blending and samples. When the device cannot make it, logs
     * why under `name` and returns an empty handle.
     */
    [[nodiscard]] Graphics::PipelineHandle MakeFullscreenPipeline(Graphics::GraphicsDevice& device, ILogger& logger,
                                                                  Graphics::PipelineDesc desc, std::string_view name);
} // namespace Fenrir::Render
