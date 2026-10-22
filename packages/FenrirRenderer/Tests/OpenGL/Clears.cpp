#include <catch2/catch_test_macros.hpp>

#include "Device/CommandList.hpp"
#include "GlProbe.hpp"
#include "GlTestApp.hpp"

using Fenrir::GlProbe;
using namespace Fenrir::Graphics;

TEST_CASE("Clearing an offscreen target to a colour reads back that colour", "[renderer][gl]")
{
    GlTest::GlTestApp test;
    test.RequireOpenGL();
    GraphicsDevice& device = *test.device;
    device.BeginFrame(0);
    const TextureHandle target = test.MakeTarget();

    const GlProbe::Pixel colour{0.25f, 0.5f, 0.75f, 1.0f};
    CommandList list;
    {
        PassDesc pass;
        pass.label = "Clear";
        pass.colour.push_back({target, LoadOp::Clear, colour});
        pass.uses.push_back({target, {}, ResourceUsage::ColourAttachment});
        PassEncoder encoder(list, pass);
    }
    device.Submit(list);
    const GlProbe::Image image = GlProbe::Read(device, target);

    INFO("the whole target reads back, at its size");
    REQUIRE(image.width == GlTest::TargetSize);
    REQUIRE(image.height == GlTest::TargetSize);
    INFO("every corner and the centre hold the clear colour");
    REQUIRE(GlProbe::Near(image.At(0, 0), colour));
    REQUIRE(GlProbe::Near(image.At(GlTest::TargetSize - 1, 0), colour));
    REQUIRE(GlProbe::Near(image.At(0, GlTest::TargetSize - 1), colour));
    REQUIRE(GlProbe::Near(image.At(GlTest::TargetSize - 1, GlTest::TargetSize - 1), colour));
    REQUIRE(GlProbe::Near(image.At(GlTest::TargetSize / 2, GlTest::TargetSize / 2), colour));
}
