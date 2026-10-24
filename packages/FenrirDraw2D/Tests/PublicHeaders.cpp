// FenrirDraw2D is plain data with no GPU code (ADR-0027): it knows no graphics API, window, renderer or shader
// compiler. This file includes its header before anything else, so if it ever reaches for one of them, or one of their
// include paths reaches a user of FenrirDraw2D (this test target is one), the build fails here.
#include "FenrirDraw2D/DrawList2D.hpp"

#if defined(GLAD_GL_H_) || defined(__gl_h_) || defined(__GL_H__) || defined(GL_VERSION_1_1) || defined(GL_TRUE)
#error "FenrirDraw2D includes GL or its loader."
#endif

#if defined(SDL_h_) || defined(SDL_VERSION)
#error "FenrirDraw2D includes SDL."
#endif

#if __has_include(<glad/gl.h>) || __has_include(<SDL3/SDL.h>)
#error "A graphics or window library's include path reaches a user of FenrirDraw2D."
#endif

#if __has_include("FenrirRenderer/Renderer.hpp") || __has_include("FenrirWindow/Window.hpp")
#error "FenrirDraw2D depends on FenrirRenderer or FenrirWindow: it may depend on FenrirAssets and FenrirMath only."
#endif

#if __has_include(<glslang/Public/ShaderLang.h>) || __has_include(<spirv_reflect.h>)
#error "The shader compiler's libraries reach a user of FenrirDraw2D."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("FenrirDraw2D's header compiles with no GPU, window or renderer code", "[draw2d][seam]")
{
    const Fenrir::DrawList2D list;

    INFO("a new draw list holds nothing");
    REQUIRE(list.commands.empty());
}
