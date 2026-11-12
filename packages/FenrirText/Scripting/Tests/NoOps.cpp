// The no-op rules (spec: "Scripting"), with no .NET: through NativeCallHarness, which runs the text extension as
// shipped (Text::ScriptingExtension()), each native function is called from C++ by its registered name through the
// registration's own thunk. With no FontLibrary in the app, or a paragraph destroyed or never made, a call does nothing
// and returns zero, and a debug build warns once per native and reason, however often it is called, in the words
// TextScripting names.

#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <cstdint>
#include <string_view>

#include "FenrirAssets/Assets.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/TextStyle.hpp"
#include "FenrirTextScripting/ScriptText.hpp"
#include "FenrirTextScripting/TextScripting.hpp"
#include "FenrirTextScripting/TextScriptingState.hpp"

namespace
{
    using namespace Fenrir;
    using namespace std::string_view_literals;

    // A paragraph made through the native, as C#'s Paragraph.Create makes one.
    ScriptText::Paragraph Create(Testing::NativeCallHarness& harness)
    {
        ScriptText::Paragraph paragraph;
        harness.Call<void>("TextNative.CreateParagraph", &paragraph);
        return paragraph;
    }
} // namespace

TEST_CASE("With no FontLibrary in the app, fonts register nothing and paragraphs do not lay out, saying so once",
          "[text][scripting][noops]")
{
    Testing::NativeCallHarness harness({Text::ScriptingExtension()});
    const ScriptText::Paragraph paragraph = Create(harness);
    {
        INFO("A paragraph needs no fonts to be made");
        REQUIRE(paragraph.id != 0);
    }
    const ScriptText::TextStyle style;
    const Math::Vec2 nowhere{0.0f, 0.0f};

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": with no FontLibrary nothing registers or lays out, and a paragraph reads as empty");
        REQUIRE_FALSE(harness.Call<bool>("TextNative.AddFace", "Roboto"sv, "Roboto.ttf"sv, std::uint16_t{400},
                                         FontStyle::Normal));
        REQUIRE(harness.Call<bool>("TextNative.SetText", &paragraph, "hello"sv, "default"sv, &style));
        REQUIRE_FALSE(harness.Call<bool>("TextNative.Layout", &paragraph, 100.0f));
        Math::Vec2 size{1.0f, 1.0f};
        REQUIRE(harness.Call<bool>("TextNative.Size", &paragraph, &size));
        REQUIRE(size == Math::Vec2(0.0f, 0.0f));
        REQUIRE(harness.Call<std::int32_t>("TextNative.Lines", &paragraph, static_cast<LineMetrics*>(nullptr), 0) == 0);
        REQUIRE(harness.Call<std::uint32_t>("TextNative.CharacterAt", &paragraph, &nowhere) == 0);
    }
    INFO("AddFace and Layout should each warn once in a debug build, and never in a release build");
    REQUIRE(harness.WarnedOnce("TextNative.AddFace", TextScripting::NoFontLibrary));
    REQUIRE(harness.WarnedOnce("TextNative.Layout", TextScripting::NoFontLibrary));
}

TEST_CASE("With a FontLibrary but no file at the path, a face registers nothing and says so once",
          "[text][scripting][noops]")
{
    Testing::NativeCallHarness harness({Text::ScriptingExtension()});
    Assets assets = Assets::Install(harness.GetApp(), {});
    harness.GetApp().AddResource<FontLibrary>(assets.Store());

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": a path with no font file should register no face");
        REQUIRE_FALSE(harness.Call<bool>("TextNative.AddFace", "Roboto"sv, "no-such-font.ttf"sv, std::uint16_t{400},
                                         FontStyle::Normal));
    }
    INFO("AddFace should warn once in a debug build, and never in a release build");
    REQUIRE(harness.WarnedOnce("TextNative.AddFace", TextScripting::FontNotLoaded));
    harness.GetApp().RemoveResource<FontLibrary>();
}

TEST_CASE("A paragraph destroyed or never made does nothing, returns zero and says so once per native",
          "[text][scripting][noops]")
{
    Testing::NativeCallHarness harness({Text::ScriptingExtension()});
    const ScriptText::Paragraph destroyed = Create(harness);
    {
        INFO("A made paragraph should destroy, and its id should not be handed out again");
        REQUIRE(harness.Call<bool>("TextNative.DestroyParagraph", &destroyed));
        const ScriptText::Paragraph again = Create(harness);
        REQUIRE(again.id != destroyed.id);
    }
    const ScriptText::TextStyle style;
    const ParagraphStyle layout;
    const Math::Vec2 nowhere{0.0f, 0.0f};

    for (const ScriptText::Paragraph& paragraph : {destroyed, ScriptText::Paragraph{}})
    {
        for (int call = 0; call < 2; ++call)
        {
            INFO("Paragraph " << paragraph.id << ", call " << call);
            REQUIRE_FALSE(harness.Call<bool>("TextNative.DestroyParagraph", &paragraph));
            REQUIRE_FALSE(harness.Call<bool>("TextNative.SetText", &paragraph, "hello"sv, "default"sv, &style));
            REQUIRE_FALSE(harness.Call<bool>("TextNative.AddRun", &paragraph, "hello"sv, "default"sv, &style));
            REQUIRE_FALSE(harness.Call<bool>("TextNative.ClearParagraph", &paragraph));
            REQUIRE_FALSE(harness.Call<bool>("TextNative.SetStyle", &paragraph, &layout));
            REQUIRE_FALSE(harness.Call<bool>("TextNative.Layout", &paragraph, 100.0f));
            Math::Vec2 size{0.0f, 0.0f};
            REQUIRE_FALSE(harness.Call<bool>("TextNative.Size", &paragraph, &size));
            REQUIRE(harness.Call<std::int32_t>("TextNative.Lines", &paragraph, static_cast<LineMetrics*>(nullptr), 0) ==
                    0);
            REQUIRE(harness.Call<std::uint32_t>("TextNative.CharacterAt", &paragraph, &nowhere) == 0);
            REQUIRE(harness.Call<std::int32_t>("TextNative.RangeRects", &paragraph, std::uint32_t{0}, std::uint32_t{4},
                                               static_cast<Rect2D*>(nullptr), 0) == 0);
        }
    }
    for (const std::string_view native :
         {"TextNative.DestroyParagraph"sv, "TextNative.SetText"sv, "TextNative.AddRun"sv, "TextNative.ClearParagraph"sv,
          "TextNative.SetStyle"sv, "TextNative.Layout"sv, "TextNative.Size"sv, "TextNative.Lines"sv,
          "TextNative.CharacterAt"sv, "TextNative.RangeRects"sv})
    {
        INFO(native << " should warn once for both paragraphs in a debug build, and never in a release build");
        REQUIRE(harness.WarnedOnce(native, TextScripting::NoParagraph));
    }
}
