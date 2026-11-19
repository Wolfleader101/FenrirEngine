#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "TextTest.hpp"

using TextTest::FullFont;
using TextTest::Glyphs;
using TextTest::Style;
using TextTest::SubsetFont;
using TextTest::TextApp;

namespace
{
    // The font each of the paragraph's glyphs was drawn in, in visual order.
    std::vector<Fenrir::AssetHandle<Fenrir::Font>> FontsOf(const Fenrir::Paragraph& paragraph)
    {
        std::vector<Fenrir::AssetHandle<Fenrir::Font>> fonts;
        for (const Fenrir::GlyphRun& run : paragraph.Runs())
        {
            fonts.insert(fonts.end(), run.glyphs.size(), run.font);
        }
        return fonts;
    }
} // namespace

TEST_CASE("A family with no faces resolves to the embedded default face", "[text][fallback]")
{
    TextApp test;
    INFO("the embedded face is in the store");
    REQUIRE(test.store.IsValid(test.fonts.DefaultFont()));
    INFO("an unknown family's chain is the default face alone");
    REQUIRE(test.fonts.FaceChain("No Such Family", Fenrir::FontWeightNormal, Fenrir::FontStyle::Normal) ==
            std::vector<Fenrir::AssetHandle<Fenrir::Font>>{test.fonts.DefaultFont()});

    Fenrir::Paragraph paragraph;
    paragraph.SetText("Hello", Style("No Such Family"));
    REQUIRE(paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth));
    INFO("all five letters are drawn, in the default face");
    REQUIRE(FontsOf(paragraph) == std::vector<Fenrir::AssetHandle<Fenrir::Font>>(5, test.fonts.DefaultFont()));
    REQUIRE(test.errors->empty());
}

TEST_CASE("A glyph a family lacks falls through its fallbacks to the embedded face", "[text][fallback]")
{
    TextApp test;
    // A subset of Roboto with only a, b and c.
    const Fenrir::AssetHandle<Fenrir::Font> abc = test.Register("ABC", SubsetFont("Roboto-Regular.abc.ttf"));
    const Fenrir::AssetHandle<Fenrir::Font> roboto = test.Register("Roboto", FullFont("Roboto-Regular.ttf"));

    Fenrir::Paragraph paragraph;
    paragraph.SetText("abz", Style("ABC"));
    REQUIRE(paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth));
    INFO("with no fallbacks, a and b come from the family and z from the embedded face");
    REQUIRE(FontsOf(paragraph) == std::vector<Fenrir::AssetHandle<Fenrir::Font>>{abc, abc, test.fonts.DefaultFont()});

    test.fonts.SetFallbacks("ABC", {"Missing", "Roboto"});
    INFO("changing the fallbacks lays the paragraph out again");
    REQUIRE(paragraph.Layout(test.fonts, Fenrir::UnlimitedWidth));
    INFO("z now comes from Roboto, the first fallback family that has it");
    REQUIRE(FontsOf(paragraph) == std::vector<Fenrir::AssetHandle<Fenrir::Font>>{abc, abc, roboto});
    INFO("the chain skips the family with no faces");
    REQUIRE(test.fonts.FaceChain("ABC", Fenrir::FontWeightNormal, Fenrir::FontStyle::Normal) ==
            std::vector<Fenrir::AssetHandle<Fenrir::Font>>{abc, roboto, test.fonts.DefaultFont()});
}

TEST_CASE("A family's nearest face by style and weight is tried first", "[text][fallback]")
{
    TextApp test;
    const Fenrir::AssetHandle<Fenrir::Font> regular = test.Register("Noto", FullFont("Roboto-Regular.ttf"));
    const Fenrir::AssetHandle<Fenrir::Font> bold =
        test.store.Load<Fenrir::Font>(SubsetFont("NotoSans-Bold.ttf").string(), Fenrir::AssetOwner::App()).handle;
    test.fonts.AddFace("Noto", bold, Fenrir::FontWeightBold);

    INFO("600 is nearer 700 than 400");
    REQUIRE(test.fonts.FaceChain("Noto", 600, Fenrir::FontStyle::Normal).front() == bold);
    INFO("300 takes the regular face");
    REQUIRE(test.fonts.FaceChain("Noto", 300, Fenrir::FontStyle::Normal).front() == regular);
    INFO("700 asked of faces at 650 and 900 takes 900: from 400 up a heavier face beats a nearer lighter one");
    Fenrir::FontLibrary other(test.store);
    other.AddFace("Spread", regular, 650);
    other.AddFace("Spread", bold, 900);
    REQUIRE(other.FaceChain("Spread", Fenrir::FontWeightBold, Fenrir::FontStyle::Normal).front() == bold);
    INFO("an italic asked of a family with none takes its upright faces");
    REQUIRE(test.fonts.FaceChain("Noto", Fenrir::FontWeightBold, Fenrir::FontStyle::Italic).front() == bold);
}
