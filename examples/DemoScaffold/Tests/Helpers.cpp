#include <cmath>
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

TEST_CASE("YawPitch turns -Z left for a positive yaw, up for a positive pitch, and pitches about the turned X",
          "[demo][helpers]")
{
    INFO("no yaw and no pitch leaves -Z as it is");
    REQUIRE(Near(Forward(0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f)));

    INFO("a quarter turn of yaw turns -Z to -X, a turn to the left about +Y");
    REQUIRE(Near(Forward(QuarterTurn, 0.0f), Vec3(-1.0f, 0.0f, 0.0f)));

    INFO("a quarter turn of pitch turns -Z to +Y, looking straight up");
    REQUIRE(Near(Forward(0.0f, QuarterTurn), Vec3(0.0f, 1.0f, 0.0f)));

    INFO("a quarter turn of pitch down after a quarter turn of yaw still looks straight down, not off to the side");
    REQUIRE(Near(Forward(QuarterTurn, -QuarterTurn), Vec3(0.0f, -1.0f, 0.0f)));

    INFO("a pitch after a yaw tilts about the turned X: a little pitch up keeps the look in the yawed vertical plane");
    const Vec3 tilted = Forward(QuarterTurn, 0.5f);
    REQUIRE(Near(tilted, Vec3(-std::cos(0.5f), std::sin(0.5f), 0.0f)));
}

TEST_CASE("ScreenQuad is a centred quad of the given size facing +Z, with UV from 0 to the repeat", "[demo][helpers]")
{
    const Fenrir::MeshData quad = FenrirDemo::ScreenQuad(4.0f, 2.0f, 3.0f);

    INFO("it has four vertices, as two triangles");
    REQUIRE(quad.positions.size() == 4);
    REQUIRE(quad.indices.size() == 6);

    INFO("its corners are at plus and minus half the width and half the height, in the XY plane");
    REQUIRE(Near(quad.positions[0], Vec3(-2.0f, -1.0f, 0.0f)));
    REQUIRE(Near(quad.positions[1], Vec3(2.0f, -1.0f, 0.0f)));
    REQUIRE(Near(quad.positions[2], Vec3(2.0f, 1.0f, 0.0f)));
    REQUIRE(Near(quad.positions[3], Vec3(-2.0f, 1.0f, 0.0f)));

    INFO("its triangles wind counter-clockwise seen from +Z, so its front faces +Z");
    const Vec3 a = quad.positions[quad.indices[0]];
    const Vec3 b = quad.positions[quad.indices[1]];
    const Vec3 c = quad.positions[quad.indices[2]];
    REQUIRE(Fenrir::Math::Cross(b - a, c - a).z > 0.0f);

    INFO("every normal is +Z, and every tangent +X with a bitangent sign of +1");
    REQUIRE(quad.normals.size() == 4);
    REQUIRE(quad.tangents.size() == 4);
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(Near(quad.normals[i], Vec3(0.0f, 0.0f, 1.0f)));
        REQUIRE(Near(Vec3(quad.tangents[i]), Vec3(1.0f, 0.0f, 0.0f)));
        REQUIRE(quad.tangents[i].w == 1.0f);
    }

    INFO("its UV is (0, 0) at the top-left corner and the repeat at the bottom-right, v growing down");
    REQUIRE(quad.uv0.size() == 4);
    REQUIRE(Near(quad.uv0[0], Vec2(0.0f, 3.0f)));
    REQUIRE(Near(quad.uv0[1], Vec2(3.0f, 3.0f)));
    REQUIRE(Near(quad.uv0[2], Vec2(3.0f, 0.0f)));
    REQUIRE(Near(quad.uv0[3], Vec2(0.0f, 0.0f)));
}

TEST_CASE("ScreenQuad's UV runs from 0 to 1 when no repeat is given", "[demo][helpers]")
{
    const Fenrir::MeshData quad = FenrirDemo::ScreenQuad(1.0f, 1.0f);

    INFO("the bottom-right corner's UV is (1, 1)");
    REQUIRE(Near(quad.uv0[1], Vec2(1.0f, 1.0f)));
}
