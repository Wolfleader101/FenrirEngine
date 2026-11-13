// The text entry (spec: "FenrirUI" controls (text entry), seam 1, and its draw list, seam 2): a headless App with
// FenrirInput's test feed types, selects, deletes and submits, and reads back the value, caret, anchor, UIEvents, the
// input claims and the caret and selection the entry painted. Text is in the embedded default face, a monospaced one,
// so every character of a test string is as wide as the next. The clipboard test needs FenrirWindow's clipboard, so
// it makes a hidden window and is skipped without a display, as FenrirWindow's own clipboard test is.

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPainter/PainterVertex.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirUI/Controls.hpp"
#include "FenrirUI/UIEvent.hpp"
#include "FenrirWindow/Window.hpp"
#include "UITestApp.hpp"

using Fenrir::ElementId;
using Fenrir::Key;
using Fenrir::KeyModifiers;
using Fenrir::MouseButton;
using Fenrir::Scancode;
using Fenrir::TextEntry;
using Fenrir::UIEvent;
using Fenrir::UIEventKind;
using Fenrir::UIRoot;
using Fenrir::Math::Vec2;
using namespace UITest;

namespace
{
    constexpr KeyModifiers Ctrl = KeyModifiers::Ctrl;
    constexpr KeyModifiers Shift = KeyModifiers::Shift;

    // A root over a 400 by 300 capture target holding one text entry at 20, 20, 200 wide and 30 tall, laid out by a
    // first frame. Pixel snapping is off, so the painted rectangles keep the paragraph's fractional positions.
    template <typename TApp>
    struct EntryScene
    {
        TApp test;
        std::shared_ptr<CaptureTarget> target;
        UIRoot& root;
        ElementId entry;

        explicit EntryScene(std::string_view sheet = "") : root(MakeRoot())
        {
            if (!sheet.empty())
            {
                root.AddStylesheet(sheet, "test.css");
            }
            entry = root.Create(root.GetRootElement(), "textentry");
            REQUIRE(root.SetStyleProperty(entry, "position", "absolute"));
            REQUIRE(root.SetStyleProperty(entry, "left", "20px"));
            REQUIRE(root.SetStyleProperty(entry, "top", "20px"));
            REQUIRE(root.SetStyleProperty(entry, "width", "200px"));
            REQUIRE(root.SetStyleProperty(entry, "height", "30px"));
            test.Frame();
        }

        UIRoot& MakeRoot()
        {
            target = std::make_shared<CaptureTarget>(Fenrir::UITargetSize{{400.0f, 300.0f}, 1.0f});
            Fenrir::UIRootConfig config;
            config.scaleRule = Fenrir::ScaleRule::ContentScale;
            config.snapToPixels = false;
            config.target = target;
            return test.Roots().CreateRoot(config);
        }

        TextEntry& Entry()
        {
            auto* control = dynamic_cast<TextEntry*>(root.GetElement(entry));
            REQUIRE(control != nullptr);
            return *control;
        }

        void Focus()
        {
            REQUIRE(root.Focus(entry));
        }

        // Types `text` through FenrirInput's text events, one a codepoint, and runs the frame that routes them.
        void TypeText(std::u32string_view text)
        {
            test.Feed().Text(testWindow, text);
            test.Frame();
        }

        // Presses and releases `key` with `modifiers`, and runs the frame that routes them.
        void Press(Key key, Scancode scancode, KeyModifiers modifiers = KeyModifiers::None)
        {
            test.Type(key, scancode, modifiers);
            test.Frame();
        }

        Fenrir::Rect2D Content() const
        {
            return root.GetBoxes(entry).content;
        }

        // The width in UI units of one character of the monospaced default face, as the entry laid it out.
        float Advance()
        {
            const Fenrir::Paragraph& paragraph = Entry().GetParagraph();
            REQUIRE_FALSE(paragraph.Text().empty());
            return paragraph.Size().x / static_cast<float>(Entry().GetValue().size());
        }

