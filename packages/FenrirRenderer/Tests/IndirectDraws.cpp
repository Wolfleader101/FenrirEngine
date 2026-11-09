// Indirect draws through the graphics device on the null backend (ADR-0057): a recorded CmdDrawIndexedIndirect reaches
// Submit as recorded, and the null backend's checks on it, each a StaleUses line, as for a stale handle.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Device/GraphicsDevice.hpp"
#include "DeviceTest.hpp"
#include "RendererProbe.hpp"

using Fenrir::RendererProbe;
using namespace Fenrir::Graphics;
using RendererTest::Pass;

namespace
{
    constexpr std::size_t CommandSize = sizeof(DrawIndexedIndirectCommand);

    /** @brief A null device in frame 0, an index buffer and a command buffer of `commandCount` commands. */
    struct IndirectFixture
    {
        std::unique_ptr<GraphicsDevice> device = CreateNullDevice();
        BufferHandle indices;
        BufferHandle commands;

        explicit IndirectFixture(std::uint32_t commandCount = 4, BufferUsage commandUsage = BufferUsage::Indirect)
        {
            device->BeginFrame(0);
            indices = device->CreateBuffer({256, BufferUsage::Index});
            commands = device->CreateBuffer({commandCount * CommandSize, commandUsage});
        }

        /** @brief A pass into the backbuffer declaring `IndirectRead` on the command buffer, unless `declared` is off.
         */
        [[nodiscard]] PassDesc IndirectPass(bool declared = true) const
        {
            PassDesc pass = Pass("Opaque", device->Backbuffer());
            if (declared)
            {
                pass.uses.push_back({{}, commands, ResourceUsage::IndirectRead});
            }
            return pass;
        }

        /** @brief Records one pass of one indirect draw into `list` and submits it. */
        void Submit(PassDesc pass, const CmdDrawIndexedIndirect& draw, std::size_t indexOffset = 0)
        {
            CommandList list;
            {
                PassEncoder encoder(list, std::move(pass));
                encoder.SetIndexBuffer(indices, IndexFormat::UInt32, indexOffset);
                encoder.DrawIndexedIndirect(draw);
            }
            device->Submit(list);
        }
    };

    /** @brief Requires that one StaleUses line was recorded and that it mentions `what`. */
    void RequireOneStaleUse(const GraphicsDevice& device, const std::string& what)
    {
        const std::vector<std::string>& lines = RendererProbe::StaleUses(device);
        INFO("the failed check is one StaleUses line");
        REQUIRE(lines.size() == 1);
        INFO("the line says what went wrong: " << lines.front());
        REQUIRE(lines.front().find(what) != std::string::npos);
    }
} // namespace

TEST_CASE("An indirect command is five words laid out as GL and Vulkan read them", "[renderer][commands][indirect]")
{
    INFO("indexCount, instanceCount, firstIndex, baseVertex, baseInstance, four bytes each");
    REQUIRE(CommandSize == 20);
    const DrawIndexedIndirectCommand command{36, 2, 6, 24, 7};
    REQUIRE(command.indexCount == 36);
    REQUIRE(command.instanceCount == 2);
    REQUIRE(command.firstIndex == 6);
    REQUIRE(command.baseVertex == 24);
    REQUIRE(command.baseInstance == 7);
}

TEST_CASE("A recorded indirect draw reaches the null backend's Submit with its fields",
          "[renderer][commands][indirect]")
{
    IndirectFixture fixture;
    const CmdDrawIndexedIndirect draw{fixture.commands, CommandSize, 2, static_cast<std::uint32_t>(CommandSize)};
    fixture.Submit(fixture.IndirectPass(), draw);

    const std::vector<CommandList>& submitted = RendererProbe::Submitted(*fixture.device);
    INFO("the device received the one list");
    REQUIRE(submitted.size() == 1);
    const std::vector<Command>& commands = submitted[0].commands;
    INFO("begin, index buffer, the indirect draw and end");
    REQUIRE(commands.size() == 4);
    REQUIRE(std::get<CmdDrawIndexedIndirect>(commands[2]) == draw);
    INFO("a live Indirect buffer the pass declared IndirectRead on, inside its range, passes every check");
    REQUIRE(RendererProbe::StaleUses(*fixture.device).empty());
}

TEST_CASE("An indirect draw from a buffer made without Indirect usage is a StaleUses line",
          "[renderer][commands][indirect]")
{
    IndirectFixture fixture(4, BufferUsage::Storage);
    fixture.Submit(fixture.IndirectPass(), {fixture.commands, 0, 1});
    RequireOneStaleUse(*fixture.device, "Indirect usage");
}

TEST_CASE("An indirect draw whose pass did not declare IndirectRead on its buffer is a StaleUses line",
          "[renderer][commands][indirect]")
{
    IndirectFixture fixture;
    fixture.Submit(fixture.IndirectPass(false), {fixture.commands, 0, 1});
    RequireOneStaleUse(*fixture.device, "IndirectRead");
}

TEST_CASE("An indirect draw whose commands run past its buffer's end is a StaleUses line",
          "[renderer][commands][indirect]")
{
    IndirectFixture fixture(4);
    // Four commands fit; the fifth, from the second command on, does not.
    fixture.Submit(fixture.IndirectPass(), {fixture.commands, CommandSize, 4, static_cast<std::uint32_t>(CommandSize)});
    RequireOneStaleUse(*fixture.device, "past the end");
}

TEST_CASE("An indirect draw whose command stride is under a command's size is a StaleUses line",
          "[renderer][commands][indirect]")
{
    IndirectFixture fixture;
    fixture.Submit(fixture.IndirectPass(), {fixture.commands, 0, 1, 16});
    RequireOneStaleUse(*fixture.device, "stride");
}

TEST_CASE("An indirect draw with the index buffer bound past its start is a StaleUses line",
          "[renderer][commands][indirect]")
{
    IndirectFixture fixture;
    fixture.Submit(fixture.IndirectPass(), {fixture.commands, 0, 1}, 4);
    RequireOneStaleUse(*fixture.device, "index buffer");
}

TEST_CASE("An indirect draw from a destroyed command buffer is one stale-handle line and no other",
          "[renderer][commands][indirect]")
{
    IndirectFixture fixture;
    REQUIRE(fixture.device->Destroy(fixture.commands));
    fixture.Submit(fixture.IndirectPass(), {fixture.commands, 0, 1});
    INFO("the pass's declared use of the destroyed buffer, then the draw's command buffer, each a line");
    REQUIRE(RendererProbe::StaleUses(*fixture.device).size() == 2);
    REQUIRE(RendererProbe::StaleUses(*fixture.device).back().find("indirect command buffer") != std::string::npos);
}
