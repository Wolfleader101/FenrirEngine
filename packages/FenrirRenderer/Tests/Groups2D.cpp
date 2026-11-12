// The image 2D target and offscreen groups through the ECS facade (ADR-0060): 2D draw lists submitted to an image
// through the ImageTargets2D resource, and groups in lists on the window overlay, drawn on the null backend and read
// back through RendererProbe (the spec's seam 4).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DrawLists2D.hpp"
#include "FenrirRenderer/ImageTargets2D.hpp"
#include "FenrirRenderer/Overlay.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::AddQuad;
using RendererTest::Pipeline2D;
using RendererTest::RendererTestApp;

namespace
{
    constexpr Fenrir::WindowId MainWindow = Fenrir::WindowTarget{}.window;

    /** @brief An image with no pixels of `width` x `height`, encoded as `space`: what an image 2D target draws into. */
    Fenrir::AssetHandle<Fenrir::Image> TargetImage(RendererTestApp& test, std::uint32_t width, std::uint32_t height,
                                                   Fenrir::ColourSpace space)
    {
        Fenrir::ImageData data;
        data.width = width;
        data.height = height;
        data.colourSpace = space;
        return test.Store().Add<Fenrir::Image>(std::move(data), Fenrir::AssetOwner::App());
    }

    /** @brief A list with one builtin:2d pipeline, premultiplied as a painter's are, and no commands. */
    Fenrir::DrawList2D List(RendererTestApp& test)
    {
        Fenrir::DrawList2D list;
        list.AddPipeline(Pipeline2D(test.Builtin("builtin:2d"), Fenrir::BlendMode2D::PremultipliedAlpha));
        return list;
    }

    /** @brief The texture the draw's set 1 group binds; empty if it binds none. */
    Fenrir::Graphics::TextureHandle SampledBy(const RendererTestApp& test, const RendererTest::RecordedDraw& draw)
    {
        for (const Fenrir::Graphics::BindingGroupEntry& entry :
             RendererProbe::Entries(test.Device(), draw.groups.at(1)))
        {
            if (!entry.texture.IsEmpty())
            {
                return entry.texture;
            }
        }
        return {};
    }

    /** @brief Where the first pass labelled `label` ran among the last frame's passes; the pass count if none did. */
    std::size_t PositionOf(const std::vector<std::string>& passes, const std::string& label)
    {
        return static_cast<std::size_t>(std::distance(passes.begin(), std::find(passes.begin(), passes.end(), label)));
    }

    /**
     * @brief A quad (a), a group over (10, 10)-(50, 30) holding a quad (b), a group over (20, 15)-(30, 20) of one quad
     * (c) at half opacity, and a quad (d), then a quad (e) after both.
     */
    Fenrir::DrawList2D NestedGroups(RendererTestApp& test)
    {
        Fenrir::DrawList2D list = List(test);
        AddQuad(list, 0, {0.0f, 0.0f}, {4.0f, 4.0f});
        list.BeginGroup({10.0f, 10.0f, 40.0f, 20.0f});
        AddQuad(list, 0, {10.0f, 10.0f}, {20.0f, 20.0f});
        list.BeginGroup({20.0f, 15.0f, 10.0f, 5.0f});
        AddQuad(list, 0, {20.0f, 15.0f}, {30.0f, 20.0f});
        Fenrir::EndGroup2D half;
        half.opacity = 0.5f;
        list.EndGroup(half);
        AddQuad(list, 0, {30.0f, 20.0f}, {40.0f, 30.0f});
        list.EndGroup();
        AddQuad(list, 0, {60.0f, 0.0f}, {64.0f, 4.0f});
        return list;
    }
} // namespace

