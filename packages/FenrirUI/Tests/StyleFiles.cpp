// Markup documents and stylesheets loaded from files through a headless App (the spec's seam 1): files written to a
// temporary asset root, loaded by path, and read back as the elements and styles they made; then rewritten, with
// FenrirTime ticking past the hot reload interval, and read back again.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirUI/Markup.hpp"
#include "UITestApp.hpp"

namespace
{
    using Fenrir::ElementId;
    using Fenrir::MarkupLoad;

    // A logger that keeps the warnings it is given, for the tests that a failed reload logs.
    class WarningLog : public Fenrir::ILogger
    {
      public:
        explicit WarningLog(std::shared_ptr<std::vector<std::string>> warnings) : m_warnings(std::move(warnings))
        {
        }
        void AddLogger(std::shared_ptr<ILogger>) override
        {
        }
        void RemoveLogger(std::shared_ptr<ILogger>) override
        {
        }

      protected:
        void LogImpl(std::string_view) override
        {
        }
        void InfoImpl(std::string_view) override
        {
        }
        void WarnImpl(std::string_view message) override
        {
            m_warnings->emplace_back(message);
        }
        void ErrorImpl(std::string_view message) override
        {
            m_warnings->emplace_back(message);
        }
        void FatalImpl(std::string_view) override
        {
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_warnings;
    };

    // A fresh directory under the system's temporary one, removed with everything in it at the end.
    struct TemporaryRoot
    {
        std::filesystem::path path;

        TemporaryRoot()
        {
            std::random_device random;
            path = std::filesystem::temp_directory_path() / ("FenrirUIFiles-" + std::to_string(random()));
            std::filesystem::create_directories(path);
        }
        TemporaryRoot(const TemporaryRoot&) = delete;
        TemporaryRoot& operator=(const TemporaryRoot&) = delete;
        ~TemporaryRoot()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };

    struct FileTest
    {
        TemporaryRoot files;
        std::shared_ptr<std::vector<std::string>> warnings = std::make_shared<std::vector<std::string>>();
        UITest::UITestApp test{Fenrir::AssetsConfig{files.path}, std::make_unique<WarningLog>(warnings)};
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});

        // Writes `text` to `relative` under the asset root, its modification time moved on from the last write's so
        // a rewrite within the clock's resolution still reads as changed.
        void Write(std::string_view relative, std::string_view text)
        {
            const std::filesystem::path file = files.path / relative;
            std::filesystem::create_directories(file.parent_path());
            std::error_code missing;
            const std::filesystem::file_time_type before = std::filesystem::last_write_time(file, missing);
            {
                std::ofstream out(file, std::ios::binary | std::ios::trunc);
                out << text;
            }
            if (!missing)
            {
                std::filesystem::last_write_time(file, before + std::chrono::seconds(2));
            }
        }

        // Runs a frame of `seconds` of FenrirTime.
        void Tick(double seconds)
        {
            test.Frames(1, seconds);
        }

        ElementId Find(std::string_view id) const
        {
            const ElementId element = root.FindById(id);
            INFO("an element has the id " << id);
            REQUIRE(root.IsAlive(element));
            return element;
        }

        bool Logged(std::string_view text) const
        {
            return std::any_of(warnings->begin(), warnings->end(),
                               [&](const std::string& warning) { return warning.find(text) != std::string::npos; });
        }
    };

    std::string Errors(const MarkupLoad& load)
    {
        std::string text;
        for (const Fenrir::MarkupError& error : load.errors)
        {
            text += error.file + ":" + std::to_string(error.line) + ": " + error.message + "\n";
        }
        return text;
    }

    bool Same(const Fenrir::Colour& a, const Fenrir::Colour& b)
    {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    const Fenrir::Colour red = Fenrir::Colour::Red;
    const Fenrir::Colour blue = Fenrir::Colour::Blue;
} // namespace

