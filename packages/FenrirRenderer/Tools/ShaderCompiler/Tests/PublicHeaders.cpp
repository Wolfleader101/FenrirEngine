// FenrirShaderCompiler links glslang and SPIRV-Reflect PRIVATE, and no engine target links the tool (ADR-0018). This
// test target links only FenrirShaderContainer, as the renderer and FenrirAssets do, so if either library's include
// path ever reaches a user of the container format, the build fails here.
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssets/ShaderGlBindings.hpp"

#if __has_include(<glslang/Public/ShaderLang.h>) || __has_include(<SPIRV/GlslangToSpv.h>)
#error "glslang's include path reaches a target that uses shader containers: the tool must link it PRIVATE."
#endif

#if __has_include(<spirv_reflect.h>)
#error "SPIRV-Reflect's include path reaches a target that uses shader containers: the tool must link it PRIVATE."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The shader container headers compile without the shader compiler's libraries", "[shader-compiler][seam]")
{
    INFO("the container format needs nothing from glslang or SPIRV-Reflect");
    REQUIRE(Fenrir::ShaderContainerVersion == 1);
}
