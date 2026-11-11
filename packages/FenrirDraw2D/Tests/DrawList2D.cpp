#include "FenrirDraw2D/DrawList2D.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <variant>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A texture slot samples with linear filtering clamped to the edge by default", "[draw2d]")
{
    const Fenrir::TextureSlot2D slot;

    INFO("the default sampler is linear clamp, with no anisotropy (ADR-0027)");
    REQUIRE(slot.sampler.wrapU == Fenrir::SamplerWrap::ClampToEdge);
    REQUIRE(slot.sampler.wrapV == Fenrir::SamplerWrap::ClampToEdge);
    REQUIRE(slot.sampler.filter == Fenrir::SamplerFilter::Bilinear);
    REQUIRE(slot.sampler.anisotropy == 1);
}

TEST_CASE("Adding a pipeline equal to one the list has gives that one's index", "[draw2d]")
{
    Fenrir::DrawList2D list;
    Fenrir::PipelineDescription2D straight;
    straight.vertexLayout = Fenrir::Vertex2DLayout();
    Fenrir::PipelineDescription2D premultiplied = straight;
    premultiplied.blendMode = Fenrir::BlendMode2D::PremultipliedAlpha;

    const std::uint32_t first = list.AddPipeline(straight);
    const std::uint32_t second = list.AddPipeline(premultiplied);
    const std::uint32_t again = list.AddPipeline(Fenrir::PipelineDescription2D{straight});

    INFO("two different descriptions take two places; an equal one reuses the first");
    REQUIRE(first == 0);
    REQUIRE(second == 1);
    REQUIRE(again == 0);
    REQUIRE(list.pipelines.size() == 2);
}

TEST_CASE("Appended vertices and indices are numbered from the ones already in the list", "[draw2d]")
{
    Fenrir::DrawList2D list;
    const std::array<Fenrir::Vertex2D, 4> quad{};
    const std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};

    const std::uint32_t firstQuad = list.AppendVertices(std::span<const Fenrir::Vertex2D>(quad));
    const std::uint32_t secondQuad = list.AppendVertices(std::span<const Fenrir::Vertex2D>(quad));
    list.AppendIndices(std::span<const std::uint16_t>(indices));
    const std::uint32_t secondIndices = list.AppendIndices(std::span<const std::uint16_t>(indices));

    INFO("the second quad starts at vertex 4 and its indices at index 6, 20 bytes a vertex and 2 an index");
    REQUIRE(firstQuad == 0);
    REQUIRE(secondQuad == 4);
    REQUIRE(secondIndices == 6);
    REQUIRE(list.vertices.size() == 160);
    REQUIRE(list.indices.size() == 24);
}

TEST_CASE("A list with nested groups gives back its commands in the order and with the values they were added",
          "[draw2d][groups]")
{
    Fenrir::DrawList2D list;
    Fenrir::DrawCommand2D draw;
    draw.pipeline = 1;
    draw.indexOffset = 6;
    draw.indexCount = 12;
    draw.scissor = Fenrir::Rect2D{1.0f, 2.0f, 3.0f, 4.0f};
    Fenrir::EndGroup2D faded;
    faded.opacity = 0.5f;
    faded.blend = Fenrir::GroupBlend2D::Additive;
    Fenrir::EndGroup2D shadowed;
    shadowed.filter = Fenrir::GroupFilter2D::DropShadow;
    shadowed.radius = 4.0f;
    shadowed.offset = {2.0f, 3.0f};

    list.commands.push_back(draw);
    list.BeginGroup({10.0f, 20.0f, 100.0f, 50.0f});
    list.commands.push_back(draw);
    list.BeginGroup({15.0f, 25.0f, 20.0f, 10.0f});
    list.commands.push_back(draw);
    list.EndGroup(faded);
    list.EndGroup(shadowed);

    INFO("seven commands: a draw, a group holding a draw and a group of one draw, in that order");
    REQUIRE(list.commands.size() == 7);
    REQUIRE(std::get<Fenrir::DrawCommand2D>(list.commands[0]) == draw);
    REQUIRE(std::get<Fenrir::BeginGroup2D>(list.commands[1]).bounds == Fenrir::Rect2D{10.0f, 20.0f, 100.0f, 50.0f});
    REQUIRE(std::get<Fenrir::DrawCommand2D>(list.commands[2]) == draw);
    REQUIRE(std::get<Fenrir::BeginGroup2D>(list.commands[3]).bounds == Fenrir::Rect2D{15.0f, 25.0f, 20.0f, 10.0f});
    REQUIRE(std::get<Fenrir::DrawCommand2D>(list.commands[4]) == draw);
    INFO("the inner group ends first, with its composite, then the outer with its own");
    REQUIRE(std::get<Fenrir::EndGroup2D>(list.commands[5]) == faded);
    REQUIRE(std::get<Fenrir::EndGroup2D>(list.commands[6]) == shadowed);
}

TEST_CASE("A new command is a draw, and a group's defaults composite plainly at full opacity", "[draw2d][groups]")
{
    const Fenrir::Command2D command;
    const Fenrir::BeginGroup2D begin;
    const Fenrir::EndGroup2D end;

    INFO("a default command is a draw of nothing");
    REQUIRE(std::holds_alternative<Fenrir::DrawCommand2D>(command));
    REQUIRE(std::get<Fenrir::DrawCommand2D>(command).indexCount == 0);
    INFO("a group's image is premultiplied RGBA8, the only format");
    REQUIRE(begin.format == Fenrir::GroupFormat2D::Rgba8Premultiplied);
    REQUIRE(begin.bounds == Fenrir::Rect2D{});
    INFO("a group composites at opacity 1, drawn over what is under it, with no filter, and is no mask");
    REQUIRE(end.opacity == 1.0f);
    REQUIRE(end.blend == Fenrir::GroupBlend2D::Normal);
    REQUIRE(end.filter == Fenrir::GroupFilter2D::None);
    REQUIRE_FALSE(end.mask);
    REQUIRE(end.radius == 0.0f);
    INFO("groups nest eight deep");
    REQUIRE(Fenrir::MaxGroupDepth2D == 8);
}
