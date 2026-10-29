// FenrirSkeleton depends on FenrirAssets, FenrirECS, FenrirScene, FenrirApp, FenrirMath and FenrirTime only (ADR-0028,
// ADR-0041): it knows no renderer, physics, animation, window or graphics API. This file includes its headers before
// anything else, so if one of them reaches for any of those, or their include paths reach a user of FenrirSkeleton
// (this test target is one), the build fails here.
#include "FenrirSkeleton/PoseStages.hpp"
#include "FenrirSkeleton/SkeletonPose.hpp"
#include "FenrirSkeleton/Skeletons.hpp"

#if defined(GLAD_GL_H_) || defined(__gl_h_) || defined(__GL_H__) || defined(GL_VERSION_1_1) || defined(GL_TRUE)
#error "FenrirSkeleton includes GL or its loader."
#endif

#if __has_include(<glad/gl.h>) || __has_include(<SDL3/SDL.h>)
#error "A graphics or window library's include path reaches a user of FenrirSkeleton."
#endif

#if __has_include("FenrirRenderer/Renderer.hpp") || __has_include("FenrirWindow/Window.hpp")
#error "FenrirSkeleton depends on FenrirRenderer or FenrirWindow."
#endif

#if __has_include("FenrirPhysics/Physics.hpp") || __has_include(<Jolt/Jolt.h>)
#error "FenrirSkeleton depends on FenrirPhysics."
#endif

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirSkeleton's headers compile with no renderer, physics or GPU code", "[skeleton][seam]")
{
    INFO("SkeletonPose is plain data that C# can mirror: trivially copyable, with a frame-posed default");
    STATIC_REQUIRE(std::is_trivially_copyable_v<Fenrir::SkeletonPose>);
    REQUIRE(Fenrir::SkeletonPose{}.timing == Fenrir::PoseTiming::Frame);
    REQUIRE(Fenrir::SkeletonPose{}.skeleton.IsEmpty());
}
