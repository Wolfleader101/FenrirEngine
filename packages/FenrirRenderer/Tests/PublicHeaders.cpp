// FenrirRenderer's public headers name no graphics API, windowing library or shader compiler (ADR-0010, ADR-0016).
// This file includes them before anything else, so if one of them ever reaches for GL, its loader, SDL, glslang or
// SPIRV-Reflect, or one of those include paths reaches a user of FenrirRenderer (this test target is one), the build
// fails here.
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirRenderer/SpawnModel.hpp"

#if defined(GLAD_GL_H_) || defined(__gl_h_) || defined(__GL_H__) || defined(GL_VERSION_1_1) || defined(GL_TRUE)
#error "A FenrirRenderer public header includes GL or its loader."
#endif

#if defined(SDL_h_) || defined(SDL_VERSION)
#error "A FenrirRenderer public header includes SDL."
#endif

#if __has_include(<glad/gl.h>)
#error "The GL loader's include path reaches a user of FenrirRenderer: only the OpenGL backend links it, PRIVATE."
#endif

#if __has_include(<SDL3/SDL.h>)
#error "SDL's include path reaches a user of FenrirRenderer: only FenrirWindow links SDL, PRIVATE."
#endif

#if __has_include(<glslang/Public/ShaderLang.h>) || __has_include(<spirv_reflect.h>)
#error "The shader compiler's libraries reach a user of FenrirRenderer: only FenrirShaderCompiler links them."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The renderer's public headers compile without GL, SDL or the shader compiler's libraries",
          "[renderer][seam]")
{
    const Fenrir::MeshRenderer renderer;
    const Fenrir::RendererConfig config;

    INFO("a MeshRenderer is visible, and the default backend is OpenGL");
    REQUIRE(renderer.visible);
    REQUIRE(config.backend == Fenrir::GraphicsBackend::OpenGL);
}
