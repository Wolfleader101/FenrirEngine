#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "FenrirDraw2D/DrawList2D.hpp"

namespace RendererTest
{
    /** @brief An RGBA8 colour as Vertex2D packs it, red in the lowest byte. */
    constexpr std::uint32_t Rgba8(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
    {
        return std::uint32_t{r} | (std::uint32_t{g} << 8u) | (std::uint32_t{b} << 16u) | (std::uint32_t{a} << 24u);
    }

    /** @brief A 2D pipeline description for builtin:2d (or `shader`) on the ImGui vertex layout. */
    inline Fenrir::PipelineDescription2D Pipeline2D(Fenrir::AssetHandle<Fenrir::Shader> shader,
                                                    Fenrir::BlendMode2D blend = Fenrir::BlendMode2D::AlphaBlend,
                                                    Fenrir::ColourSpace input = Fenrir::ColourSpace::Srgb)
    {
        Fenrir::PipelineDescription2D pipeline;
        pipeline.shader = shader;
        pipeline.vertexLayout = Fenrir::Vertex2DLayout();
        pipeline.blendMode = blend;
        pipeline.inputColourSpace = input;
        return pipeline;
    }

    /**
     * @brief Appends a quad from `min` to `max` (local units) of one colour to `list`, six 16-bit indices, and a
     * command drawing it with pipeline `pipeline`; returns the command.
     */
    inline Fenrir::Command2D& AddQuad(Fenrir::DrawList2D& list, std::uint32_t pipeline, Fenrir::Math::Vec2 min,
                                      Fenrir::Math::Vec2 max, std::uint32_t colour = 0xFFFFFFFFu)
    {
        const std::array<Fenrir::Vertex2D, 4> corners{{
            {{min.x, min.y}, {0.0f, 0.0f}, colour},
            {{max.x, min.y}, {1.0f, 0.0f}, colour},
            {{max.x, max.y}, {1.0f, 1.0f}, colour},
            {{min.x, max.y}, {0.0f, 1.0f}, colour},
        }};
        const auto first = static_cast<std::uint16_t>(list.AppendVertices(std::span<const Fenrir::Vertex2D>(corners)));
        const std::array<std::uint16_t, 6> indices{
            first, static_cast<std::uint16_t>(first + 2), static_cast<std::uint16_t>(first + 1),
            first, static_cast<std::uint16_t>(first + 3), static_cast<std::uint16_t>(first + 2)};
        Fenrir::Command2D& command = list.commands.emplace_back();
        command.pipeline = pipeline;
        command.indexOffset = list.AppendIndices(std::span<const std::uint16_t>(indices));
        command.indexCount = 6;
        return command;
    }
} // namespace RendererTest
