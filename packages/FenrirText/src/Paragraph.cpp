#include "FenrirText/Paragraph.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <utility>

#include <SheenBidi/SheenBidi.h>

#include "FenrirText/FontLibrary.hpp"
#include "FontLibraryState.hpp"
#include "Unicode.hpp"

namespace Fenrir
{
    namespace Detail
    {
        /** @brief One grapheme of a laid-out line, where the caret stops: its bytes and its extent on screen. */
        struct CaretSegment
        {
            std::uint32_t begin = 0;
            std::uint32_t end = 0;
            float x0 = 0.0f;
            float x1 = 0.0f;
            bool rightToLeft = false;
        };

        struct ParagraphLayout
        {
            bool dirty = true;
            bool laidOut = false;
            float width = 0.0f;
            std::uint64_t generation = 0;

            std::vector<LineMetrics> lines;
            std::vector<GlyphRun> runs;
            std::vector<std::vector<CaretSegment>> segments;
            Math::Vec2 size{0.0f};
        };
    } // namespace Detail

    namespace
    {
        // A glyph as HarfBuzz shaped it, in pixels.
        struct ShapedGlyph
        {
            std::uint32_t glyph = 0;
            std::uint32_t cluster = 0;
            float advance = 0.0f;
            float xOffset = 0.0f;
            float yOffset = 0.0f;
        };

        // A stretch of text of one style, bidi level, script and face, shaped as one: its glyphs in visual order.
        struct Item
        {
            std::uint32_t begin = 0;
            std::uint32_t end = 0;
            std::size_t style = 0;
            std::uint8_t level = 0;
            hb_script_t script = HB_SCRIPT_COMMON;
            Detail::SizedFace* face = nullptr;
            std::vector<ShapedGlyph> glyphs;
        };

        // A glyph placed on a line, before alignment.
        struct Placed
        {
            const ShapedGlyph* shaped = nullptr;
            const Item* item = nullptr;
            float x = 0.0f;
            float advance = 0.0f;
            bool hidden = false;
        };

        // Odd bidi levels read right to left (UAX#9).
        bool IsRightToLeft(std::uint8_t level)
        {
            return (level & 1u) != 0;
        }

        bool SameWidth(float a, float b)
        {
            return a == b || (std::isinf(a) && std::isinf(b));
        }

