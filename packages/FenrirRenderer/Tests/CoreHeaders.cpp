// Only src/Backend/OpenGL/ may include GL or its loader (ADR-0016). This file includes every header of the renderer
// core, the graphics device and both backends before anything else, so if one of them ever reaches for GL, glad or
// SDL, or the loader's include path reaches a user of the OpenGL backend (this test target links it), the build fails
// here.
#include "Backend/Null/NullDevice.hpp"
#include "Backend/OpenGL/OpenGLDevice.hpp"
#include "Core/FrameRingBuffer.hpp"
#include "Core/RenderFeature.hpp"
#include "Core/RenderGraph.hpp"
#include "Core/RendererCore.hpp"
#include "Core/TransientPool.hpp"
#include "Device/CommandList.hpp"
#include "Device/GraphicsDevice.hpp"
#include "Device/GraphicsSurface.hpp"
#include "Device/Handles.hpp"
#include "Device/ResourceTable.hpp"
#include "Device/Resources.hpp"

#if defined(GLAD_GL_H_) || defined(__gl_h_) || defined(__GL_H__) || defined(GL_VERSION_1_1) || defined(GL_TRUE)
#error "A renderer core or backend header includes GL or its loader."
#endif

#if defined(SDL_h_) || defined(SDL_VERSION)
#error "A renderer core or backend header includes SDL."
#endif

#if __has_include(<glad/gl.h>)
#error "The GL loader's include path reaches a target outside the OpenGL backend: it must be linked PRIVATE."
#endif

#if __has_include(<SDL3/SDL.h>)
#error "SDL's include path reaches the renderer core: only FenrirWindow links SDL."
#endif

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The core's and the backends' headers compile without GL, its loader or SDL", "[renderer][seam]")
{
    Fenrir::Graphics::BufferHandle handle;

    INFO("a handle made with no arguments is empty");
    REQUIRE(handle.IsEmpty());
}