TEST_CASE("A document loaded by path resolves its includes, sheets and imports relative to their files", "[ui][files]")
{
    FileTest scene;
    scene.Write("ui/hud.ui", "<panel id=\"hud\"><include src=\"parts/title.ui\"/></panel>\n"
                             "<style src=\"styles/hud.css\"/>\n");
    scene.Write("ui/parts/title.ui", "<label id=\"title\">Hi</label>\n");
    scene.Write("ui/styles/hud.css", "@import \"base.css\";\n#title { width: 50px; }\n");
    scene.Write("ui/styles/base.css", "#hud { width: 300px; }\n#title { height: 20px; }\n");

    const MarkupLoad load = scene.root.LoadDocument(scene.root.GetRootElement(), "ui/hud.ui");
    INFO("the document, its include, its sheet and the sheet's import all load: " << Errors(load));
    REQUIRE(load.errors.empty());
    REQUIRE(load.stylesheets.size() == 1);
    scene.test.Frame();

    INFO("the import's rules apply, and the importing sheet's, which come after them, too");
    REQUIRE(scene.root.GetBoxes(scene.Find("hud")).border.width == 300.0f);
    REQUIRE(scene.root.GetBoxes(scene.Find("title")).border.width == 50.0f);
    REQUIRE(scene.root.GetBoxes(scene.Find("title")).border.height == 20.0f);
}

TEST_CASE("Imported rules sit where the import is, so the importing sheet's later rules win", "[ui][files]")
{
    FileTest scene;
    scene.Write("main.css", "@import \"theme.css\";\n#box { width: 40px; }\n");
    scene.Write("theme.css", "#box { width: 10px; height: 30px; }\n");
    const ElementId box = scene.root.Create(scene.root.GetRootElement(), "panel");
    scene.root.SetIdString(box, "box");

    {
        INFO("the sheet and its import load");
        REQUIRE(scene.root.LoadStylesheet("main.css").IsValid());
    }
    scene.test.Frame();

    INFO("the import's height applies, and its width loses to the importer's rule after the import");
    REQUIRE(scene.root.GetBoxes(box).border.width == 40.0f);
    REQUIRE(scene.root.GetBoxes(box).border.height == 30.0f);
}

TEST_CASE("An import loop is one error, and the sheets in it still load", "[ui][files]")
{
    FileTest scene;
    scene.Write("menu.ui", "<panel id=\"menu\"/><style src=\"a.css\"/>");
    scene.Write("a.css", "@import \"b.css\";\n#menu { width: 120px; }\n");
    scene.Write("b.css", "@import \"a.css\";\n#menu { height: 60px; }\n");

    const MarkupLoad load = scene.root.LoadDocument(scene.root.GetRootElement(), "menu.ui");
    INFO("b.css importing a.css, which is being loaded, is the one error: " << Errors(load));
    REQUIRE(load.errors.size() == 1);
    REQUIRE(load.errors.front().file == "b.css");
    REQUIRE(load.errors.front().line == 1);
    REQUIRE(load.errors.front().message.find("a.css") != std::string::npos);
    scene.test.Frame();

    INFO("both sheets' rules apply");
    REQUIRE(scene.root.GetBoxes(scene.Find("menu")).border.width == 120.0f);
    REQUIRE(scene.root.GetBoxes(scene.Find("menu")).border.height == 60.0f);
}

TEST_CASE("A document that is not there is one error naming it, and loads nothing", "[ui][files]")
{
    FileTest scene;
    const MarkupLoad load = scene.root.LoadDocument(scene.root.GetRootElement(), "nowhere.ui");
    INFO("the load says which file is missing: " << Errors(load));
    REQUIRE(load.elements.empty());
    REQUIRE(load.errors.size() == 1);
    REQUIRE(load.errors.front().file == "nowhere.ui");
}

