// FenrirAnimation depends on FenrirSkeleton and FenrirAssets only (ADR-0028): it knows no renderer, physics, window or
// graphics API. This file includes its headers before anything else, so if one of them reaches for any of those, or
// their include paths reach a user of FenrirAnimation (this test target is one), the build fails here.
#include "FenrirAnimation/Animation.hpp"
#include "FenrirAnimation/AnimationEvents.hpp"
#include "FenrirAnimation/Animator.hpp"

#if defined(GLAD_GL_H_) || defined(__gl_h_) || defined(__GL_H__) || defined(GL_VERSION_1_1) || defined(GL_TRUE)
#error "FenrirAnimation includes GL or its loader."
#endif

#if __has_include(<glad/gl.h>) || __has_include(<SDL3/SDL.h>)
#error "A graphics or window library's include path reaches a user of FenrirAnimation."
#endif

#if __has_include("FenrirRenderer/Renderer.hpp") || __has_include("FenrirWindow/Window.hpp")
#error "FenrirAnimation depends on FenrirRenderer or FenrirWindow."
#endif

#if __has_include("FenrirPhysics/Physics.hpp") || __has_include(<Jolt/Jolt.h>)
#error "FenrirAnimation depends on FenrirPhysics."
#endif

#if __has_include(<ozz/animation/runtime/animation.h>)
#error "FenrirAnimation's runtime is its own, not ozz-animation."
#endif

#include <type_traits>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirAnimation's headers compile with no renderer, physics or GPU code", "[animation][seam]")
{
    INFO("Animator and the events are plain data that C# can mirror: trivially copyable");
    STATIC_REQUIRE(std::is_trivially_copyable_v<Fenrir::Animator>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Fenrir::AnimationEvent>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Fenrir::ClipFinished>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<Fenrir::AnimationEventsDropped>);

    INFO("an Animator plays forward, looping, at speed 1, with no fade; the budget defaults to 64");
    const Fenrir::Animator animator;
    REQUIRE(animator.clip.IsEmpty());
    REQUIRE(animator.speed == 1.0f);
    REQUIRE(animator.loop == Fenrir::ClipLoop::Loop);
    REQUIRE(animator.playing);
    REQUIRE(animator.fadeSeconds == 0.0f);
    REQUIRE(animator.fadeCurve == Fenrir::FadeCurve::Smooth);
    REQUIRE(Fenrir::AnimationConfig{}.maxEventsPerInstancePerFrame == 64);
}
