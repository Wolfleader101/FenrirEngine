// SelectLod (ADR-0057, culling ticket 07): the CPU's choice of a copy's level of detail, the formula builtin:cull
// repeats, so these cases are the GL readback test's oracle. The view is a 64-pixel-high camera at the origin with a
// 60 degree field of view, so one unit at distance 1 covers 32 / tan(30 deg) = 55.43 pixels.

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/LevelOfDetail.hpp"
#include "FenrirMath/Math.hpp"

using Fenrir::Math::Vec3;
using Fenrir::Render::LodView;
using Fenrir::Render::SelectLod;

namespace
{
    /** @brief Pixels per unit at distance 1 for a 64-pixel-high view with a 60 degree vertical field of view. */
    const float PixelsPerUnit = 32.0f / std::tan(std::numbers::pi_v<float> / 6.0f);

    /** @brief The camera at the origin looking along -Z. */
    const LodView View{Vec3(0.0f), PixelsPerUnit, 0.1f, true};

    /** @brief A unit box about the origin. */
    const Fenrir::Math::AABB UnitBox{Vec3(-0.5f), Vec3(0.5f)};

    /** @brief Three levels: full detail, then errors of 0.02 and 0.08 units. */
    const std::vector<float> Errors{0.0f, 0.02f, 0.08f};

    /** @brief The unit box at `distance` along -Z, scaled by `scale`. */
    Fenrir::Math::Mat4 At(float distance, float scale = 1.0f)
    {
        return glm::scale(Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), Vec3(0.0f, 0.0f, -distance)), Vec3(scale));
    }
} // namespace

TEST_CASE("SelectLod picks level 0 for a copy the camera is inside of", "[renderer][culling][lod]")
{
    INFO("the distance to the sphere's nearest point is 0 inside it, and no error projects under a pixel there");
    REQUIRE(SelectLod(Errors, UnitBox, At(0.0f), View, 1.0f) == 0);
    REQUIRE(SelectLod(Errors, UnitBox, At(0.5f), View, 1.0f) == 0);
}

TEST_CASE("SelectLod picks coarser levels further out, switching where each error projects to a pixel",
          "[renderer][culling][lod]")
{
    // The sphere's radius is the box's half-diagonal, 0.866, so a copy at distance d is d - 0.866 from the camera. An
    // error e projects to e * 55.43 / (d - 0.866) pixels: level 1's falls under a pixel past d = 1.97, level 2's past
    // d = 5.30.
    INFO("inside level 1's switch: full detail");
    REQUIRE(SelectLod(Errors, UnitBox, At(1.9f), View, 1.0f) == 0);
    INFO("past it: level 1");
    REQUIRE(SelectLod(Errors, UnitBox, At(2.1f), View, 1.0f) == 1);
    REQUIRE(SelectLod(Errors, UnitBox, At(5.2f), View, 1.0f) == 1);
    INFO("past level 2's: level 2");
    REQUIRE(SelectLod(Errors, UnitBox, At(5.4f), View, 1.0f) == 2);
    INFO("a stricter threshold keeps full detail further out: half a pixel doubles each switch distance");
    REQUIRE(SelectLod(Errors, UnitBox, At(2.1f), View, 0.5f) == 0);
    REQUIRE(SelectLod(Errors, UnitBox, At(3.2f), View, 0.5f) == 1);
}

TEST_CASE("SelectLod never picks past the last level, however far the copy", "[renderer][culling][lod]")
{
    REQUIRE(SelectLod(Errors, UnitBox, At(1000.0f), View, 1.0f) == 2);
    INFO("a mesh with only level 0 draws it at any distance");
    const std::vector<float> one{0.0f};
    REQUIRE(SelectLod(one, UnitBox, At(1000.0f), View, 1.0f) == 0);
    INFO("so does a mesh whose levels are not known at all");
    REQUIRE(SelectLod({}, UnitBox, At(1000.0f), View, 1.0f) == 0);
}

TEST_CASE("A copy scaled up switches levels further away than an unscaled one, its errors scaled with it",
          "[renderer][culling][lod]")
{
    // Scaled by 3, level 1's error is 0.06 units and the sphere's radius 2.6: under a pixel past d = 5.92.
    INFO("at 4 units the unscaled copy is at level 1 and the scaled one still at level 0");
    REQUIRE(SelectLod(Errors, UnitBox, At(4.0f), View, 1.0f) == 1);
    REQUIRE(SelectLod(Errors, UnitBox, At(4.0f, 3.0f), View, 1.0f) == 0);
    INFO("at 7 units the scaled copy reaches level 1 too");
    REQUIRE(SelectLod(Errors, UnitBox, At(7.0f, 3.0f), View, 1.0f) == 1);
    INFO("the largest axis scale counts: a copy stretched along one axis is scaled by that");
    const Fenrir::Math::Mat4 stretched =
        glm::scale(Fenrir::Math::Translate(Fenrir::Math::Mat4(1.0f), Vec3(0.0f, 0.0f, -4.0f)), Vec3(1.0f, 3.0f, 1.0f));
    REQUIRE(SelectLod(Errors, UnitBox, stretched, View, 1.0f) == 0);
}

TEST_CASE("With minProjectedPixels set a far copy is dropped, and the same copy is kept with it 0",
          "[renderer][culling][lod]")
{
    // At 100 units the sphere's radius, 0.866, projects to 0.866 * 55.43 / 99.13 = 0.48 pixels.
    INFO("under two pixels: dropped");
    REQUIRE(SelectLod(Errors, UnitBox, At(100.0f), View, 1.0f, 2.0f) == std::nullopt);
    INFO("with the least size 0 it is kept, at the last level");
    REQUIRE(SelectLod(Errors, UnitBox, At(100.0f), View, 1.0f, 0.0f) == 2);
    INFO("a near copy projects large and is kept whatever the least size");
    REQUIRE(SelectLod(Errors, UnitBox, At(2.0f), View, 1.0f, 2.0f) == 1);
}

TEST_CASE("An orthographic view projects the same at any distance, so the level never changes with it",
          "[renderer][culling][lod]")
{
    // 64 pixels over an orthographic height of 4 units: 16 pixels a unit, so level 1's error covers 0.32 pixels and
    // level 2's 1.28 at any distance.
    const LodView orthographic{Vec3(0.0f), 16.0f, 0.1f, false};
    REQUIRE(SelectLod(Errors, UnitBox, At(1.0f), orthographic, 1.0f) == 1);
    REQUIRE(SelectLod(Errors, UnitBox, At(1000.0f), orthographic, 1.0f) == 1);
}