TEST_CASE("A <style src> that is not there is an error at its <style>, and the rest of the document loads",
          "[ui][files]")
{
    FileTest scene;
    scene.Write("menu.ui", "<panel id=\"menu\"/><style src=\"missing.css\"/>");
    const MarkupLoad load = scene.root.LoadDocument(scene.root.GetRootElement(), "menu.ui");
    INFO("the panel loads and the one error names the sheet: " << Errors(load));
    REQUIRE(load.elements.size() == 1);
    REQUIRE(load.errors.size() == 1);
    REQUIRE(load.errors.front().message.find("missing.css") != std::string::npos);
}

TEST_CASE("A stylesheet loaded by a path that is not there gives no sheet, and is logged", "[ui][files]")
{
    FileTest scene;
    INFO("no sheet is added, and the log names the file");
    REQUIRE_FALSE(scene.root.LoadStylesheet("gone.css").IsValid());
    REQUIRE(scene.Logged("gone.css"));
}

namespace
{
    // Copies HarfBuzz's test Roboto under the asset root as fonts/Roboto.ttf, and returns its font asset.
    Fenrir::AssetHandle<Fenrir::Font> CopyRoboto(FileTest& scene)
    {
        const std::filesystem::path roboto =
            std::filesystem::path(FENRIR_TEXT_HARFBUZZ_TEST) / "subset" / "data" / "fonts" / "Roboto-Regular.ttf";
        std::filesystem::create_directories(scene.files.path / "fonts");
        std::filesystem::copy_file(roboto, scene.files.path / "fonts" / "Roboto.ttf");
        return scene.test.assets.Store().Load<Fenrir::Font>("fonts/Roboto.ttf", Fenrir::AssetOwner::App()).handle;
    }

    constexpr std::string_view robotoFace =
        "@font-face {\n  font-family: \"Body Text\";\n  src: url(\"fonts/Roboto.ttf\");\n"
        "  font-weight: bold;\n  font-style: italic;\n  font-rendering: sdf;\n}\n";
} // namespace

TEST_CASE("@font-face registers a family that a label then measures with", "[ui][files][text]")
{
    FileTest scene;
    const Fenrir::AssetHandle<Fenrir::Font> font = CopyRoboto(scene);
    scene.Write("fonts.css", std::string(robotoFace) +
                                 ".body { font-family: 'Body Text'; font-weight: 700; font-style: italic; }\n");
    INFO("the sheet with the face loads");
    REQUIRE(scene.root.LoadStylesheet("fonts.css").IsValid());

    const ElementId body = scene.root.Create(scene.root.GetRootElement(), "label");
    scene.root.AddClass(body, "body");
    scene.root.SetText(body, "Measured");
    const ElementId plain = scene.root.Create(scene.root.GetRootElement(), "label");
    scene.root.SetText(plain, "Measured");
    scene.root.SetStyleProperty(scene.root.GetRootElement(), "align-items", "flex-start");
    scene.test.Frame();

    INFO("the label in Body Text bold italic lays out in the face the sheet named");
    const Fenrir::Paragraph* paragraph = scene.root.GetParagraph(body);
    REQUIRE(paragraph != nullptr);
    REQUIRE(paragraph->Runs().front().font == font);
    INFO("in the SDF mode the face's font-rendering: sdf chose");
    REQUIRE(paragraph->Runs().front().rendering == Fenrir::FontRendering::Sdf);
    INFO("and measures to it: Roboto is narrower than the embedded monospace default");
    REQUIRE(scene.root.GetBoxes(body).content.width < scene.root.GetBoxes(plain).content.width);
}