TEST_CASE("A list submitted to an image 2D target draws one pass into the image, at its size and encoding",
          "[renderer][overlay][image2d]")
{
    RendererTestApp test;
    const auto linear = TargetImage(test, 32, 16, Fenrir::ColourSpace::Linear);
    const auto srgb = TargetImage(test, 8, 4, Fenrir::ColourSpace::Srgb);
    Fenrir::ImageTargets2D* targets = test.app.TryGetResource<Fenrir::ImageTargets2D>();
    INFO("Renderer::Install adds the ImageTargets2D app resource");
    REQUIRE(targets != nullptr);
    Fenrir::DrawList2D list = List(test);
    AddQuad(list, 0, {0.0f, 0.0f}, {32.0f, 16.0f});
    targets->Submit(linear, list);
    targets->Submit(srgb, std::move(list));

    test.Frame();
    const std::vector<RendererTest::PassRecord> passes = RendererTest::Labelled(test.Passes(), "Image 2D");

    INFO("one pass for each image, in submission order, each drawing into the image's texture");
    REQUIRE(passes.size() == 2);
    const Fenrir::Graphics::TextureHandle linearTexture = RendererProbe::ImageTexture(test.renderer, linear);
    const Fenrir::Graphics::TextureHandle srgbTexture = RendererProbe::ImageTexture(test.renderer, srgb);
    REQUIRE(passes[0].pass.colour.size() == 1);
    REQUIRE(passes[0].pass.colour[0].texture == linearTexture);
    REQUIRE(passes[1].pass.colour[0].texture == srgbTexture);
    INFO("the linear image is a linear RGBA8 texture of its size, the sRGB one an sRGB RGBA8 texture");
    const Fenrir::Graphics::TextureDesc linearDesc = RendererProbe::Desc(test.Device(), linearTexture);
    REQUIRE(linearDesc.width == 32);
    REQUIRE(linearDesc.height == 16);
    REQUIRE(linearDesc.format == Fenrir::Graphics::TextureFormat::Rgba8Unorm);
    REQUIRE(RendererProbe::Desc(test.Device(), srgbTexture).format == Fenrir::Graphics::TextureFormat::Rgba8Srgb);
    INFO("the image is cleared to transparent and drawn over its whole size");
    REQUIRE(passes[0].pass.colour[0].load == Fenrir::Graphics::LoadOp::Clear);
    REQUIRE(passes[0].pass.colour[0].clear == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});
    REQUIRE(passes[0].viewport == Fenrir::Graphics::PixelRect{0, 0, 32, 16});
    REQUIRE(test.Draws("Image 2D").size() == 2);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("Image 2D target submissions are cleared after the frame", "[renderer][overlay][image2d]")
{
    RendererTestApp test;
    const auto image = TargetImage(test, 16, 16, Fenrir::ColourSpace::Srgb);
    Fenrir::DrawList2D list = List(test);
    AddQuad(list, 0, {0.0f, 0.0f}, {16.0f, 16.0f});
    test.app.GetResource<Fenrir::ImageTargets2D>().Submit(image, std::move(list));

    test.Frame();
    INFO("the first frame draws the image");
    REQUIRE(RendererTest::Labelled(test.Passes(), "Image 2D").size() == 1);

    test.Frame();
    INFO("the next, with nothing submitted, leaves it as it is");
    REQUIRE(RendererTest::Labelled(test.Passes(), "Image 2D").empty());
}

