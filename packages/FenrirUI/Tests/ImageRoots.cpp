// Image roots on the renderer's null backend (the spec's seams 1 and 4): a root whose target is a UIImageTarget draws
// into an Image asset the UI owns, through the renderer's ImageTargets2D, read back through RendererProbe as the
// renderer's own image target tests are.

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirInput/Input.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIImageTarget.hpp"
#include "RendererTestApp.hpp"

using Fenrir::RendererProbe;
using RendererTest::RendererTestApp;

namespace
{
    /** @brief A root drawing into `target`, holding one panel over its whole size that takes pointer input. */
    Fenrir::UIRoot& PanelRoot(RendererTestApp& test, std::shared_ptr<Fenrir::UIImageTarget> target)
    {
        Fenrir::UIRootConfig config;
        config.target = std::move(target);
        Fenrir::UIRoot& root = test.app.GetResource<Fenrir::UIRoots>().CreateRoot(config);
        const Fenrir::ElementId panel = root.Create(root.GetRootElement(), "panel");
        Fenrir::StyleDeclarations* style = root.EditInlineStyle(panel);
        REQUIRE(style != nullptr);
        style->SetWidth(Fenrir::Length::Percent(100.0f))
            .SetHeight(Fenrir::Length::Percent(100.0f))
            .SetBackgroundColor(Fenrir::Colour::FromSrgb(0x336699FF))
            .SetPointerEvents(Fenrir::PointerEvents::All);
        return root;
    }

    /** @brief Checks the last frame drew one pass into `image`'s texture at `width` by `height`, with the painter. */
    void RequireDrawnInto(RendererTestApp& test, Fenrir::AssetHandle<Fenrir::Image> image, std::uint32_t width,
                          std::uint32_t height)
    {
        const std::vector<RendererTest::PassRecord> passes = RendererTest::Labelled(test.Passes(), "Image 2D");
        INFO("one image pass, into the image's texture, at its size");
        REQUIRE(passes.size() == 1);
        REQUIRE(passes[0].pass.colour.size() == 1);
        const Fenrir::Graphics::TextureHandle texture = RendererProbe::ImageTexture(test.renderer, image);
        REQUIRE(passes[0].pass.colour[0].texture == texture);
        const Fenrir::Graphics::TextureDesc desc = RendererProbe::Desc(test.Device(), texture);
        REQUIRE(desc.width == width);
        REQUIRE(desc.height == height);
        REQUIRE(passes[0].viewport == Fenrir::Graphics::PixelRect{0, 0, width, height});

        const std::vector<RendererTest::RecordedDraw> draws = test.Draws("Image 2D");
        INFO("the root's panel is one draw of builtin:painter's base variant: the linear blend space, not GAMMA");
        REQUIRE(draws.size() == 1);
        REQUIRE(RendererProbe::Label(test.Device(), draws[0].pipeline) == "BuiltinPainter");
        REQUIRE(RendererProbe::Keys(test.Device(), draws[0].pipeline).empty());
        INFO("an sRGB image encodes the linear values on write");
        REQUIRE(RendererProbe::State(test.Device(), draws[0].pipeline).encodeSrgb);
    }
} // namespace

TEST_CASE("An image root's image exists before its first frame: a render target of the root's size", "[ui][image]")
{
    RendererTestApp test;
    const Fenrir::UI ui = Fenrir::UI::Install(test.app);
    auto target = std::make_shared<Fenrir::UIImageTarget>(test.app, 640, 360);
    (void)PanelRoot(test, target);

    const Fenrir::ImageData* data = test.Store().TryGet(target->GetImage());
    INFO("the image is missing, or not a 640 by 360 sRGB image with no pixels");
    REQUIRE(data != nullptr);
    REQUIRE(data->width == 640);
    REQUIRE(data->height == 360);
    REQUIRE(data->colourSpace == Fenrir::ColourSpace::Srgb);
    REQUIRE(data->pixels.empty());
}

