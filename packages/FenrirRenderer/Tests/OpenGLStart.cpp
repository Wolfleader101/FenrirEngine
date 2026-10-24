// Starting the OpenGL backend where it cannot start: no GPU or window needed, so these run with the null tests.

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "Backend/OpenGL/OpenGLDevice.hpp"
#include "FakeSurface.hpp"
#include "RecordingLogger.hpp"

using namespace Fenrir::Graphics;

TEST_CASE("The OpenGL backend fails to start, saying why, on a surface with no OpenGL context", "[renderer][opengl]")
{
    auto messages = std::make_shared<RendererTest::LoggedMessages>();
    RendererTest::RecordingLogger logger(messages);
    // The fake surface offers no GL functions, as a window without a GL context would not.
    RendererTest::FakeSurface surface;

    const OpenGLDeviceResult result = CreateOpenGLDevice(surface, DeviceConfig{}, logger);

    INFO("no device is made, and nothing falls back to another backend");
    REQUIRE(result.device == nullptr);
    INFO("the diagnostic says why");
    REQUIRE_FALSE(result.diagnostic.empty());
}
