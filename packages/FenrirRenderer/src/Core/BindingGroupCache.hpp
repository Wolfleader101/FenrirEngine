#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <utility>

#include "Device/GraphicsDevice.hpp"

namespace Fenrir::Render
{
    /**
     * @brief One part of a cached binding group's key: a texture or buffer the group binds, which the cache checks is
     * still live, or a plain value that tells groups apart, such as a block's slot.
     */
    struct BindingGroupKeyPart
    {
        enum class Kind : std::uint8_t
        {
            Value,
            Texture,
            Buffer,
        };

        Kind kind = Kind::Value;
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        /** @brief A texture the group binds. */
        [[nodiscard]] static constexpr BindingGroupKeyPart Of(Graphics::TextureHandle texture) noexcept
        {
            return {Kind::Texture, texture.index, texture.generation};
        }
        /** @brief A buffer the group binds. */
        [[nodiscard]] static constexpr BindingGroupKeyPart Of(Graphics::BufferHandle buffer) noexcept
        {
            return {Kind::Buffer, buffer.index, buffer.generation};
        }
        /** @brief A value that is not a device resource. */
        [[nodiscard]] static constexpr BindingGroupKeyPart Value(std::uint32_t value) noexcept
        {
            return {Kind::Value, value, 0};
        }

        friend constexpr auto operator<=>(const BindingGroupKeyPart&, const BindingGroupKeyPart&) = default;
    };

    /**
     * @brief Binding groups made once per key of `Parts` parts and kept while every texture and buffer in the key
     * lives. A group of a transient texture outlives its frame, and a group of a ring's buffer outlives the ring
     * remaking it, so the first Get of each frame destroys the groups whose resources the device has since let go of.
     * A cache not asked in a frame keeps its groups until it next is, or is cleared.
     */
    template <std::size_t Parts>
    class BindingGroupCache
    {
      public:
        /** @brief What tells one group from another: the resources it binds and any plain values, in a fixed order. */
        using Key = std::array<BindingGroupKeyPart, Parts>;

        /** @brief An empty cache of groups made on `device`, which it destroys when pruned, cleared or destroyed. */
        explicit BindingGroupCache(Graphics::GraphicsDevice& device) : m_device(device)
        {
        }
        ~BindingGroupCache()
        {
            Clear();
        }
        BindingGroupCache(const BindingGroupCache&) = delete;
        BindingGroupCache& operator=(const BindingGroupCache&) = delete;
        BindingGroupCache(BindingGroupCache&&) = delete;
        BindingGroupCache& operator=(BindingGroupCache&&) = delete;

        /**
         * @brief The group of `key` in frame `frameIndex`, made by `make` (which returns a BindingGroupHandle) when
         * first asked for. The first call of a new frame first destroys the groups of keys naming a texture or buffer
         * the device no longer holds.
         */
        template <typename Make>
        [[nodiscard]] Graphics::BindingGroupHandle Get(const Key& key, std::uint64_t frameIndex, Make&& make)
        {
            if (m_frame != frameIndex)
            {
                Prune();
                m_frame = frameIndex;
            }
            if (const auto found = m_groups.find(key); found != m_groups.end())
            {
                return found->second;
            }
            const Graphics::BindingGroupHandle group = std::forward<Make>(make)();
            m_groups.emplace(key, group);
            return group;
        }

        /** @brief Destroys every group. */
        void Clear()
        {
            for (const auto& [key, group] : m_groups)
            {
                m_device.Destroy(group);
            }
            m_groups.clear();
        }

      private:
        /** @brief Destroys the groups of keys naming a texture or buffer the device no longer holds. */
        void Prune()
        {
            for (auto it = m_groups.begin(); it != m_groups.end();)
            {
                if (!IsLive(it->first))
                {
                    m_device.Destroy(it->second);
                    it = m_groups.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        [[nodiscard]] bool IsLive(const Key& key) const
        {
            for (const BindingGroupKeyPart& part : key)
            {
                const bool live = part.kind == BindingGroupKeyPart::Kind::Texture
                                      ? m_device.IsLive(Graphics::TextureHandle{part.index, part.generation})
                                  : part.kind == BindingGroupKeyPart::Kind::Buffer
                                      ? m_device.IsLive(Graphics::BufferHandle{part.index, part.generation})
                                      : true;
                if (!live)
                {
                    return false;
                }
            }
            return true;
        }

        Graphics::GraphicsDevice& m_device;
        std::map<Key, Graphics::BindingGroupHandle> m_groups;
        /** @brief The frame of the last Get, whose first call pruned; none before the first. */
        std::optional<std::uint64_t> m_frame;
    };
} // namespace Fenrir::Render
