// The skinning maths on the CPU (the spec's CPU unit tests; ADR-0035): GpuBone packing, and the C++ mirror of
// Fenrir/Skinning.glsl's normals and tangents, against the inverse transpose and hand-worked values.

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Skinning.hpp"
#include "FenrirMath/Math.hpp"

using Fenrir::Math::Mat3;
using Fenrir::Math::Mat4;
using Fenrir::Math::Vec3;
using Fenrir::Math::Vec4;
using Fenrir::Render::GpuBone;

namespace
{
    bool Near(Vec3 a, Vec3 b, float tolerance = 1e-5f)
    {
        return glm::all(glm::lessThan(glm::abs(a - b), Vec3(tolerance)));
    }

    bool Near(Vec4 a, Vec4 b, float tolerance = 1e-5f)
    {
        return glm::all(glm::lessThan(glm::abs(a - b), Vec4(tolerance)));
    }

    bool IsFinite(Vec3 v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    /** @brief One bone weighted wholly: the blended skin is that bone's matrix. */
    GpuBone Only(const Mat4& skin)
    {
        const std::array<GpuBone, 1> palette{GpuBone::FromMatrix(skin)};
        return Fenrir::Render::BlendBones(palette, 0, {0, 0, 0, 0}, {65535, 0, 0, 0});
    }

    /** @brief A translation, rotation and non-uniform scale, in that order of application reversed (T x R x S). */
    Mat4 Trs(Vec3 translation, float degrees, Vec3 axis, Vec3 scale)
    {
        Mat4 m = Fenrir::Math::Translate(Mat4(1.0f), translation);
        m = Fenrir::Math::Rotate(m, glm::radians(degrees), axis);
        return glm::scale(m, scale);
    }
} // namespace

TEST_CASE("GpuBone holds rows 0 to 2 of the skinning matrix", "[renderer][skinning-maths]")
{
    // Columns (1, 2, 3), (4, 5, 6), (7, 8, 9) and the translation (10, 11, 12): row k is (c0[k], c1[k], c2[k], t[k]).
    const Mat4 skin(Vec4(1, 2, 3, 0), Vec4(4, 5, 6, 0), Vec4(7, 8, 9, 0), Vec4(10, 11, 12, 1));

    const GpuBone bone = GpuBone::FromMatrix(skin);

    INFO("each row reads across glm's columns");
    REQUIRE(bone.r0 == Vec4(1, 4, 7, 10));
    REQUIRE(bone.r1 == Vec4(2, 5, 8, 11));
    REQUIRE(bone.r2 == Vec4(3, 6, 9, 12));
    INFO("the rows give back the affine matrix");
    REQUIRE(bone.ToMatrix() == skin);
}

TEST_CASE("A GpuBone moves points as its matrix does, by dot products with its rows", "[renderer][skinning-maths]")
{
    const Mat4 skin = Trs({1.0f, -2.0f, 3.0f}, 30.0f, glm::normalize(Vec3(1.0f, 2.0f, 0.5f)), {2.0f, 0.5f, 1.5f});
    const GpuBone bone = GpuBone::FromMatrix(skin);
    const Vec4 p(0.3f, -1.2f, 2.0f, 1.0f);

    const Vec3 rows(glm::dot(bone.r0, p), glm::dot(bone.r1, p), glm::dot(bone.r2, p));

    REQUIRE(Near(rows, Vec3(skin * p)));
}

TEST_CASE("Blending bones mixes their rows by the quantised weights, and no weight is the identity",
          "[renderer][skinning-maths]")
{
    const std::array<GpuBone, 3> palette{
        GpuBone::FromMatrix(Mat4(1.0f)),
        GpuBone::FromMatrix(Fenrir::Math::Translate(Mat4(1.0f), {4.0f, 0.0f, 0.0f})),
        GpuBone::FromMatrix(Fenrir::Math::Translate(Mat4(1.0f), {0.0f, 8.0f, 0.0f})),
    };

    // A palette range starting at 1: joints 0 and 1 are palette entries 1 and 2, weighted a quarter and three quarters.
    const GpuBone mixed = Fenrir::Render::BlendBones(palette, 1, {0, 1, 0, 0}, {16384, 49151, 0, 0});
    INFO("the translations mix: (4 x 1/4, 8 x 3/4, 0)");
    REQUIRE(Near(Vec3(mixed.r0.w, mixed.r1.w, mixed.r2.w), {1.0f, 6.0f, 0.0f}, 1e-3f));

    const GpuBone none = Fenrir::Render::BlendBones(palette, 1, {0, 1, 0, 0}, {0, 0, 0, 0});
    INFO("all-zero weights are the identity: the vertex stays at its bind position");
    REQUIRE(none.ToMatrix() == Mat4(1.0f));
}

TEST_CASE("Skinned normals follow the inverse transpose for well-conditioned matrices, negative determinants included",
          "[renderer][skinning-maths]")
{
    const std::vector<Mat3> matrices{
        Mat3(Trs({}, 40.0f, {0.0f, 0.0f, 1.0f}, {3.0f, 1.0f, 0.5f})),
        Mat3(Trs({}, -70.0f, glm::normalize(Vec3(1.0f, 1.0f, 0.0f)), {0.2f, 2.0f, 1.0f})),
        Mat3(Trs({}, 25.0f, {0.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 1.0f})),
        Mat3(Trs({}, 110.0f, glm::normalize(Vec3(0.3f, -1.0f, 0.7f)), {-2.0f, 0.5f, -3.0f})),
        Mat3(Trs({}, 10.0f, {1.0f, 0.0f, 0.0f}, {-1.0f, -1.0f, -1.0f})),
    };
    const std::vector<Vec3> normals{
        {0.0f, 0.0f, 1.0f}, glm::normalize(Vec3(1.0f, 1.0f, 1.0f)), glm::normalize(Vec3(-0.2f, 0.9f, 0.1f))};
    for (const Mat3& a : matrices)
    {
        for (const Vec3 n : normals)
        {
            const Vec3 reference = glm::normalize(glm::transpose(glm::inverse(a)) * n);
            INFO("the cofactor normal with det " << glm::determinant(a) << " points as the inverse transpose's");
            REQUIRE(Near(Fenrir::Render::SkinModelNormal(a, n), reference, 1e-4f));
        }
    }
}

TEST_CASE("A near-singular blend's normal falls back to A x n, then to the bind normal, never to NaN",
          "[renderer][skinning-maths]")
{
    const Vec3 n = glm::normalize(Vec3(0.3f, 0.4f, 0.8f));

    // Flattened along Z: a determinant of 0, but A x n still has a direction.
    const Mat3 flat(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f));
    INFO("a singular A gives normalize(A x n)");
    REQUIRE(Near(Fenrir::Render::SkinModelNormal(flat, n), glm::normalize(Vec3(0.3f, 0.4f, 0.0f)), 1e-5f));