TEST_CASE("A material sampling an image 2D target samples it after the image is drawn", "[renderer][overlay][image2d]")
{
    RendererTestApp test;
    test.AddCamera();
    const auto image = TargetImage(test, 16, 16, Fenrir::ColourSpace::Srgb);
    test.AddMeshRenderer(test.Mesh(), test.SampledMaterial(image));
    Fenrir::DrawList2D list = List(test);
    AddQuad(list, 0, {0.0f, 0.0f}, {16.0f, 16.0f});
    test.app.GetResource<Fenrir::ImageTargets2D>().Submit(image, std::move(list));

    test.Frame();
    const std::vector<std::string> passes = RendererTest::PassesIn(RendererProbe::Submitted(test.renderer));

    INFO("the image's pass runs before the Opaque pass whose material samples it");
    REQUIRE(PositionOf(passes, "Image 2D") < passes.size());
    REQUIRE(PositionOf(passes, "Image 2D") < PositionOf(passes, "Opaque"));
    const std::vector<RendererTest::RecordedDraw> opaque = test.Draws("Opaque");
    REQUIRE(opaque.size() == 1);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("A list with two nested groups draws two transient images, three passes and two composites, innermost first",
          "[renderer][overlay][groups]")
{
    RendererTestApp test;
    test.AddCamera();
    test.app.GetResource<Fenrir::Overlay>().Submit(MainWindow, 0, NestedGroups(test));

    test.Frame();
    const std::vector<RendererTest::PassRecord> all = test.Passes();
    std::vector<std::string> twoD;
    for (const RendererTest::PassRecord& pass : all)
    {
        if (pass.pass.label.ends_with(" 2D"))
        {
            twoD.push_back(pass.pass.label);
        }
    }
    const std::vector<RendererTest::PassRecord> groups = RendererTest::Labelled(all, "Group 2D");

    INFO("the inner group's pass, then the outer group's, then the overlay's");
    REQUIRE(twoD == std::vector<std::string>{"Group 2D", "Group 2D", "Overlay 2D"});
    INFO("each group draws into a transient image of its bounds, cleared to transparent");
    const Fenrir::Graphics::TextureHandle inner = groups[0].pass.colour.at(0).texture;
    const Fenrir::Graphics::TextureHandle outer = groups[1].pass.colour.at(0).texture;
    REQUIRE(inner != outer);
    const Fenrir::Graphics::TextureDesc innerDesc = RendererProbe::Desc(test.Device(), inner);
    const Fenrir::Graphics::TextureDesc outerDesc = RendererProbe::Desc(test.Device(), outer);
    REQUIRE(innerDesc.width == 10);
    REQUIRE(innerDesc.height == 5);
    REQUIRE(outerDesc.width == 40);
    REQUIRE(outerDesc.height == 20);
    REQUIRE(innerDesc.format == Fenrir::Graphics::TextureFormat::Rgba8Srgb);
    REQUIRE(groups[0].pass.colour[0].load == Fenrir::Graphics::LoadOp::Clear);
    REQUIRE(groups[0].pass.colour[0].clear == std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f});

    const std::vector<RendererTest::RecordedDraw> groupDraws = test.Draws("Group 2D");
    const std::vector<RendererTest::RecordedDraw> overlayDraws = test.Draws("Overlay 2D");
    INFO("the inner group draws its quad; the outer its quad, the inner group's image and its last quad");
    REQUIRE(groupDraws.size() == 4);
    REQUIRE(SampledBy(test, groupDraws[2]) == inner);
    REQUIRE(test.GroupLabel(groupDraws[2], 1) == "2D composite");
    INFO("the overlay draws the first quad, the outer group's image, then the last quad");
    REQUIRE(overlayDraws.size() == 3);
    REQUIRE(test.GroupLabel(overlayDraws[0], 1) == "2D command");
    REQUIRE(SampledBy(test, overlayDraws[1]) == outer);
    REQUIRE(test.GroupLabel(overlayDraws[1], 1) == "2D composite");
    REQUIRE(test.GroupLabel(overlayDraws[2], 1) == "2D command");
    INFO("a composite draws the premultiplied image over what is under it");
    REQUIRE(RendererProbe::State(test.Device(), overlayDraws[1].pipeline).blend ==
            Fenrir::Graphics::BlendMode::Premultiplied);
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("RendererStats counts the frame's 2D lists, commands, groups and transient image bytes",
          "[renderer][overlay][groups]")
{
    RendererTestApp test;
    test.AddCamera();
    test.app.GetResource<Fenrir::Overlay>().Submit(MainWindow, 0, NestedGroups(test));
    Fenrir::DrawList2D image = List(test);
    AddQuad(image, 0, {0.0f, 0.0f}, {8.0f, 8.0f});
    test.app.GetResource<Fenrir::ImageTargets2D>().Submit(TargetImage(test, 8, 8, Fenrir::ColourSpace::Srgb),
                                                          std::move(image));

    test.Frame();
    const Fenrir::RendererStats stats = test.renderer.Stats();

    INFO("two lists, of nine commands and one");
    REQUIRE(stats.lists2D == 2);
    REQUIRE(stats.commands2D == 10);
    INFO("two groups, of 40 x 20 and 10 x 5 pixels at four bytes each");
    REQUIRE(stats.groups2D == 2);
    REQUIRE(stats.transientImageBytes2D == (40 * 20 + 10 * 5) * 4);
}

TEST_CASE("A list whose groups do not balance is skipped and logged once", "[renderer][overlay][groups]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::Overlay& overlay = test.app.GetResource<Fenrir::Overlay>();

    for (int frame = 0; frame < 2; ++frame)
    {
        Fenrir::DrawList2D open = List(test);
        open.BeginGroup({0.0f, 0.0f, 8.0f, 8.0f});
        AddQuad(open, 0, {0.0f, 0.0f}, {8.0f, 8.0f});
        Fenrir::DrawList2D closed = List(test);
        AddQuad(closed, 0, {0.0f, 0.0f}, {8.0f, 8.0f});
        closed.EndGroup();
        Fenrir::DrawList2D mask = List(test);
        mask.commands.emplace_back(Fenrir::EndMask2D{});
        Fenrir::DrawList2D good = List(test);
        AddQuad(good, 0, {0.0f, 0.0f}, {8.0f, 8.0f});
        overlay.Submit(MainWindow, 0, std::move(open));
        overlay.Submit(MainWindow, 0, std::move(closed));
        overlay.Submit(MainWindow, 0, std::move(mask));
        overlay.Submit(MainWindow, 0, std::move(good));
        test.Frame();

        INFO("frame " << frame << ": only the balanced list draws, and no group");
        REQUIRE(test.Draws("Overlay 2D").size() == 1);
        REQUIRE(RendererTest::Labelled(test.Passes(), "Group 2D").empty());
    }

    INFO("an unbalanced list was logged once over both frames");
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "do not balance") == 1);
}