        // Where the caret sits before character `index` of a single-byte value, in UI units, with no scroll.
        Vec2 At(std::size_t index)
        {
            const Fenrir::Rect2D content = Content();
            return {content.x + Advance() * static_cast<float>(index), content.y + content.height / 2.0f};
        }
    };

    // A UITestApp with a hidden window before the rest, for FenrirWindow's clipboard.
    struct WindowUITestApp
    {
        Fenrir::App app{std::make_unique<SilentLogger>(), 1};
        Fenrir::Window window = Fenrir::Window::Install(app, HiddenWindow());
        Fenrir::Assets assets = Fenrir::Assets::Install(app);
        Fenrir::Input input = Fenrir::Input::Install(app);
        Fenrir::UI ui = Fenrir::UI::Install(app);

        static Fenrir::WindowConfig HiddenWindow()
        {
            Fenrir::WindowConfig config;
            config.title = "FenrirUITests";
            config.logicalSize = {320, 240};
            config.visible = false;
            return config;
        }

        Fenrir::InputFeed& Feed()
        {
            return app.GetResource<Fenrir::InputFeed>();
        }

        void Type(Key key, Scancode scancode, KeyModifiers modifiers = KeyModifiers::None)
        {
            Feed().KeyDown(testWindow, key, scancode, modifiers);
            Feed().KeyUp(testWindow, key, scancode, modifiers);
        }

        Fenrir::UIRoots& Roots()
        {
            return app.GetResource<Fenrir::UIRoots>();
        }

        void Frame()
        {
            app.RunFrame(1.0 / 60.0);
        }
    };

    using Scene = EntryScene<UITestApp>;

    std::vector<UIEvent> Of(Fenrir::EventReader<UIEvent>& reader, Fenrir::App& app, UIEventKind kind, ElementId target)
    {
        std::vector<UIEvent> found;
        for (const UIEvent& event : reader.Read(app))
        {
            if (event.kind == kind && event.target == target)
            {
                found.push_back(event);
            }
        }
        return found;
    }

    constexpr std::uint32_t Rgba8(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a)
    {
        return r | (g << 8u) | (b << 16u) | (a << 24u);
    }

    // A painted rectangle: its shape's box, from its quad's centre and half size.
    struct PaintedRect
    {
        Fenrir::Rect2D rect;
        Fenrir::ShapeKind shape;
        std::uint32_t colour;
    };

    std::vector<PaintedRect> Painted(const Fenrir::DrawList2D& list)
    {
        std::vector<Fenrir::PainterVertex> vertices(list.vertices.size() / sizeof(Fenrir::PainterVertex));
        std::memcpy(vertices.data(), list.vertices.data(), vertices.size() * sizeof(Fenrir::PainterVertex));
        std::vector<PaintedRect> painted;
        for (std::size_t i = 0; i + 3 < vertices.size(); i += 4)
        {
            Vec2 centre(0.0f);
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                centre += vertices[i + corner].position;
            }
            centre /= 4.0f;
            const Vec2 half = vertices[i].halfSize;
            painted.push_back({{centre.x - half.x, centre.y - half.y, half.x * 2.0f, half.y * 2.0f},
                               static_cast<Fenrir::ShapeKind>(vertices[i].kinds[0]),
                               vertices[i].colour});
        }
        return painted;
    }

    std::vector<PaintedRect> OfColour(const std::vector<PaintedRect>& painted, Fenrir::ShapeKind shape,
                                      std::uint32_t colour)
    {
        std::vector<PaintedRect> found;
        for (const PaintedRect& rect : painted)
        {
            if (rect.shape == shape && rect.colour == colour)
            {
                found.push_back(rect);
            }
        }
        return found;
    }
} // namespace

