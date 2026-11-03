// The light buffer's order (ADR-0048, ADR-0050), which the shaders index it by: the lights keeping spot shadows in
// their tiles' order, then those keeping point shadows in their cubes', then the other point and spot lights, then the
// rect lights.

#include <array>
#include <cstddef>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/PunctualLights.hpp"
#include "Core/RectLights.hpp"

using Fenrir::Render::GpuLight;
using Fenrir::Render::LightKind;
using Fenrir::Render::LightRecords;
using Fenrir::Render::PunctualLight;
using Fenrir::Render::RectLight;

namespace
{
    PunctualLight Light(LightKind kind, float x)
    {
        PunctualLight light;
        light.kind = kind;
        light.position = {x, 0.0f, 0.0f};
        return light;
    }

    /** @brief Each record's x, which these tests set apart for every light. */
    std::vector<float> Xs(const std::vector<GpuLight>& records)
    {
        std::vector<float> xs;
        for (const GpuLight& record : records)
        {
            xs.push_back(record.position.x);
        }
        return xs;
    }

    const std::array<PunctualLight, 4> Lights{Light(LightKind::Point, 0.0f), Light(LightKind::Spot, 1.0f),
                                              Light(LightKind::Point, 2.0f), Light(LightKind::Spot, 3.0f)};
} // namespace

TEST_CASE("Shadowed spot lights come first in the light buffer, in their tiles' order", "[renderer][lights]")
{
    const std::array<std::size_t, 2> spots{3, 1};
    const std::vector<GpuLight> records = LightRecords(Lights, {}, spots, {});
    INFO("lights 3 and 1 first, as chosen, then 0 and 2 in scene order");
    REQUIRE(Xs(records) == std::vector<float>{3.0f, 1.0f, 0.0f, 2.0f});
}

TEST_CASE("Shadowed point lights follow the shadowed spot lights, before the rest", "[renderer][lights]")
{
    const std::array<std::size_t, 1> spots{3};
    const std::array<std::size_t, 2> points{2, 0};
    const std::vector<GpuLight> records = LightRecords(Lights, {}, spots, points);
    INFO("spot 3, then points 2 and 0 as chosen, then 1, the one left");
    REQUIRE(Xs(records) == std::vector<float>{3.0f, 2.0f, 0.0f, 1.0f});
}

TEST_CASE("Rect lights come last in the light buffer, in scene order", "[renderer][lights][rect-lights]")
{
    std::array<RectLight, 2> rects{};
    rects[0].position = {8.0f, 0.0f, 0.0f};
    rects[1].position = {9.0f, 0.0f, 0.0f};
    const std::array<std::size_t, 1> points{2};
    const std::vector<GpuLight> records = LightRecords(Lights, rects, {}, points);
    INFO("point 2, then 0, 1 and 3, then both rect lights");
    REQUIRE(Xs(records) == std::vector<float>{2.0f, 0.0f, 1.0f, 3.0f, 8.0f, 9.0f});
    INFO("the rect lights' records are of kind Rect");
    REQUIRE(records[4].kind == static_cast<std::uint32_t>(LightKind::Rect));
}
