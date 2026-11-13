// Markup documents through a headless App (the spec's seam 1): documents loaded from strings into a root, read back as
// the elements, attributes, text and sheets they made, and the errors they report with their files and lines.
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/Markup.hpp"
#include "UITestApp.hpp"

namespace
{
    using Fenrir::ElementId;
    using Fenrir::MarkupError;
    using Fenrir::MarkupLoad;
    using UITest::UITestApp;

    struct MarkupTest
    {
        UITestApp test;
        std::shared_ptr<UITest::CaptureTarget> target;
        Fenrir::UIRoot& root = test.MakeRoot(target, {800.0f, 600.0f});

        MarkupLoad Load(std::string_view text, std::string_view name = "markup.ui",
                        const Fenrir::MarkupResolver& resolver = {})
        {
            return root.LoadMarkup(root.GetRootElement(), text, name, resolver);
        }

        ElementId Find(std::string_view id) const
        {
            const ElementId element = root.FindById(id);
            INFO("an element has the id " << id);
            REQUIRE(root.IsAlive(element));
            return element;
        }

        template <typename T>
        T& As(std::string_view id) const
        {
            auto* element = dynamic_cast<T*>(root.GetElement(Find(id)));
            INFO("the element " << id << " was made by its tag's factory");
            REQUIRE(element != nullptr);
            return *element;
        }
    };

    // A resolver over a fixed set of documents and sheets, by name.
    Fenrir::MarkupResolver Files(std::map<std::string, std::string, std::less<>> files)
    {
        return [files = std::move(files)](std::string_view src) -> std::optional<std::string> {
            const auto it = files.find(src);
            if (it == files.end())
            {
                return std::nullopt;
            }
            return it->second;
        };
    }

    std::string Describe(const std::vector<MarkupError>& errors)
    {
        std::string out;
        for (const MarkupError& error : errors)
        {
            out += error.file + ":" + std::to_string(error.line) + ": " + error.message + "\n";
        }
        return out;
    }

    // An error a load should report: its line in markup.ui, and what its message says.
    struct ExpectedError
    {
        std::uint32_t line;
        std::string_view says;
    };

    void RequireErrors(const MarkupLoad& load, const std::vector<ExpectedError>& expected)
    {
        INFO(Describe(load.errors));
        INFO("the load reports each error, in document order, and no others");
        REQUIRE(load.errors.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            INFO("error " << i << " is at markup.ui:" << expected[i].line << " and says " << expected[i].says);
            REQUIRE(load.errors[i].file == "markup.ui");
            REQUIRE(load.errors[i].line == expected[i].line);
            REQUIRE(load.errors[i].message.find(expected[i].says) != std::string::npos);
        }
    }

    // A custom element with one attribute of its own, registered under `minimap`.
    class Minimap : public Fenrir::Element
    {
      public:
        Fenrir::AttributeResult SetAttribute(std::string_view name, std::string_view value) override
        {
            if (name != "zoom")
            {
                return Fenrir::AttributeResult::Unknown;
            }
            zoom = std::string(value);
            return Fenrir::AttributeResult::Set;
        }

        std::string zoom;
    };
} // namespace

