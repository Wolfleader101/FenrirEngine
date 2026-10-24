#pragma once

#include <cstdint>

namespace Fenrir::Graphics
{
    /**
     * @brief A typed reference to a resource the graphics device owns (ADR-0016): a slot index plus the generation the
     * slot had when the resource was made. When the resource is destroyed and its slot freed, the slot's generation
     * moves on, so an old handle is found stale without reference counts. Made with no arguments, it is empty and names
     * nothing.
     */
    template <typename Tag>
    struct Handle
    {
        /** @brief The slot in the device's table for this kind of resource. */
        std::uint32_t index = 0;
        /** @brief The slot's generation when the resource was made; never 0 for a handle the device made. */
        std::uint32_t generation = 0;

        /** @brief Whether this handle names nothing: made with no arguments, never by the device. */
        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return generation == 0;
        }

        friend constexpr bool operator==(const Handle&, const Handle&) = default;
    };

    /** @name Handle types
     * One handle type per kind of device resource, so a texture handle cannot be passed where a buffer is wanted.
     * @{
     */
    using BufferHandle = Handle<struct BufferTag>;
    using TextureHandle = Handle<struct TextureTag>;
    using SamplerHandle = Handle<struct SamplerTag>;
    using BindingGroupLayoutHandle = Handle<struct BindingGroupLayoutTag>;
    using BindingGroupHandle = Handle<struct BindingGroupTag>;
    using PipelineHandle = Handle<struct PipelineTag>;
    /** @} */
} // namespace Fenrir::Graphics
