// Ambient occlusion (ADR-0049) on the null backend (the spec's seam 1): each view whose camera has it gets GTAO's
// GtaoHorizons and GtaoDenoise passes after its DepthPrepass, reading the prepass's depth and normals; its Opaque pass
// binds their occlusion at set 1 binding 1, and every other pass, and every view without it, the white texture.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "Core/Gtao.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::PixelRect;
using Fenrir::Graphics::ResourceUsage;
using Fenrir::Graphics::TextureFormat;
using Fenrir::Graphics::TextureHandle;
using RendererTest::Labelled;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief Whether `pass` samples `texture`. */
    bool Samples(const PassRecord& pass, TextureHandle texture)
    {
        return std::any_of(pass.pass.uses.begin(), pass.pass.uses.end(), [&](const auto& use) {
            return use.texture == texture && use.usage == ResourceUsage::Sampled;
        });
    }

    /** @brief The labels of `passes`, in order. */
    std::vector<std::string> Labels(const std::vector<PassRecord>& passes)
    {
        std::vector<std::string> labels;
        for (const PassRecord& pass : passes)
        {
            labels.push_back(pass.pass.label);
        }
        return labels;
    }

    /** @brief The ambient occlusion texture the view group a pass bound at set 1 binds. */
    TextureHandle ViewOcclusion(const RendererTestApp& test, const PassRecord& pass)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(1));
        const auto found = std::find_if(entries.begin(), entries.end(), [](const auto& e) { return e.binding == 1; });
        REQUIRE(found != entries.end());
        return found->texture;
    }

    /** @brief Two cameras side by side, each with half the 1280 x 720 window, the right drawn second; the left has
     * ambient occlusion. */
    Fenrir::Camera& AddTwoCameras(RendererTestApp& test)
    {
        Fenrir::Camera& left = test.AddCamera().GetComponent<Fenrir::Camera>();
        left.viewport = {0.0f, 0.0f, 0.5f, 1.0f};
        left.ambientOcclusion.enabled = true;
        left.look.bloom.intensity = 0.0f;
        Fenrir::Camera& right = test.AddCamera().GetComponent<Fenrir::Camera>();
        right.viewport = {0.5f, 0.0f, 0.5f, 1.0f};
        right.order = 1;
        right.look.bloom.intensity = 0.0f;
        test.AddMeshRenderer(test.Mesh(), test.Material());
        return left;
    }

    /** @brief One camera with ambient occlusion of radius 0.75 and intensity 2 over the 1280 x 720 window, bloom off.
     */
    Fenrir::Camera& AddOccludedCamera(RendererTestApp& test)
    {
        Fenrir::Camera& camera = test.AddCamera().GetComponent<Fenrir::Camera>();
        camera.look.bloom.intensity = 0.0f;
        camera.ambientOcclusion = {true, 0.75f, 2.0f};
        test.AddMeshRenderer(test.Mesh(), test.Material());
        return camera;
    }

    /** @brief The float at byte `offset` of the block a pass bound at set 1. */
    float BlockFloat(const RendererTestApp& test, const PassRecord& pass, std::size_t offset)
    {
        const auto entries = RendererProbe::Entries(test.Device(), pass.groups.at(1));
        REQUIRE(entries.size() == 1);
        return RendererTest::FloatAt(RendererProbe::Bytes(test.Device(), entries[0].buffer),
                                     entries[0].offset + offset);
    }

    bool Near(float a, float b)
    {
        return std::abs(a - b) < 1e-5f;
    }
} // namespace

TEST_CASE("Ambient occlusion is on only when enabled with a radius and an intensity above 0", "[renderer][gtao]")
{
    INFO("off by default");
    REQUIRE_FALSE(Fenrir::Render::HasAmbientOcclusion({}));
    INFO("on when enabled with the default radius and intensity");
    REQUIRE(Fenrir::Render::HasAmbientOcclusion({true, 0.5f, 1.0f}));
    INFO("off at radius 0");
    REQUIRE_FALSE(Fenrir::Render::HasAmbientOcclusion({true, 0.0f, 1.0f}));
    INFO("off at intensity 0");
    REQUIRE_FALSE(Fenrir::Render::HasAmbientOcclusion({true, 0.5f, 0.0f}));
}

TEST_CASE("A default camera's view has no GTAO pass", "[renderer][gtao]")
{
    RendererTestApp test;
    test.AddCamera().GetComponent<Fenrir::Camera>().look.bloom.intensity = 0.0f;
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    INFO("the prepass (in its two occlusion phases), the scene and Output only");
    REQUIRE(Labels(test.Passes()) == RendererTest::WithOcclusion({"Cull", "BrdfLut", "DepthPrepass", "Opaque",
                                                                  "Background", "Output", "CullReadback"}));
}

