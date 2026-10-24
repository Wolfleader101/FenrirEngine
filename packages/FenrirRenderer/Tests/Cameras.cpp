// Cameras on the null backend (the spec's seam 1): several views in order with their viewport rectangles, image
// targets ordered before the views that sample them, self-sampling replaced, skipped cameras and MSAA (ADR-0026,
// ADR-0019).

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using Fenrir::Graphics::LoadOp;
using Fenrir::Graphics::PixelRect;
using Fenrir::Graphics::ResourceUsage;
using Fenrir::Graphics::TextureHandle;
using RendererTest::PassRecord;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A camera at `position` looking along -Z, drawing into `viewport` of `target` at `order`. */
    Fenrir::Entity AddCamera(RendererTestApp& test, std::int32_t order, Fenrir::Viewport viewport = {},
                             Fenrir::RenderTarget target = {})
    {
        Fenrir::Entity entity = test.AddCamera();
        Fenrir::Camera& camera = entity.GetComponent<Fenrir::Camera>();
        camera.order = order;
        camera.viewport = viewport;
        camera.target = target;
        return entity;
    }

    /** @brief The device texture an Output pass draws into. */
    TextureHandle TargetOf(const PassRecord& output)
    {
        return output.pass.colour.at(0).texture;
    }

    /** @brief The index in `passes` of the first pass labelled `label` drawing or sampling `texture`, else -1. */
    std::ptrdiff_t IndexOf(const std::vector<PassRecord>& passes, std::string_view label, TextureHandle texture,
                           ResourceUsage usage)
    {
        for (std::size_t i = 0; i < passes.size(); ++i)
        {
            const auto& uses = passes[i].pass.uses;
            const bool found = std::any_of(uses.begin(), uses.end(), [&](const Fenrir::Graphics::PassResourceUse& u) {
                return u.texture == texture && u.usage == usage;
            });
            if (passes[i].pass.label == label && found)
            {
                return static_cast<std::ptrdiff_t>(i);
            }
        }
        return -1;
    }

    /** @brief The textures a binding group binds. */
    std::vector<TextureHandle> TexturesOf(const RendererTestApp& test, Fenrir::Graphics::BindingGroupHandle group)
    {
        std::vector<TextureHandle> textures;
        for (const Fenrir::Graphics::BindingGroupEntry& entry : RendererProbe::Entries(test.Device(), group))
        {
            if (!entry.texture.IsEmpty())
            {
                textures.push_back(entry.texture);
            }
        }
        return textures;
    }
} // namespace

TEST_CASE("Two cameras into the window draw in order, each into its viewport rectangle", "[renderer][cameras]")
{
    RendererTestApp test;
    // Made first, but drawn second: order decides, not entity id.
    AddCamera(test, 1, {0.0f, 0.0f, 0.5f, 1.0f});
    AddCamera(test, 0, {0.5f, 0.25f, 0.5f, 0.5f});
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    const std::vector<PassRecord> outputs = RendererTest::Labelled(test.Passes(), "Output");

    INFO("each view writes the window once");
    REQUIRE(outputs.size() == 2);
    REQUIRE(TargetOf(outputs[0]) == test.Device().Backbuffer());
    REQUIRE(TargetOf(outputs[1]) == test.Device().Backbuffer());
    INFO("order 0 first, into the right half's middle of the 1280 x 720 surface, clearing the window");
    REQUIRE(outputs[0].viewport == PixelRect{640, 180, 640, 360});
    REQUIRE(outputs[0].pass.colour.at(0).load == LoadOp::Clear);
    INFO("then order 1, into the left half, over what order 0 drew");
    REQUIRE(outputs[1].viewport == PixelRect{0, 0, 640, 720});
    REQUIRE(outputs[1].pass.colour.at(0).load == LoadOp::Load);

    INFO("each view's scene targets are its viewport's size");
    const std::vector<PassRecord> opaque = RendererTest::Labelled(test.Passes(), "Opaque");
    REQUIRE(opaque.size() == 2);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> sizes;
    for (const PassRecord& pass : opaque)
    {
        const auto colour = RendererProbe::Desc(test.Device(), pass.pass.colour.at(0).texture);
        const auto depth = RendererProbe::Desc(test.Device(), pass.pass.depth->texture);
        REQUIRE(colour.width == depth.width);
        REQUIRE(colour.height == depth.height);
        sizes.emplace_back(colour.width, colour.height);
    }
    std::sort(sizes.begin(), sizes.end());
    REQUIRE(sizes == std::vector<std::pair<std::uint32_t, std::uint32_t>>{{640, 360}, {640, 720}});
}

