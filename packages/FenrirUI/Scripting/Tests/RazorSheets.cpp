// Scoped component stylesheets (spec: "Scoped component stylesheets", "Fenrir.UI.Razor.props and the generators";
// ADR-0070): the fixture's Alpha.razor.css and Beta.razor.css both style .title and each colours only its own; Outer's
// ::deep rule reaches its child Leaf's label in its list, and its plain label rule reaches neither Leaf; Inline's
// <style> block is scoped the same way; a game's own sheet styles Alpha's host by its readable class; Hot's sheet, a
// file the test writes, loads by path and is read again when rewritten, and from its recorded text when the file is not
// there. Alpha (and the others) have their namespace from @namespace, Beta from the root namespace.

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include "FenrirUI/Style.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "UIScriptingApp.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* SheetsProbeType = "UIScriptFixture.SheetsProbe";

    /** @brief Whether the colour is (r, g, b) and opaque. */
    bool Is(const Colour& colour, float r, float g, float b)
    {
        return Near(colour.r, r) && Near(colour.g, g) && Near(colour.b, b) && Near(colour.a, 1.0f);
    }

    /** @brief The fixture with a root that lays out, and its probe's calls. */
    struct SheetsScriptingApp : UIScriptingApp
    {
        SheetsScriptingApp() : root(MakeRoot(target))
        {
        }

        /** @brief The text colour the element resolved to, by its id string. */
        Colour ColourOf(std::string_view id)
        {
            const ElementId element = root.FindById(id);
            INFO("Nothing has the id " << id);
            REQUIRE(root.IsAlive(element));
            const ResolvedStyle* style = root.GetResolvedStyle(element);
            REQUIRE(style != nullptr);
            return style->color;
        }

        std::shared_ptr<CaptureTarget> target;
        UIRoot& root;
    };

    /** @brief The fixture with Alpha, Beta, Outer and Inline mounted and built. */
    struct MountedSheetsApp : SheetsScriptingApp
    {
        MountedSheetsApp()
        {
            Call<void>(SheetsProbeType, "Mount", root.GetId().value);
            Frame();
            Frame();
        }
    };

    /** @brief Hot's sheet, where the fixture's build recorded it. */
    const std::filesystem::path& HotSheet()
    {
        static const std::filesystem::path path{FENRIR_TEST_UI_HOT_SHEET};
        return path;
    }

    /** @brief Writes Hot's sheet, its modification time moved on from the last write's so a rewrite reads as changed.
     */
    void WriteHotSheet(std::string_view text)
    {
        std::filesystem::create_directories(HotSheet().parent_path());
        std::error_code missing;
        const std::filesystem::file_time_type before = std::filesystem::last_write_time(HotSheet(), missing);
        {
            std::ofstream out(HotSheet(), std::ios::binary | std::ios::trunc);
            out << text;
        }
        if (!missing)
        {
            std::filesystem::last_write_time(HotSheet(), before + std::chrono::seconds(2));
        }
    }

    /** @brief Removes Hot's sheet when the test ends, so another test finds it gone. */
    struct HotSheetRemoved
    {
        ~HotSheetRemoved()
        {
            std::error_code ignored;
            std::filesystem::remove(HotSheet(), ignored);
        }
    };
} // namespace

TEST_CASE("Two Razor components' sheets that both style .title each colour only their own",
          "[ui][scripting][razor][sheets]")
{
    MountedSheetsApp test;
    {
        INFO("Alpha.razor.css did not colour Alpha's title red, or Beta's or Inline's sheet reached it");
        REQUIRE(Is(test.ColourOf("alpha-title"), 1.0f, 0.0f, 0.0f));
    }
    INFO("Beta.razor.css did not colour Beta's title blue, or Alpha's or Inline's sheet reached it");
    REQUIRE(Is(test.ColourOf("beta-title"), 0.0f, 0.0f, 1.0f));
}

