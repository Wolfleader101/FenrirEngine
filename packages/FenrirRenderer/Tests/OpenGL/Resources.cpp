#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "GlProbe.hpp"
#include "GlTestApp.hpp"

using Fenrir::GlProbe;
using namespace Fenrir::Graphics;

TEST_CASE("A destroyed resource's GL object is deleted only once framesInFlight frames have begun", "[renderer][gl]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    INFO("the device keeps two frames in flight");
    REQUIRE(device.FramesInFlight() == 2);

    device.BeginFrame(0);
    const BufferHandle buffer = device.CreateBuffer({64, BufferUsage::Uniform}, {}, "Destroyed buffer");
    const TextureHandle texture = test.MakeTarget();
    const std::uint32_t bufferName = GlProbe::GlName(device, buffer);
    const std::uint32_t textureName = GlProbe::GlName(device, texture);
    INFO("GL made a buffer and a texture behind the handles");
    REQUIRE(GlProbe::IsGlBuffer(device, bufferName));
    REQUIRE(GlProbe::IsGlTexture(device, textureName));

    REQUIRE(device.Destroy(buffer));
    REQUIRE(device.Destroy(texture));
    device.BeginFrame(1);
    INFO("one frame later, frame 0 may still be in flight: GL still has both");
    REQUIRE(GlProbe::IsGlBuffer(device, bufferName));
    REQUIRE(GlProbe::IsGlTexture(device, textureName));

    device.BeginFrame(2);
    INFO("two frames later both GL objects are deleted");
    REQUIRE_FALSE(GlProbe::IsGlBuffer(device, bufferName));
    REQUIRE_FALSE(GlProbe::IsGlTexture(device, textureName));
}
