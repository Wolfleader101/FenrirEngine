#include <cmath>
#include <cstddef>
#include <numbers>

#include <catch2/catch_test_macros.hpp>

#include "FenrirDemoScaffold/DemoScaffold.hpp"

namespace
{
    using Fenrir::Math::Vec2;
    using Fenrir::Math::Vec3;

    constexpr float QuarterTurn = std::numbers::pi_v<float> * 0.5f;

    bool Near(const Vec3& a, const Vec3& b)
    {
        return std::abs(a.x - b.x) < 1e-5f && std::abs(a.y - b.y) < 1e-5f && std::abs(a.z - b.z) < 1e-5f;
    }

    bool Near(const Vec2& a, const Vec2& b)
    {
        return std::abs(a.x - b.x) < 1e-5f && std::abs(a.y - b.y) < 1e-5f;
    }

    // Where YawPitch(yaw, pitch) turns -Z, the way a camera with that rotation looks.
    Vec3 Forward(float yaw, float pitch)
    {
        return FenrirDemo::YawPitch(yaw, pitch) * Vec3(0.0f, 0.0f, -1.0f);
    }
} // namespace

TEST_CASE("YawPitch with no yaw and no pitch looks along -Z", "[demo][helpers]")
{
    INFO("-Z is left as it is");
    REQUIRE(Near(Forward(0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)));
}

TEST_CASE("YawPitch's positive yaw turns to the left about +Y", "[demo][helpers]")
{
    INFO("a quarter turn of yaw turns -Z to -X");
    REQUIRE(Near(Forward(QuarterTurn, 0.0f), Vec3(-1.0f, 0.0f, 0.0f)));
}

TEST_CASE("YawPitch's positive pitch turns up", "[demo][helpers]")
{
    INFO("a quarter turn of pitch turns -Z to +Y, looking straight up");
    REQUIRE(Near(Forward(0.0f, QuarterTurn), Vec3(0.0f, 1.0f, 0.0f)));
}

TEST_CASE("YawPitch pitches about the X its yaw has turned", "[demo][helpers]")
{
    INFO("a quarter turn down after a quarter turn of yaw looks straight down, not off to the side");
    REQUIRE(Near(Forward(QuarterTurn, -QuarterTurn), Vec3(0.0f, -1.0f, 0.0f)));

    INFO("a little pitch up after a quarter turn of yaw stays in the yawed vertical plane, the X-Y plane");
    REQUIRE(Near(Forward(QuarterTurn, 0.5f), Vec3(-std::cos(0.5f), std::sin(0.5f), 0.0f)));
}

TEST_CASE("ScreenQuad's four corners are at plus and minus half its width and height, in the XY plane",
          "[demo][helpers]")
{
    const Fenrir::MeshData quad = FenrirDemo::ScreenQuad(4.0f, 2.0f, 3.0f);

    INFO("it has four vertices, drawn as two triangles");
    REQUIRE(quad.positions.size() == 4);
    REQUIRE(quad.indices.size() == 6);

    INFO("its corners are bottom-left, bottom-right, top-right, top-left of a 4 by 2 quad about the origin");
    REQUIRE(Near(quad.positions[0], Vec3(-2.0f, -1.0f, 0.0f)));
    REQUIRE(Near(quad.positions[1], Vec3(2.0f, -1.0f, 0.0f)));
    REQUIRE(Near(quad.positions[2], Vec3(2.0f, 1.0f, 0.0f)));
    REQUIRE(Near(quad.positions[3], Vec3(-2.0f, 1.0f, 0.0f)));
}

TEST_CASE("ScreenQuad faces +Z, with normal +Z and tangent +X", "[demo][helpers]")
{
    const Fenrir::MeshData quad = FenrirDemo::ScreenQuad(4.0f, 2.0f, 3.0f);

    INFO("both triangles wind counter-clockwise seen from +Z, so their front faces +Z");
    for (std::size_t first = 0; first < quad.indices.size(); first += 3)
    {
        const Vec3 a = quad.positions[quad.indices[first]];
        const Vec3 b = quad.positions[quad.indices[first + 1]];
        const Vec3 c = quad.positions[quad.indices[first + 2]];
        REQUIRE(Fenrir::Math::Cross(b - a, c - a).z > 0.0f);
    }

    INFO("every normal is +Z, and every tangent +X with a bitangent sign of +1");
    REQUIRE(quad.normals.size() == 4);
    REQUIRE(quad.tangents.size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
    {
        REQUIRE(Near(quad.normals[i], Vec3(0.0f, 0.0f, 1.0f)));
        REQUIRE(Near(Vec3(quad.tangents[i]), Vec3(1.0f, 0.0f, 0.0f)));
        REQUIRE(quad.tangents[i].w == 1.0f);
    }
}

TEST_CASE("ScreenQuad's UV runs from 0 at the top-left corner to the repeat at the bottom-right", "[demo][helpers]")
{
    const Fenrir::MeshData quad = FenrirDemo::ScreenQuad(4.0f, 2.0f, 3.0f);

    INFO("with a repeat of 3, v grows down from 0 at the top edge to 3 at the bottom");
    REQUIRE(quad.uv0.size() == 4);
    REQUIRE(Near(quad.uv0[0], Vec2(0.0f, 3.0f)));
    REQUIRE(Near(quad.uv0[1], Vec2(3.0f, 3.0f)));
    REQUIRE(Near(quad.uv0[2], Vec2(3.0f, 0.0f)));
    REQUIRE(Near(quad.uv0[3], Vec2(0.0f, 0.0f)));

    INFO("with no repeat given, the bottom-right corner's UV is (1, 1)");
    REQUIRE(Near(FenrirDemo::ScreenQuad(1.0f, 1.0f).uv0[1], Vec2(1.0f, 1.0f)));
}
