#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Device/Handles.hpp"

namespace Fenrir::Graphics
{
    /** @brief Which queue a pass's work is meant for. Every backend so far runs all three in order on one queue. */
    enum class QueueKind : std::uint8_t
    {
        Graphics,
        Compute,
        Copy,
    };

    /**
     * @brief How a pass uses a resource. Passes declare every use, and each backend derives its barriers from them, so
     * the renderer never sees a barrier (ADR-0016); the OpenGL backend derives none.
     */
    enum class ResourceUsage : std::uint8_t
    {
        ColourAttachment,
        DepthRead,
        DepthWrite,
        Sampled,
        StorageRead,
        StorageWrite,
        CopySource,
        CopyDestination,
    };

    /** @brief One declared use of a texture or a buffer by a pass: exactly one of the two handles is set. */
    struct PassResourceUse
    {
        TextureHandle texture;
        BufferHandle buffer;
        ResourceUsage usage = ResourceUsage::Sampled;

        friend bool operator==(const PassResourceUse&, const PassResourceUse&) = default;
    };

    /** @brief What an attachment holds when its pass begins. */
    enum class LoadOp : std::uint8_t
    {
        /** @brief What earlier passes left. */
        Load,
        /** @brief The pass's clear value. */
        Clear,
        /** @brief Anything: the pass covers every texel it reads. */
        DontCare,
    };

    /** @brief One colour target of a pass. */
    struct ColourAttachment
    {
        TextureHandle texture;
        LoadOp load = LoadOp::Clear;
        /** @brief The linear RGBA it is cleared to when `load` is `Clear`. */
        std::array<float, 4> clear{0.0f, 0.0f, 0.0f, 1.0f};

        friend bool operator==(const ColourAttachment&, const ColourAttachment&) = default;
    };

    /** @brief The depth target of a pass. */
    struct DepthAttachment
    {
        TextureHandle texture;
        LoadOp load = LoadOp::Clear;
        /** @brief The depth it is cleared to when `load` is `Clear`: 0 is the far plane under reversed-Z (ADR-0026). */
        float clear = 0.0f;
        /** @brief Whether the pass only tests against it. */
        bool readOnly = false;

        friend bool operator==(const DepthAttachment&, const DepthAttachment&) = default;
    };

    /** @brief A graphics device pass: its queue, the targets it draws into and every resource use it declares. */
    struct PassDesc
    {
        /** @brief The render pass's name, for tools and the backend's debug labels. */
        std::string label;
        QueueKind queue = QueueKind::Graphics;
        std::vector<ColourAttachment> colour;
        std::optional<DepthAttachment> depth;
        /** @brief Every resource the pass uses and how, attachments included: what barriers are derived from. */
        std::vector<PassResourceUse> uses;

        friend bool operator==(const PassDesc&, const PassDesc&) = default;
    };

