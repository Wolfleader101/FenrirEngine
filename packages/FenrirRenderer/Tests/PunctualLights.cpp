// Point and spot lights' falloff on the CPU (the spec's CPU unit tests; ADR-0048): Core/PunctualLights mirrors
// Fenrir/Lighting.glsl step for step, so these check the maths every lit material shares. Inverse-square falloff
// windowed to zero at the range, and a spot cone smoothed between its inner and outer angles.

#include <algorithm>
#include <cmath>

#include <catch2/catch_test_macros.hpp>

#include "Core/PunctualLights.hpp"

using Fenrir::Render::ConeFalloff;
using Fenrir::Render::DistanceFalloff;
using Fenrir::Render::GpuLight;
using Fenrir::Render::LightKind;
using Fenrir::Render::PunctualLight;
using Fenrir::Render::ToGpuLight;

namespace
{
    bool Near(float a, float b, float tolerance = 1e-5f)
    {
        return std::abs(a - b) <= tolerance * std::max(1.0f, std::abs(b));
    }
} // namespace

TEST_CASE("Distance falloff is finite at 0, inverse-square well inside the range, and windowed to 0 at the range",
          "[renderer][lights]")
{
    constexpr float Range = 10.0f;

    INFO("at the light, d^2 is held at 1e-4: 10000, not infinity");
    REQUIRE(Near(DistanceFalloff(0.0f, Range), 1.0e4f));
    INFO("one unit away, a tenth of the range, the window barely shows: 1 / 1^2 x (1 - 0.1^4)^2");
    REQUIRE(Near(DistanceFalloff(1.0f, Range), 1.0f, 1e-3f));

    INFO("at mid range, 1 / d^2 times the window (1 - (1/2)^4)^2");
    constexpr float Mid = Range * 0.5f;
    constexpr float Window = (1.0f - 0.0625f) * (1.0f - 0.0625f);
    REQUIRE(Near(DistanceFalloff(Mid, Range), Window / (Mid * Mid)));

    INFO("at the range and past it, nothing");
    REQUIRE(DistanceFalloff(Range, Range) == 0.0f);
    REQUIRE(DistanceFalloff(Range * 2.0f, Range) == 0.0f);
    INFO("just inside, a little, falling smoothly to it");
    REQUIRE(DistanceFalloff(Range * 0.99f, Range) > 0.0f);
    REQUIRE(DistanceFalloff(Range * 0.99f, Range) < 1e-3f);

    INFO("well inside the range, doubling the distance quarters the light");
    const float near = DistanceFalloff(0.5f, 100.0f);
    const float far = DistanceFalloff(1.0f, 100.0f);
    REQUIRE(Near(near / far, 4.0f, 1e-3f));

    INFO("a range of 0 lights nothing");
    REQUIRE(DistanceFalloff(0.5f, 0.0f) == 0.0f);
}

TEST_CASE("Distance falloff falls as the distance grows", "[renderer][lights]")
{
    float previous = DistanceFalloff(0.0f, 5.0f);
    for (int step = 1; step <= 50; ++step)
    {
        const float falloff = DistanceFalloff(0.1f * static_cast<float>(step), 5.0f);
        REQUIRE(falloff <= previous);
        previous = falloff;
    }
    REQUIRE(previous == 0.0f);
}

TEST_CASE("A spot cone is full inside its inner angle, dark outside its outer, and smooth between",
          "[renderer][lights]")
{
    constexpr float Inner = 0.3f;
    constexpr float Outer = 0.6f;

    INFO("on its axis and inside the inner angle, full light");
    REQUIRE(Near(ConeFalloff(0.0f, Inner, Outer), 1.0f));
    REQUIRE(Near(ConeFalloff(Inner * 0.9f, Inner, Outer), 1.0f));
    INFO("at and outside the outer angle, none");
    REQUIRE(ConeFalloff(Outer, Inner, Outer) < 1e-4f);
    REQUIRE(ConeFalloff(1.2f, Inner, Outer) == 0.0f);

    INFO("halfway between the cosines, smoothstep's half");
    const float halfway = std::acos(0.5f * (std::cos(Inner) + std::cos(Outer)));
    REQUIRE(Near(ConeFalloff(halfway, Inner, Outer), 0.5f, 1e-4f));

    INFO("between, it falls the further off the axis");
    float previous = 1.0f;
    for (int step = 0; step <= 30; ++step)
    {
        const float angle = Inner + (Outer - Inner) * static_cast<float>(step) / 30.0f;
        const float falloff = ConeFalloff(angle, Inner, Outer);
        REQUIRE(falloff <= previous + 1e-6f);
        previous = falloff;
    }

    INFO("an inner angle past the outer is held just inside it: a hard edge, still dark outside");
    REQUIRE(Near(ConeFalloff(0.0f, 0.8f, 0.5f), 1.0f));
    REQUIRE(ConeFalloff(0.55f, 0.8f, 0.5f) == 0.0f);
}

TEST_CASE("A point light's record takes the cone falloff as 1 in every direction", "[renderer][lights]")
{
    PunctualLight point;
    point.kind = LightKind::Point;
    point.range = 4.0f;
    const GpuLight record = ToGpuLight(point);
    REQUIRE(record.kind == 0);
    REQUIRE(record.coneScale == 0.0f);
    REQUIRE(record.coneOffset == 1.0f);
    for (const float cosine : {1.0f, 0.0f, -1.0f})
    {
        REQUIRE(Near(ConeFalloff(cosine, record), 1.0f));
    }
}