TEST_CASE("A document loads to its tree of elements, in document order, with their ids and classes", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load(R"(<panel id="hud" class="hud  top">
  <panel id="row" class="row">
    <label id="title" class="big">Score</label>
    <image id="icon"/>
  </panel>
  <slider id="volume"/>
  <checkbox id="mute"/>
  <textentry id="name"/>
  <button id="ok"/>
  <progress id="health"/>
  <scroll id="list"/>
</panel>)");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());

    INFO("the document's top-level element is under the parent it was loaded into");
    const ElementId hud = t.Find("hud");
    REQUIRE(load.element == hud);
    REQUIRE(load.elements == std::vector<ElementId>{hud});
    REQUIRE(t.root.GetParent(hud) == t.root.GetRootElement());

    INFO("its children are in document order, each made under its tag");
    const auto children = t.root.GetChildren(hud);
    const std::array<std::string_view, 7> tags{"panel",  "slider",   "checkbox", "textentry",
                                               "button", "progress", "scroll"};
    REQUIRE(children.size() == tags.size());
    for (std::size_t i = 0; i < tags.size(); ++i)
    {
        REQUIRE(t.root.GetTag(children[i]) == tags[i]);
    }
    REQUIRE(t.root.GetChildren(t.Find("row")).size() == 2);
    REQUIRE(t.root.GetChildren(t.Find("row"))[0] == t.Find("title"));
    REQUIRE(t.root.GetChildren(t.Find("row"))[1] == t.Find("icon"));
    (void)t.As<Fenrir::Label>("title");
    (void)t.As<Fenrir::ScrollView>("list");

    INFO("class splits on whitespace into classes, in order");
    REQUIRE(t.root.GetClasses(hud).size() == 2);
    REQUIRE(t.root.GetClasses(hud)[0] == "hud");
    REQUIRE(t.root.GetClasses(hud)[1] == "top");
    REQUIRE(t.root.HasClass(t.Find("title"), "big"));
}

TEST_CASE("A label's and a button's text content is their text, trimmed at its ends", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load("<panel>\n"
                                   "  <label id=\"title\">\n"
                                   "    Hello &amp; welcome\n"
                                   "  </label>\n"
                                   "  <button id=\"ok\">OK</button>\n"
                                   "  <label id=\"attribute\" text=\"From the attribute\"/>\n"
                                   "</panel>");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    REQUIRE(t.root.GetText(t.Find("title")) == "Hello & welcome");
    REQUIRE(t.root.GetText(t.Find("ok")) == "OK");
    REQUIRE(t.root.GetText(t.Find("attribute")) == "From the attribute");
}

TEST_CASE("An image loads its src from the asset store and takes its fit", "[ui][markup]")
{
    MarkupTest t;
    Fenrir::ImageData pixels;
    pixels.width = 4;
    pixels.height = 2;
    pixels.pixels.assign(4 * 2 * 4, std::byte{0xFF});
    const Fenrir::AssetHandle<Fenrir::Image> icon =
        t.test.app.GetResource<Fenrir::AssetStore>().AddBuiltin<Fenrir::Image>("builtin:markup-icon",
                                                                               std::move(pixels));
    const MarkupLoad load = t.Load("<image id=\"icon\" src=\"builtin:markup-icon\" fit=\"contain\"/>");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    REQUIRE(t.As<Fenrir::ImageView>("icon").GetSource() == icon);
    REQUIRE(t.As<Fenrir::ImageView>("icon").GetFit() == Fenrir::ImageFit::Contain);
}

TEST_CASE("A slider's and a progress bar's range and value apply whatever order they are written in", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load("<panel>\n"
                                   "  <slider id=\"volume\" value=\"7.5\" max=\"10\" min=\"2\" step=\"0.5\"/>\n"
                                   "  <slider id=\"tall\" orientation=\"vertical\"/>\n"
                                   "  <progress id=\"health\" value=\"30\" min=\"10\" max=\"210\"/>\n"
                                   "</panel>");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    const Fenrir::Slider& volume = t.As<Fenrir::Slider>("volume");
    REQUIRE(volume.GetMin() == 2.0f);
    REQUIRE(volume.GetMax() == 10.0f);
    REQUIRE(volume.GetStep() == 0.5f);
    REQUIRE(volume.GetValue() == 7.5f);
    REQUIRE(t.As<Fenrir::Slider>("tall").GetOrientation() == Fenrir::Orientation::Vertical);
    const Fenrir::ProgressBar& health = t.As<Fenrir::ProgressBar>("health");
    REQUIRE(health.GetValue() == 30.0f);
    REQUIRE(health.GetFraction() == 20.0f / 200.0f);
}