TEST_CASE("A group nine deep is logged once and its commands draw flat into the eighth", "[renderer][overlay][groups]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::DrawList2D list = List(test);
    for (int depth = 0; depth < 9; ++depth)
    {
        list.BeginGroup({0.0f, 0.0f, 16.0f, 16.0f});
    }
    AddQuad(list, 0, {0.0f, 0.0f}, {16.0f, 16.0f});
    for (int depth = 0; depth < 9; ++depth)
    {
        list.EndGroup();
    }
    test.app.GetResource<Fenrir::Overlay>().Submit(MainWindow, 0, std::move(list));

    test.Frame();
    const std::vector<RendererTest::RecordedDraw> groupDraws = test.Draws("Group 2D");

    INFO("eight groups are realised, the ninth drawn flat");
    REQUIRE(RendererTest::Labelled(test.Passes(), "Group 2D").size() == 8);
    INFO("the innermost realised group draws the quad itself; each other draws the group inside it");
    REQUIRE(groupDraws.size() == 8);
    REQUIRE(test.GroupLabel(groupDraws[0], 1) == "2D command");
    REQUIRE(test.Draws("Overlay 2D").size() == 1);
    REQUIRE(RendererTest::CountContaining(test.messages->errors, "deeper than 8") == 1);
}

TEST_CASE("An empty group draws nothing and makes no image", "[renderer][overlay][groups]")
{
    RendererTestApp test;
    test.AddCamera();
    Fenrir::DrawList2D list = List(test);
    list.BeginGroup({0.0f, 0.0f, 16.0f, 16.0f});
    list.BeginGroup({0.0f, 0.0f, 8.0f, 8.0f});
    list.EndGroup();
    list.EndGroup();
    list.BeginGroup({2000.0f, 2000.0f, 16.0f, 16.0f});
    AddQuad(list, 0, {2000.0f, 2000.0f}, {2016.0f, 2016.0f});
    list.EndGroup();
    AddQuad(list, 0, {0.0f, 0.0f}, {8.0f, 8.0f});
    test.app.GetResource<Fenrir::Overlay>().Submit(MainWindow, 0, std::move(list));

    test.Frame();

    INFO("a group of nothing, a group holding only an empty group and a group off the target make no pass");
    REQUIRE(RendererTest::Labelled(test.Passes(), "Group 2D").empty());
    INFO("the quad outside them draws as it would without them");
    REQUIRE(test.Draws("Overlay 2D").size() == 1);
    REQUIRE(test.renderer.Stats().groups2D == 0);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("With offscreen groups off, groups draw flat, filters are ignored, a mask's shape is not drawn and no "
          "transient image is made",
          "[renderer][overlay][groups]")
{
    Fenrir::RendererConfig config = RendererTest::NullBackend();
    config.offscreenGroups2D = false;
    RendererTestApp test(config);
    test.AddCamera();
    Fenrir::DrawList2D list = NestedGroups(test);
    list.BeginGroup({0.0f, 0.0f, 16.0f, 16.0f});
    AddQuad(list, 0, {0.0f, 0.0f}, {16.0f, 16.0f});
    Fenrir::EndGroup2D blur;
    blur.filter = Fenrir::GroupFilter2D::Blur;
    blur.radius = 4.0f;
    list.EndGroup(blur);
    list.BeginGroup({0.0f, 0.0f, 16.0f, 16.0f});
    AddQuad(list, 0, {0.0f, 0.0f}, {16.0f, 16.0f});
    Fenrir::EndGroup2D mask;
    mask.mask = true;
    list.EndGroup(mask);
    AddQuad(list, 0, {0.0f, 0.0f}, {16.0f, 16.0f});
    list.commands.emplace_back(Fenrir::EndMask2D{});
    test.app.GetResource<Fenrir::Overlay>().Submit(MainWindow, 0, std::move(list));

    test.Frame();

    REQUIRE(RendererTest::Labelled(test.Passes(), "Group 2D").empty());
    INFO("the nested groups' five quads, the blurred group's quad and the masked quad draw on the overlay; the mask's "
         "shape does not");
    const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Overlay 2D");
    REQUIRE(draws.size() == 7);
    for (const RendererTest::RecordedDraw& draw : draws)
    {
        REQUIRE(test.GroupLabel(draw, 1) == "2D command");
    }
    REQUIRE(test.renderer.Stats().groups2D == 0);
    REQUIRE(test.renderer.Stats().transientImageBytes2D == 0);
    REQUIRE(test.messages->errors.empty());
}