    // |det A| = 1e-9, under 1e-6 x c^3 for its longest column c = 1.
    const Mat3 thin(Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1e-4f, 0.0f), Vec3(0.0f, 0.0f, 1e-5f));
    INFO("a near-singular A gives normalize(A x n) too");
    REQUIRE(Near(Fenrir::Render::SkinModelNormal(thin, n), glm::normalize(thin * n), 1e-5f));

    INFO("a zero A gives the bind normal");
    REQUIRE(Near(Fenrir::Render::SkinModelNormal(Mat3(0.0f), n), n));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    INFO("a non-finite A gives a finite unit normal");
    for (const Mat3& broken : {Mat3(nan), Mat3(Vec3(inf, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f))})
    {
        const Vec3 result = Fenrir::Render::SkinModelNormal(broken, n);
        REQUIRE(IsFinite(result));
        REQUIRE(std::abs(glm::length(result) - 1.0f) < 1e-5f);
    }
}

TEST_CASE("Skinned tangents stay unit and perpendicular to the normal, with handedness flipped by each mirror",
          "[renderer][skinning-maths]")
{
    const Vec4 tangent(1.0f, 0.0f, 0.0f, 1.0f);
    const Vec3 normal(0.0f, 0.0f, 1.0f);
    // Non-uniform and negative scales of the bone (A) and of the world (W): the sign of w is t.w x sign(det A) x
    // sign(det W).
    struct Case
    {
        Vec3 boneScale;
        Vec3 worldScale;
        float handedness;
    };
    const std::array<Case, 4> cases{{
        {{2.0f, 0.5f, 3.0f}, {1.0f, 4.0f, 1.0f}, 1.0f},
        {{-1.0f, 2.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, -1.0f},
        {{1.0f, 1.0f, 1.0f}, {1.0f, -3.0f, 1.0f}, -1.0f},
        {{-1.0f, 1.0f, 1.0f}, {2.0f, 1.0f, -1.0f}, 1.0f},
    }};
    for (const Case& c : cases)
    {
        const GpuBone skin = Only(Trs({}, 30.0f, {0.0f, 0.0f, 1.0f}, c.boneScale));
        const Mat4 world = Trs({1.0f, 2.0f, 3.0f}, 50.0f, glm::normalize(Vec3(1.0f, 1.0f, 0.0f)), c.worldScale);
        const float worldSign = glm::determinant(Mat3(world)) < 0.0f ? -1.0f : 1.0f;
        const Vec3 worldNormal = Fenrir::Render::SkinNormal(skin, glm::transpose(glm::inverse(world)), normal);

        const Vec4 result = Fenrir::Render::SkinTangent(skin, world, worldSign, tangent, worldNormal);

        INFO("bone scale " << c.boneScale.x << "," << c.boneScale.y << "," << c.boneScale.z << ", world scale "
                           << c.worldScale.x << "," << c.worldScale.y << "," << c.worldScale.z);
        REQUIRE(std::abs(glm::length(Vec3(result)) - 1.0f) < 1e-5f);
        REQUIRE(std::abs(glm::dot(Vec3(result), worldNormal)) < 1e-5f);
        REQUIRE(result.w == c.handedness);
    }
}

TEST_CASE("A tangent along the normal falls back to a unit vector perpendicular to it", "[renderer][skinning-maths]")
{
    const GpuBone skin = Only(Mat4(1.0f));
    const Vec3 worldNormal(0.0f, 0.0f, 1.0f);

    const Vec4 result = Fenrir::Render::SkinTangent(skin, Mat4(1.0f), 1.0f, {0.0f, 0.0f, 2.0f, -1.0f}, worldNormal);

    INFO("the world axis least aligned with +Z, X, made perpendicular, and the handedness kept");
    REQUIRE(Near(result, {1.0f, 0.0f, 0.0f, -1.0f}));
}

TEST_CASE("For rigid transforms the skinned and rigid paths give the same positions, normals and tangents",
          "[renderer][skinning-maths]")
{
    const Mat4 bone = Trs({0.5f, -1.0f, 2.0f}, 65.0f, glm::normalize(Vec3(0.2f, 1.0f, -0.4f)), Vec3(1.0f));
    const Mat4 world = Trs({3.0f, 0.0f, -1.0f}, -20.0f, {0.0f, 1.0f, 0.0f}, Vec3(1.0f));
    const GpuBone skin = Only(bone);
    // The rigid path draws the same vertex with the world matrix world x bone.
    const Mat4 rigid = world * bone;
    const Vec3 p(0.1f, 0.7f, -0.3f);
    const Vec3 n = glm::normalize(Vec3(1.0f, -2.0f, 0.5f));
    const Vec4 t(glm::normalize(glm::cross(n, Vec3(0.0f, 0.0f, 1.0f))), 1.0f);

    INFO("positions");
    REQUIRE(Near(Fenrir::Render::SkinPosition(skin, world, p), Vec3(rigid * Vec4(p, 1.0f)), 1e-5f));
    const Vec3 skinnedNormal = Fenrir::Render::SkinNormal(skin, glm::transpose(glm::inverse(world)), n);
    const Vec3 rigidNormal = Fenrir::Render::RigidNormal(glm::transpose(glm::inverse(rigid)), n);
    INFO("normals");
    REQUIRE(Near(skinnedNormal, rigidNormal, 1e-5f));
    INFO("tangents: the rigid path's is the tangent through the world matrix alone, with the identity skin");
    const Vec4 skinnedTangent = Fenrir::Render::SkinTangent(skin, world, 1.0f, t, skinnedNormal);
    const Vec4 rigidTangent = Fenrir::Render::SkinTangent(Only(Mat4(1.0f)), rigid, 1.0f, t, rigidNormal);
    REQUIRE(Near(skinnedTangent, rigidTangent, 1e-5f));
}