TEST_CASE("A checkbox and a text entry take their attributes, a bare boolean being true", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load =
        t.Load("<panel>\n"
               "  <checkbox id=\"mute\" checked/>\n"
               "  <textentry id=\"name\" value=\"Ada\" placeholder=\"Your name\" maxlength=\"12\" password=\"true\"/>\n"
               "</panel>");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    REQUIRE(t.As<Fenrir::Checkbox>("mute").IsChecked());
    REQUIRE(t.root.HasPseudoClass(t.Find("mute"), Fenrir::PseudoClass::Checked));
    const Fenrir::TextEntry& name = t.As<Fenrir::TextEntry>("name");
    REQUIRE(name.GetValue() == "Ada");
    REQUIRE(name.GetPlaceholder() == "Your name");
    REQUIRE(name.GetMaxLength() == 12);
    REQUIRE(name.IsPassword());
}

TEST_CASE("disabled and focusable reach a control, focusable winning wherever it is written", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load("<panel>\n"
                                   "  <button id=\"off\" disabled=\"true\"/>\n"
                                   "  <button id=\"before\" focusable=\"false\" disabled=\"false\"/>\n"
                                   "  <scroll id=\"list\" focusable=\"false\"/>\n"
                                   "  <panel id=\"plain\" focusable/>\n"
                                   "</panel>");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    REQUIRE(t.As<Fenrir::Button>("off").IsDisabled());
    REQUIRE(t.root.HasPseudoClass(t.Find("off"), Fenrir::PseudoClass::Disabled));
    REQUIRE_FALSE(t.root.IsFocusable(t.Find("off")));
    REQUIRE_FALSE(t.root.IsFocusable(t.Find("before")));
    REQUIRE_FALSE(t.root.IsFocusable(t.Find("list")));
    REQUIRE(t.root.IsFocusable(t.Find("plain")));
}

TEST_CASE("The five XML entities and numeric ones decode in text and attribute values", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load =
        t.Load("<panel>\n"
               "  <label id=\"text\">&lt;a&gt; &amp; &quot;b&quot; &apos;c&apos; &#65;&#x263a;</label>\n"
               "  <textentry id=\"entry\" placeholder=\"&lt;&#x42;&#67;&gt;\"/>\n"
               "</panel>");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    REQUIRE(t.root.GetText(t.Find("text")) == "<a> & \"b\" 'c' A\xE2\x98\xBA");
    REQUIRE(t.As<Fenrir::TextEntry>("entry").GetPlaceholder() == "<BC>");
}

TEST_CASE("Comments are skipped wherever they are, tags inside them included", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load("<!-- a screen -->\n"
                                   "<panel id=\"top\">\n"
                                   "  <!-- <label id=\"hidden\">not loaded</label> -->\n"
                                   "  <label id=\"shown\">kept<!-- not text --></label>\n"
                                   "</panel>\n"
                                   "<!-- after -->");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    REQUIRE(t.root.GetChildren(t.Find("top")).size() == 1);
    REQUIRE_FALSE(t.root.IsAlive(t.root.FindById("hidden")));
    REQUIRE(t.root.GetText(t.Find("shown")) == "kept");
}

TEST_CASE("An include splices another document's elements in its place", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load("<panel id=\"list\">\n"
                                   "  <label>before</label>\n"
                                   "  <include src=\"rows.ui\"/>\n"
                                   "  <label>after</label>\n"
                                   "</panel>",
                                   "markup.ui", Files({{"rows.ui", "<label>one</label>\n<label>two</label>"}}));
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    const auto rows = t.root.GetChildren(t.Find("list"));
    REQUIRE(rows.size() == 4);
    const std::array<std::string_view, 4> texts{"before", "one", "two", "after"};
    for (std::size_t i = 0; i < texts.size(); ++i)
    {
        REQUIRE(t.root.GetText(rows[i]) == texts[i]);
    }

    INFO("an include at the top level adds its elements to the load's");
    const MarkupLoad top =
        t.Load("<include src=\"rows.ui\"/>", "top.ui", Files({{"rows.ui", "<label>one</label>\n<label>two</label>"}}));
    REQUIRE(top.errors.empty());
    REQUIRE(top.elements.size() == 2);
    REQUIRE(top.element == top.elements[0]);
}

