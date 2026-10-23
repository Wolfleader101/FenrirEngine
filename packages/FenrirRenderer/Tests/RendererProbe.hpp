#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "Backend/Null/NullDevice.hpp"
#include "Core/RendererCore.hpp"
#include "Core/TransientPool.hpp"
#include "Facade/RendererState.hpp"
#include "FenrirRenderer/Renderer.hpp"

namespace Fenrir
{
    /**
     * @brief The tests' one way inside the renderer: what the null backend received, the device's live handles, the
     * bytes, bindings and labels it keeps, and the Renderer owner's core and surface. NullDevice, RendererCore,
     * TransientPool and Renderer name this type as a friend, so none of it is part of the renderer's interface. Every
     * function that reads the device needs the null backend, except SceneColour.
     */
    struct RendererProbe
    {
        /** @brief The null device behind `device`. */
        [[nodiscard]] static const Graphics::NullDevice& Null(const Graphics::GraphicsDevice& device)
        {
            return dynamic_cast<const Graphics::NullDevice&>(device);
        }

        /** @brief The null device `core` draws through. */
        [[nodiscard]] static const Graphics::NullDevice& Null(const Render::RendererCore& core)
        {
            return Null(*core.m_device);
        }

        /** @brief The CommandLists Submit received since the device's frame began, in order. */
        [[nodiscard]] static const std::vector<Graphics::CommandList>& Submitted(const Graphics::GraphicsDevice& device)
        {
            return Null(device).m_submitted;
        }

        /** @brief The CommandLists the core's last frame submitted, in order. */
        [[nodiscard]] static const std::vector<Graphics::CommandList>& Submitted(const Render::RendererCore& core)
        {
            return Null(core).m_submitted;
        }

        /** @brief One line per stale or empty handle a submitted command named. */
        [[nodiscard]] static const std::vector<std::string>& StaleUses(const Graphics::GraphicsDevice& device)
        {
            return Null(device).m_staleUses;
        }

        /** @brief How many times the device presented. */
        [[nodiscard]] static std::uint64_t Presents(const Graphics::GraphicsDevice& device)
        {
            return Null(device).m_presents;
        }

        /** @brief How many buffers are live. */
        [[nodiscard]] static std::size_t LiveBuffers(const Graphics::GraphicsDevice& device)
        {
            return Null(device).m_buffers.LiveCount();
        }

        /** @brief How many textures are live, the backbuffer included. */
        [[nodiscard]] static std::size_t LiveTextures(const Graphics::GraphicsDevice& device)
        {
            return Null(device).m_textures.LiveCount();
        }

        /** @brief How many pipelines are live. */
        [[nodiscard]] static std::size_t LivePipelines(const Graphics::GraphicsDevice& device)
        {
            return Null(device).m_pipelines.LiveCount();
        }

        /** @brief How many destroyed buffers still hold their slots, waiting out the frames in flight. */
        [[nodiscard]] static std::size_t PendingBuffers(const Graphics::GraphicsDevice& device)
        {
            return Null(device).m_buffers.PendingCount();
        }

        /** @brief How many textures and buffers the transient pool keeps, over every frame's set. */
        [[nodiscard]] static std::size_t TransientCount(const Render::TransientPool& pool)
        {
            std::size_t count = 0;
            for (const auto& set : pool.m_sets)
            {
                count += set.size();
            }
            return count;
        }

        /** @brief How many textures and buffers the core's transient pool keeps. */
        [[nodiscard]] static std::size_t TransientCount(const Render::RendererCore& core)
        {
            return TransientCount(core.m_transients);
        }

        /** @brief The renderer core a valid Renderer owner draws with. */
        [[nodiscard]] static Render::RendererCore& Core(const Renderer& renderer)
        {
            return *renderer.m_state->core;
        }

        /** @brief The graphics device a valid Renderer owner draws through. */
        [[nodiscard]] static Graphics::GraphicsDevice& Device(const Renderer& renderer)
        {
            return renderer.m_state->core->Device();
        }

        /** @brief The CommandLists the renderer's last frame submitted, in order. */
        [[nodiscard]] static const std::vector<Graphics::CommandList>& Submitted(const Renderer& renderer)
        {
            return Submitted(Core(renderer));
        }

        /**
         * @brief Makes the null backend's surface report `size` from now on, as a resized or (with an empty size) a
         * minimised window would.
         */
        static void SetSurfaceSize(const Renderer& renderer, Graphics::PixelSize size)
        {
            dynamic_cast<Detail::NullSurface&>(*renderer.m_state->surface).sizeOverride = size;
        }

