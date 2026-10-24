#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "Device/CommandList.hpp"
#include "Device/Handles.hpp"
#include "Device/Resources.hpp"

namespace Fenrir::Graphics
{
    /** @brief How a graphics device is set up, whichever backend it is. */
    struct DeviceConfig
    {
        /**
         * @brief How many frames the GPU may still be working on while the next is recorded: a destroyed resource's
         * slot is freed this many frames later, and per-frame buffers ring over this many copies (ADR-0019).
         */
        std::uint32_t framesInFlight = 2;
        /**
         * @brief Whether the graphics API's own validation reports misuse, to the logger the backend was given. Costly;
         * the null backend has none.
         */
        bool debugValidation = false;
    };

    /** @brief How presenting waits for the display. */
    enum class Vsync : std::uint8_t
    {
        /** @brief Presents at once; frames may tear. */
        Off,
        /** @brief Waits for vertical sync. */
        On,
        /** @brief Waits for vertical sync unless the frame is late, which then tears instead of waiting. */
        Adaptive,
    };

    /** @brief A new pipeline, or why the backend refused its shader or state. */
    struct PipelineResult
    {
        /** @brief Empty when the pipeline could not be made. */
        PipelineHandle pipeline;
        /** @brief Why not; empty when it was made. */
        std::string diagnostic;
    };

    /**
     * @brief The private, thin interface every graphics backend implements (ADR-0016), shaped like the explicit APIs:
     * buffers, textures, samplers, binding-group layouts and groups, pipelines, and passes recorded into CommandLists
     * that Submit replays. Everything it makes is named by a typed generational handle it owns.
     *
     * Destruction is deferred by frame index: a destroyed resource's handle is stale at once, but its slot (and, on a
     * GPU, the object) is freed only when the frame `framesInFlight` after the one it was destroyed in begins, since
     * frames still in flight may use it. Every call comes from one thread.
     */
    class GraphicsDevice
    {
      public:
        virtual ~GraphicsDevice() = default;

        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;
        GraphicsDevice(GraphicsDevice&&) = delete;
        GraphicsDevice& operator=(GraphicsDevice&&) = delete;

        /** @brief How many frames may be in flight, as the device was configured. */
        [[nodiscard]] virtual std::uint32_t FramesInFlight() const = 0;

        /**
         * @brief Starts frame `frameIndex`, which counts up by one each frame, drawn or not. Frees the slots of
         * resources destroyed `framesInFlight` or more frames before it.
         */
        virtual void BeginFrame(std::uint64_t frameIndex) = 0;

        /** @name Resources
         * Each call makes a resource and returns its handle; `label` names it in debugging tools.
         * @{
         */
        /** @brief A buffer of `desc.size` bytes, starting with `initial` if given (at most that size). */
        [[nodiscard]] virtual BufferHandle CreateBuffer(const BufferDesc& desc, std::span<const std::byte> initial = {},
                                                        std::string_view label = {}) = 0;
        /** @brief A texture with undefined contents. */
        [[nodiscard]] virtual TextureHandle CreateTexture(const TextureDesc& desc, std::string_view label = {}) = 0;
        [[nodiscard]] virtual SamplerHandle CreateSampler(const SamplerDesc& desc, std::string_view label = {}) = 0;
        [[nodiscard]] virtual BindingGroupLayoutHandle CreateBindingGroupLayout(const BindingGroupLayoutDesc& desc,
                                                                                std::string_view label = {}) = 0;
        [[nodiscard]] virtual BindingGroupHandle CreateBindingGroup(const BindingGroupDesc& desc,
                                                                    std::string_view label = {}) = 0;
        /** @brief A pipeline, or a diagnostic if the backend refuses the shader variant or the state. */
        [[nodiscard]] virtual PipelineResult CreatePipeline(const PipelineDesc& desc, std::string_view label = {}) = 0;
        /** @} */

        /** @brief Writes `bytes` into a live buffer from `offset`. False, writing nothing, for a stale handle. */
        virtual bool WriteBuffer(BufferHandle buffer, std::size_t offset, std::span<const std::byte> bytes) = 0;

        /**
         * @brief Writes one mip level of one layer (a cube face, else 0) of a live texture, as tightly packed rows top
         * row first. False, writing nothing, for a stale handle.
         */
        virtual bool WriteTexture(TextureHandle texture, std::uint32_t mipLevel, std::uint32_t layer,
                                  std::span<const std::byte> texels) = 0;

        /** @name Destruction
         * Destroys a live resource, deferred by frame index. False, doing nothing, for a stale or empty handle.
         * @{
         */
        virtual bool Destroy(BufferHandle buffer) = 0;
        virtual bool Destroy(TextureHandle texture) = 0;
        virtual bool Destroy(SamplerHandle sampler) = 0;
        virtual bool Destroy(BindingGroupLayoutHandle layout) = 0;
        virtual bool Destroy(BindingGroupHandle group) = 0;
        virtual bool Destroy(PipelineHandle pipeline) = 0;
        /** @} */

        /** @name Liveness
         * Whether a handle names a resource that exists and has not been destroyed. An empty handle, or one whose
         * resource was destroyed, is stale.
         * @{
         */
        [[nodiscard]] virtual bool IsLive(BufferHandle buffer) const = 0;
        [[nodiscard]] virtual bool IsLive(TextureHandle texture) const = 0;
        [[nodiscard]] virtual bool IsLive(SamplerHandle sampler) const = 0;
        [[nodiscard]] virtual bool IsLive(BindingGroupLayoutHandle layout) const = 0;
        [[nodiscard]] virtual bool IsLive(BindingGroupHandle group) const = 0;
        [[nodiscard]] virtual bool IsLive(PipelineHandle pipeline) const = 0;
        /** @} */

        /**
         * @brief The surface's colour image for this frame, as a texture a pass draws into and the render graph
         * imports. The same handle every frame on backends that have one image; never destroyed by the caller.
         */
        [[nodiscard]] virtual TextureHandle Backbuffer() const = 0;

        /** @brief Replays a recorded list's commands in order. */
        virtual void Submit(const CommandList& list) = 0;

        /** @brief Shows the frame's backbuffer, the last step of a drawn frame. */
        virtual void Present() = 0;

        /** @brief Blocks until the GPU has finished every submitted frame, as teardown does. */
        virtual void WaitIdle() = 0;

        /**
         * @brief Sets how Present waits for the display, returning the mode now in effect. A mode the surface does not
         * support falls back to `On`, logged.
         */
        virtual Vsync SetVsync(Vsync mode) = 0;

      protected:
        GraphicsDevice() = default;
    };

    /**
     * @brief The null backend (ADR-0016): needs no window or context, makes handles and keeps every book a backend
     * keeps, accepts any shader container, and draws nothing.
     */
    [[nodiscard]] std::unique_ptr<GraphicsDevice> CreateNullDevice(const DeviceConfig& config = {});
} // namespace Fenrir::Graphics