TEST_CASE("A recursive include is one error, at the include that recurses, and the rest loads", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load("<panel id=\"top\"><include src=\"a.ui\"/></panel>", "markup.ui",
                                   Files({{"a.ui", "<panel id=\"a\">\n  <include src=\"b.ui\"/>\n</panel>"},
                                          {"b.ui", "<panel id=\"b\">\n\n  <include src=\"a.ui\"/>\n</panel>"}}));
    INFO(Describe(load.errors));
    REQUIRE(load.errors.size() == 1);
    REQUIRE(load.errors[0].file == "b.ui");
    REQUIRE(load.errors[0].line == 3);
    REQUIRE(t.root.GetParent(t.Find("a")) == t.Find("top"));
    REQUIRE(t.root.GetParent(t.Find("b")) == t.Find("a"));
    REQUIRE(t.root.GetChildren(t.Find("b")).empty());

    INFO("an include of the document itself is caught too");
    const MarkupLoad self = t.Load("<panel>\n<include src=\"self.ui\"/></panel>", "self.ui", Files({}));
    REQUIRE(self.errors.size() == 1);
    REQUIRE(self.errors[0].line == 2);

    INFO("an include whose src the resolver does not have is an error");
    const MarkupLoad missing = t.Load("<include src=\"gone.ui\"/>", "markup.ui", Files({}));
    REQUIRE(missing.errors.size() == 1);
    REQUIRE(missing.errors[0].line == 1);
}

TEST_CASE("Each error kind reports its file and line, and the rest of the document still loads", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load("<panel id=\"top\">\n"                                            // 1
                                   "  <mystery id=\"x\"><label id=\"inside\">no</label></mystery>\n" // 2
                                   "  <label id=\"a\" colour=\"red\">fine</label>\n"                 // 3
                                   "  <panel id=\"b\">stray text</panel>\n"                          // 4
                                   "  <label id=\"c\">bad &nope; entity</label>\n"                   // 5
                                   "  <panel id=\"d\">\n"                                            // 6
                                   "    <label id=\"e\">kept\n"                                      // 7
                                   "  </panel>\n"                                                    // 8
                                   "  <slider id=\"f\" min=\"low\" style=\"width: banana\"/>\n"      // 9
                                   "</panel>\n");                                                    // 10
    RequireErrors(load, {{2, "<mystery> is not an element tag"},
                         {3, "<label> has no attribute 'colour'"},
                         {4, "<panel> takes no text"},
                         {5, "'&nope;' is not an entity"},
                         {7, "<label> is not closed"},
                         {9, "<slider> cannot read 'low' as its 'min'"},
                         {9, "<slider>'s style"}});

    INFO("an unknown tag loads nothing of itself or under it");
    REQUIRE_FALSE(t.root.IsAlive(t.root.FindById("x")));
    REQUIRE_FALSE(t.root.IsAlive(t.root.FindById("inside")));
    INFO("the elements around the errors load, with what of them was good");
    REQUIRE(t.root.GetText(t.Find("a")) == "fine");
    REQUIRE(t.root.GetText(t.Find("b")).empty());
    REQUIRE(t.root.GetText(t.Find("c")) == "bad &nope; entity");
    REQUIRE(t.root.GetParent(t.Find("e")) == t.Find("d"));
    REQUIRE(t.root.GetText(t.Find("e")) == "kept");
    REQUIRE(t.root.GetParent(t.Find("f")) == t.Find("top"));
    REQUIRE(t.root.GetChildren(t.Find("top")).size() == 5);
}

