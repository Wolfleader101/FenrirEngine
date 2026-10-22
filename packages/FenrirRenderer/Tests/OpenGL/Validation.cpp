// Debug validation: GL's debug messages reach the backend's logger from a synchronous callback, so a GL error fails the
// GL test whose call caused it.

#include <string>

#include <catch2/catch_test_macros.hpp>

#include "GlTestApp.hpp"

using namespace Fenrir::Graphics;

namespace
{
    /** @brief A texture GL refuses to make: a texture with no texels is a GL error. */
    TextureDesc EmptyTexture()
    {
        TextureDesc desc;
        desc.width = 0;
        desc.height = 0;
        return desc;
    }
} // namespace

// Passes only by failing: the harness turns the GL error into a failure of this test, through the debug callback.
TEST_CASE("A GL error under debug validation fails the test whose call caused it", "[renderer][gl][!shouldfail]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    test.device->BeginFrame(0);

    INFO("a zero-sized texture is a GL error, which must fail this test");
    static_cast<void>(test.device->CreateTexture(EmptyTexture(), "Empty"));
}

TEST_CASE("Under debug validation a GL error reaches the logger as an error", "[renderer][gl]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    test.logger.failOnError = false;
    test.device->BeginFrame(0);

    static_cast<void>(test.device->CreateTexture(EmptyTexture(), "Empty"));

    INFO("the GL error was logged as an error during the call that caused it");
    REQUIRE_FALSE(test.messages->errors.empty());
    INFO("and the message says it came from OpenGL: " << test.messages->errors.front());
    REQUIRE(test.messages->errors.front().find("OpenGL") != std::string::npos);
}