TEST_CASE("@font-face's face is recorded with its weight, style and SDF opt-in", "[ui][files][text]")
{
    FileTest scene;
    const Fenrir::AssetHandle<Fenrir::Font> font = CopyRoboto(scene);
    scene.Write("fonts.css", robotoFace);
    INFO("the sheet with the face loads");
    REQUIRE(scene.root.LoadStylesheet("fonts.css").IsValid());

    INFO("the face is recorded as written, font-rendering: sdf among it, for the SDF text mode to read");
    const std::span<const Fenrir::UIFontFace> faces = scene.test.Roots().GetFontFaces();
    REQUIRE(faces.size() == 1);
    REQUIRE(faces.front().family == "Body Text");
    REQUIRE(faces.front().font == font);
    REQUIRE(faces.front().weight == Fenrir::FontWeightBold);
    REQUIRE(faces.front().style == Fenrir::FontStyle::Italic);
    REQUIRE(faces.front().rendering == Fenrir::FontRendering::Sdf);
}

TEST_CASE("A @font-face missing what it needs, or naming an unknown descriptor, is an error", "[ui][files][text]")
{
    FileTest scene;
    scene.Write("menu.ui", "<panel/><style src=\"fonts.css\"/>");
    scene.Write("fonts.css",
                "@font-face {\n  font-family: Body;\n}\n"
                "@font-face {\n  font-family: Body;\n  src: url(fonts/none.ttf);\n}\n"
                "@font-face {\n  font-family: Body;\n  src: url(fonts/none.ttf);\n  font-stretch: 50%;\n}\n");

    const MarkupLoad load = scene.root.LoadDocument(scene.root.GetRootElement(), "menu.ui");
    INFO("no src, a src that cannot be loaded, and an unknown descriptor: " << Errors(load));
    REQUIRE(load.errors.size() == 3);
    REQUIRE(load.errors[0].file == "fonts.css");
    REQUIRE(load.errors[0].line == 1);
    REQUIRE(load.errors[1].line == 6);
    REQUIRE(load.errors[2].line == 11);
    REQUIRE(scene.test.Roots().GetFontFaces().empty());
}

TEST_CASE("A rewritten stylesheet restyles once 250 ms have ticked past, without reloading the elements",
          "[ui][files][hot-reload]")
{
    FileTest scene;
    scene.Write("ui/hud.ui", "<label id=\"title\">Hi</label><style src=\"hud.css\"/>");
    scene.Write("ui/hud.css", "@import \"colours.css\";\n");
    scene.Write("ui/colours.css", "#title { color: red; }\n");
    scene.root.SetHotReload(true);
    {
        INFO("the document, its sheet and the import load, and the label is red");
        REQUIRE(scene.root.LoadDocument(scene.root.GetRootElement(), "ui/hud.ui").errors.empty());
        scene.Tick(0.1);
        REQUIRE(Same(scene.root.GetResolvedStyle(scene.Find("title"))->color, red));
    }
    const ElementId title = scene.Find("title");

    scene.Write("ui/colours.css", "#title { color: blue; }\n");
    scene.Tick(0.1);
    {
        INFO("200 ms in, the files have not been polled yet");
        REQUIRE(Same(scene.root.GetResolvedStyle(title)->color, red));
    }

    scene.Tick(0.1);
    INFO("past 250 ms the rewritten import is read, its sheet parsed again and every style resolved again, and the "
         "label is the one it was");
    REQUIRE(scene.root.IsAlive(title));
    REQUIRE(Same(scene.root.GetResolvedStyle(title)->color, blue));
}

TEST_CASE("A rewritten stylesheet with errors keeps the version before and logs them", "[ui][files][hot-reload]")
{
    FileTest scene;
    scene.Write("hud.css", "#title { color: red; }\n");
    const ElementId title = scene.root.Create(scene.root.GetRootElement(), "label");
    scene.root.SetIdString(title, "title");
    scene.root.SetHotReload(true);
    {
        INFO("the sheet loads");
        REQUIRE(scene.root.LoadStylesheet("hud.css").IsValid());
    }
    scene.Tick(0.1);

    scene.Write("hud.css", "#title { color: blue; colour: green; }\n");
    scene.Tick(0.3);
    {
        INFO("the unknown property is logged with the file and line, and the sheet before it stays");
        REQUIRE(Same(scene.root.GetResolvedStyle(title)->color, red));
        REQUIRE(scene.Logged("hud.css:1"));
    }

    scene.Write("hud.css", "#title { color: blue; }\n");
    scene.Tick(0.3);
    INFO("fixed, the next poll takes it");
    REQUIRE(Same(scene.root.GetResolvedStyle(title)->color, blue));
}