TEST_CASE("Malformed markup and the XML forms markup has none of are errors", "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load = t.Load("\xEF\xBB\xBF<?xml version=\"1.0\"?>\n"       // 1
                                   "<!DOCTYPE panel>\n"                          // 2
                                   "<panel id=\"top\">\n"                        // 3
                                   "  <![CDATA[ raw ]]>\n"                       // 4
                                   "  </label>\n"                                // 5
                                   "  <label id=\"a\" text=a>x</label>\n"        // 6
                                   "  <label id=\"b\" id=\"c\"/>\n"              // 7
                                   "  <label id=\"d\"text=\"x\"/>\n"             // 8
                                   "  <panel id=\"g\"\n"                         // 9
                                   "  <label id=\"h\">in &#0000065;</label>\n"   // 10
                                   "  </panel>\n"                                // 11
                                   "  <style>.x { width: 1px; }</style >\n"      // 12
                                   "  <style/>\n"                                // 13
                                   "  <label id=\"both\" text=\"a\">b</label>\n" // 14
                                   "</panel>\n"                                  // 15
                                   "<panel id=\"open\">\n");                     // 16
    RequireErrors(load, {{1, "processing instructions are not markup"},
                         {2, "DTDs and declarations are not markup"},
                         {4, "CDATA sections are not markup"},
                         {5, "</label> closes no open tag"},
                         {6, "the value of 'text' is not in quotes"},
                         {7, "<label> has 'id' twice"},
                         {8, "need whitespace between them"},
                         {9, "the tag <panel> is not finished with '>'"},
                         {13, "<style> holds no stylesheet"},
                         {14, "has both a text attribute and text content"},
                         {16, "<panel> is not closed"}});
    INFO("the elements around the errors load: a tag missing its '>' holds what follows it");
    REQUIRE(t.root.GetText(t.Find("a")) == "x");
    REQUIRE(t.root.GetText(t.Find("d")) == "x");
    REQUIRE(t.root.GetParent(t.Find("h")) == t.Find("g"));
    REQUIRE(t.root.GetText(t.Find("h")) == "in A");
    REQUIRE(t.root.GetText(t.Find("both")) == "a");
    REQUIRE(load.elements.size() == 2);
    INFO("a style closed by </style > attaches its sheet; the empty one attaches none");
    REQUIRE(load.stylesheets.size() == 1);
}

TEST_CASE("A custom element's tag loads through the registry and takes its own attributes", "[ui][markup]")
{
    MarkupTest t;
    t.test.Roots().RegisterElement("minimap", [] { return std::make_unique<Minimap>(); });
    const MarkupLoad load = t.Load("<minimap id=\"map\" zoom=\"2x\"/>");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    REQUIRE(t.As<Minimap>("map").zoom == "2x");
}

TEST_CASE("Each load of a document makes elements of its own", "[ui][markup]")
{
    MarkupTest t;
    constexpr std::string_view document = "<panel class=\"card\"><label>hi</label></panel>";
    const MarkupLoad first = t.Load(document);
    const MarkupLoad second = t.Load(document);
    REQUIRE(first.errors.empty());
    REQUIRE(second.errors.empty());
    REQUIRE(first.element != second.element);
    REQUIRE(t.root.FindByClass("card").size() == 2);
    REQUIRE(t.root.GetChildren(t.root.GetRootElement()).size() == 2);
}

