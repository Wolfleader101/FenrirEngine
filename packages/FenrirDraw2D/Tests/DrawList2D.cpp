#include "FenrirDraw2D/DrawList2D.hpp"

#include <array>
#include <cstdint>
#include <span>

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