    /** @brief A rectangle in pixels, from the target's top-left corner (ADR-0016). */
    struct PixelRect
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        friend bool operator==(const PixelRect&, const PixelRect&) = default;
    };

    /** @brief The width of an index buffer's indices. */
    enum class IndexFormat : std::uint8_t
    {
        UInt16,
        UInt32,
    };

    /** @name Commands
     * The plain structs a CommandList holds. Each is what one PassEncoder call recorded, replayed by the backend's
     * `Submit` in order.
     * @{
     */
    /** @brief Starts a pass; every command up to the matching CmdEndPass belongs to it. */
    struct CmdBeginPass
    {
        PassDesc pass;
        friend bool operator==(const CmdBeginPass&, const CmdBeginPass&) = default;
    };
    /** @brief Ends the current pass. */
    struct CmdEndPass
    {
        friend bool operator==(const CmdEndPass&, const CmdEndPass&) = default;
    };
    /** @brief Draws with a pipeline from here on. */
    struct CmdSetPipeline
    {
        PipelineHandle pipeline;
        friend bool operator==(const CmdSetPipeline&, const CmdSetPipeline&) = default;
    };
    /** @brief Binds a binding group to one of the four binding sets. */
    struct CmdSetBindingGroup
    {
        std::uint32_t set = 0;
        BindingGroupHandle group;
        friend bool operator==(const CmdSetBindingGroup&, const CmdSetBindingGroup&) = default;
    };
    /** @brief Binds a vertex buffer to a slot of the pipeline's vertex input. */
    struct CmdSetVertexBuffer
    {
        std::uint32_t slot = 0;
        BufferHandle buffer;
        std::size_t offset = 0;
        friend bool operator==(const CmdSetVertexBuffer&, const CmdSetVertexBuffer&) = default;
    };
    /** @brief Binds the index buffer indexed draws read. */
    struct CmdSetIndexBuffer
    {
        BufferHandle buffer;
        std::size_t offset = 0;
        IndexFormat format = IndexFormat::UInt32;
        friend bool operator==(const CmdSetIndexBuffer&, const CmdSetIndexBuffer&) = default;
    };
    /** @brief Sets the viewport, the rectangle clip space maps to. */
    struct CmdSetViewport
    {
        PixelRect rect;
        friend bool operator==(const CmdSetViewport&, const CmdSetViewport&) = default;
    };
    /** @brief Sets the scissor, outside which nothing is drawn. */
    struct CmdSetScissor
    {
        PixelRect rect;
        friend bool operator==(const CmdSetScissor&, const CmdSetScissor&) = default;
    };
    /** @brief Draws vertices in order. */
    struct CmdDraw
    {
        std::uint32_t vertexCount = 0;
        std::uint32_t instanceCount = 1;
        std::uint32_t firstVertex = 0;
        /** @brief Read by shaders as `gl_BaseInstance`, which indexes the draw (ADR-0018). */
        std::uint32_t firstInstance = 0;
        friend bool operator==(const CmdDraw&, const CmdDraw&) = default;
    };
    /** @brief Draws vertices through the index buffer. */
    struct CmdDrawIndexed
    {
        std::uint32_t indexCount = 0;
        std::uint32_t instanceCount = 1;
        std::uint32_t firstIndex = 0;
        std::int32_t baseVertex = 0;
        /** @brief Read by shaders as `gl_BaseInstance`, which indexes the draw (ADR-0018). */
        std::uint32_t firstInstance = 0;
        friend bool operator==(const CmdDrawIndexed&, const CmdDrawIndexed&) = default;
    };
    /** @brief Runs the compute pipeline over a grid of work groups. */
    struct CmdDispatch
    {
        std::uint32_t groupsX = 1;
        std::uint32_t groupsY = 1;
        std::uint32_t groupsZ = 1;
        friend bool operator==(const CmdDispatch&, const CmdDispatch&) = default;
    };
    /** @brief Copies bytes from one buffer to another. */
    struct CmdCopyBuffer
    {
        BufferHandle source;
        std::size_t sourceOffset = 0;
        BufferHandle destination;
        std::size_t destinationOffset = 0;
        std::size_t size = 0;
        friend bool operator==(const CmdCopyBuffer&, const CmdCopyBuffer&) = default;
    };
    /** @} */

    /** @brief Any one command. */
    using Command = std::variant<CmdBeginPass, CmdEndPass, CmdSetPipeline, CmdSetBindingGroup, CmdSetVertexBuffer,
                                 CmdSetIndexBuffer, CmdSetViewport, CmdSetScissor, CmdDraw, CmdDrawIndexed, CmdDispatch,
                                 CmdCopyBuffer>;

    /**
     * @brief The commands of one or more passes as plain structs, owned by the renderer, recorded through a
     * PassEncoder with no backend and replayed by GraphicsDevice::Submit (ADR-0016).
     */
    struct CommandList
    {
        std::vector<Command> commands;
    };

    /**
     * @brief Records one pass into a CommandList: begins the pass when made and ends it on End, with every call between
     * appended as a command. It needs no graphics device, so recording can happen anywhere.
     */
    class PassEncoder
    {
      public:
        /** @brief Begins `pass` in `list`, which must outlive the encoder. */
        PassEncoder(CommandList& list, PassDesc pass) : m_list(&list)
        {
            m_list->commands.emplace_back(CmdBeginPass{std::move(pass)});
        }

        /** @brief Ends the pass if End was not called. */
        ~PassEncoder()
        {
            End();
        }

        PassEncoder(const PassEncoder&) = delete;
        PassEncoder& operator=(const PassEncoder&) = delete;
        PassEncoder(PassEncoder&&) = delete;
        PassEncoder& operator=(PassEncoder&&) = delete;

        /** @brief Ends the pass. Later calls record nothing. */
        void End()
        {
            if (m_list != nullptr)
            {
                m_list->commands.emplace_back(CmdEndPass{});
                m_list = nullptr;
            }
        }

        /** @brief Draws with `pipeline` from here on. */
        void SetPipeline(PipelineHandle pipeline)
        {
            Record(CmdSetPipeline{pipeline});
        }

        /** @brief Binds `group` to binding set `set`. */
        void SetBindingGroup(std::uint32_t set, BindingGroupHandle group)
        {
            Record(CmdSetBindingGroup{set, group});
        }

        /** @brief Binds `buffer`, from `offset` bytes, to vertex buffer slot `slot`. */
        void SetVertexBuffer(std::uint32_t slot, BufferHandle buffer, std::size_t offset = 0)
        {
            Record(CmdSetVertexBuffer{slot, buffer, offset});
        }

        /** @brief Binds `buffer`, from `offset` bytes, as the index buffer. */
        void SetIndexBuffer(BufferHandle buffer, IndexFormat format, std::size_t offset = 0)
        {
            Record(CmdSetIndexBuffer{buffer, offset, format});
        }

        /** @brief Sets the viewport. */
        void SetViewport(PixelRect rect)
        {
            Record(CmdSetViewport{rect});
        }

        /** @brief Sets the scissor. */
        void SetScissor(PixelRect rect)
        {
            Record(CmdSetScissor{rect});
        }

        /** @brief Records a draw of vertices in order. */
        void Draw(const CmdDraw& draw)
        {
            Record(draw);
        }

        /** @brief Records a draw through the index buffer. */
        void DrawIndexed(const CmdDrawIndexed& draw)
        {
            Record(draw);
        }

        /** @brief Records a compute dispatch. */
        void Dispatch(const CmdDispatch& dispatch)
        {
            Record(dispatch);
        }

        /** @brief Records a copy between buffers. */
        void CopyBuffer(const CmdCopyBuffer& copy)
        {
            Record(copy);
        }

      private:
        template <typename T>
        void Record(T command)
        {
            if (m_list != nullptr)
            {
                m_list->commands.emplace_back(std::move(command));
            }
        }

        CommandList* m_list;
    };
} // namespace Fenrir::Graphics