TEST_CASE("Cameras of equal order into one target draw by entity id, the tie logged once", "[renderer][cameras]")
{
    RendererTestApp test;
    const Fenrir::Entity first = AddCamera(test, 3, {0.0f, 0.0f, 0.5f, 1.0f});
    const Fenrir::Entity second = AddCamera(test, 3, {0.5f, 0.0f, 0.5f, 1.0f});
    REQUIRE(first.GetId() < second.GetId());

    test.Frame();
    test.Frame();
    const std::vector<PassRecord> outputs = RendererTest::Labelled(test.Passes(), "Output");

    INFO("the lower entity id draws first");
    REQUIRE(outputs.size() == 2);
    REQUIRE(outputs[0].viewport.x == 0);
    REQUIRE(outputs[1].viewport.x == 640);
    INFO("the tie was logged once over two frames");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "same order") == 1);
}

TEST_CASE("A view into an image is drawn before the view whose material samples the image", "[renderer][cameras]")
{
    RendererTestApp test;
    const auto image = test.TargetImage(256, 128);
    // The window's camera first by order and id, so only the image's dependency puts its writer first.
    AddCamera(test, 0);
    AddCamera(test, 5, {}, Fenrir::ImageTarget{image});
    test.AddMeshRenderer(test.Mesh(), test.SampledMaterial(image));

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<PassRecord> outputs = RendererTest::Labelled(passes, "Output");
    REQUIRE(outputs.size() == 2);
    const auto intoImage = std::find_if(outputs.begin(), outputs.end(),
                                        [&](const PassRecord& p) { return TargetOf(p) != test.Device().Backbuffer(); });
    REQUIRE(intoImage != outputs.end());
    const TextureHandle texture = TargetOf(*intoImage);

    INFO("the image is RGBA8 sRGB at its asset's size, and the view fills it");
    const auto desc = RendererProbe::Desc(test.Device(), texture);
    REQUIRE(desc.format == Fenrir::Graphics::TextureFormat::Rgba8Srgb);
    REQUIRE(desc.width == 256);
    REQUIRE(desc.height == 128);
    REQUIRE(intoImage->viewport == PixelRect{0, 0, 256, 128});

    INFO("the window's Opaque pass samples the image, after the Output pass that draws it");
    const std::ptrdiff_t writer = IndexOf(passes, "Output", texture, ResourceUsage::ColourAttachment);
    const std::ptrdiff_t reader = IndexOf(passes, "Opaque", texture, ResourceUsage::Sampled);
    REQUIRE(writer >= 0);
    REQUIRE(reader > writer);
    INFO("and its draw binds the image's texture");
    REQUIRE(TexturesOf(test, passes[static_cast<std::size_t>(reader)].groups.at(2)) ==
            std::vector<TextureHandle>{texture});
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A view whose own draws sample its target gets the default texture there, logged once", "[renderer][cameras]")
{
    RendererTestApp test;
    const auto image = test.TargetImage(64, 64);
    AddCamera(test, 0, {}, Fenrir::ImageTarget{image});
    test.AddMeshRenderer(test.Mesh(), test.SampledMaterial(image));

    test.Frame();
    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<PassRecord> outputs = RendererTest::Labelled(passes, "Output");
    const std::vector<PassRecord> opaque = RendererTest::Labelled(passes, "Opaque");
    REQUIRE(outputs.size() == 1);
    REQUIRE(opaque.size() == 1);
    const TextureHandle texture = TargetOf(outputs[0]);

    INFO("the view's draw binds a default texture, not its own target");
    const std::vector<TextureHandle> bound = TexturesOf(test, opaque[0].groups.at(2));
    REQUIRE(bound.size() == 1);
    REQUIRE(bound[0] != texture);
    INFO("so its Opaque pass does not sample the target");
    REQUIRE(IndexOf(passes, "Opaque", texture, ResourceUsage::Sampled) == -1);
    INFO("logged once over two frames");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "its own target") == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Two views sampling each other's images both draw, the cycle broken with a default texture",
          "[renderer][cameras]")
{
    RendererTestApp test;
    const auto first = test.TargetImage(32, 32);
    const auto second = test.TargetImage(16, 16);
    AddCamera(test, 0, {}, Fenrir::ImageTarget{first});
    AddCamera(test, 1, {}, Fenrir::ImageTarget{second});
    test.AddMeshRenderer(test.Mesh(), test.SampledMaterial(first));
    test.AddMeshRenderer(test.Mesh(), test.SampledMaterial(second));

    test.Frame();

    INFO("no cycle reached the render graph: nothing was logged as an error, and both views drew");
    REQUIRE(test.messages->errors.empty());
    REQUIRE(RendererTest::Labelled(test.Passes(), "Output").size() == 2);
    INFO("one view's sampling of the other's image was replaced, logged");
    REQUIRE(RendererTest::CountContaining(test.messages->warnings, "in a cycle") == 1);
}