        /** @brief The label a pipeline was made with. */
        [[nodiscard]] static std::string Label(const Graphics::GraphicsDevice& device,
                                               Graphics::PipelineHandle pipeline)
        {
            const auto* found = const_cast<Graphics::NullDevice&>(Null(device)).m_pipelines.Find(pipeline);
            return found == nullptr ? std::string() : found->label;
        }

        /** @brief The label a binding group was made with. */
        [[nodiscard]] static std::string Label(const Graphics::GraphicsDevice& device,
                                               Graphics::BindingGroupHandle group)
        {
            const auto* found = const_cast<Graphics::NullDevice&>(Null(device)).m_groups.Find(group);
            return found == nullptr ? std::string() : found->label;
        }

        /** @brief The description a live texture was made with; the default description for a stale one. */
        [[nodiscard]] static Graphics::TextureDesc Desc(const Graphics::GraphicsDevice& device,
                                                        Graphics::TextureHandle texture)
        {
            const auto* found = const_cast<Graphics::NullDevice&>(Null(device)).m_textures.Find(texture);
            return found == nullptr ? Graphics::TextureDesc{} : found->desc;
        }

        /** @brief The samples per pixel a live pipeline draws with; 0 for a stale one. */
        [[nodiscard]] static std::uint32_t Samples(const Graphics::GraphicsDevice& device,
                                                   Graphics::PipelineHandle pipeline)
        {
            const auto* found = const_cast<Graphics::NullDevice&>(Null(device)).m_pipelines.Find(pipeline);
            return found == nullptr ? 0 : found->samples;
        }

        /** @brief The variant keys of the shader a live pipeline runs; empty for a stale one. */
        [[nodiscard]] static std::vector<std::string> Keys(const Graphics::GraphicsDevice& device,
                                                           Graphics::PipelineHandle pipeline)
        {
            const auto* found = const_cast<Graphics::NullDevice&>(Null(device)).m_pipelines.Find(pipeline);
            return found == nullptr ? std::vector<std::string>() : found->keys;
        }

        /** @brief What a live binding group binds; empty for a stale one. */
        [[nodiscard]] static std::vector<Graphics::BindingGroupEntry> Entries(const Graphics::GraphicsDevice& device,
                                                                              Graphics::BindingGroupHandle group)
        {
            const auto* found = const_cast<Graphics::NullDevice&>(Null(device)).m_groups.Find(group);
            return found == nullptr ? std::vector<Graphics::BindingGroupEntry>() : found->desc.entries;
        }

        /** @brief A live buffer's bytes as last written; empty for a stale one. */
        [[nodiscard]] static std::vector<std::byte> Bytes(const Graphics::GraphicsDevice& device,
                                                          Graphics::BufferHandle buffer)
        {
            const auto* found = const_cast<Graphics::NullDevice&>(Null(device)).m_buffers.Find(buffer);
            return found == nullptr ? std::vector<std::byte>() : found->bytes;
        }

        /** @brief The texture behind a render target image the renderer has prepared, on any backend; else empty. */
        [[nodiscard]] static Graphics::TextureHandle ImageTexture(const Renderer& renderer, AssetHandle<Image> image)
        {
            const auto& targets = renderer.m_state->scene->m_targets;
            const auto found = targets.find(Render::KeyOf(image));
            return found == targets.end() ? Graphics::TextureHandle{} : found->second.texture;
        }

        /**
         * @brief A SceneColor texture the last frame drew into (the transient pool's RGBA16F colour target), on any
         * backend; empty if there is none.
         */
        [[nodiscard]] static Graphics::TextureHandle SceneColour(const Renderer& renderer)
        {
            return SceneColour(renderer, 0, 0);
        }

        /**
         * @brief The SceneColor texture of `width` by `height` pixels the last frame drew into, on any backend, for a
         * frame with several views; any size when both are 0. Empty if there is none.
         */
        [[nodiscard]] static Graphics::TextureHandle SceneColour(const Renderer& renderer, std::uint32_t width,
                                                                 std::uint32_t height)
        {
            const Render::RendererCore& core = Core(renderer);
            // The set the last frame took from.
            for (const auto& entry : core.m_transients.m_sets.at(core.m_transients.m_current))
            {
                const auto* desc = std::get_if<Graphics::TextureDesc>(&entry.desc);
                if (desc != nullptr && desc->format == Graphics::TextureFormat::Rgba16Float &&
                    (width == 0 || (desc->width == width && desc->height == height)) &&
                    core.m_device->IsLive(entry.texture))
                {
                    return entry.texture;
                }
            }
            return {};
        }
    };
} // namespace Fenrir