TEST_CASE("A camera with ambient occlusion at intensity 0 has no GTAO pass", "[renderer][gtao]")
{
    RendererTestApp test;
    AddOccludedCamera(test).ambientOcclusion.intensity = 0.0f;

    test.Frame();
    INFO("enabled, but at intensity 0");
    REQUIRE(Labelled(test.Passes(), "GtaoHorizons").empty());
}

TEST_CASE("GTAO's passes run between a camera's DepthPrepass and Opaque", "[renderer][gtao]")
{
    RendererTestApp test;
    AddOccludedCamera(test);

    test.Frame();
    INFO("the horizon search, then two denoise passes, after the prepass's second phase and before Opaque");
    REQUIRE(Labels(test.Passes()) ==
            RendererTest::WithOcclusion({"Cull", "BrdfLut", "DepthPrepass", "GtaoHorizons", "GtaoDenoise",
                                         "GtaoDenoise", "Opaque", "Background", "Output", "CullReadback"}));
}

TEST_CASE("GTAO's horizon search reads the prepass's depth and normals into an occlusion and an edge target",
          "[renderer][gtao]")
{
    RendererTestApp test;
    AddOccludedCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord prepass = Labelled(passes, "DepthPrepass").at(0);
    const PassRecord horizons = Labelled(passes, "GtaoHorizons").at(0);
    INFO("it samples SceneDepth");
    REQUIRE(Samples(horizons, prepass.pass.depth->texture));
    INFO("and SceneNormals");
    REQUIRE(Samples(horizons, prepass.pass.colour.at(0).texture));
    INFO("into two targets");
    REQUIRE(horizons.pass.colour.size() == 2);
    INFO("over the whole view");
    REQUIRE(horizons.viewport == PixelRect{0, 0, 1280, 720});
}

TEST_CASE("GTAO's denoise passes blur the occlusion by the edges, twice, into the view's occlusion", "[renderer][gtao]")
{
    RendererTestApp test;
    AddOccludedCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord horizons = Labelled(passes, "GtaoHorizons").at(0);
    const std::vector<PassRecord> denoise = Labelled(passes, "GtaoDenoise");
    REQUIRE(denoise.size() == 2);
    INFO("the first samples the horizon search's occlusion");
    REQUIRE(Samples(denoise[0], horizons.pass.colour.at(0).texture));
    INFO("and its edges");
    REQUIRE(Samples(denoise[0], horizons.pass.colour.at(1).texture));
    INFO("the second samples the first's result");
    REQUIRE(Samples(denoise[1], denoise[0].pass.colour.at(0).texture));
    INFO("and the same edges");
    REQUIRE(Samples(denoise[1], horizons.pass.colour.at(1).texture));
}

TEST_CASE("GTAO's targets are single-sampled R8 at the view's size", "[renderer][gtao]")
{
    RendererTestApp test;
    AddOccludedCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord horizons = Labelled(passes, "GtaoHorizons").at(0);
    const std::vector<PassRecord> denoise = Labelled(passes, "GtaoDenoise");
    for (const TextureHandle target :
         {horizons.pass.colour.at(0).texture, horizons.pass.colour.at(1).texture,
          denoise.at(0).pass.colour.at(0).texture, denoise.at(1).pass.colour.at(0).texture})
    {
        const auto desc = RendererProbe::Desc(test.Device(), target);
        INFO("a target is R8, 1280 x 720, one sample");
        REQUIRE(
            (desc.format == TextureFormat::R8Unorm && desc.width == 1280 && desc.height == 720 && desc.samples == 1));
    }
}

TEST_CASE("GTAO's block holds XeGTAO's constants scaled by the camera's settings", "[renderer][gtao]")
{
    RendererTestApp test;
    AddOccludedCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord horizons = Labelled(passes, "GtaoHorizons").at(0);
    const std::vector<PassRecord> denoise = Labelled(passes, "GtaoDenoise");
    INFO("the effect radius: the camera's 0.75 times XeGTAO's 1.457");
    REQUIRE(Near(BlockFloat(test, horizons, 144), 0.75f * 1.457f));
    INFO("the power: XeGTAO's 2.2 times the camera's intensity of 2");
    REQUIRE(Near(BlockFloat(test, horizons, 148), 4.4f));
    INFO("the first denoise pass's centre weight: a fifth of XeGTAO's blur beta, 1.2");
    REQUIRE(Near(BlockFloat(test, denoise.at(0), 152), 0.24f));
    INFO("the final pass's: all of it");
    REQUIRE(Near(BlockFloat(test, denoise.at(1), 152), 1.2f));
}