TEST_CASE("A rewritten document reloads in its place, its old ids go stale and the reload is sent",
          "[ui][files][hot-reload]")
{
    FileTest scene;
    Fenrir::EventReader<Fenrir::UIEvent> events;
    const ElementId rootElement = scene.root.GetRootElement();
    const ElementId before = scene.root.Create(rootElement, "panel");
    scene.Write("menu.ui", "<panel id=\"menu\"><label id=\"name\">Old</label></panel>");
    scene.root.SetHotReload(true);
    {
        INFO("the document loads between two panels");
        REQUIRE(scene.root.LoadDocument(rootElement, "menu.ui").errors.empty());
    }
    const ElementId after = scene.root.Create(rootElement, "panel");
    scene.Tick(0.1);
    const ElementId oldMenu = scene.Find("menu");
    const ElementId oldName = scene.Find("name");
    UITest::ReadAll(events, scene.test.app);

    scene.Write("menu.ui", "<panel id=\"menu\"><label id=\"name\">New</label><button id=\"ok\"/></panel>");
    scene.Tick(0.3);

    INFO("the old elements are gone at once, and their ids with them");
    REQUIRE_FALSE(scene.root.IsAlive(oldMenu));
    REQUIRE_FALSE(scene.root.IsAlive(oldName));
    INFO("the new tree is in the same place among the parent's children");
    const ElementId menu = scene.Find("menu");
    const std::span<const ElementId> children = scene.root.GetChildren(rootElement);
    REQUIRE(children.size() == 3);
    REQUIRE(children[0] == before);
    REQUIRE(children[1] == menu);
    REQUIRE(children[2] == after);
    REQUIRE(scene.root.GetText(scene.Find("name")) == "New");
    REQUIRE(scene.root.IsAlive(scene.Find("ok")));

    INFO("the document-reloaded event is sent at the document's new element, for the game to find its ids again");
    const std::vector<Fenrir::UIEvent> sent = UITest::ReadAll(events, scene.test.app);
    const auto reloaded = std::find_if(sent.begin(), sent.end(), [](const Fenrir::UIEvent& event) {
        return event.kind == Fenrir::UIEventKind::DocumentReloaded;
    });
    REQUIRE(reloaded != sent.end());
    REQUIRE(reloaded->target == menu);
    REQUIRE(reloaded->root == scene.root.GetId());
}

TEST_CASE("A rewritten include reloads the document that includes it", "[ui][files][hot-reload]")
{
    FileTest scene;
    scene.Write("menu.ui", "<panel id=\"menu\"><include src=\"name.ui\"/></panel>");
    scene.Write("name.ui", "<label id=\"name\">Old</label>");
    scene.root.SetHotReload(true);
    {
        INFO("the document and its include load");
        REQUIRE(scene.root.LoadDocument(scene.root.GetRootElement(), "menu.ui").errors.empty());
    }
    scene.Tick(0.1);

    scene.Write("name.ui", "<label id=\"name\">New</label>");
    scene.Tick(0.3);
    INFO("the include is one of the files the document loaded");
    REQUIRE(scene.root.GetText(scene.Find("name")) == "New");
}

