#include "Core/FullscreenPasses.hpp"

#include "Core/BuiltinShaders.hpp"

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
        return MakeBuiltinPipeline(device, logger, desc, name);
    }
} // namespace Fenrir::Render