        std::vector<ShapedGlyph> Shape(hb_buffer_t* buffer, const std::string& text, std::uint32_t begin,
                                       std::uint32_t end, bool rightToLeft, hb_script_t script, float letterSpacing,
                                       Detail::SizedFace& face)
        {
            hb_buffer_clear_contents(buffer);
            hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), begin,
                               static_cast<int>(end - begin));
            hb_buffer_set_direction(buffer, rightToLeft ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
            if (script != HB_SCRIPT_COMMON && script != HB_SCRIPT_INHERITED && script != HB_SCRIPT_UNKNOWN)
            {
                hb_buffer_set_script(buffer, script);
            }
            hb_buffer_guess_segment_properties(buffer);
            // Letter spacing turns ligatures off, as CSS does.
            const hb_feature_t noLigatures[] = {
                {HB_TAG('l', 'i', 'g', 'a'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
                {HB_TAG('c', 'l', 'i', 'g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
                {HB_TAG('d', 'l', 'i', 'g'), 0, HB_FEATURE_GLOBAL_START, HB_FEATURE_GLOBAL_END},
            };
            const bool spaced = letterSpacing != 0.0f;
            hb_shape(face.shaper, buffer, spaced ? noLigatures : nullptr, spaced ? 3u : 0u);

            unsigned int count = 0;
            const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
            const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
            std::vector<ShapedGlyph> glyphs(count);
            for (unsigned int i = 0; i < count; ++i)
            {
                glyphs[i].glyph = infos[i].codepoint;
                glyphs[i].cluster = infos[i].cluster;
                // An SDF face shapes at its raster face's size: its positions are scaled to its own.
                glyphs[i].advance = static_cast<float>(positions[i].x_advance) / 64.0f * face.scale;
                glyphs[i].xOffset = static_cast<float>(positions[i].x_offset) / 64.0f * face.scale;
                glyphs[i].yOffset = static_cast<float>(positions[i].y_offset) / 64.0f * face.scale;
            }
            return glyphs;
        }
    } // namespace

    Paragraph::Paragraph() : m_graphemes{0}, m_layout(std::make_unique<Detail::ParagraphLayout>())
    {
    }

    Paragraph::~Paragraph() = default;
    Paragraph::Paragraph(Paragraph&&) noexcept = default;
    Paragraph& Paragraph::operator=(Paragraph&&) noexcept = default;

    void Paragraph::SetText(std::string_view utf8, const TextStyle& style)
    {
        m_text.clear();
        m_runs.clear();
        AddRun(utf8, style);
    }

    void Paragraph::AddRun(std::string_view utf8, const TextStyle& style)
    {
        const auto begin = static_cast<std::uint32_t>(m_text.size());
        m_text.append(utf8);
        m_runs.push_back({begin, static_cast<std::uint32_t>(m_text.size()), style});
        TextChanged();
    }

    void Paragraph::Clear()
    {
        m_text.clear();
        m_runs.clear();
        TextChanged();
    }

    void Paragraph::SetStyle(const ParagraphStyle& style)
    {
        if (!(style == m_style))
        {
            m_style = style;
            m_layout->dirty = true;
        }
    }

    const ParagraphStyle& Paragraph::Style() const noexcept
    {
        return m_style;
    }

    const std::string& Paragraph::Text() const noexcept
    {
        return m_text;
    }

    void Paragraph::TextChanged()
    {
        m_graphemes = Detail::GraphemeBoundaries(m_text);
        m_layout->dirty = true;
    }

    bool Paragraph::Layout(FontLibrary& fonts, float width)
    {
        Detail::ParagraphLayout& layout = *m_layout;
        Detail::FontLibraryState& library = fonts.State();
        if (!layout.dirty && layout.laidOut && layout.generation == library.generation &&
            SameWidth(layout.width, width))
        {
            return false;
        }
        layout.dirty = false;
        layout.laidOut = true;
        layout.generation = library.generation;
        layout.width = width;
        layout.lines.clear();
        layout.runs.clear();
        layout.segments.clear();
        layout.size = Math::Vec2{0.0f};

        const auto n = static_cast<std::uint32_t>(m_text.size());
        if (n == 0 || m_runs.empty())
        {
            return true;
        }
        const bool limited = std::isfinite(width);
        const bool collapse = m_style.whiteSpace == WhiteSpace::Normal || m_style.whiteSpace == WhiteSpace::NoWrap;
        const bool wrap =
            limited && (m_style.whiteSpace == WhiteSpace::Normal || m_style.whiteSpace == WhiteSpace::PreWrap);

        // White space: the text as shaped, of the same bytes, with the characters drawn as nothing marked hidden.
        // Collapsed white space reads as spaces, the first of a stretch kept; a kept newline breaks the line and is
        // never drawn; a tab is one space.
        std::string shaped = m_text;
        std::vector<bool> hidden(n, false);
        bool previousSpace = true;
        for (std::uint32_t i = 0; i < n;)
        {
            const Detail::DecodedCodepoint decoded = Detail::DecodeUtf8(m_text, i);
            const char32_t c = decoded.codepoint;
            if (collapse && Detail::IsCollapsibleSpace(c))
            {
                shaped[i] = ' ';
                hidden[i] = previousSpace;
                previousSpace = true;
            }
            else
            {
                if (c == U'\t')
                {
                    shaped[i] = ' ';
                }
                else if (c == U'\n' || c == U'\r' || c == U'\f')
                {
                    hidden[i] = true;
                }
                previousSpace = false;
            }
            i += decoded.length;
        }

        std::vector<std::size_t> styleAt(n, 0);
        for (std::size_t r = 0; r < m_runs.size(); ++r)
        {
            std::fill(styleAt.begin() + m_runs[r].begin, styleAt.begin() + m_runs[r].end, r);
        }

        // Bidi levels per byte, by paragraph (Unicode's P1: a kept newline ends one).
        SBCodepointSequence sequence{SBStringEncodingUTF8, shaped.data(), n};
        SBAlgorithmRef algorithm = SBAlgorithmCreate(&sequence);
        const SBLevel baseLevel = m_style.direction == TextDirection::LeftToRight   ? SBLevel{0}
                                  : m_style.direction == TextDirection::RightToLeft ? SBLevel{1}
                                                                                    : SBLevel{SBLevelDefaultLTR};
        std::vector<SBParagraphRef> bidiParagraphs;
        std::vector<std::uint8_t> levels(n, 0);
        std::vector<std::uint32_t> paragraphAt(n, 0);
        for (std::uint32_t offset = 0; offset < n;)
        {
            SBParagraphRef bidi = SBAlgorithmCreateParagraph(algorithm, offset, n - offset, baseLevel);
            const auto length = static_cast<std::uint32_t>(SBParagraphGetLength(bidi));
            const SBLevel* paragraphLevels = SBParagraphGetLevelsPtr(bidi);
            std::copy(paragraphLevels, paragraphLevels + length, levels.begin() + offset);
            std::fill(paragraphAt.begin() + offset, paragraphAt.begin() + std::min(n, offset + length),
                      static_cast<std::uint32_t>(bidiParagraphs.size()));
            bidiParagraphs.push_back(bidi);
            offset += std::max(length, 1u);
        }
        const auto paragraphOf = [&](std::uint32_t at) { return bidiParagraphs[paragraphAt[at]]; };

        // Scripts per byte, common and inherited characters taking their neighbours' (SheenBidi's script locator).
        std::vector<hb_script_t> scripts(n, HB_SCRIPT_COMMON);
        SBScriptLocatorRef locator = SBScriptLocatorCreate();
        SBScriptLocatorLoadCodepoints(locator, &sequence);
        while (SBScriptLocatorMoveNext(locator) != 0)
        {
            const SBScriptAgent* agent = SBScriptLocatorGetAgent(locator);
            const hb_script_t script = hb_script_from_iso15924_tag(SBScriptGetUnicodeTag(agent->script));
            std::fill(scripts.begin() + static_cast<std::ptrdiff_t>(agent->offset),
                      scripts.begin() + static_cast<std::ptrdiff_t>(agent->offset + agent->length), script);
        }
        SBScriptLocatorRelease(locator);

        // A face per grapheme: the first of its style's chain that has every character of it. White space keeps the
        // face before it, so a fallback run is not split at each space.
        std::map<std::size_t, std::vector<Detail::ChainFace>> chains;
        const auto chainOf = [&](std::size_t style) -> const std::vector<Detail::ChainFace>& {
            auto [found, added] = chains.try_emplace(style);
            if (added)
            {
                const TextStyle& s = m_runs[style].style;
                found->second = library.Chain(s.family, s.weight, s.style);
            }
            return found->second;
        };
        std::vector<Detail::SizedFace*> faceAt(n, nullptr);
        Detail::SizedFace* previousFace = nullptr;
        std::size_t previousStyle = 0;
        for (std::size_t g = 0; g + 1 < m_graphemes.size(); ++g)
        {
            const std::uint32_t begin = m_graphemes[g];
            const std::uint32_t end = m_graphemes[g + 1];
            const std::size_t style = styleAt[begin];
            const float size = m_runs[style].style.size;
            const auto covers = [&](const Detail::SizedFace& face) {
                for (std::uint32_t i = begin; i < end;)
                {
                    const Detail::DecodedCodepoint decoded = Detail::DecodeUtf8(shaped, i);
                    if (!hidden[i] && !Detail::IsDefaultIgnorable(decoded.codepoint) && !face.Has(decoded.codepoint))
                    {
                        return false;
                    }
                    i += decoded.length;
                }
                return true;
            };
            Detail::SizedFace* chosen = nullptr;
            const char32_t first = Detail::DecodeUtf8(shaped, begin).codepoint;
            if (previousFace != nullptr && previousStyle == style && (first == U' ' || hidden[begin]) &&
                covers(*previousFace))
            {
                chosen = previousFace;
            }
            Detail::SizedFace* firstHaving = nullptr;
            for (const Detail::ChainFace& candidate : chainOf(style))
            {
                if (chosen != nullptr)
                {
                    break;
                }
                Detail::SizedFace* face = library.Face(candidate, size);
                if (face == nullptr)
                {
                    continue;
                }
                if (covers(*face))
                {
                    chosen = face;
                }
                else if (firstHaving == nullptr && face->Has(first))
                {
                    firstHaving = face;
                }
            }
            if (chosen == nullptr)
            {
                chosen = firstHaving != nullptr ? firstHaving : library.Face(chainOf(style).back(), size);
            }
            std::fill(faceAt.begin() + begin, faceAt.begin() + end, chosen);
            previousFace = chosen;
            previousStyle = style;
        }

        // Items: split wherever the style, level, script, face or bidi paragraph changes.
        std::vector<Item> items;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            const bool split = items.empty() || styleAt[i] != items.back().style || levels[i] != items.back().level ||
                               scripts[i] != items.back().script || faceAt[i] != items.back().face ||
                               paragraphAt[i] != paragraphAt[items.back().begin];
            if (split)
            {
                items.push_back({i, i + 1, styleAt[i], levels[i], scripts[i], faceAt[i], {}});
            }
            else
            {
                items.back().end = i + 1;
            }
        }

        // Shaping, and each byte's share of the advances: what a range of text measures, in logical order.
        hb_buffer_t* buffer = hb_buffer_create();
        std::vector<float> advanceAt(n, 0.0f);
        std::vector<const Item*> itemAt(n, nullptr);
        for (Item& item : items)
        {
            std::fill(itemAt.begin() + item.begin, itemAt.begin() + item.end, &item);
            if (item.face == nullptr)
            {
                continue;
            }
            const TextStyle& style = m_runs[item.style].style;
            item.glyphs = Shape(buffer, shaped, item.begin, item.end, IsRightToLeft(item.level), item.script,
                                style.letterSpacing, *item.face);
            for (ShapedGlyph& glyph : item.glyphs)
            {
                if (hidden[glyph.cluster])
                {
                    glyph.advance = 0.0f;
                }
                else if (glyph.advance > 0.0f)
                {
                    glyph.advance += style.letterSpacing;
                }
                advanceAt[glyph.cluster] += glyph.advance;
            }
        }

        std::vector<float> prefix(n + 1, 0.0f);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            prefix[i + 1] = prefix[i] + advanceAt[i];
        }
        const auto isSpace = [&](std::uint32_t i) { return shaped[i] == ' ' || hidden[i]; };
        // Where a line's content ends: before its trailing spaces and newline, which hang past the width.
        const auto contentEnd = [&](std::uint32_t begin, std::uint32_t end) {
            while (end > begin && isSpace(end - 1))
            {
                --end;
            }
            return end;
        };
        const auto measure = [&](std::uint32_t begin, std::uint32_t end) {
            return prefix[contentEnd(begin, end)] - prefix[begin];
        };

        // Line breaking: greedy, at UAX#14's opportunities.
        const std::vector<Detail::BreakAfter> breaks = Detail::LineBreaks(shaped);
        std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
        std::uint32_t lineStart = 0;
        std::uint32_t lastOpportunity = 0;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            if (breaks[i] == Detail::BreakAfter::None)
            {
                continue;
            }
            const std::uint32_t at = i + 1;
            if (wrap && lastOpportunity > lineStart && measure(lineStart, at) > width)
            {
                ranges.emplace_back(lineStart, lastOpportunity);
                lineStart = lastOpportunity;
            }
            if (breaks[i] == Detail::BreakAfter::Mandatory)
            {
                ranges.emplace_back(lineStart, at);
                lineStart = at;
            }
            lastOpportunity = at;
        }
        bool cut = false;
        if (m_style.maxLines != 0 && ranges.size() > m_style.maxLines)
        {
            ranges.resize(m_style.maxLines);
            cut = true;
        }

        // The ellipsis, if the last line needs one: shaped in the face and style its text ends in.
        std::vector<ShapedGlyph> ellipsis;
        Item ellipsisItem;
        if (m_style.ellipsis && !ranges.empty())
        {
            auto& [begin, end] = ranges.back();
            if (cut || (limited && measure(begin, end) > width))
            {
                const std::uint32_t last = end > begin ? end - 1 : begin;
                ellipsisItem.style = styleAt[last];
                ellipsisItem.face = faceAt[last];
                ellipsisItem.level = SBParagraphGetBaseLevel(paragraphOf(begin));
                const TextStyle& style = m_runs[ellipsisItem.style].style;
                std::string mark = "\xE2\x80\xA6"; // U+2026 in UTF-8
                for (const Detail::ChainFace& candidate : chainOf(ellipsisItem.style))
                {
                    Detail::SizedFace* face = library.Face(candidate, style.size);
                    if (face != nullptr && face->Has(U'\u2026'))
                    {
                        ellipsisItem.face = face;
                        break;
                    }
                }
                if (ellipsisItem.face != nullptr && !ellipsisItem.face->Has(U'\u2026'))
                {
                    mark = "...";
                }
                if (ellipsisItem.face != nullptr)
                {
                    ellipsis = Shape(buffer, mark, 0, static_cast<std::uint32_t>(mark.size()),
                                     IsRightToLeft(ellipsisItem.level), HB_SCRIPT_COMMON, 0.0f, *ellipsisItem.face);
                }
                float ellipsisWidth = 0.0f;
                for (const ShapedGlyph& glyph : ellipsis)
                {
                    ellipsisWidth += glyph.advance;
                }
                // Keep the most graphemes that fit with it.
                if (limited)
                {
                    std::uint32_t keep = begin;
                    for (const std::uint32_t boundary : m_graphemes)
                    {
                        if (boundary > begin && boundary <= end && measure(begin, boundary) + ellipsisWidth <= width)
                        {
                            keep = boundary;
                        }
                    }
                    end = keep;
                }
                ellipsisItem.begin = end;
                ellipsisItem.end = end;
            }
        }
        hb_buffer_destroy(buffer);

        // Each line: its glyphs in visual order (SheenBidi's line runs, rule L1 and L2), its metrics, its caret stops.
        struct LaidLine
        {
            std::vector<Placed> glyphs;
            std::vector<Detail::CaretSegment> segments;
            float width = 0.0f;
            bool rightToLeft = false;
        };
        std::vector<LaidLine> laid;
        float widest = 0.0f;
        for (std::size_t l = 0; l < ranges.size(); ++l)
        {
            const auto [begin, end] = ranges[l];
            const std::uint32_t content = contentEnd(begin, end);
            LaidLine line;
            SBParagraphRef bidi = paragraphOf(begin);
            line.rightToLeft = IsRightToLeft(SBParagraphGetBaseLevel(bidi));
            float x = 0.0f;
            const auto placeEllipsis = [&] {
                for (const ShapedGlyph& glyph : ellipsis)
                {
                    line.glyphs.push_back({&glyph, &ellipsisItem, x, glyph.advance, false});
                    x += glyph.advance;
                }
            };
            const bool ellipsised = l + 1 == ranges.size() && !ellipsis.empty();
            if (ellipsised && line.rightToLeft)
            {
                placeEllipsis();
            }
            if (end > begin)
            {
                SBLineRef bidiLine = SBParagraphCreateLine(bidi, begin, end - begin);
                const SBUInteger runCount = SBLineGetRunCount(bidiLine);
                const SBRun* runs = SBLineGetRunsPtr(bidiLine);
                for (SBUInteger r = 0; r < runCount; ++r)
                {
                    const auto runBegin = static_cast<std::uint32_t>(runs[r].offset);
                    const auto runEnd = static_cast<std::uint32_t>(runs[r].offset + runs[r].length);
                    const bool rtl = IsRightToLeft(runs[r].level);
                    // The run's items, in visual order.
                    std::vector<const Item*> runItems;
                    for (const Item& item : items)
                    {
                        if (item.begin < runEnd && item.end > runBegin)
                        {
                            runItems.push_back(&item);
                        }
                    }
                    if (rtl)
                    {
                        std::ranges::reverse(runItems);
                    }
                    for (const Item* item : runItems)
                    {
                        const std::uint32_t from = std::max(item->begin, runBegin);
                        const std::uint32_t to = std::min(item->end, runEnd);
                        const std::size_t first = line.glyphs.size();
                        for (const ShapedGlyph& glyph : item->glyphs)
                        {
                            if (glyph.cluster < from || glyph.cluster >= to)
                            {
                                continue;
                            }
                            // Trailing spaces hang: they take no room.
                            const bool trailing = glyph.cluster >= content;
                            const bool none = trailing || hidden[glyph.cluster];
                            const float advance = trailing ? 0.0f : glyph.advance;
                            line.glyphs.push_back({&glyph, item, x, advance, none});
                            x += advance;
                        }
                        // Caret stops: each cluster's extent, shared evenly by the graphemes it holds.
                        std::vector<std::uint32_t> clusters;
                        for (std::size_t g = first; g < line.glyphs.size(); ++g)
                        {
                            clusters.push_back(line.glyphs[g].shaped->cluster);
                        }
                        std::ranges::sort(clusters);
                        clusters.erase(std::unique(clusters.begin(), clusters.end()), clusters.end());
                        std::vector<Detail::CaretSegment> pieces;
                        for (std::size_t c = 0; c < clusters.size(); ++c)
                        {
                            const std::uint32_t clusterEnd = c + 1 < clusters.size() ? clusters[c + 1] : to;
                            float x0 = 0.0f;
                            float x1 = 0.0f;
                            bool seen = false;
                            for (std::size_t g = first; g < line.glyphs.size(); ++g)
                            {
                                if (line.glyphs[g].shaped->cluster != clusters[c])
                                {
                                    continue;
                                }
                                const float left = line.glyphs[g].x;
                                const float right = left + line.glyphs[g].advance;
                                x0 = seen ? std::min(x0, left) : left;
                                x1 = seen ? std::max(x1, right) : right;
                                seen = true;
                            }
                            const auto from = std::ranges::lower_bound(m_graphemes, clusters[c]);
                            const auto upTo = std::ranges::lower_bound(m_graphemes, clusterEnd);
                            std::vector<std::uint32_t> inside(from, std::max(from, upTo));
                            if (inside.empty())
                            {
                                inside.push_back(clusters[c]);
                            }
                            const float share = (x1 - x0) / static_cast<float>(inside.size());
                            for (std::size_t k = 0; k < inside.size(); ++k)
                            {
                                const std::uint32_t graphemeEnd = k + 1 < inside.size() ? inside[k + 1] : clusterEnd;
                                // In right-to-left text the first grapheme is the rightmost.
                                const float a =
                                    rtl ? x1 - share * static_cast<float>(k + 1) : x0 + share * static_cast<float>(k);
                                pieces.push_back({inside[k], graphemeEnd, a, a + share, rtl});
                            }
                        }
                        std::ranges::sort(pieces, {}, [](const Detail::CaretSegment& s) { return s.x0; });
                        line.segments.insert(line.segments.end(), pieces.begin(), pieces.end());
                    }
                }
                SBLineRelease(bidiLine);
            }
            if (ellipsised && !line.rightToLeft)
            {
                placeEllipsis();
            }
            line.width = x;
            widest = std::max(widest, x);
            laid.push_back(std::move(line));
        }

        // Alignment, line boxes, and the glyphs rasterised into their atlases.
        const float alignWidth = limited ? width : widest;
        float top = 0.0f;
        for (std::size_t l = 0; l < laid.size(); ++l)
        {
            LaidLine& line = laid[l];
            const auto [begin, end] = ranges[l];
            TextAlign align = m_style.align;
            if (align == TextAlign::Start)
            {
                align = line.rightToLeft ? TextAlign::Right : TextAlign::Left;
            }
            else if (align == TextAlign::End)
            {
                align = line.rightToLeft ? TextAlign::Left : TextAlign::Right;
            }
            const float left = align == TextAlign::Right    ? alignWidth - line.width
                               : align == TextAlign::Center ? (alignWidth - line.width) / 2.0f
                                                            : 0.0f;

            // The line's faces: those of its glyphs, or of its first character's face when it has none.
            float ascent = 0.0f;
            float descent = 0.0f;
            float gap = 0.0f;
            float largest = 0.0f;
            const auto include = [&](const Detail::SizedFace* face, float size) {
                if (face != nullptr)
                {
                    ascent = std::max(ascent, face->ascent);
                    descent = std::max(descent, face->descent);
                    gap = std::max(gap, face->lineGap);
                }
                largest = std::max(largest, size);
            };
            for (const Placed& placed : line.glyphs)
            {
                include(placed.item->face, m_runs[placed.item->style].style.size);
            }
            if (line.glyphs.empty())
            {
                const std::uint32_t at = std::min(begin, n - 1);
                include(faceAt[at], m_runs[styleAt[at]].style.size);
            }
            const float height = m_style.lineHeight > 0.0f ? m_style.lineHeight * largest : ascent + descent + gap;
            const float baseline = top + (height - ascent - descent) / 2.0f + ascent;

            LineMetrics metrics;
            metrics.begin = begin;
            metrics.end = end;
            metrics.top = top;
            metrics.height = height;
            metrics.baseline = baseline;
            metrics.ascent = ascent;
            metrics.descent = descent;
            metrics.left = left;
            metrics.width = line.width;
            layout.lines.push_back(metrics);

            GlyphRun* run = nullptr;
            const Item* runItem = nullptr;
            for (const Placed& placed : line.glyphs)
            {
                const Item& item = *placed.item;
                const TextStyle& style = m_runs[item.style].style;
                const float penX = left + placed.x + placed.shaped->xOffset;
                const float quarterX = std::round(penX * 4.0f) / 4.0f;
                const float whole = std::floor(quarterX);
                const auto quarter = static_cast<std::uint32_t>((quarterX - whole) * 4.0f);
                const Math::Vec2 origin{quarterX, baseline - placed.shaped->yOffset};
                if (run == nullptr || runItem != &item)
                {
                    run = nullptr;
                }
                if (placed.hidden || item.face == nullptr)
                {
                    // Spaces draw nothing, but a run they fall in reaches over them, for its decoration.
                    if (run != nullptr)
                    {
                        run->width = left + placed.x + placed.advance - run->left;
                    }
                    continue;
                }
                const Detail::RasterisedGlyph& glyph = library.Glyph(*item.face, placed.shaped->glyph, quarter);
                if (!glyph.atlas)
                {
                    continue;
                }
                const Detail::SizedFace& raster = item.face->Raster();
                const AssetHandle<Image> page = raster.atlas.Pages()[glyph.atlas->page];
                const bool sdf = item.face->rendering == FontRendering::Sdf;
                if (run == nullptr || run->page != page)
                {
                    GlyphRun next;
                    next.font = item.face->font;
                    next.size = item.face->size;
                    next.page = page;
                    next.rendering = item.face->rendering;
                    next.sdfSpread = sdf ? static_cast<float>(library.config.sdfSpread) : 0.0f;
                    next.colour = style.colour;
                    next.decoration = style.decoration;
                    next.line = static_cast<std::uint32_t>(l);
                    next.rightToLeft = IsRightToLeft(item.level);
                    next.left = left + placed.x;
                    next.baseline = baseline;
                    next.underlineOffset = item.face->underlineOffset;
                    next.underlineThickness = item.face->underlineThickness;
                    next.ascent = item.face->ascent;
                    layout.runs.push_back(std::move(next));
                    run = &layout.runs.back();
                    runItem = &item;
                }
                run->width = left + placed.x + placed.advance - run->left;
                PositionedGlyph positioned;
                positioned.glyph = placed.shaped->glyph;
                positioned.cluster = &item == &ellipsisItem ? ellipsisItem.begin : placed.shaped->cluster;
                positioned.origin = origin;
                const float regionWidth = static_cast<float>(glyph.atlas->region.width);
                const float regionHeight = static_cast<float>(glyph.atlas->region.height);
                if (sdf)
                {
                    // A distance field scales: its bitmap, bearing and spread from the raster face's size to this
                    // one's, at the pen position as shaped, unrounded.
                    const float scale = item.face->scale;
                    const Math::Vec2 pen{penX, origin.y};
                    positioned.origin = pen;
                    positioned.quad = {pen.x + static_cast<float>(glyph.left) * scale,
                                       pen.y - static_cast<float>(glyph.top) * scale, regionWidth * scale,
                                       regionHeight * scale};
                }
                else
                {
                    positioned.quad = {whole + static_cast<float>(glyph.left),
                                       std::round(origin.y) - static_cast<float>(glyph.top), regionWidth, regionHeight};
                }
                positioned.atlas = *glyph.atlas;
                run->glyphs.push_back(positioned);
            }

            for (Detail::CaretSegment& segment : line.segments)
            {
                segment.x0 += left;
                segment.x1 += left;
            }
            layout.segments.push_back(std::move(line.segments));
            top += height;
        }
        layout.size = Math::Vec2{widest, top};

        for (SBParagraphRef bidi : bidiParagraphs)
        {
            SBParagraphRelease(bidi);
        }
        SBAlgorithmRelease(algorithm);
        return true;
    }

