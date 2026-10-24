#include "Core/DebugFeature.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <utility>

#include "Core/BuiltinShaders.hpp"
#include "Core/FrameRingBuffer.hpp"
#include "FenrirLogger/ILogger.hpp"

namespace Fenrir::Render
{
    namespace
    {
        using namespace Fenrir::Graphics;

        /** @brief One debug line as `builtin:debug-lines` reads it (std430). */
        struct LineRecord
        {
            /** @brief xyz: the start; w: the thickness in pixels. */
            Math::Vec4 from;
            Math::Vec4 to;
            /** @brief The start's and end's sRGB-encoded RGBA8 colours, then two unused. */
            std::array<std::uint32_t, 4> colours{};
        };
        static_assert(sizeof(LineRecord) == 48);

        /** @brief One vertex of `builtin:debug-triangles`: position, then colour as sRGB-encoded RGBA8. */
        struct TriangleVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            std::uint32_t colour = 0;
        };
        static_assert(sizeof(TriangleVertex) == 16);

        /** @brief One vertex of `builtin:debug-text`, the ImGui layout: position, UV, sRGB-encoded RGBA8 colour. */
        struct TextVertex
        {
            float x = 0.0f;
            float y = 0.0f;
            float u = 0.0f;
            float v = 0.0f;
            std::uint32_t colour = 0;
        };
        static_assert(sizeof(TextVertex) == 20);

        /** @brief The target block, as `Fenrir/2D.glsl` declares it (std140). */
        struct TargetBlock
        {
            Math::Mat4 localToClip;
            Math::Vec4 pixelSize;
            Math::Vec4 contentScale;
        };
        static_assert(sizeof(TargetBlock) == 96);

        /** @brief Where on-screen messages start, from the window's top-left, and the gap between two, logical units.
         */
        constexpr float MessageMargin = 8.0f;
        constexpr float MessageGap = 2.0f;

        /** @brief A linear colour as sRGB-encoded RGBA8, red in the low byte; alpha stays linear. */
        std::uint32_t PackSrgb(const Colour& colour)
        {
            const auto encode = [](float linear) {
                const float c = std::clamp(linear, 0.0f, 1.0f);
                const float srgb = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
                return static_cast<std::uint32_t>(std::lround(srgb * 255.0f));
            };
            const auto alpha = static_cast<std::uint32_t>(std::lround(std::clamp(colour.a, 0.0f, 1.0f) * 255.0f));
            return encode(colour.r) | (encode(colour.g) << 8u) | (encode(colour.b) << 16u) | (alpha << 24u);
        }

        ShaderContainer ReadBuiltin(BuiltinProgram program, ILogger& logger)
        {
            ShaderContainerResult read = ReadShaderContainer(GetBuiltinShader(program).bytes);
            if (!read.Ok())
            {
                logger.Error("FenrirRenderer: the built-in shader '{}' does not read: {}",
                             GetBuiltinShader(program).name, read.diagnostic);
                return {};
            }
            return std::move(*read.container);
        }

        template <typename T>
        std::span<const std::byte> BytesOf(const std::vector<T>& values)
        {
            return std::as_bytes(std::span(values));
        }