TEST_CASE("An image root blends linear unless its config names a blend space", "[ui][image]")
{
    RendererTestApp test;
    const Fenrir::UI ui = Fenrir::UI::Install(test.app);
    Fenrir::UIRoots& roots = test.app.GetResource<Fenrir::UIRoots>();
    Fenrir::UIRootConfig unset;
    unset.target = std::make_shared<Fenrir::UIImageTarget>(test.app, 64, 64);
    Fenrir::UIRootConfig gamma = unset;
    gamma.blendSpace = Fenrir::BlendSpace2D::Gamma;

    INFO("a root in an image with no blend space did not take Linear (ADR-0065)");
    REQUIRE(roots.CreateRoot(unset).GetConfig().blendSpace == Fenrir::BlendSpace2D::Linear);
    INFO("a root in an image told Gamma did not keep it");
    REQUIRE(roots.CreateRoot(gamma).GetConfig().blendSpace == Fenrir::BlendSpace2D::Gamma);
}

TEST_CASE("An image root submits a list to an image 2D target of its size each frame, in the linear blend space",
          "[ui][image]")
{
    RendererTestApp test;
    const Fenrir::Input input = Fenrir::Input::Install(test.app);
    const Fenrir::UI ui = Fenrir::UI::Install(test.app);
    REQUIRE(ui.IsValid());
    auto target = std::make_shared<Fenrir::UIImageTarget>(test.app, 640, 360);
    Fenrir::UIRoot& root = PanelRoot(test, target);
    const Fenrir::AssetHandle<Fenrir::Image> image = target->GetImage();

    for (int frame = 0; frame < 3; ++frame)
    {
        test.Frame();
        INFO("frame " << frame << ": the root is the image's size at a content scale of one");
        REQUIRE(root.GetTargetSize() == Fenrir::UITargetSize{{640.0f, 360.0f}, 1.0f});
        RequireDrawnInto(test, image, 640, 360);
    }
    REQUIRE(RendererProbe::StaleUses(test.Device()).empty());
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("An image root's size is the game's: SetSize remakes the image at the new size under the same handle",
          "[ui][image]")
{
    RendererTestApp test;
    const Fenrir::UI ui = Fenrir::UI::Install(test.app);
    auto target = std::make_shared<Fenrir::UIImageTarget>(test.app, 640, 360);
    Fenrir::UIRoot& root = PanelRoot(test, target);
    test.Frame();
    const Fenrir::AssetHandle<Fenrir::Image> image = target->GetImage();

    target->SetSize(320, 200);
    test.Frame();

    INFO("the same image, now 320 by 200, drawn at its new size");
    REQUIRE(target->GetImage() == image);
    const Fenrir::ImageData* data = test.Store().TryGet(image);
    REQUIRE(data != nullptr);
    REQUIRE(data->width == 320);
    REQUIRE(data->height == 200);
    REQUIRE(root.GetTargetSize() == Fenrir::UITargetSize{{320.0f, 200.0f}, 1.0f});
    RequireDrawnInto(test, image, 320, 200);
    REQUIRE(test.messages->errors.empty());
}

TEST_CASE("An image root takes no window input: it has no input router, and its elements see only code", "[ui][image]")
{
    RendererTestApp test;
    const Fenrir::Input input = Fenrir::Input::Install(test.app);
    const Fenrir::UI ui = Fenrir::UI::Install(test.app);
    Fenrir::EventReader<Fenrir::UIEvent> events;
    Fenrir::UIRoot& root = PanelRoot(test, std::make_shared<Fenrir::UIImageTarget>(test.app, 640, 360));
    const Fenrir::ElementId panel = root.GetChildren(root.GetRootElement()).front();
    test.Frame();
    (void)events.Read(test.app);

    Fenrir::InputFeed& feed = test.app.GetResource<Fenrir::InputFeed>();
    feed.MouseMove(Fenrir::WindowId{1}, {100.0f, 100.0f}, {0.0f, 0.0f});
    feed.MouseButtonDown(Fenrir::WindowId{1}, Fenrir::MouseButton::Left);
    feed.MouseButtonUp(Fenrir::WindowId{1}, Fenrir::MouseButton::Left);
    test.Frame();

    INFO("a click on the window over the panel sends the image root nothing and is not taken from the game");
    REQUIRE(events.Read(test.app).empty());
    REQUIRE_FALSE(root.HasPseudoClass(panel, Fenrir::PseudoClass::Hover));
    REQUIRE(test.app.GetResource<Fenrir::Mouse>().JustPressed(Fenrir::MouseButton::Left));

    INFO("code still gives its elements events");
    Fenrir::UIEvent click;
    click.kind = Fenrir::UIEventKind::Click;
    click.target = panel;
    root.SendEvent(click);
    REQUIRE(events.Read(test.app).size() == 1);
}