TEST_CASE("Typing through text events inserts at the caret and sends Change", "[ui][textentry]")
{
    Scene scene;
    Fenrir::EventReader<UIEvent> events;
    scene.Focus();

    scene.TypeText(U"helo");
    INFO("the codepoints are inserted in order, the caret after them");
    REQUIRE(scene.Entry().GetValue() == "helo");
    REQUIRE(scene.Entry().GetCaret() == 4);
    REQUIRE(Of(events, scene.test.app, UIEventKind::Change, scene.entry).size() == 4);

    scene.Press(Key::Left, Scancode::Left);
    scene.TypeText(U"l");
    INFO("typing after a move inserts where the caret is");
    REQUIRE(scene.Entry().GetValue() == "hello");
    REQUIRE(scene.Entry().GetCaret() == 4);

    scene.TypeText(U"é");
    INFO("a codepoint past ASCII is stored as UTF-8, the caret after its bytes");
    REQUIRE(scene.Entry().GetValue() == "hell\xC3\xA9o");
    REQUIRE(scene.Entry().GetCaret() == 6);

    scene.Press(Key::Home, Scancode::Home);
    REQUIRE(scene.Entry().GetCaret() == 0);
    scene.Press(Key::End, Scancode::End);
    REQUIRE(scene.Entry().GetCaret() == 7);
}

TEST_CASE("A combining sequence moves and deletes as one grapheme", "[ui][textentry]")
{
    Scene scene;
    scene.Focus();
    // e, a combining acute accent (U+0301, two bytes) and x: two graphemes in four bytes.
    scene.TypeText(U"éx");
    REQUIRE(scene.Entry().GetValue() == "e\xCC\x81x");

    scene.Press(Key::Left, Scancode::Left);
    scene.Press(Key::Left, Scancode::Left);
    INFO("two steps left from the end pass both graphemes, not three codepoints");
    REQUIRE(scene.Entry().GetCaret() == 0);

    scene.Press(Key::Delete, Scancode::Delete);
    INFO("Delete takes the e and its accent together");
    REQUIRE(scene.Entry().GetValue() == "x");

    scene.Press(Key::Home, Scancode::Home);
    scene.TypeText(U"á");
    scene.Press(Key::Backspace, Scancode::Backspace);
    INFO("Backspace takes the a and its accent together");
    REQUIRE(scene.Entry().GetValue() == "x");
    REQUIRE(scene.Entry().GetCaret() == 0);
}

TEST_CASE("Ctrl with the arrows jumps by word", "[ui][textentry]")
{
    Scene scene;
    scene.Focus();
    scene.Entry().SetValue("hello, brave world");
    INFO("SetValue puts the caret at the end");
    REQUIRE(scene.Entry().GetCaret() == 18);

    scene.Press(Key::Left, Scancode::Left, Ctrl);
    INFO("Ctrl+Left goes to the start of the word before the caret");
    REQUIRE(scene.Entry().GetCaret() == 13);
    scene.Press(Key::Left, Scancode::Left, Ctrl);
    REQUIRE(scene.Entry().GetCaret() == 7);
    scene.Press(Key::Left, Scancode::Left, Ctrl);
    INFO("the comma and the space are not words: they are passed over");
    REQUIRE(scene.Entry().GetCaret() == 0);

    scene.Press(Key::Right, Scancode::Right, Ctrl);
    INFO("Ctrl+Right goes to the end of the word after the caret");
    REQUIRE(scene.Entry().GetCaret() == 5);
    scene.Press(Key::Right, Scancode::Right, Ctrl);
    REQUIRE(scene.Entry().GetCaret() == 12);

    scene.Press(Key::Left, Scancode::Left, Ctrl | Shift);
    INFO("with Shift it selects the word it passes");
    REQUIRE(scene.Entry().GetAnchor() == 12);
    REQUIRE(scene.Entry().GetCaret() == 7);

    scene.Press(Key::Backspace, Scancode::Backspace, Ctrl);
    INFO("Backspace takes the selection first, whatever the modifiers");
    REQUIRE(scene.Entry().GetValue() == "hello,  world");
    scene.Press(Key::Backspace, Scancode::Backspace, Ctrl);
    INFO("Ctrl+Backspace takes back to the start of the word before");
    REQUIRE(scene.Entry().GetValue() == " world");
}

