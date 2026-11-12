#include "FenrirTextScripting/TextScripting.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirDraw2D/DrawList2D.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "FenrirScripting/NativeMarshalling.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirText/FontLibrary.hpp"
#include "FenrirText/Paragraph.hpp"
#include "FenrirText/TextStyle.hpp"
#include "FenrirTextScripting/ScriptText.hpp"
#include "FenrirTextScripting/TextScriptingState.hpp"
#include "Generated/FenrirTextNatives.hpp"

// Everything C# sees of text is generated (api/FenrirText.binding.toml): the mirrors of ScriptText.hpp's structs and
// of FenrirText's plain data, their mappings, checked against the Fenrir.Text contract whenever a script assembly
// loads, and from the native functions below (described into api/FenrirText.natives.json) their C# declarations,
// TextNative, the Fonts facade and the Paragraph mirror's methods, and this registration of them all.

namespace Fenrir
{
    namespace
    {
        /** @brief FenrirText's TextStyle for C#'s, in `family`. */
        TextStyle StyleOf(std::string_view family, const ScriptText::TextStyle& style)
        {
            return {.family = std::string(family),
                    .size = style.size,
                    .weight = style.weight,
                    .style = style.style,
                    .colour = style.colour,
                    .letterSpacing = style.letterSpacing,
                    .decoration = style.decoration};
        }

        /** @brief Copies as many of `values` as fit into the `count` from `out`; returns how many there are. */
        template <typename T>
        std::int32_t CopyOut(std::span<const T> values, T* out, std::int32_t count)
        {
            const std::span<T> room = SpanOf(out, count);
            const std::size_t copied = std::min(room.size(), values.size());
            std::copy_n(values.begin(), copied, room.begin());
            return static_cast<std::int32_t>(values.size());
        }
    } // namespace

    // The native functions, named apart from FenrirText's own. Each acts on the app's FontLibrary or on a paragraph
    // the extension keeps, as the FenrirText call of its name does.
    namespace TextNatives
    {
        /**
         * @brief Registers the font file at `path` (TrueType or OpenType, loaded once as a Font asset the app owns)
         * as the face of `family` at `weight` and `style` in the app's FontLibrary, replacing any face registered
         * there (FontLibrary::AddFace). Paragraphs laid out in the library lay out again on their next Layout.
         * @param family The family's name, as text styles name it.
         * @param path The font file: relative to the asset root, or absolute.
         * @param weight The CSS weight, 100 to 900; 400 is normal and 700 bold.
         * @param style Upright or italic.
         * @return Whether the face was registered: false when the file does not load, or the app has no FontLibrary or
         * AssetStore.
         * @facade Fonts.AddFace
         */
        bool AddFace(const CallContext& context, TextScripting& text, std::string_view family, std::string_view path,
                     std::uint16_t weight, FontStyle style)
        {
            FontLibrary* fonts = text.Fonts(context);
            if (fonts == nullptr)
            {
                return false;
            }
            const NativeGuard guard(context, text.warnings);
            AssetStore* store =
                guard.Require(context.GetApp().TryGetResource<AssetStore>(), std::string(TextScripting::NoAssetStore));
            if (store == nullptr)
            {
                return false;
            }
            const LoadResult<Font> font = store->Load<Font>(path, AssetOwner::App());
            if (!font.Ok())
            {
                guard.Warn(std::string(TextScripting::FontNotLoaded));
                return false;
            }
            fonts->AddFace(family, font.handle, weight, style);
            return true;
        }

        /**
         * @brief A new paragraph with no text, in the default paragraph style, kept until it is destroyed.
         * @param[out] paragraph The paragraph made.
         * @return The paragraph made.
         * @facade Paragraph.Create returns paragraph
         */
        void CreateParagraph(const CallContext& context, TextScripting& text, ScriptText::Paragraph* paragraph)
        {
            static_cast<void>(context);
            const std::uint64_t id = text.nextId++;
            text.paragraphs.emplace(id, Fenrir::Paragraph());
            paragraph->id = id;
        }

        /**
         * @brief Destroys the paragraph: its calls do nothing from now on, and its id is never handed out again.
         * @return Whether there was a paragraph to destroy.
         * @facade Paragraph.Destroy
         */
        bool DestroyParagraph(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph)
        {
            if (text.Find(context, *paragraph) == nullptr)
            {
                return false;
            }
            text.paragraphs.erase(paragraph->id);
            return true;
        }

        /**
         * @brief Makes `utf8` in `style`, in the family `family`, the paragraph's whole text (Paragraph::SetText).
         * @param utf8 The text.
         * @param family The font family's name, as registered with Fonts.AddFace; one with no faces is the default
         * face.
         * @param style How the text looks.
         * @return Whether the paragraph exists.
         * @facade Paragraph.SetText
         */
        bool SetText(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph,
                     std::string_view utf8, std::string_view family, const ScriptText::TextStyle* style)
        {
            Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return false;
            }
            found->SetText(utf8, StyleOf(family, *style));
            return true;
        }