    Math::Vec2 Paragraph::Size() const noexcept
    {
        return m_layout->size;
    }

    std::span<const LineMetrics> Paragraph::Lines() const noexcept
    {
        return m_layout->lines;
    }

    std::span<const GlyphRun> Paragraph::Runs() const noexcept
    {
        return m_layout->runs;
    }

    std::vector<Rect2D> Paragraph::RangeRects(std::uint32_t begin, std::uint32_t end) const
    {
        std::vector<Rect2D> rects;
        if (begin >= end)
        {
            return rects;
        }
        for (std::size_t l = 0; l < m_layout->lines.size(); ++l)
        {
            const LineMetrics& line = m_layout->lines[l];
            bool extending = false;
            for (const Detail::CaretSegment& segment : m_layout->segments[l])
            {
                const bool inside = segment.begin < end && segment.end > begin;
                if (!inside)
                {
                    // A zero-width stop (a collapsed space) neither joins nor breaks the box.
                    extending = extending && segment.x1 <= segment.x0;
                    continue;
                }
                if (segment.x1 <= segment.x0)
                {
                    continue;
                }
                if (extending && std::abs(rects.back().x + rects.back().width - segment.x0) < 0.01f)
                {
                    rects.back().width = segment.x1 - rects.back().x;
                }
                else
                {
                    rects.push_back({segment.x0, line.top, segment.x1 - segment.x0, line.height});
                }
                extending = true;
            }
        }
        return rects;
    }