TEST_CASE("Style blocks attach their sheets to the root, inline or by src, and report their own errors' lines",
          "[ui][markup]")
{
    MarkupTest t;
    const MarkupLoad load =
        t.Load("<panel id=\"top\">\n"                // 1
               "  <style>\n"                         // 2
               "    .row > panel { width: 30px; }\n" // 3
               "    .row { height: nope; }\n"        // 4
               "  </style>\n"                        // 5
               "  <style src=\"theme.css\"/>\n"      // 6
               "  <style src=\"gone.css\"/>\n"       // 7
               "  <panel class=\"row\"><panel id=\"cell\"/></panel>\n"
               "</panel>",
               "markup.ui", Files({{"theme.css", "#cell { height: 20px; }\n#cell { width: oops; }"}}));
    INFO(Describe(load.errors));
    REQUIRE(load.errors.size() == 3);
    REQUIRE(load.errors[0].file == "markup.ui");
    REQUIRE(load.errors[0].line == 4);
    REQUIRE(load.errors[1].file == "theme.css");
    REQUIRE(load.errors[1].line == 2);
    REQUIRE(load.errors[2].file == "markup.ui");
    REQUIRE(load.errors[2].line == 7);
    REQUIRE(load.stylesheets.size() == 2);
    REQUIRE(t.root.GetStylesheet(load.stylesheets[1])->name == "theme.css");

    t.test.Frame();
    const Fenrir::ElementBoxes cell = t.root.GetBoxes(t.Find("cell"));
    INFO("the cell is sized by the inline sheet's width and the src sheet's height");
    REQUIRE(UITest::Near(cell.border.width, 30.0f));
    REQUIRE(UITest::Near(cell.border.height, 20.0f));
}

TEST_CASE("A loaded document with an inline sheet lays out as the same tree built from code", "[ui][markup][layout]")
{
    UITestApp codeApp;
    std::shared_ptr<UITest::CaptureTarget> codeTarget;
    Fenrir::UIRoot& codeRoot = codeApp.MakeRoot(codeTarget, {800.0f, 600.0f});
    const ElementId row = codeRoot.Create(codeRoot.GetRootElement(), "panel");
    const ElementId a = codeRoot.Create(row, "panel");
    const ElementId b = codeRoot.Create(row, "panel");
    const ElementId c = codeRoot.Create(b, "label");
    codeRoot.AddClass(row, "row");
    codeRoot.SetStyleProperty(a, "flex", "1");
    codeRoot.SetStyleProperty(b, "padding", "6px");
    codeRoot.SetText(c, "Score");
    codeRoot.AddStylesheet(".row { flex-direction: row; width: 400px; height: 100px; padding: 10px; gap: 8px; }\n"
                           ".row > panel { flex: 2; }\n"
                           "label { font-size: 20px; }",
                           "code");
    codeApp.Frame();

    MarkupTest t;
    const MarkupLoad load =
        t.Load("<panel class=\"row\">\n"
               "  <style>\n"
               "    .row { flex-direction: row; width: 400px; height: 100px; padding: 10px; gap: 8px; }\n"
               "    .row > panel { flex: 2; }\n"
               "    label { font-size: 20px; }\n"
               "  </style>\n"
               "  <panel id=\"a\" style=\"flex: 1\"/>\n"
               "  <panel id=\"b\" style=\"padding: 6px\"><label id=\"c\">Score</label></panel>\n"
               "</panel>");
    INFO(Describe(load.errors));
    REQUIRE(load.errors.empty());
    t.test.Frame();

    const std::array<std::pair<ElementId, ElementId>, 4> pairs{std::pair{row, load.element}, std::pair{a, t.Find("a")},
                                                               std::pair{b, t.Find("b")}, std::pair{c, t.Find("c")}};
    for (const auto& [built, loaded] : pairs)
    {
        INFO("the loaded element " << t.root.GetIdString(loaded) << " has the built one's border and content boxes");
        REQUIRE(UITest::Near(t.root.GetBoxes(loaded).border, codeRoot.GetBoxes(built).border));
        REQUIRE(UITest::Near(t.root.GetBoxes(loaded).content, codeRoot.GetBoxes(built).content));
    }
    INFO("the label has a size from its text, so the text reached it");
    REQUIRE(codeRoot.GetBoxes(c).content.width > 0.0f);
}