TEST_CASE("Shift+End selects to the end, and typing replaces the selection", "[ui][textentry]")
{
    Scene scene;
    scene.Focus();
    scene.Entry().SetValue("abcdef");
    scene.Press(Key::Home, Scancode::Home);
    scene.Press(Key::Right, Scancode::Right);
    scene.Press(Key::Right, Scancode::Right);

    scene.Press(Key::End, Scancode::End, Shift);
    INFO("the anchor stays where the caret was and the caret goes to the end");
    REQUIRE(scene.Entry().GetAnchor() == 2);
    REQUIRE(scene.Entry().GetCaret() == 6);

    scene.TypeText(U"Z");
    INFO("the typed text replaces what was selected");
    REQUIRE(scene.Entry().GetValue() == "abZ");
    REQUIRE(scene.Entry().GetCaret() == 3);
    REQUIRE(scene.Entry().GetAnchor() == 3);

    scene.Press(Key::A, Scancode::A, Ctrl);
    INFO("Ctrl+A selects everything");
    REQUIRE(scene.Entry().GetAnchor() == 0);
    REQUIRE(scene.Entry().GetCaret() == 3);
    scene.Press(Key::Left, Scancode::Left);
    INFO("Left with a selection goes to its start");
    REQUIRE(scene.Entry().GetCaret() == 0);
    REQUIRE(scene.Entry().GetAnchor() == 0);
}

TEST_CASE("Ctrl+C then Ctrl+V duplicates through the clipboard", "[ui][textentry][display]")
{
    EntryScene<WindowUITestApp> scene;
    if (!scene.test.window.IsValid())
    {
        SKIP("SDL's video could not start (no display), so there is no clipboard");
    }
    scene.Focus();
    scene.Entry().SetValue("abc");

    scene.Press(Key::A, Scancode::A, Ctrl);
    scene.Press(Key::C, Scancode::C, Ctrl);
    INFO("Ctrl+C puts the selection on the clipboard");
    REQUIRE(Fenrir::Window::ClipboardText(scene.test.app) == "abc");
    scene.Press(Key::End, Scancode::End);
    scene.Press(Key::V, Scancode::V, Ctrl);
    INFO("Ctrl+V pastes it at the caret");
    REQUIRE(scene.Entry().GetValue() == "abcabc");
    REQUIRE(scene.Entry().GetCaret() == 6);
}

TEST_CASE("Ctrl+X puts the selection on the clipboard and takes it out", "[ui][textentry][display]")
{
    EntryScene<WindowUITestApp> scene;
    if (!scene.test.window.IsValid())
    {
        SKIP("SDL's video could not start (no display), so there is no clipboard");
    }
    scene.Focus();
    scene.Entry().SetValue("abc");

    scene.Press(Key::Left, Scancode::Left, Shift);
    scene.Press(Key::X, Scancode::X, Ctrl);
    INFO("the c is cut from the value and is what the clipboard holds");
    REQUIRE(scene.Entry().GetValue() == "ab");
    REQUIRE(Fenrir::Window::ClipboardText(scene.test.app) == "c");
}

TEST_CASE("Enter sends Submit with the value, and Escape blurs", "[ui][textentry]")
{
    Scene scene;
    std::vector<std::string> submitted;
    scene.root.AddListener(scene.entry, UIEventKind::Submit, [&](Fenrir::UIListenerContext&) {
        submitted.push_back(std::string(scene.Entry().GetValue()));
    });
    scene.Focus();
    scene.TypeText(U"Freya");

    scene.Press(Key::Enter, Scancode::Enter);
    INFO("Enter submits once, with the value as it was");
    REQUIRE(submitted == std::vector<std::string>{"Freya"});
    INFO("and keeps the focus");
    REQUIRE(scene.root.GetFocused() == scene.entry);

    scene.Press(Key::Escape, Scancode::Escape);
    INFO("Escape takes the focus away and submits nothing");
    REQUIRE_FALSE(scene.root.GetFocused().IsValid());
    REQUIRE(submitted.size() == 1);
}

