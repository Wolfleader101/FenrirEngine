#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Backend/Null/NullDevice.hpp"
#include "Core/RendererCore.hpp"
#include "Core/TransientPool.hpp"

namespace Fenrir
{
    /**
     * @brief The tests' one way inside the renderer: what the null backend received and the device's live handles.
     * NullDevice, RendererCore and TransientPool name this type as a friend, so none of it is part of the renderer's
     * interface. Every function needs the null backend.
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
    };
} // namespace Fenrir