TEST_CASE("Opaque binds the view's occlusion at set 1 binding 1", "[renderer][gtao]")
{
    RendererTestApp test;
    AddOccludedCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const PassRecord opaque = Labelled(passes, "Opaque").at(0);
    const TextureHandle occlusion = Labelled(passes, "GtaoDenoise").at(1).pass.colour.at(0).texture;
    INFO("Opaque samples the final denoise pass's target");
    REQUIRE(Samples(opaque, occlusion));
    INFO("and its view group binds it");
    REQUIRE(ViewOcclusion(test, opaque) == occlusion);
}

TEST_CASE("Passes other than Opaque bind the white texture as the view's occlusion", "[renderer][gtao]")
{
    RendererTestApp test;
    AddOccludedCamera(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const TextureHandle white = ViewOcclusion(test, Labelled(passes, "DepthPrepass").at(0));
    INFO("the prepass binds a 1 x 1 texture");
    REQUIRE(RendererProbe::Desc(test.Device(), white).width == 1);
    INFO("and Background the same one");
    REQUIRE(ViewOcclusion(test, Labelled(passes, "Background").at(0)) == white);
    INFO("no error was logged");
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("GTAO passes run only for the cameras that enable it", "[renderer][gtao]")
{
    RendererTestApp test;
    AddTwoCameras(test);

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<PassRecord> prepasses = Labelled(passes, "DepthPrepass");
    const std::vector<PassRecord> opaque = Labelled(passes, "Opaque");
    const std::vector<PassRecord> horizons = Labelled(passes, "GtaoHorizons");
    const std::vector<PassRecord> denoise = Labelled(passes, "GtaoDenoise");
    REQUIRE(opaque.size() == 2);
    INFO("one view's horizon search");
    REQUIRE(horizons.size() == 1);
    INFO("and its two denoise passes");
    REQUIRE(denoise.size() == 2);
    INFO("the left view's, at its size");
    REQUIRE(horizons[0].viewport == PixelRect{0, 0, 640, 720});
    INFO("from its prepass's depth");
    REQUIRE(Samples(horizons[0], prepasses[0].pass.depth->texture));
    INFO("the left view's Opaque binds its occlusion");
    REQUIRE(ViewOcclusion(test, opaque[0]) == denoise[1].pass.colour.at(0).texture);
    INFO("the right view's the white texture");
    REQUIRE(RendererProbe::Desc(test.Device(), ViewOcclusion(test, opaque[1])).width == 1);
}

TEST_CASE("A camera showing its ambient occlusion has Output draw it, and the lit scene's passes are culled",
          "[renderer][gtao]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddOccludedCamera(test);
    camera.debugView = Fenrir::CameraDebugView::AmbientOcclusion;

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    INFO("nothing reads SceneColor, so Opaque and Background go");
    REQUIRE(Labels(passes) == RendererTest::WithOcclusion({"Cull", "BrdfLut", "DepthPrepass", "GtaoHorizons",
                                                           "GtaoDenoise", "GtaoDenoise", "Output", "CullReadback"}));
    INFO("Output samples the view's occlusion");
    REQUIRE(Samples(Labelled(passes, "Output").at(0), Labelled(passes, "GtaoDenoise").at(1).pass.colour.at(0).texture));
}

TEST_CASE("A camera showing its ambient occlusion with it off draws Output alone", "[renderer][gtao]")
{
    RendererTestApp test;
    Fenrir::Camera& camera = AddOccludedCamera(test);
    camera.debugView = Fenrir::CameraDebugView::AmbientOcclusion;
    camera.ambientOcclusion.enabled = false;

    test.Frame();
    INFO("it shows black: Output, sampling nothing of the frame's; the cull passes and the pyramid's build stay, as "
         "they write what the camera keeps across frames, but the prepass's second phase goes with Opaque");
    std::vector<std::string> expected{"Cull", "BrdfLut", "DepthPrepass"};
    expected.insert(expected.end(), 11, "HiZ");
    expected.insert(expected.end(), {"Cull", "Output", "CullReadback"});
    REQUIRE(Labels(test.Passes()) == expected);
    INFO("no error was logged");
    REQUIRE(test.messages->errors.empty());
}