TEST_CASE("While focused the root claims text input with the caret's rectangle inside the entry", "[ui][textentry]")
{
    Scene scene;
    const auto claims = [&] { return scene.test.app.GetResource<Fenrir::InputClaims>().For(testWindow); };
    INFO("unfocused, no text input is claimed");
    REQUIRE_FALSE(claims().textInput);

    scene.test.Click({scene.Content().x + 2.0f, scene.Content().y + scene.Content().height / 2.0f});
    scene.test.Frame();
    REQUIRE(scene.root.GetFocused() == scene.entry);
    scene.TypeText(U"abc");
    scene.test.Frame();
    INFO("focused by a click and typed in, it is, with the caret after the text");
    const Fenrir::WindowInputClaims focused = claims();
    REQUIRE(focused.textInput);
    const Fenrir::Rect2D border = scene.root.GetBoxes(scene.entry).border;
    REQUIRE(focused.caret.size.y > 0.0f);
    REQUIRE(focused.caret.position.x >= border.x);
    REQUIRE(focused.caret.position.y >= border.y);
    REQUIRE(focused.caret.position.x + focused.caret.size.x <= border.x + border.width);
    REQUIRE(focused.caret.position.y + focused.caret.size.y <= border.y + border.height);
    REQUIRE(Near(focused.caret.position.x, scene.At(3).x, 0.01f));

    scene.Press(Key::Escape, Scancode::Escape);
    INFO("blurred, it is not");
    REQUIRE_FALSE(claims().textInput);
}

TEST_CASE("The maximum length stops input past it", "[ui][textentry]")
{
    Scene scene;
    scene.Focus();
    scene.Entry().SetMaxLength(3);

    scene.TypeText(U"abcdef");
    INFO("only three graphemes go in");
    REQUIRE(scene.Entry().GetValue() == "abc");

    scene.Press(Key::Home, Scancode::Home);
    scene.TypeText(U"é");
    INFO("a grapheme that does not fit goes in neither whole nor in part");
    REQUIRE(scene.Entry().GetValue() == "abc");

    scene.Press(Key::Backspace, Scancode::Backspace);
    scene.Press(Key::End, Scancode::End);
    scene.Press(Key::Backspace, Scancode::Backspace);
    scene.TypeText(U"éz");
    INFO("with room for one again, one goes in");
    REQUIRE(scene.Entry().GetValue() == "ab\xC3\xA9");
}

TEST_CASE("A value set past the maximum length can be edited shorter but not longer", "[ui][textentry]")
{
    Scene scene;
    scene.Focus();
    scene.Entry().SetValue("abcdef");
    scene.Entry().SetMaxLength(3);

    scene.Press(Key::A, Scancode::A, Ctrl);
    scene.TypeText(U"xy");
    INFO("typing over the whole value replaces it, the result inside the limit");
    REQUIRE(scene.Entry().GetValue() == "xy");

    scene.Entry().SetValue("abcdef");
    scene.Entry().Select(0, 2);
    scene.TypeText(U"z");
    INFO("replacing two graphemes of an over-long value with one shortens it");
    REQUIRE(scene.Entry().GetValue() == "zcdef");
    scene.TypeText(U"w");
    INFO("but nothing makes it longer");
    REQUIRE(scene.Entry().GetValue() == "zcdef");
}

