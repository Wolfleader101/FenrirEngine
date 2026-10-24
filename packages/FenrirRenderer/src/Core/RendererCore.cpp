#include "Core/RendererCore.hpp"

#include <utility>

namespace Fenrir::Render
{
    RendererCore::RendererCore(std::unique_ptr<Graphics::GraphicsDevice> device, Graphics::GraphicsSurface& surface)
        : m_device(std::move(device)), m_surface(surface), m_transients(m_device->FramesInFlight())
    {
    }

    RendererCore::~RendererCore()
    {
        m_device->WaitIdle();
        // Features hold device resources (ring buffers, pipelines), so they go before the device.
        m_features.clear();
        m_transients.Clear(*m_device);
    }

    void RendererCore::AddFeature(std::unique_ptr<RenderFeature> feature)
    {
        if (feature)
        {
            m_features.push_back(std::move(feature));
        }
    }

    FrameReport RendererCore::RenderFrame()
    {
        FrameReport report;
        report.frameIndex = m_frameIndex++;
        const Graphics::PixelSize size = m_surface.GetPixelSize();
        m_device->BeginFrame(report.frameIndex);
        if (size.IsEmpty())
        {
            return report;
        }

        m_transients.BeginFrame(report.frameIndex);
        RenderGraph graph;
        Graphics::TextureDesc backbufferDesc;
        backbufferDesc.format = Graphics::TextureFormat::Rgba8Srgb;
        backbufferDesc.width = size.width;
        backbufferDesc.height = size.height;
        backbufferDesc.usage = Graphics::TextureUsage::ColourAttachment;
        const GraphResource backbuffer = graph.ImportTexture("Backbuffer", m_device->Backbuffer(), backbufferDesc);

        const FrameContext frame{*m_device, report.frameIndex, size, backbuffer};
        for (const std::unique_ptr<RenderFeature>& feature : m_features)
        {
            feature->AddPasses(graph, frame);
        }

        const GraphCompilation compilation = graph.Compile();
        report.errors = compilation.errors;
        if (compilation.Ok())
        {
            // Each pass records into its own CommandList, all on this thread for now (ADR-0019), then all are
            // submitted in order.
            const std::vector<RecordedPass> recorded =
                graph.Record(compilation, *m_device, m_transients, report.frameIndex);
            for (const RecordedPass& pass : recorded)
            {
                m_device->Submit(pass.commands);
            }
            m_device->Present();
            report.drawn = true;
            report.passesRun = static_cast<std::uint32_t>(compilation.order.size());
            report.passesCulled = static_cast<std::uint32_t>(compilation.culled.size());
        }
        m_transients.EndFrame(*m_device);
        return report;
    }
} // namespace Fenrir::Render
