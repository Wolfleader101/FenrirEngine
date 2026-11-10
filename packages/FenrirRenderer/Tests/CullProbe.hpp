#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Device/CommandList.hpp"
#include "RendererProbe.hpp"

// The cull pass as the null backend records it (ADR-0057), for every test harness that reads a frame's draws: a view's
// Cull pass, its group table, and the direct draw an indirect draw of a counted command stands for.
namespace RendererTest
{
    /** @brief The 32-bit unsigned integer at byte `offset` of `bytes`. */
    inline std::uint32_t UintAt(const std::vector<std::byte>& bytes, std::size_t offset)
    {
        std::uint32_t value = 0;
        REQUIRE(offset + sizeof(value) <= bytes.size());
        std::memcpy(&value, std::span(bytes).subspan(offset).data(), sizeof(value));
        return value;
    }

    /** @brief `Fenrir/DrawRecord.glsl`'s FenrirNoCopy: an indirection entry's copy for a lone or merged item. */
    constexpr std::uint32_t NoCopy = 0xFFFFFFFFu;
    /** @brief `Fenrir/DrawRecord.glsl`'s FenrirNoIndirection: the first entry's record of the buffer that holds none.
     */
    constexpr std::uint32_t NoIndirection = 0xFFFFFFFFu;

    /** @brief One pass of a view's GPU cull (ADR-0057), as the device received it. */
    struct RecordedCull
    {
        Fenrir::Graphics::PassDesc pass;
        Fenrir::Graphics::PipelineHandle pipeline;
        /** @brief Its set 0, the job block, and set 1: the records, the group table, the commands and the indirection.
         */
        Fenrir::Graphics::BindingGroupHandle job;
        Fenrir::Graphics::BindingGroupHandle target;
        /** @brief The templates over the view's command buffer, and the one dispatch. */
        std::vector<Fenrir::Graphics::CmdCopyBuffer> copies;
        std::vector<Fenrir::Graphics::CmdDispatch> dispatches;
    };

    /** @brief Every `Cull` pass the device received, in order. */
    inline std::vector<RecordedCull> CullsIn(const std::vector<Fenrir::Graphics::CommandList>& lists)
    {
        using namespace Fenrir::Graphics;
        std::vector<RecordedCull> culls;
        bool inPass = false;
        for (const CommandList& list : lists)
        {
            for (const Command& command : list.commands)
            {
                if (const auto* begin = std::get_if<CmdBeginPass>(&command))
                {
                    inPass = begin->pass.label == "Cull";
                    if (inPass)
                    {
                        culls.push_back({begin->pass, {}, {}, {}, {}, {}});
                    }
                }
                else if (!inPass)
                {
                    continue;
                }
                else if (const auto* pipeline = std::get_if<CmdSetPipeline>(&command))
                {
                    culls.back().pipeline = pipeline->pipeline;
                }
                else if (const auto* group = std::get_if<CmdSetBindingGroup>(&command))
                {
                    (group->set == 0 ? culls.back().job : culls.back().target) = group->group;
                }
                else if (const auto* copy = std::get_if<CmdCopyBuffer>(&command))
                {
                    culls.back().copies.push_back(*copy);
                }
                else if (const auto* dispatch = std::get_if<CmdDispatch>(&command))
                {
                    culls.back().dispatches.push_back(*dispatch);
                }
            }
        }
        return culls;
    }

    /** @brief What a live binding group binds at `binding`; an empty entry when it binds nothing there. */
    inline Fenrir::Graphics::BindingGroupEntry EntryAt(const Fenrir::Graphics::GraphicsDevice& device,
                                                       Fenrir::Graphics::BindingGroupHandle group,
                                                       std::uint32_t binding)
    {
        for (const Fenrir::Graphics::BindingGroupEntry& entry : Fenrir::RendererProbe::Entries(device, group))
        {
            if (entry.binding == binding)
            {
                return entry;
            }
        }
        return {};
    }

    /** @brief One entry of the frame's group table, as `builtin:cull` reads it: a work group's run and candidates. */
    struct CullGroup
    {
        /** @brief The run's command in the view's command buffer. */
        std::uint32_t command = 0;
        /** @brief The run's first record. */
        std::uint32_t record = 0;
        /** @brief The run's candidates. */
        std::uint32_t count = 0;
        /** @brief The first of them this group culls. */
        std::uint32_t first = 0;
    };

    /** @brief A Cull pass's group table entries, as the device holds them: the view's own, from its job block. */
    inline std::vector<CullGroup> GroupsOf(const Fenrir::Graphics::GraphicsDevice& device, const RecordedCull& cull)
    {
        using Fenrir::RendererProbe;
        const Fenrir::Graphics::BindingGroupEntry job = EntryAt(device, cull.job, 0);
        const std::vector<std::byte> jobBytes = RendererProbe::Bytes(device, job.buffer);
        const std::uint32_t first = UintAt(jobBytes, job.offset);
        const std::uint32_t count = UintAt(jobBytes, job.offset + 4);
        const std::vector<std::byte> table = RendererProbe::Bytes(device, EntryAt(device, cull.target, 1).buffer);
        std::vector<CullGroup> groups;
        for (std::uint32_t g = first; g < first + count; ++g)
        {
            const std::size_t at = std::size_t{g} * sizeof(CullGroup);
            groups.push_back({UintAt(table, at), UintAt(table, at + 4), UintAt(table, at + 8), UintAt(table, at + 12)});
        }
        return groups;
    }

    /** @brief The `n`th 32-bit word of a draw command's bytes at `offset` of `bytes`. */
    inline std::uint32_t CommandWord(const std::vector<std::byte>& bytes, std::size_t offset, std::size_t n)
    {
        return UintAt(bytes, offset + n * sizeof(std::uint32_t));
    }

    /**
     * @brief The draw an indirect draw of `command` stands for, read through the frame's Cull passes as the GPU would
     * draw it with every candidate kept (ADR-0057): the command's index count, first index and base vertex, the run's
     * candidates as its instances, and the run's first record as its first instance, so a draw's record is found as a
     * direct draw's is. The device must be the null backend, which applied the templates' copy.
     */
    inline std::optional<Fenrir::Graphics::CmdDrawIndexed> ResolveIndirect(
        const Fenrir::Graphics::GraphicsDevice& device, const std::vector<RecordedCull>& culls,
        const Fenrir::Graphics::CmdDrawIndexedIndirect& command)
    {
        using Fenrir::RendererProbe;
        for (const RecordedCull& cull : culls)
        {
            if (EntryAt(device, cull.target, 2).buffer != command.buffer)
            {
                continue;
            }
            const std::vector<std::byte> commands = RendererProbe::Bytes(device, command.buffer);
            const auto index =
                static_cast<std::uint32_t>(command.offset / sizeof(Fenrir::Graphics::DrawIndexedIndirectCommand));
            for (const CullGroup& group : GroupsOf(device, cull))
            {
                if (group.command == index)
                {
                    return Fenrir::Graphics::CmdDrawIndexed{
                        CommandWord(commands, command.offset, 0), group.count, CommandWord(commands, command.offset, 2),
                        static_cast<std::int32_t>(CommandWord(commands, command.offset, 3)), group.record};
                }
            }
        }
        return std::nullopt;
    }
} // namespace RendererTest
