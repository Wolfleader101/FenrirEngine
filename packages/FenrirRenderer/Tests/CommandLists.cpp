#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

#include "Device/GraphicsDevice.hpp"
#include "DeviceTest.hpp"
#include "RendererProbe.hpp"

using Fenrir::RendererProbe;
using namespace Fenrir::Graphics;
using RendererTest::Pass;
using RendererTest::UniformAt;

TEST_CASE("Recording a pass needs no device and keeps every command in call order", "[renderer][commands]")
{
    CommandList list;
    {
        PassEncoder pass(list, Pass("Opaque"));
        pass.SetPipeline(PipelineHandle{3, 1});
        pass.Draw({36});
    }

    INFO("begin, pipeline, draw and end, in that order");
    REQUIRE(list.commands.size() == 4);
    REQUIRE(std::get<CmdBeginPass>(list.commands[0]).pass.label == "Opaque");
    REQUIRE(std::get<CmdSetPipeline>(list.commands[1]).pipeline == PipelineHandle{3, 1});
    REQUIRE(std::get<CmdDraw>(list.commands[2]).vertexCount == 36);
    REQUIRE(std::holds_alternative<CmdEndPass>(list.commands[3]));
}

TEST_CASE("A recorded CommandList replays through the null backend's Submit and reads back in order",
          "[renderer][commands]")
{
    auto device = CreateNullDevice();
    device->BeginFrame(0);

    const BufferHandle vertices = device->CreateBuffer({1024, BufferUsage::Vertex});
    const BufferHandle indices = device->CreateBuffer({256, BufferUsage::Index});
    const BufferHandle frameBlock = device->CreateBuffer({128, BufferUsage::Uniform});
    const BindingGroupLayoutHandle layout =
        device->CreateBindingGroupLayout({{{0, Fenrir::ShaderBindingKind::UniformBuffer}}});
    const BindingGroupHandle frameGroup = device->CreateBindingGroup({layout, {UniformAt(0, frameBlock)}});
    const BindingGroupHandle viewGroup = device->CreateBindingGroup({layout, {UniformAt(0, frameBlock)}});
    const Fenrir::ShaderVariant shader;
    PipelineDesc litDesc;
    litDesc.shader = &shader;
    litDesc.sets = {layout, layout, {}, {}};
    const PipelineHandle lit = device->CreatePipeline(litDesc).pipeline;
    const PipelineHandle unlit = device->CreatePipeline(litDesc).pipeline;

    CommandList list;
    {
        PassEncoder pass(list, Pass("Opaque", device->Backbuffer()));
        pass.SetViewport({0, 0, 64, 32});
        pass.SetPipeline(lit);
        pass.SetBindingGroup(0, frameGroup);
        pass.SetBindingGroup(1, viewGroup);
        pass.SetVertexBuffer(0, vertices);
        pass.SetIndexBuffer(indices, IndexFormat::UInt32);
        pass.DrawIndexed({36, 1, 0, 0, 0});
        pass.SetPipeline(unlit);
        pass.DrawIndexed({6, 1, 36, 24, 1});
    }
    device->Submit(list);

    const std::vector<CommandList>& submitted = RendererProbe::Submitted(*device);
    INFO("the device received the one list");
    REQUIRE(submitted.size() == 1);
    const std::vector<Command>& commands = submitted[0].commands;
    INFO("it holds the pass, its state and its two draws in the order recorded");
    REQUIRE(commands.size() == 11);
    REQUIRE(std::get<CmdBeginPass>(commands[0]).pass.colour.at(0).texture == device->Backbuffer());
    REQUIRE(std::get<CmdSetViewport>(commands[1]).rect == PixelRect{0, 0, 64, 32});
    REQUIRE(std::get<CmdSetPipeline>(commands[2]).pipeline == lit);
    REQUIRE(std::get<CmdSetBindingGroup>(commands[3]).set == 0);
    REQUIRE(std::get<CmdSetBindingGroup>(commands[3]).group == frameGroup);
    REQUIRE(std::get<CmdSetBindingGroup>(commands[4]).set == 1);
    REQUIRE(std::get<CmdSetBindingGroup>(commands[4]).group == viewGroup);
    REQUIRE(std::get<CmdSetVertexBuffer>(commands[5]).buffer == vertices);
    REQUIRE(std::get<CmdSetIndexBuffer>(commands[6]).buffer == indices);
    REQUIRE(std::get<CmdDrawIndexed>(commands[7]).indexCount == 36);
    REQUIRE(std::get<CmdSetPipeline>(commands[8]).pipeline == unlit);
    REQUIRE(std::get<CmdDrawIndexed>(commands[9]) == CmdDrawIndexed{6, 1, 36, 24, 1});
    REQUIRE(std::holds_alternative<CmdEndPass>(commands[10]));
    INFO("every handle it named was live");
    REQUIRE(RendererProbe::StaleUses(*device).empty());

    device->BeginFrame(1);
    INFO("the next frame starts with nothing submitted");
    REQUIRE(RendererProbe::Submitted(*device).empty());
}
