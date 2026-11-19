// FenrirText's SDF mode (ADR-0062): a face registered with FontRendering::Sdf is rasterised once, in FreeType's
// single-channel SDF render mode at the library's SDF size, into an atlas of its own that every size shares, and its
// paragraphs are laid out with positions scaled from that size. Laid out in HarfBuzz's Roboto.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "TextTest.hpp"

using TextTest::TextApp;

namespace
{
    // Roboto registered as `family` in `rendering`.
    Fenrir::AssetHandle<Fenrir::Font> RegisterRoboto(TextApp& test, std::string_view family,
                                                     Fenrir::FontRendering rendering)
    {
        const Fenrir::AssetHandle<Fenrir::Font> font =
            test.store.Load<Fenrir::Font>(TextTest::FullFont("Roboto-Regular.ttf").string(), Fenrir::AssetOwner::App())
                .handle;
        REQUIRE_FALSE(font.IsEmpty());
        test.fonts.AddFace(family, font, Fenrir::FontWeightNormal, Fenrir::FontStyle::Normal, rendering);
        return font;
    }

    Fenrir::Paragraph LaidOut(TextApp& test, std::string_view text, std::string family, float size)
    {
        Fenrir::Paragraph paragraph;
        paragraph.SetText(text, TextTest::Style(std::move(family), size));
        paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth);
        return paragraph;
    }

    bool Proportional(float small, float large, float ratio)
    {
        return std::abs(large - small * ratio) <= 1e-3f * std::max(1.0f, std::abs(large));
    }
} // namespace

TEST_CASE("An SDF face's atlas holds one entry per glyph whatever the size", "[text][sdf]")
{
    TextApp test;
    const Fenrir::AssetHandle<Fenrir::Font> roboto = RegisterRoboto(test, "Field", Fenrir::FontRendering::Sdf);

    // The first glyph opens the atlas's page; each glyph after it is uploaded as its own region.
    const Fenrir::Paragraph first = LaidOut(test, "H", "Field", 12.0f);
    INFO("the runs are in SDF mode, on the font's one SDF atlas");
    REQUIRE(first.Runs().size() == 1);
    REQUIRE(first.Runs().front().rendering == Fenrir::FontRendering::Sdf);
    REQUIRE(first.Runs().front().sdfSpread > 0.0f);
    const std::span<const Fenrir::AssetHandle<Fenrir::Image>> pages =
        test.fonts.AtlasPages(roboto, 0.0f, Fenrir::FontRendering::Sdf);
    REQUIRE(pages.size() == 1);
    REQUIRE(first.Runs().front().page == pages.front());
    std::uint32_t revision = test.store.Revision(pages.front());

    const Fenrir::Paragraph small = LaidOut(test, "Hello", "Field", 12.0f);
    INFO("e, l and o are added once each, the second l reusing the first's entry and H the one it had");
    REQUIRE(test.store.ImageUpdatesSince(pages.front(), revision).value_or(std::vector<Fenrir::ImageRegion>{}).size() ==
            3);
    REQUIRE(small.Runs().front().page == pages.front());
    revision = test.store.Revision(pages.front());

    const Fenrir::Paragraph large = LaidOut(test, "Hello", "Field", 64.0f);
    INFO("the same text at 64 pixels adds nothing to the atlas and draws from the same regions");
    REQUIRE(test.store.ImageUpdatesSince(pages.front(), revision) == std::vector<Fenrir::ImageRegion>{});
    REQUIRE(large.Runs().front().page == pages.front());
    const auto smallGlyphs = TextTest::Glyphs(small);
    const auto largeGlyphs = TextTest::Glyphs(large);
    REQUIRE(smallGlyphs.size() == largeGlyphs.size());
    for (std::size_t i = 0; i < smallGlyphs.size(); ++i)
    {
        REQUIRE(smallGlyphs[i].atlas == largeGlyphs[i].atlas);
    }
    INFO("no bitmap atlas was opened for the face at either size");
    REQUIRE(test.fonts.AtlasPages(roboto, 12.0f).empty());
    REQUIRE(test.fonts.AtlasPages(roboto, 64.0f).empty());
    INFO("and nothing was logged as an error");
    REQUIRE(test.errors->empty());
}