        constexpr std::size_t IndexOf(DebugDepth depth)
        {
            return static_cast<std::size_t>(depth);
        }
    } // namespace

    class DebugFeature::VertexRing
    {
      public:
        VertexRing(GraphicsDevice& device, std::string label) : m_device(device), m_label(std::move(label))
        {
        }

        /** @brief Writes `bytes` from the start of frame `frameIndex`'s buffer, remaking the buffers if they are small.
         */
        void Write(std::uint64_t frameIndex, std::span<const std::byte> bytes)
        {
            if (bytes.size() > m_capacity || m_ring == nullptr)
            {
                // Grow by doubling, so debug drawing that keeps growing remakes its buffers rarely.
                m_capacity = std::max({bytes.size(), m_capacity * 2, std::size_t{4096}});
                m_ring.reset();
                m_ring =
                    std::make_unique<FrameRingBuffer>(m_device, BufferDesc{m_capacity, BufferUsage::Vertex}, m_label);
            }
            m_ring->Write(frameIndex, 0, bytes);
        }

        [[nodiscard]] BufferHandle Current(std::uint64_t frameIndex) const
        {
            return m_ring == nullptr ? BufferHandle{} : m_ring->Current(frameIndex);
        }

      private:
        GraphicsDevice& m_device;
        std::string m_label;
        std::size_t m_capacity = 0;
        std::unique_ptr<FrameRingBuffer> m_ring;
    };

    DebugFeature::DebugFeature(GraphicsDevice& device, ILogger& logger, const SceneRenderer& scene)
        : m_device(device), m_logger(logger), m_scene(scene)
    {
        m_lineShader = ReadBuiltin(BuiltinProgram::DebugLines, m_logger);
        m_triangleShader = ReadBuiltin(BuiltinProgram::DebugTriangles, m_logger);
        m_textShader = ReadBuiltin(BuiltinProgram::DebugText, m_logger);

        m_lineLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::StorageBuffer}}}, "Debug lines");
        m_targetLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::UniformBuffer}}}, "Debug target");
        m_atlasLayout = m_device.CreateBindingGroupLayout({{{0, ShaderBindingKind::Texture}}}, "Debug font atlas");

        // The font atlas: one channel, sampled with linear filtering so the distance field interpolates.
        TextureDesc atlas;
        atlas.format = TextureFormat::R8Unorm;
        atlas.width = std::max(m_font.AtlasWidth(), 1u);
        atlas.height = std::max(m_font.AtlasHeight(), 1u);
        atlas.usage = TextureUsage::Sampled | TextureUsage::CopyDestination;
        m_atlas = m_device.CreateTexture(atlas, "Debug font atlas");
        if (!m_font.AtlasTexels().empty())
        {
            m_device.WriteTexture(m_atlas, 0, 0, m_font.AtlasTexels());
        }
        else
        {
            m_logger.Error("FenrirRenderer: the embedded debug font could not be read, so debug text draws nothing");
        }
        SamplerDesc sampler;
        sampler.mipFilter = Filter::Nearest;
        sampler.addressU = AddressMode::ClampToEdge;
        sampler.addressV = AddressMode::ClampToEdge;
        sampler.addressW = AddressMode::ClampToEdge;
        m_atlasSampler = m_device.CreateSampler(sampler, "Debug font atlas");
        BindingGroupEntry atlasEntry;
        atlasEntry.binding = 0;
        atlasEntry.texture = m_atlas;
        atlasEntry.sampler = m_atlasSampler;
        m_atlasGroup = m_device.CreateBindingGroup({m_atlasLayout, {atlasEntry}}, "Debug font atlas");

        // Into a view's SceneColor, testing (or not) its SceneDepth without writing it, alpha-blended, both faces.
        PipelineDesc scene3D;
        scene3D.cull = CullMode::None;
        scene3D.blend = Graphics::BlendMode::Alpha;
        scene3D.colourFormats = {TextureFormat::Rgba16Float};
        scene3D.samples = m_scene.Samples();
        scene3D.depth.write = false;
        for (const DebugDepth depth : {DebugDepth::Tested, DebugDepth::OnTop})
        {
            PipelineDesc lines = scene3D;
            lines.depth.test = depth == DebugDepth::Tested;
            lines.sets = {BindingGroupLayoutHandle{}, m_scene.ViewLayout(), m_lineLayout, BindingGroupLayoutHandle{}};
            m_linePipelines.at(IndexOf(depth)) = MakePipeline(m_lineShader, lines);

            PipelineDesc triangles = scene3D;
            triangles.depth.test = depth == DebugDepth::Tested;
            triangles.sets = {BindingGroupLayoutHandle{}, m_scene.ViewLayout(), BindingGroupLayoutHandle{},
                              BindingGroupLayoutHandle{}};
            triangles.vertexBuffers = {
                {sizeof(TriangleVertex), {{0, VertexFormat::Float3, 0}, {1, VertexFormat::Unorm8x4, 12}}}};
            m_trianglePipelines.at(IndexOf(depth)) = MakePipeline(m_triangleShader, triangles);
        }
        PipelineDesc text = scene3D;
        text.depth.test = false;
        text.sets = {m_targetLayout, m_atlasLayout, BindingGroupLayoutHandle{}, BindingGroupLayoutHandle{}};
        text.vertexBuffers = {
            {sizeof(TextVertex),
             {{0, VertexFormat::Float2, 0}, {1, VertexFormat::Float2, 8}, {2, VertexFormat::Unorm8x4, 16}}}};
        m_viewTextPipeline = MakePipeline(m_textShader, text);
        // The window's backbuffer: sRGB, which encodes the linear colour on write, with no depth or MSAA.
        text.colourFormats = {TextureFormat::Rgba8Srgb};
        text.depthFormat.reset();
        text.samples = 1;
        m_overlayTextPipeline = MakePipeline(m_textShader, text);

        m_lineBlocks = std::make_unique<BlockRing>(m_device, m_lineLayout, true, sizeof(LineRecord), "Debug lines");
        m_targetBlocks =
            std::make_unique<BlockRing>(m_device, m_targetLayout, false, sizeof(TargetBlock), "Debug target");
        m_triangleVertices = std::make_unique<VertexRing>(m_device, "Debug triangles");
        m_textVertices = std::make_unique<VertexRing>(m_device, "Debug text");
    }

    DebugFeature::~DebugFeature()
    {
        m_lineBlocks.reset();
        m_targetBlocks.reset();
        m_triangleVertices.reset();
        m_textVertices.reset();
        for (const PipelineHandle pipeline : {m_linePipelines[0], m_linePipelines[1], m_trianglePipelines[0],
                                              m_trianglePipelines[1], m_viewTextPipeline, m_overlayTextPipeline})
        {
            m_device.Destroy(pipeline);
        }
        m_device.Destroy(m_atlasGroup);
        m_device.Destroy(m_atlasSampler);
        m_device.Destroy(m_atlas);
        m_device.Destroy(m_lineLayout);
        m_device.Destroy(m_targetLayout);
        m_device.Destroy(m_atlasLayout);
    }

    PipelineHandle DebugFeature::MakePipeline(const ShaderContainer& container, PipelineDesc desc)
    {
        desc.shader = container.FindVariant({});
        const PipelineResult made = m_device.CreatePipeline(desc, container.name);
        if (made.pipeline.IsEmpty())
        {
            m_logger.Error("FenrirRenderer: the built-in pipeline '{}' could not be made: {}", container.name,
                           made.diagnostic);
        }
        return made.pipeline;
    }

    void DebugFeature::SetFrame(DebugFrame frame)
    {
        m_frame = std::move(frame);
    }

    void DebugFeature::AppendText(const DebugText& text, Math::Vec2 origin, float size)
    {
        const std::uint32_t colour = PackSrgb(text.colour);
        for (const GlyphQuad& quad : m_font.Layout(text.text, size))
        {
            const Math::Vec2 min = origin + quad.min;
            const Math::Vec2 max = origin + quad.max;
            const std::array<TextVertex, 4> corners{{
                {min.x, min.y, quad.uvMin.x, quad.uvMin.y, colour},
                {max.x, min.y, quad.uvMax.x, quad.uvMin.y, colour},
                {max.x, max.y, quad.uvMax.x, quad.uvMax.y, colour},
                {min.x, max.y, quad.uvMin.x, quad.uvMax.y, colour},
            }};
            for (const std::size_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
            {
                const auto bytes = std::as_bytes(std::span(&corners.at(corner), 1));
                m_textBytes.insert(m_textBytes.end(), bytes.begin(), bytes.end());
            }
        }
    }

    void DebugFeature::WriteTarget(std::uint64_t frameIndex, std::size_t slot, Math::Vec2 extent, Math::Vec2 pixels,
                                   float contentScale)
    {
        // Local units, top-left origin and Y down, to clip space, Y up (ADR-0027).
        Math::Mat4 localToClip(1.0f);
        localToClip[0][0] = 2.0f / extent.x;
        localToClip[1][1] = -2.0f / extent.y;
        localToClip[2][2] = 0.0f;
        localToClip[3] = Math::Vec4(-1.0f, 1.0f, 0.5f, 1.0f);
        const TargetBlock block{localToClip, Math::Vec4(pixels, 0.0f, 0.0f),
                                Math::Vec4(contentScale, 0.0f, 0.0f, 0.0f)};
        m_targetBlocks->Write(frameIndex, slot, std::as_bytes(std::span(&block, 1)));
    }

    void DebugFeature::AddPasses(RenderGraph& graph, const FrameContext& frame)
    {
        const std::vector<DrawnView>& drawn = m_scene.DrawnViews();
        if (m_frame.Empty() || drawn.empty())
        {
            return;
        }
        const std::vector<ViewRequest>& requests = m_scene.Frame().views;
        const std::uint64_t frameIndex = frame.frameIndex;

        // Every line and triangle once a frame, the depth-tested ones first, so each depth mode is one draw.
        std::vector<LineRecord> lines;
        std::vector<TriangleVertex> triangles;
        std::array<std::uint32_t, 2> lineCounts{};
        std::array<std::uint32_t, 2> triangleCounts{};
        for (const DebugDepth depth : {DebugDepth::Tested, DebugDepth::OnTop})
        {
            for (const DebugLines& batch : m_frame.lines)
            {
                if (batch.depth != depth)
                {
                    continue;
                }
                for (std::size_t i = 0; i + 1 < batch.vertices.size(); i += 2)
                {
                    const DebugShapeVertex& from = batch.vertices[i];
                    const DebugShapeVertex& to = batch.vertices[i + 1];
                    lines.push_back({Math::Vec4(from.position, batch.thickness),
                                     Math::Vec4(to.position, 0.0f),
                                     {PackSrgb(from.colour), PackSrgb(to.colour), 0, 0}});
                    ++lineCounts.at(IndexOf(depth));
                }
            }
            for (const DebugTriangles& batch : m_frame.triangles)
            {
                if (batch.depth != depth)
                {
                    continue;
                }
                const std::size_t whole = batch.vertices.size() / 3 * 3;
                for (std::size_t i = 0; i < whole; ++i)
                {
                    const DebugShapeVertex& vertex = batch.vertices[i];
                    triangles.push_back(
                        {vertex.position.x, vertex.position.y, vertex.position.z, PackSrgb(vertex.colour)});
                    ++triangleCounts.at(IndexOf(depth));
                }
            }
        }
        if (!lines.empty())
        {
            m_lineBlocks->Reserve(lines.size());
            m_lineBlocks->Write(frameIndex, 0, BytesOf(lines));
        }
        if (!triangles.empty())
        {
            m_triangleVertices->Write(frameIndex, BytesOf(triangles));
        }

        // World text, laid out in each view's own pixels, then the window's screen text and messages.
        m_textBytes.clear();
        m_targetBlocks->Reserve(drawn.size() + 1);
        std::vector<TextDraw> viewTexts(drawn.size());
        for (std::size_t d = 0; d < drawn.size(); ++d)
        {
            const ViewRequest& request = requests.at(drawn[d].view);
            const Math::Vec2 pixels(static_cast<float>(request.viewport.width),
                                    static_cast<float>(request.viewport.height));
            // In a view of the window, a pixel size is a logical one, as screen text's is.
            const float scale = request.target.IsEmpty() ? m_frame.contentScale : 1.0f;
            TextDraw& draw = viewTexts[d];
            draw.firstVertex = static_cast<std::uint32_t>(m_textBytes.size() / sizeof(TextVertex));
            draw.targetSlot = d;
            WriteTarget(frameIndex, d, pixels, pixels, scale);
            if (!request.drawDebug)
            {
                continue;
            }
            for (const DebugText& text : m_frame.worldTexts)
            {
                const Math::Vec4 inView = request.view * Math::Vec4(text.position, 1.0f);
                // In front of the camera is view-space -Z; behind it, world text is skipped.
                if (inView.z >= 0.0f)
                {
                    continue;
                }
                const Math::Vec4 clip = request.projection * inView;
                const Math::Vec2 ndc(clip.x / clip.w, clip.y / clip.w);
                const Math::Vec2 at((ndc.x + 1.0f) * 0.5f * pixels.x, (1.0f - ndc.y) * 0.5f * pixels.y);
                const float size = text.size * scale;
                AppendText(text, at - m_font.Measure(text.text, size) * 0.5f, size);
            }
            draw.vertexCount = static_cast<std::uint32_t>(m_textBytes.size() / sizeof(TextVertex)) - draw.firstVertex;
        }

        const bool windowDrawn = std::any_of(
            drawn.begin(), drawn.end(), [&](const DrawnView& view) { return requests.at(view.view).target.IsEmpty(); });
        TextDraw overlay;
        overlay.targetSlot = drawn.size();
        overlay.firstVertex = static_cast<std::uint32_t>(m_textBytes.size() / sizeof(TextVertex));
        if (windowDrawn)
        {
            const float scale = m_frame.contentScale > 0.0f ? m_frame.contentScale : 1.0f;
            const Math::Vec2 pixels(static_cast<float>(frame.size.width), static_cast<float>(frame.size.height));
            WriteTarget(frameIndex, overlay.targetSlot, pixels / scale, pixels, scale);
            for (const DebugText& text : m_frame.screenTexts)
            {
                AppendText(text, Math::Vec2(text.position), text.size);
            }
            float y = MessageMargin;
            for (const DebugText& message : m_frame.messages)
            {
                AppendText(message, {MessageMargin, y}, message.size);
                y += m_font.Measure(message.text, message.size).y + MessageGap;
            }
            overlay.vertexCount =
                static_cast<std::uint32_t>(m_textBytes.size() / sizeof(TextVertex)) - overlay.firstVertex;
        }
        if (!m_textBytes.empty())
        {
            m_textVertices->Write(frameIndex, m_textBytes);
        }

        const BindingGroupHandle lineGroup = lines.empty() ? BindingGroupHandle{} : m_lineBlocks->Group(frameIndex, 0);
        const BufferHandle triangleBuffer = m_triangleVertices->Current(frameIndex);
        const BufferHandle textBuffer = m_textVertices->Current(frameIndex);
        for (std::size_t d = 0; d < drawn.size(); ++d)
        {
            const DrawnView& view = drawn[d];
            const ViewRequest& request = requests.at(view.view);
            const TextDraw text = viewTexts[d];
            if (!request.drawDebug || (lines.empty() && triangles.empty() && text.vertexCount == 0))
            {
                continue;
            }
            const PixelRect viewport{0, 0, request.viewport.width, request.viewport.height};
            const BindingGroupHandle targetGroup = m_targetBlocks->Group(frameIndex, text.targetSlot);
            const GraphResource sceneColour = view.sceneColour;
            const GraphResource sceneDepth = view.sceneDepth;
            graph.AddPass(
                "Debug", RenderStage::Debug, QueueKind::Graphics,
                [=](PassBuilder& pass) {
                    pass.ColourAttachment(sceneColour, LoadOp::Load);
                    pass.DepthReadOnly(sceneDepth);
                },
                [this, viewport, viewGroup = view.viewGroup, lineGroup, triangleBuffer, textBuffer, targetGroup, text,
                 lineCounts, triangleCounts](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    std::uint32_t firstLine = 0;
                    std::uint32_t firstTriangleVertex = 0;
                    for (const DebugDepth depth : {DebugDepth::Tested, DebugDepth::OnTop})
                    {
                        const std::size_t i = IndexOf(depth);
                        if (triangleCounts.at(i) > 0)
                        {
                            encoder.SetPipeline(m_trianglePipelines.at(i));
                            encoder.SetBindingGroup(1, viewGroup);
                            encoder.SetVertexBuffer(0, triangleBuffer);
                            encoder.Draw({.vertexCount = triangleCounts.at(i), .firstVertex = firstTriangleVertex});
                        }
                        if (lineCounts.at(i) > 0)
                        {
                            encoder.SetPipeline(m_linePipelines.at(i));
                            encoder.SetBindingGroup(1, viewGroup);
                            encoder.SetBindingGroup(2, lineGroup);
                            // Six vertices a line: the two triangles of its quad.
                            encoder.Draw({.vertexCount = lineCounts.at(i) * 6, .firstVertex = firstLine * 6});
                        }
                        firstTriangleVertex += triangleCounts.at(i);
                        firstLine += lineCounts.at(i);
                    }
                    if (text.vertexCount > 0)
                    {
                        encoder.SetPipeline(m_viewTextPipeline);
                        encoder.SetBindingGroup(0, targetGroup);
                        encoder.SetBindingGroup(1, m_atlasGroup);
                        encoder.SetVertexBuffer(0, textBuffer);
                        encoder.Draw({.vertexCount = text.vertexCount, .firstVertex = text.firstVertex});
                    }
                });
        }

        if (overlay.vertexCount > 0)
        {
            const GraphResource backbuffer = frame.backbuffer;
            const PixelRect viewport{0, 0, frame.size.width, frame.size.height};
            const BindingGroupHandle targetGroup = m_targetBlocks->Group(frameIndex, overlay.targetSlot);
            // The fixed top layer of the window's overlay: 2D overlay layers, when there are any, draw beneath it.
            graph.AddPass(
                "Overlay", RenderStage::Overlay, QueueKind::Graphics,
                [=](PassBuilder& pass) { pass.ColourAttachment(backbuffer, LoadOp::Load); },
                [this, viewport, textBuffer, targetGroup, overlay](PassContext& context) {
                    PassEncoder& encoder = context.Encoder();
                    encoder.SetViewport(viewport);
                    encoder.SetPipeline(m_overlayTextPipeline);
                    encoder.SetBindingGroup(0, targetGroup);
                    encoder.SetBindingGroup(1, m_atlasGroup);
                    encoder.SetVertexBuffer(0, textBuffer);
                    encoder.Draw({.vertexCount = overlay.vertexCount, .firstVertex = overlay.firstVertex});
                });
        }
    }
} // namespace Fenrir::Render
