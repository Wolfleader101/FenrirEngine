#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Core/BlockRing.hpp"
#include "Core/DebugFont.hpp"
#include "Core/RenderFeature.hpp"
#include "Core/SceneRenderer.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

namespace Fenrir
{
    class ILogger;
    struct RendererProbe;
} // namespace Fenrir

namespace Fenrir::Render
{
    /** @brief Whether a debug shape is tested against the view's SceneDepth or drawn over everything. */
    enum class DebugDepth : std::uint8_t
    {
        Tested,
        OnTop,
    };

    /** @brief One vertex of a debug line or triangle, in world space. */
    struct DebugShapeVertex
    {
        Math::Vec3 position{0.0f};
        /** @brief Linear, with alpha. */
        Colour colour;
    };

    /** @brief Debug lines that draw alike: every two vertices are one line; two equal ones are a dot. */
    struct DebugLines
    {
        DebugDepth depth = DebugDepth::Tested;
        /** @brief In pixels. */
        float thickness = 1.0f;
        std::vector<DebugShapeVertex> vertices;
    };

    /** @brief Solid debug triangles that draw alike: every three vertices are one, alpha-blended without depth write.
     */
    struct DebugTriangles
    {
        DebugDepth depth = DebugDepth::Tested;
        std::vector<DebugShapeVertex> vertices;
    };

    /** @brief One run of debug text: UTF-8, at a point, with a height in pixels. */
    struct DebugText
    {
        std::string text;
        /** @brief World text: the world point it is centred on. Screen text: its top-left in logical units (z 0). */
        Math::Vec3 position{0.0f};
        float size = 16.0f;
        Colour colour;
    };

    /**
     * @brief The active scene's debug drawing for one frame, as FenrirDebugDraw tessellated it (ADR-0025): plain data
     * the facade copies in, so the core needs no FenrirDebugDraw.
     */
    struct DebugFrame
    {
        std::vector<DebugLines> lines;
        std::vector<DebugTriangles> triangles;
        std::vector<DebugText> worldTexts;
        std::vector<DebugText> screenTexts;
        /** @brief On-screen messages, newest first; their positions are unused. */
        std::vector<DebugText> messages;
        /** @brief The main window's pixels per logical unit. */
        float contentScale = 1.0f;

        /** @brief Whether there is nothing to draw. */
        [[nodiscard]] bool Empty() const noexcept
        {
            return lines.empty() && triangles.empty() && worldTexts.empty() && screenTexts.empty() && messages.empty();
        }
    };

    /**
     * @brief The render feature that draws debug drawing (ADR-0025, ADR-0026), after the scene renderer, into the views
     * it drew.
     *
     * **Each view** whose ViewRequest::drawDebug is set gets a `Debug` pass into its SceneColor, testing its SceneDepth
     * without writing it: the depth-tested triangles and lines, then the on-top ones (a pipeline with no depth test),
     * then world text. Lines are read from a storage buffer and expanded in the vertex shader into quads their
     * thickness in pixels across; triangles alpha-blend, unsorted. World text is centred on its point's place in the
     * view, at a constant pixel size (times the content scale in a view of the window), never occluded, and skipped
     * behind the camera. Shapes and text are uploaded each frame into per-frame ring buffers (ADR-0019).
     *
     * **The window**, when a view of the scene draws into it, gets one `Overlay` pass after every view's Output: screen
     * text at its logical position and the on-screen messages stacked newest first from the top-left, both scaled by
     * the content scale, as the top layer. Views into images get none.
     *
     * Text is drawn from the DebugFont's signed-distance-field atlas. With nothing to draw, no pass is added.
     */
    class DebugFeature final : public RenderFeature
    {
      public:
        /** @brief Makes the font atlas and the debug pipelines on `device`, for the views `scene` draws. */
        DebugFeature(Graphics::GraphicsDevice& device, ILogger& logger, const SceneRenderer& scene);
        ~DebugFeature() override;

        DebugFeature(const DebugFeature&) = delete;
        DebugFeature& operator=(const DebugFeature&) = delete;
        DebugFeature(DebugFeature&&) = delete;
        DebugFeature& operator=(DebugFeature&&) = delete;

        /** @brief What the next frame draws. */
        void SetFrame(DebugFrame frame);

        void AddPasses(RenderGraph& graph, const FrameContext& frame) override;

      private:
        friend struct Fenrir::RendererProbe;

        /** @brief A vertex buffer written each frame, one per frame in flight, grown as needed. */
        class VertexRing;

        /** @brief A range of a frame's text vertices drawn with one target block. */
        struct TextDraw
        {
            std::uint32_t firstVertex = 0;
            std::uint32_t vertexCount = 0;
            std::size_t targetSlot = 0;
        };

        /** @brief Appends the glyph quads of `text`, laid out from `origin` at `size`, to this frame's text vertices.
         */
        void AppendText(const DebugText& text, Math::Vec2 origin, float size);
        /** @brief Writes a target block mapping `extent` local units onto `pixels`, at `slot`. */
        void WriteTarget(std::uint64_t frameIndex, std::size_t slot, Math::Vec2 extent, Math::Vec2 pixels,
                         float contentScale);
        [[nodiscard]] Graphics::PipelineHandle MakePipeline(const ShaderContainer& container,
                                                            Graphics::PipelineDesc desc);

        Graphics::GraphicsDevice& m_device;
        ILogger& m_logger;
        const SceneRenderer& m_scene;
        DebugFont m_font;
        DebugFrame m_frame;

        ShaderContainer m_lineShader;
        ShaderContainer m_triangleShader;
        ShaderContainer m_textShader;

        Graphics::BindingGroupLayoutHandle m_lineLayout;
        Graphics::BindingGroupLayoutHandle m_targetLayout;
        Graphics::BindingGroupLayoutHandle m_atlasLayout;
        /** @brief By DebugDepth: depth tested, then on top. */
        std::array<Graphics::PipelineHandle, 2> m_linePipelines{};
        std::array<Graphics::PipelineHandle, 2> m_trianglePipelines{};
        /** @brief World text in a view's Debug pass, and screen text in the window's Overlay pass. */
        Graphics::PipelineHandle m_viewTextPipeline;
        Graphics::PipelineHandle m_overlayTextPipeline;

        Graphics::TextureHandle m_atlas;
        Graphics::SamplerHandle m_atlasSampler;
        Graphics::BindingGroupHandle m_atlasGroup;

        std::unique_ptr<BlockRing> m_lineBlocks;
        std::unique_ptr<BlockRing> m_targetBlocks;
        std::unique_ptr<VertexRing> m_triangleVertices;
        std::unique_ptr<VertexRing> m_textVertices;
        /** @brief This frame's text vertices, as they are laid out. */
        std::vector<std::byte> m_textBytes;
    };
} // namespace Fenrir::Render