        /**
         * @brief Appends `utf8` in `style`, in the family `family`, to the paragraph's text (Paragraph::AddRun).
         * @param utf8 The text.
         * @param family The font family's name, as registered with Fonts.AddFace.
         * @param style How the text looks.
         * @return Whether the paragraph exists.
         * @facade Paragraph.AddRun
         */
        bool AddRun(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph,
                    std::string_view utf8, std::string_view family, const ScriptText::TextStyle* style)
        {
            Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return false;
            }
            found->AddRun(utf8, StyleOf(family, *style));
            return true;
        }

        /**
         * @brief Removes all the paragraph's text (Paragraph::Clear).
         * @return Whether the paragraph exists.
         * @facade Paragraph.Clear
         */
        bool ClearParagraph(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph)
        {
            Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return false;
            }
            found->Clear();
            return true;
        }

        /**
         * @brief Sets how the paragraph's lines are laid out (Paragraph::SetStyle); the next Layout lays out again if
         * it differs.
         * @param style The alignment, line height, line limit, ellipsis, white space and direction.
         * @return Whether the paragraph exists.
         * @facade Paragraph.SetStyle
         */
        bool SetStyle(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph,
                      const ParagraphStyle* style)
        {
            Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return false;
            }
            found->SetStyle(*style);
            return true;
        }

        /**
         * @brief Lays the paragraph out to `width` pixels (infinity for no limit) with the app's FontLibrary, unless
         * nothing changed since it last did (Paragraph::Layout).
         * @param width The width its lines wrap at, in pixels.
         * @return Whether it laid out: false when nothing changed, or the paragraph or the FontLibrary is missing.
         * @facade Paragraph.Layout
         */
        bool Layout(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph,
                    float width)
        {
            Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return false;
            }
            FontLibrary* fonts = text.Fonts(context);
            if (fonts == nullptr)
            {
                return false;
            }
            return found->Layout(*fonts, width);
        }

        /**
         * @brief The laid-out text's extent: its widest line's width and its lines' height, in pixels
         * (Paragraph::Size).
         * @param[out] size The extent; zero before Layout.
         * @return The extent, zero for a paragraph that does not exist.
         * @facade Paragraph.Size returns size
         */
        bool Size(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph,
                  Math::Vec2* size)
        {
            const Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return false;
            }
            *size = found->Size();
            return true;
        }

        /**
         * @brief Writes the paragraph's laid-out lines, top to bottom, into `lines`, as many as fit
         * (Paragraph::Lines).
         * @param lines Where the lines are written.
         * @return How many lines the paragraph has: zero before Layout, for no text, or for a paragraph that does not
         * exist.
         * @facade Paragraph.Lines
         */
        std::int32_t Lines(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph,
                           LineMetrics* lines, std::int32_t count)
        {
            const Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return 0;
            }
            return CopyOut(found->Lines(), lines, count);
        }

        /**
         * @brief The caret position nearest `point`, a byte offset into the paragraph's UTF-8 text
         * (Paragraph::CharacterAt): the grapheme boundary at the leading edge of the character under the point, or at
         * its trailing edge past its middle.
         * @param point Pixels from the paragraph's top-left.
         * @return The byte offset; zero for a paragraph that does not exist.
         * @facade Paragraph.CharacterAt
         */
        std::uint32_t CharacterAt(const CallContext& context, TextScripting& text,
                                  const ScriptText::Paragraph* paragraph, const Math::Vec2* point)
        {
            const Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return 0;
            }
            return found->CharacterAt(*point);
        }

        /**
         * @brief Writes the rectangles covering the characters in the byte range [`begin`, `end`) into `rects`, as many
         * as fit (Paragraph::RangeRects): one per line and run of the range that is contiguous on screen, each a line
         * box tall, in pixels from the paragraph's top-left.
         * @param begin The range's first byte offset.
         * @param end The byte offset past its last.
         * @param rects Where the rectangles are written.
         * @return How many rectangles cover the range: zero for an empty range, before Layout, or for a paragraph that
         * does not exist.
         * @facade Paragraph.RangeRects
         */
        std::int32_t RangeRects(const CallContext& context, TextScripting& text, const ScriptText::Paragraph* paragraph,
                                std::uint32_t begin, std::uint32_t end, Rect2D* rects, std::int32_t count)
        {
            const Fenrir::Paragraph* found = text.Find(context, *paragraph);
            if (found == nullptr)
            {
                return 0;
            }
            const std::vector<Rect2D> covering = found->RangeRects(begin, end);
            return CopyOut(std::span<const Rect2D>(covering), rects, count);
        }
    } // namespace TextNatives

    Fenrir::ScriptingExtension Text::ScriptingExtension()
    {
        return Generated::MakeTextExtension(std::make_shared<TextScripting>());
    }
} // namespace Fenrir