    std::uint32_t Paragraph::CharacterAt(Math::Vec2 point) const
    {
        const auto& lines = m_layout->lines;
        if (lines.empty())
        {
            return 0;
        }
        std::size_t l = 0;
        while (l + 1 < lines.size() && point.y >= lines[l].top + lines[l].height)
        {
            ++l;
        }
        std::vector<Detail::CaretSegment> segments;
        for (const Detail::CaretSegment& segment : m_layout->segments[l])
        {
            if (segment.x1 > segment.x0)
            {
                segments.push_back(segment);
            }
        }
        if (segments.empty())
        {
            return lines[l].begin;
        }
        const Detail::CaretSegment& leftmost = segments.front();
        const Detail::CaretSegment& rightmost = segments.back();
        if (point.x < leftmost.x0)
        {
            return leftmost.rightToLeft ? leftmost.end : leftmost.begin;
        }
        if (point.x >= rightmost.x1)
        {
            return rightmost.rightToLeft ? rightmost.begin : rightmost.end;
        }
        for (const Detail::CaretSegment& segment : segments)
        {
            if (point.x >= segment.x0 && point.x < segment.x1)
            {
                const bool firstHalf = point.x < (segment.x0 + segment.x1) / 2.0f;
                return firstHalf != segment.rightToLeft ? segment.begin : segment.end;
            }
        }
        return rightmost.rightToLeft ? rightmost.begin : rightmost.end;
    }

    std::span<const std::uint32_t> Paragraph::GraphemeBoundaries() const noexcept
    {
        return m_graphemes;
    }

    std::uint32_t Paragraph::NextGrapheme(std::uint32_t index) const
    {
        const auto next = std::upper_bound(m_graphemes.begin(), m_graphemes.end(), index);
        return next == m_graphemes.end() ? static_cast<std::uint32_t>(m_text.size()) : *next;
    }

    std::uint32_t Paragraph::PreviousGrapheme(std::uint32_t index) const
    {
        const auto at = std::lower_bound(m_graphemes.begin(), m_graphemes.end(), index);
        return at == m_graphemes.begin() ? 0 : *(at - 1);
    }
} // namespace Fenrir