TEST_CASE("A Razor component's ::deep rule reaches a child's label, and its plain label rule does not",
          "[ui][scripting][razor][sheets]")
{
    MountedSheetsApp test;
    {
        INFO("Outer's .list ::deep label did not colour the Leaf in its list green");
        REQUIRE(Is(test.ColourOf("leaf-inside"), 0.0f, 1.0f, 0.0f));
    }
    {
        INFO("Outer's plain label rule did not colour its own label magenta");
        REQUIRE(Is(test.ColourOf("outer-own"), 1.0f, 0.0f, 1.0f));
    }
    INFO("Outer's plain label rule reached the Leaf beside its list, whose label is the child's, not Outer's");
    REQUIRE_FALSE(Is(test.ColourOf("leaf-outside"), 1.0f, 0.0f, 1.0f));
    REQUIRE_FALSE(Is(test.ColourOf("leaf-outside"), 0.0f, 1.0f, 0.0f));
}

TEST_CASE("A <style> block in a Razor component is scoped as its sheet file would be", "[ui][scripting][razor][sheets]")
{
    MountedSheetsApp test;
    {
        INFO("Inline's <style> block did not colour its own title cyan");
        REQUIRE(Is(test.ColourOf("inline-title"), 0.0f, 1.0f, 1.0f));
    }
    INFO("Inline's <style> block reached Alpha's title");
    REQUIRE(Is(test.ColourOf("alpha-title"), 1.0f, 0.0f, 0.0f));
}

TEST_CASE("A game's own sheet styles a Razor component's host by its readable class", "[ui][scripting][razor][sheets]")
{
    SheetsScriptingApp test;
    test.root.AddStylesheet(".alpha { color: #ffff00; }", "game");
    const ElementId host = Unpacked(test.Call<std::uint64_t>(SheetsProbeType, "MountAlpha", test.root.GetId().value));
    test.Frame();
    test.Frame();
    INFO("The game's .alpha rule did not colour Alpha's host yellow");
    REQUIRE(Is(test.root.GetResolvedStyle(host)->color, 1.0f, 1.0f, 0.0f));
}

TEST_CASE("Editing a Razor component's sheet file restyles it at the next reload poll",
          "[ui][scripting][razor][sheets]")
{
    const HotSheetRemoved removed;
    WriteHotSheet(".title { color: #00ff00; }\n");
    SheetsScriptingApp test;
    test.root.SetHotReload(true);
    test.Call<void>(SheetsProbeType, "MountHot", test.root.GetId().value);
    test.Frame();
    test.Frame();
    {
        INFO("Hot's sheet did not load from its file, green, but from the text the build recorded, red");
        REQUIRE(Is(test.ColourOf("hot-title"), 0.0f, 1.0f, 0.0f));
    }

    WriteHotSheet(".title { color: #0000ff; }\n");
    test.Frame(0.3);
    test.Frame();
    INFO("The rewritten sheet was not read again with Hot's scope at the next poll, 250 ms on");
    REQUIRE(Is(test.ColourOf("hot-title"), 0.0f, 0.0f, 1.0f));
}

TEST_CASE("A Razor component's sheet loads from the text the build recorded when its file is not there",
          "[ui][scripting][razor][sheets]")
{
    std::error_code ignored;
    std::filesystem::remove(HotSheet(), ignored);
    SheetsScriptingApp test;
    test.Call<void>(SheetsProbeType, "MountHot", test.root.GetId().value);
    test.Frame();
    test.Frame();
    INFO("Hot's recorded text did not colour its title red");
    REQUIRE(Is(test.ColourOf("hot-title"), 1.0f, 0.0f, 0.0f));
}

TEST_CASE("A script generation with scoped Razor component sheets is collected when released",
          "[ui][scripting][razor][sheets]")
{
    MountedSheetsApp test;
    {
        INFO("The reload did not succeed");
        REQUIRE(test.scripting.Reload().Succeeded());
    }
    INFO("The released generation was not collected: the Razor runtime still holds something of it");
    REQUIRE(test.scripting.PendingUnloadCount() == 0);
}
