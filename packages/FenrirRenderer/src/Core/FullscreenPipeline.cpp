#include "Core/FullscreenPipeline.hpp"

#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    using namespace Fenrir::Graphics;

    PipelineHandle MakeFullscreenPipeline(GraphicsDevice& device, ILogger& logger, PipelineDesc desc,
                                          std::string_view name)
    {
        desc.cull = CullMode::None;
        desc.depth.test = false;
        desc.depth.write = false;
        desc.depthFormat.reset();
        const PipelineResult made = device.CreatePipeline(desc, name);
        if (made.pipeline.IsEmpty())
        {
            logger.Error("FenrirRenderer: the built-in pipeline '{}' could not be made: {}", name, made.diagnostic);
        }
        return made.pipeline;
    }
} // namespace Fenrir::Render