namespace
{
    /** @brief Runs two frames with `camera` the only one, and checks it drew nothing and was logged once. */
    void RequireSkipped(RendererTestApp& test, std::string_view why)
    {
        test.AddMeshRenderer(test.Mesh(), test.Material());
        test.Frame();
        test.Frame();
        INFO("no view was drawn: the window was cleared");
        REQUIRE(RendererTest::PassesIn(RendererProbe::Submitted(test.renderer)) == std::vector<std::string>{"Clear"});
        INFO("the camera was logged once over two frames, saying " << why);
        REQUIRE(RendererTest::CountContaining(test.messages->warnings, why) == 1);
        REQUIRE(test.messages->errors.empty());
    }
} // namespace

TEST_CASE("A camera targeting a window other than the main window is skipped, logged once", "[renderer][cameras]")
{
    RendererTestApp test;
    AddCamera(test, 0, {}, Fenrir::WindowTarget{Fenrir::WindowId{7}});
    RequireSkipped(test, "not the main window");
}

TEST_CASE("A camera whose viewport is under one pixel is skipped, logged once", "[renderer][cameras]")
{
    RendererTestApp test;
    // 0.0001 of 1280 pixels is under one.
    AddCamera(test, 0, {0.25f, 0.25f, 0.0001f, 0.5f});
    RequireSkipped(test, "under one pixel");
}

TEST_CASE("A camera targeting an empty image is skipped, logged once", "[renderer][cameras]")
{
    RendererTestApp test;
    AddCamera(test, 0, {}, Fenrir::ImageTarget{});
    RequireSkipped(test, "no target image");
}

TEST_CASE("A camera targeting a stale image is skipped, logged once", "[renderer][cameras]")
{
    RendererTestApp test;
    const auto image = test.TargetImage(64, 64);
    test.Store().Unload(image);
    AddCamera(test, 0, {}, Fenrir::ImageTarget{image});
    RequireSkipped(test, "stale target image");
}

TEST_CASE("A camera targeting an image with pixels is skipped, logged once", "[renderer][cameras]")
{
    RendererTestApp test;
    Fenrir::ImageData picture;
    picture.width = 1;
    picture.height = 1;
    picture.pixels.resize(4);
    const auto image = test.Store().Add<Fenrir::Image>(std::move(picture), Fenrir::AssetOwner::App());
    AddCamera(test, 0, {}, Fenrir::ImageTarget{image});
    RequireSkipped(test, "not a render target");
}