TEST_CASE("A document rewritten with an error keeps the old tree and logs", "[ui][files][hot-reload]")
{
    FileTest scene;
    Fenrir::EventReader<Fenrir::UIEvent> events;
    scene.Write("menu.ui", "<panel id=\"menu\"/>");
    scene.root.SetHotReload(true);
    {
        INFO("the document loads");
        REQUIRE(scene.root.LoadDocument(scene.root.GetRootElement(), "menu.ui").errors.empty());
    }
    scene.Tick(0.1);
    const ElementId menu = scene.Find("menu");
    UITest::ReadAll(events, scene.test.app);

    scene.Write("menu.ui", "<panel id=\"broken\"><nonsense/></panel>");
    scene.Tick(0.3);
    INFO("the old tree stays, nothing of the new one is left, the error is logged with its file and line, and no "
         "reload is sent");
    REQUIRE(scene.root.IsAlive(menu));
    REQUIRE_FALSE(scene.root.IsAlive(scene.root.FindById("broken")));
    REQUIRE(scene.root.GetChildren(scene.root.GetRootElement()).size() == 1);
    REQUIRE(scene.Logged("menu.ui:1"));
    for (const Fenrir::UIEvent& event : UITest::ReadAll(events, scene.test.app))
    {
        REQUIRE(event.kind != Fenrir::UIEventKind::DocumentReloaded);
    }
}

TEST_CASE("A document version refused for its errors registers none of its font faces", "[ui][files][hot-reload]")
{
    FileTest scene;
    CopyRoboto(scene);
    scene.Write("menu.ui", "<panel id=\"menu\"/>");
    scene.root.SetHotReload(true);
    {
        INFO("the document loads with no faces");
        REQUIRE(scene.root.LoadDocument(scene.root.GetRootElement(), "menu.ui").errors.empty());
    }
    scene.Tick(0.1);

    scene.Write("menu.ui", "<panel id=\"menu\"><nonsense/></panel><style>" + std::string(robotoFace) + "</style>");
    scene.Tick(0.3);
    INFO("the font family registry is every root's: a version that is not taken leaves nothing in it");
    REQUIRE(scene.test.Roots().GetFontFaces().empty());
}

TEST_CASE("With hot reload off, no file is polled", "[ui][files][hot-reload]")
{
    FileTest scene;
    scene.Write("hud.ui", "<label id=\"title\">Hi</label><style src=\"hud.css\"/>");
    scene.Write("hud.css", "#title { color: red; }\n");
    INFO("hot reload is off by default");
    REQUIRE_FALSE(scene.root.IsHotReloadOn());
    REQUIRE(scene.root.LoadDocument(scene.root.GetRootElement(), "hud.ui").errors.empty());
    const ElementId title = scene.Find("title");

    scene.Write("hud.css", "#title { color: blue; }\n");
    std::uint32_t polled = 0;
    for (int frame = 0; frame < 10; ++frame)
    {
        scene.Tick(0.1);
        polled += scene.root.GetStats().filesPolled;
    }
    INFO("a second of frames polls nothing, and the rewrite is not seen");
    REQUIRE(polled == 0);
    REQUIRE(Same(scene.root.GetResolvedStyle(title)->color, red));
}

TEST_CASE("Hot reload turned on later polls what was loaded before it", "[ui][files][hot-reload]")
{
    FileTest scene;
    scene.Write("hud.ui", "<label id=\"title\">Hi</label><style src=\"hud.css\"/>");
    scene.Write("hud.css", "#title { color: red; }\n");
    {
        INFO("the document loads with hot reload off");
        REQUIRE(scene.root.LoadDocument(scene.root.GetRootElement(), "hud.ui").errors.empty());
    }
    const ElementId title = scene.Find("title");
    scene.Tick(0.1);
    scene.Write("hud.css", "#title { color: blue; }\n");

    scene.root.SetHotReload(true);
    scene.Tick(0.3);
    INFO("the first poll reads the document and the sheet, and takes the sheet's rewrite alone");
    REQUIRE(scene.root.GetStats().filesPolled == 2);
    REQUIRE(scene.root.IsAlive(title));
    REQUIRE(Same(scene.root.GetResolvedStyle(title)->color, blue));
}