TEST_CASE("A paragraph in SDF mode at two sizes has proportional extents", "[text][sdf]")
{
    TextApp test;
    RegisterRoboto(test, "Field", Fenrir::FontRendering::Sdf);

    const Fenrir::Paragraph small = LaidOut(test, "Wavy jumps, AV!", "Field", 13.0f);
    const Fenrir::Paragraph large = LaidOut(test, "Wavy jumps, AV!", "Field", 39.0f);
    INFO("three times the size: three times the extent, line metrics, glyph origins and glyph quads");
    REQUIRE(Proportional(small.Size().x, large.Size().x, 3.0f));
    REQUIRE(Proportional(small.Size().y, large.Size().y, 3.0f));
    REQUIRE(Proportional(small.Lines().front().baseline, large.Lines().front().baseline, 3.0f));
    REQUIRE(Proportional(small.Runs().front().ascent, large.Runs().front().ascent, 3.0f));
    const auto smallGlyphs = TextTest::Glyphs(small);
    const auto largeGlyphs = TextTest::Glyphs(large);
    REQUIRE(smallGlyphs.size() == largeGlyphs.size());
    for (std::size_t i = 0; i < smallGlyphs.size(); ++i)
    {
        INFO("glyph " << i);
        REQUIRE(Proportional(smallGlyphs[i].origin.x, largeGlyphs[i].origin.x, 3.0f));
        REQUIRE(Proportional(smallGlyphs[i].quad.x, largeGlyphs[i].quad.x, 3.0f));
        REQUIRE(Proportional(smallGlyphs[i].quad.y, largeGlyphs[i].quad.y, 3.0f));
        REQUIRE(Proportional(smallGlyphs[i].quad.width, largeGlyphs[i].quad.width, 3.0f));
        REQUIRE(Proportional(smallGlyphs[i].quad.height, largeGlyphs[i].quad.height, 3.0f));
    }
}

TEST_CASE("An SDF glyph's quad is its atlas region scaled from the SDF size", "[text][sdf]")
{
    Fenrir::FontLibraryConfig config;
    config.sdfSize = 32.0f;
    TextApp test(config);
    RegisterRoboto(test, "Field", Fenrir::FontRendering::Sdf);

    const Fenrir::Paragraph paragraph = LaidOut(test, "O", "Field", 64.0f);
    const Fenrir::PositionedGlyph glyph = TextTest::Glyphs(paragraph).front();
    INFO("at twice the SDF size the quad is twice the region, spread included");
    REQUIRE(std::abs(glyph.quad.width - 2.0f * static_cast<float>(glyph.atlas.region.width)) < 1e-3f);
    REQUIRE(std::abs(glyph.quad.height - 2.0f * static_cast<float>(glyph.atlas.region.height)) < 1e-3f);
    INFO("the spread is in the atlas's texels, the config's");
    REQUIRE(paragraph.Runs().front().sdfSpread == static_cast<float>(config.sdfSpread));
}

TEST_CASE("A paragraph mixes SDF and bitmap faces, and one font may be registered in both modes", "[text][sdf]")
{
    TextApp test;
    const Fenrir::AssetHandle<Fenrir::Font> roboto = RegisterRoboto(test, "Field", Fenrir::FontRendering::Sdf);
    test.fonts.AddFace("Crisp", roboto);

    Fenrir::Paragraph paragraph;
    paragraph.SetText("Far ", TextTest::Style("Field", 20.0f));
    paragraph.AddRun("near", TextTest::Style("Crisp", 20.0f));
    paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth);

    INFO("the SDF family's run is in SDF mode and the other family's, of the same font, in bitmap mode");
    REQUIRE(paragraph.Runs().size() == 2);
    REQUIRE(paragraph.Runs()[0].font == roboto);
    REQUIRE(paragraph.Runs()[0].rendering == Fenrir::FontRendering::Sdf);
    REQUIRE(paragraph.Runs()[1].font == roboto);
    REQUIRE(paragraph.Runs()[1].rendering == Fenrir::FontRendering::Bitmap);
    REQUIRE(paragraph.Runs()[0].page != paragraph.Runs()[1].page);
    REQUIRE(test.fonts.AtlasPages(roboto, 20.0f).size() == 1);
    REQUIRE(paragraph.Runs()[1].page == test.fonts.AtlasPages(roboto, 20.0f).front());

    INFO("the default family stays in bitmap mode");
    const Fenrir::Paragraph plain = LaidOut(test, "plain", "default", 20.0f);
    REQUIRE(plain.Runs().front().rendering == Fenrir::FontRendering::Bitmap);
    REQUIRE(plain.Runs().front().sdfSpread == 0.0f);
}