TEST_CASE("Resizing a target image through the store remakes its texture at the new size", "[renderer][cameras]")
{
    RendererTestApp test;
    const auto image = test.TargetImage(64, 64);
    AddCamera(test, 0, {}, Fenrir::ImageTarget{image});
    test.Frame();

    Fenrir::ImageData bigger;
    bigger.width = 100;
    bigger.height = 50;
    REQUIRE(test.Store().Replace(image, std::move(bigger)));
    test.Frame();
    const std::vector<PassRecord> outputs = RendererTest::Labelled(test.Passes(), "Output");

    INFO("the view draws the whole of the 100 x 50 texture");
    REQUIRE(outputs.size() == 1);
    const auto desc = RendererProbe::Desc(test.Device(), TargetOf(outputs[0]));
    REQUIRE(desc.width == 100);
    REQUIRE(desc.height == 50);
    REQUIRE(outputs[0].viewport == PixelRect{0, 0, 100, 50});
}

TEST_CASE("A camera's colour background is what its Background pass draws", "[renderer][cameras]")
{
    RendererTestApp test;
    Fenrir::Entity camera = AddCamera(test, 0);
    camera.GetComponent<Fenrir::Camera>().background = Fenrir::ColourBackground{Fenrir::Colour{0.1f, 0.2f, 0.3f, 1.0f}};

    test.Frame();
    const std::vector<PassRecord> background = RendererTest::Labelled(test.Passes(), "Background");

    INFO("the Background pass's block holds the camera's colour");
    REQUIRE(background.size() == 1);
    const auto entries = RendererProbe::Entries(test.Device(), background[0].groups.at(2));
    REQUIRE(entries.size() == 1);
    const auto bytes = RendererProbe::Bytes(test.Device(), entries[0].buffer);
    const std::size_t at = entries[0].offset;
    REQUIRE(std::abs(RendererTest::FloatAt(bytes, at) - 0.1f) < 1e-6f);
    REQUIRE(std::abs(RendererTest::FloatAt(bytes, at + 4) - 0.2f) < 1e-6f);
    REQUIRE(std::abs(RendererTest::FloatAt(bytes, at + 8) - 0.3f) < 1e-6f);
}

TEST_CASE("With msaa 4 the scene targets have 4 samples and Output resolves them", "[renderer][cameras][msaa]")
{
    RendererTestApp test([] {
        Fenrir::RendererConfig config = RendererTest::NullBackend();
        config.msaa = 4;
        return config;
    }());
    AddCamera(test, 0);
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    const std::vector<PassRecord> passes = test.Passes();
    const std::vector<PassRecord> opaque = RendererTest::Labelled(passes, "Opaque");
    const std::vector<PassRecord> outputs = RendererTest::Labelled(passes, "Output");
    REQUIRE(opaque.size() == 1);
    REQUIRE(outputs.size() == 1);

    INFO("SceneColor and SceneDepth have 4 samples");
    const TextureHandle sceneColour = opaque[0].pass.colour.at(0).texture;
    REQUIRE(RendererProbe::Desc(test.Device(), sceneColour).samples == 4);
    REQUIRE(RendererProbe::Desc(test.Device(), opaque[0].pass.depth->texture).samples == 4);
    INFO("the mesh pipeline draws with 4 samples");
    REQUIRE(RendererProbe::Samples(test.Device(), opaque[0].pipeline) == 4);
    INFO("Output samples the multisampled SceneColor with its MSAA variant, into the single-sampled window");
    REQUIRE(IndexOf(passes, "Output", sceneColour, ResourceUsage::Sampled) >= 0);
    REQUIRE(RendererProbe::Keys(test.Device(), outputs[0].pipeline) == std::vector<std::string>{"MSAA"});
    REQUIRE(TargetOf(outputs[0]) == test.Device().Backbuffer());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Without msaa the scene targets have one sample and Output samples them plainly", "[renderer][cameras][msaa]")
{
    RendererTestApp test;
    AddCamera(test, 0);
    test.AddMeshRenderer(test.Mesh(), test.Material());

    test.Frame();
    const std::vector<PassRecord> opaque = RendererTest::Labelled(test.Passes(), "Opaque");
    const std::vector<PassRecord> outputs = RendererTest::Labelled(test.Passes(), "Output");

    INFO("one sample, and the Output variant with no keys");
    REQUIRE(RendererProbe::Desc(test.Device(), opaque.at(0).pass.colour.at(0).texture).samples == 1);
    REQUIRE(RendererProbe::Keys(test.Device(), outputs.at(0).pipeline).empty());
}
