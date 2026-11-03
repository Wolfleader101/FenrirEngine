// Core/OctahedralNormals, the CPU mirror of Fenrir/Normals.glsl (ADR-0049): the octahedral packing the depth prepass
// writes view-space normals in.

#include <cmath>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "Core/OctahedralNormals.hpp"

using Fenrir::Math::Vec2;
using Fenrir::Math::Vec3;
using Fenrir::Render::OctahedralDecode;
using Fenrir::Render::OctahedralEncode;

TEST_CASE("Octahedral packing round-trips unit normals on both halves and on the axes", "[renderer][prepass]")
{
    const Vec3 n = glm::normalize(GENERATE(Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(1.0f, 0.0f, 0.0f),
                                           Vec3(0.0f, -1.0f, 0.0f), Vec3(0.3f, -0.5f, 0.8f), Vec3(-0.6f, 0.2f, -0.7f),
                                           Vec3(0.1f, 0.1f, -1.0f)));
    const Vec2 e = OctahedralEncode(n);

    INFO("the packed point is in [-1, 1]^2");
    REQUIRE(std::abs(e.x) <= 1.0f);
    REQUIRE(std::abs(e.y) <= 1.0f);
    INFO("and unpacks to the same normal");
    REQUIRE(glm::length(OctahedralDecode(e) - n) < 1e-5f);
}

TEST_CASE("Octahedral packing puts the normal facing the viewer at the origin, the cleared value",
          "[renderer][prepass]")
{
    INFO("view-space +Z, towards the camera, packs to (0, 0), which the normal target is cleared to");
    REQUIRE(glm::length(OctahedralEncode(Vec3(0.0f, 0.0f, 1.0f))) < 1e-6f);
    REQUIRE(glm::length(OctahedralDecode(Vec2(0.0f)) - Vec3(0.0f, 0.0f, 1.0f)) < 1e-6f);
}