TEST_CASE("Password mode shows and measures a bullet for each grapheme", "[ui][textentry]")
{
    Scene scene;
    scene.Focus();
    scene.Entry().SetPassword(true);
    scene.TypeText(U"páss");
    scene.test.Frame();

    INFO("the value is what was typed");
    REQUIRE(scene.Entry().GetValue() == "pa\xCC\x81ss");
    const Fenrir::Paragraph& shown = scene.Entry().GetParagraph();
    INFO("the paragraph is four bullets, one for each grapheme");
    REQUIRE(shown.Text() == "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
    INFO("and the caret, at the end of the value, is drawn after the fourth");
    REQUIRE(Near(scene.test.app.GetResource<Fenrir::InputClaims>().For(testWindow).caret.position.x,
                 scene.Content().x + shown.Size().x, 0.01f));

    scene.Press(Key::Left, Scancode::Left, Ctrl);
    INFO("a password has no words to stop at: Ctrl+Left goes to the start");
    REQUIRE(scene.Entry().GetCaret() == 0);
}

TEST_CASE("A click focuses it and places the caret at the nearer edge of the character under it", "[ui][textentry]")
{
    Scene scene;
    scene.Entry().SetValue("hello brave world");
    scene.test.Frame();

    scene.test.Click(scene.At(2) + Vec2(scene.Advance() * 0.25f, 0.0f));
    scene.test.Frame();
    INFO("a quarter into the third character, the caret goes before it");
    REQUIRE(scene.root.GetFocused() == scene.entry);
    REQUIRE(scene.Entry().GetCaret() == 2);
    REQUIRE(scene.Entry().GetAnchor() == 2);
}

TEST_CASE("A drag selects from where it was pressed to where the pointer is", "[ui][textentry]")
{
    Scene scene;
    scene.Entry().SetValue("hello brave world");
    scene.test.Frame();

    scene.test.Feed().MouseMove(testWindow, scene.At(1), {0.0f, 0.0f});
    scene.test.Feed().MouseButtonDown(testWindow, MouseButton::Left);
    scene.test.Frame();
    scene.test.Feed().MouseMove(testWindow, scene.At(4) + Vec2(scene.Advance() * 0.75f, 0.0f), {0.0f, 0.0f});
    scene.test.Feed().MouseButtonUp(testWindow, MouseButton::Left);
    scene.test.Frame();
    INFO("pressed before the second character and released past the middle of the fifth, it selects 1 to 5");
    REQUIRE(scene.Entry().GetAnchor() == 1);
    REQUIRE(scene.Entry().GetCaret() == 5);
}

TEST_CASE("A double click selects the word under it", "[ui][textentry]")
{
    Scene scene;
    scene.Entry().SetValue("hello brave world");
    scene.test.Frame();

    // Two clicks a frame apart, in "brave".
    scene.test.Click(scene.At(8));
    scene.test.Frame();
    scene.test.Click(scene.At(8));
    scene.test.Frame();
    INFO("brave, bytes 6 to 11, is selected");
    REQUIRE(scene.Entry().GetAnchor() == 6);
    REQUIRE(scene.Entry().GetCaret() == 11);
}

TEST_CASE("The selection and caret draw where the paragraph puts them, in the sheet's colours", "[ui][textentry]")
{
    Scene scene("textentry { color: #00ff00; } textentry::selection { background-color: #ff0000; }");
    scene.Entry().SetValue("abcdef");
    scene.Focus();
    scene.Entry().Select(1, 3);
    scene.test.Frame();
    const float advance = scene.Advance();
    const Fenrir::Rect2D content = scene.Content();
    const Fenrir::Paragraph& paragraph = scene.Entry().GetParagraph();
    REQUIRE(paragraph.Lines().size() == 1);
    const float lineHeight = paragraph.Lines().front().height;
    const float lineTop = content.y + (content.height - lineHeight) / 2.0f;

    INFO("the selection of b and c is one red rectangle two characters wide from the second, a line tall");
    std::vector<PaintedRect> painted = Painted(scene.target->lists.back());
    const std::vector<PaintedRect> selection = OfColour(painted, Fenrir::ShapeKind::Rectangle, Rgba8(255, 0, 0, 255));
    REQUIRE(selection.size() == 1);
    REQUIRE(Near(selection[0].rect, {content.x + advance, lineTop, advance * 2.0f, lineHeight}, 0.01f));
    INFO("with a selection there is no caret");
    REQUIRE(OfColour(painted, Fenrir::ShapeKind::Rectangle, Rgba8(0, 255, 0, 255)).empty());

    scene.Entry().Select(4, 4);
    scene.test.Frame();
    painted = Painted(scene.target->lists.back());
    INFO("with none, the caret is a green bar in the text's colour before the fifth character, a line tall");
    const std::vector<PaintedRect> caret = OfColour(painted, Fenrir::ShapeKind::Rectangle, Rgba8(0, 255, 0, 255));
    REQUIRE(caret.size() == 1);
    REQUIRE(Near(caret[0].rect, {content.x + advance * 4.0f, lineTop, TextEntry::CaretWidth, lineHeight}, 0.01f));
    REQUIRE(OfColour(painted, Fenrir::ShapeKind::Rectangle, Rgba8(255, 0, 0, 255)).empty());
}

TEST_CASE("The caret blinks on FenrirTime from the last move", "[ui][textentry]")
{
    Scene scene("textentry { color: #00ff00; }");
    scene.Entry().SetValue("abcdef");
    scene.Focus();
    scene.test.Frame();
    const auto carets = [&] {
        return OfColour(Painted(scene.target->lists.back()), Fenrir::ShapeKind::Rectangle, Rgba8(0, 255, 0, 255))
            .size();
    };
    INFO("just focused, it is shown");
    REQUIRE(carets() == 1);
    scene.test.Frames(36, 1.0 / 60.0);
    INFO("past half a second it is hidden");
    REQUIRE(carets() == 0);
    scene.test.Frames(30, 1.0 / 60.0);
    INFO("past a second it is shown again");
    REQUIRE(carets() == 1);
    scene.test.Frames(36, 1.0 / 60.0);
    scene.Press(Key::Left, Scancode::Left);
    INFO("a move shows it at once, mid-blink");
    REQUIRE(carets() == 1);
}

TEST_CASE("The view scrolls to keep the caret inside the entry", "[ui][textentry]")
{
    Scene scene("textentry { color: #00ff00; }");
    scene.Focus();
    scene.TypeText(U"the quick brown fox jumps over the lazy dog");
    scene.test.Frame();
    const Fenrir::Rect2D content = scene.Content();
    INFO("the text is wider than the entry");
    REQUIRE(scene.Entry().GetParagraph().Size().x > content.width);

    const auto caret = [&] {
        const std::vector<PaintedRect> found =
            OfColour(Painted(scene.target->lists.back()), Fenrir::ShapeKind::Rectangle, Rgba8(0, 255, 0, 255));
        REQUIRE(found.size() == 1);
        return found.front().rect;
    };
    INFO("typed to the end, the caret is at the right edge of the content box");
    REQUIRE(Near(caret().x + caret().width, content.x + content.width, 0.01f));
    REQUIRE(scene.Entry().GetScroll() > 0.0f);

    scene.Press(Key::Home, Scancode::Home);
    INFO("at the start, the view scrolls back and the caret is at the left edge");
    REQUIRE(Near(caret().x, content.x, 0.01f));
    REQUIRE(scene.Entry().GetScroll() == 0.0f);
}

TEST_CASE("An empty entry shows its placeholder in its ::placeholder colour", "[ui][textentry]")
{
    Scene scene("textentry::placeholder { color: #ff00ff; }");
    scene.Entry().SetPlaceholder("Name");
    scene.test.Frame();

    const auto glyphs = [&] {
        return OfColour(Painted(scene.target->lists.back()), Fenrir::ShapeKind::Glyph, Rgba8(255, 0, 255, 255));
    };
    INFO("empty, the four glyphs of the placeholder are drawn in magenta");
    REQUIRE(glyphs().size() == 4);
    scene.Focus();
    scene.TypeText(U"x");
    INFO("with a value, it is not drawn");
    REQUIRE(glyphs().empty());
}
