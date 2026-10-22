#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "Device/GraphicsDevice.hpp"

namespace Fenrir
{
    struct RendererProbe;
}

namespace Fenrir::Render
{
    /**
     * @brief The render graph's transient textures and buffers, kept between frames and keyed by description. Entries
     * ring by frame index: frame N takes from its own set of `framesInFlight` sets, so a frame never reuses what a
     * frame still in flight uses (ADR-0019). An entry its set did not use in a frame is destroyed, as after a resize.
     */
    class TransientPool
    {
      public:
        explicit TransientPool(std::uint32_t framesInFlight);

        TransientPool(const TransientPool&) = delete;
        TransientPool& operator=(const TransientPool&) = delete;
        TransientPool(TransientPool&&) = default;
        TransientPool& operator=(TransientPool&&) = default;
        ~TransientPool() = default;

        /** @brief Starts taking from frame `frameIndex`'s set. */
        void BeginFrame(std::uint64_t frameIndex);

        /** @brief A texture of `desc` no pass holds this frame, made if the set has none. */
        [[nodiscard]] Graphics::TextureHandle Acquire(Graphics::GraphicsDevice& device,
                                                      const Graphics::TextureDesc& desc);
        /** @brief A buffer of `desc` no pass holds this frame, made if the set has none. */
        [[nodiscard]] Graphics::BufferHandle Acquire(Graphics::GraphicsDevice& device,
                                                     const Graphics::BufferDesc& desc);

        /** @brief Gives a texture back after its last use this frame, for a later pass to take. */
        void Release(Graphics::TextureHandle texture);
        /** @brief Gives a buffer back after its last use this frame, for a later pass to take. */
        void Release(Graphics::BufferHandle buffer);

        /** @brief Ends the frame: destroys the entries of its set that no pass took. */
        void EndFrame(Graphics::GraphicsDevice& device);

        /** @brief Destroys every entry, as teardown does. */
        void Clear(Graphics::GraphicsDevice& device);

      private:
        friend struct Fenrir::RendererProbe;

        struct Entry
        {
            std::variant<Graphics::TextureDesc, Graphics::BufferDesc> desc;
            Graphics::TextureHandle texture;
            Graphics::BufferHandle buffer;
            bool held = false;
            bool usedThisFrame = false;
        };

        [[nodiscard]] std::vector<Entry>& CurrentSet();

        std::vector<std::vector<Entry>> m_sets;
        std::size_t m_current = 0;
    };
} // namespace Fenrir::Render
