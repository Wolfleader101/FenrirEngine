#include "FenrirPainter/PainterVertex.hpp"

#include <cstddef>

namespace Fenrir
{
    VertexLayout2D PainterVertexLayout()
    {
        using F = VertexFormat2D;
        const auto at = [](std::size_t offset) { return static_cast<std::uint32_t>(offset); };
        VertexLayout2D layout;
        layout.stride = sizeof(PainterVertex);
        layout.attributes = {
            {0, F::Float2, at(offsetof(PainterVertex, position))},
            {1, F::Float2, at(offsetof(PainterVertex, uv))},
            // local and clipPoint, then halfSize and clipHalfSize, each pair one vec4.
            {2, F::Float4, at(offsetof(PainterVertex, local))},
            {3, F::Float4, at(offsetof(PainterVertex, halfSize))},
            {4, F::Float4, at(offsetof(PainterVertex, radii))},
            {5, F::Float4, at(offsetof(PainterVertex, clipRadii))},
            {6, F::Float4, at(offsetof(PainterVertex, shape))},
            {7, F::Float4, at(offsetof(PainterVertex, extents))},
            {8, F::Unorm8x4, at(offsetof(PainterVertex, colour))},
            {9, F::Unorm8x4, at(offsetof(PainterVertex, colour2))},
            {10, F::Unorm8x4, at(offsetof(PainterVertex, borderColours))},
            {11, F::Unorm8x4, at(offsetof(PainterVertex, borderColours) + 4)},
            {12, F::Unorm8x4, at(offsetof(PainterVertex, borderColours) + 8)},
            {13, F::Unorm8x4, at(offsetof(PainterVertex, borderColours) + 12)},
            {14, F::Unorm8x4, at(offsetof(PainterVertex, kinds))},
        };
        return layout;
    }

    PipelineDescription2D PainterPipeline(AssetHandle<Shader> shader, BlendSpace2D blendSpace, IndexType2D indexType)
    {
        PipelineDescription2D pipeline;
        pipeline.shader = shader;
        pipeline.vertexLayout = PainterVertexLayout();
        pipeline.indexType = indexType;
        pipeline.blendMode = BlendMode2D::PremultipliedAlpha;
        pipeline.inputColourSpace = ColourSpace::Srgb;
        pipeline.blendSpace = blendSpace;
        return pipeline;
    }
} // namespace Fenrir
