#include "FenrirUI/Stylesheet.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <optional>
#include <utility>

#include "FenrirUI/UIRoot.hpp"
#include "StyleValues.hpp"

namespace Fenrir
{
    namespace
    {
        bool IsSpace(char c)
        {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
        }

        bool IsNameChar(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                   static_cast<unsigned char>(c) >= 0x80;
        }

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && IsSpace(text.front()))
            {
                text.remove_prefix(1);
            }
            while (!text.empty() && IsSpace(text.back()))
            {
                text.remove_suffix(1);
            }
            return text;
        }

        std::string Lower(std::string_view text)
        {
            std::string out(text);
            std::transform(out.begin(), out.end(), out.begin(),
                           [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
            return out;
        }

        /** @brief `text` cut at each top-level comma, keeping what is inside parentheses and strings whole. */
        std::vector<std::string_view> SplitCommas(std::string_view text)
        {
            std::vector<std::string_view> parts;
            int depth = 0;
            char quote = 0;
            std::size_t start = 0;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (quote != 0)
                {
                    quote = c == quote ? 0 : quote;
                }
                else if (c == '"' || c == '\'')
                {
                    quote = c;
                }
                else if (c == '(' || c == '[')
                {
                    ++depth;
                }
                else if (c == ')' || c == ']')
                {
                    --depth;
                }
                else if (c == ',' && depth == 0)
                {
                    parts.push_back(Trim(text.substr(start, i - start)));
                    start = i + 1;
                }
            }
            parts.push_back(Trim(text.substr(start)));
            return parts;
        }

        struct PseudoName
        {
            std::string_view name;
            std::uint16_t flag;
            std::uint8_t structural;
        };

        constexpr std::array<PseudoName, 11> PseudoClasses{{
            {"hover", static_cast<std::uint16_t>(PseudoClass::Hover), 0},
            {"active", static_cast<std::uint16_t>(PseudoClass::Active), 0},
            {"focus", static_cast<std::uint16_t>(PseudoClass::Focus), 0},
            {"focus-within", static_cast<std::uint16_t>(PseudoClass::FocusWithin), 0},
            {"disabled", static_cast<std::uint16_t>(PseudoClass::Disabled), 0},
            {"checked", static_cast<std::uint16_t>(PseudoClass::Checked), 0},
            {"intro", static_cast<std::uint16_t>(PseudoClass::Intro), 0},
            {"outro", static_cast<std::uint16_t>(PseudoClass::Outro), 0},
            {"first-child", 0, static_cast<std::uint8_t>(StructuralPseudoClass::FirstChild)},
            {"last-child", 0, static_cast<std::uint8_t>(StructuralPseudoClass::LastChild)},
            {"root", 0, static_cast<std::uint8_t>(StructuralPseudoClass::Root)},
        }};

        /** @brief Reads one complex selector; nothing, with `error` saying why, for one FenrirUI cannot match. */
        std::optional<Selector> ParseSelector(std::string_view text, std::string& error)
        {
            Selector selector;
            std::size_t i = 0;
            std::optional<SelectorCombinator> pending;
            const auto readName = [&]() {
                const std::size_t start = i;
                while (i < text.size() && IsNameChar(text[i]))
                {
                    ++i;
                }
                return text.substr(start, i - start);
            };
            while (i < text.size())
            {
                if (IsSpace(text[i]))
                {
                    ++i;
                    if (!selector.compounds.empty() && !pending)
                    {
                        pending = SelectorCombinator::Descendant;
                    }
                    continue;
                }
                if (text[i] == '>')
                {
                    if (selector.compounds.empty() || pending == SelectorCombinator::Child)
                    {
                        error = "a > has no compound selector before it";
                        return std::nullopt;
                    }
                    pending = SelectorCombinator::Child;
                    ++i;
                    continue;
                }
                CompoundSelector compound;
                const std::size_t compoundStart = i;
                if (IsNameChar(text[i]))
                {
                    compound.tag = std::string(readName());
                }
                while (i < text.size() && !IsSpace(text[i]) && text[i] != '>')
                {
                    const char kind = text[i++];
                    if (kind == ':' && i < text.size() && text[i] == ':')
                    {
                        error = "pseudo-elements (::) are not supported";
                        return std::nullopt;
                    }
                    if (kind != '.' && kind != '#' && kind != ':')
                    {
                        error = "'" + std::string(1, kind) + "' is not supported in a selector";
                        return std::nullopt;
                    }
                    const std::string_view name = readName();
                    if (name.empty())
                    {
                        error = "a '" + std::string(1, kind) + "' has no name after it";
                        return std::nullopt;
                    }
                    if (kind == '.')
                    {
                        compound.classes.emplace_back(name);
                    }
                    else if (kind == '#')
                    {
                        if (!compound.id.empty())
                        {
                            error = "a compound selector has two ids";
                            return std::nullopt;
                        }
                        compound.id = std::string(name);
                    }
                    else
                    {
                        const std::string lower = Lower(name);
                        const auto found = std::find_if(PseudoClasses.begin(), PseudoClasses.end(),
                                                        [&](const PseudoName& p) { return p.name == lower; });
                        if (found == PseudoClasses.end())
                        {
                            error = "':" + std::string(name) + "' is not a pseudo-class FenrirUI knows";
                            return std::nullopt;
                        }
                        compound.pseudoClasses = static_cast<std::uint16_t>(compound.pseudoClasses | found->flag);
                        compound.structural = static_cast<std::uint8_t>(compound.structural | found->structural);
                    }
                }
                if (i == compoundStart)
                {
                    error = "it is empty";
                    return std::nullopt;
                }
                if (!selector.compounds.empty())
                {
                    selector.combinators.push_back(pending.value_or(SelectorCombinator::Descendant));
                }
                pending.reset();
                selector.compounds.push_back(std::move(compound));
            }
            if (selector.compounds.empty())
            {
                error = "it is empty";
                return std::nullopt;
            }
            if (pending == SelectorCombinator::Child)
            {
                error = "a > has no compound selector after it";
                return std::nullopt;
            }
            std::uint32_t ids = 0;
            std::uint32_t classes = 0;
            std::uint32_t tags = 0;
            for (const CompoundSelector& compound : selector.compounds)
            {
                ids += compound.id.empty() ? 0u : 1u;
                classes += static_cast<std::uint32_t>(compound.classes.size()) +
                           static_cast<std::uint32_t>(std::popcount(compound.pseudoClasses)) +
                           static_cast<std::uint32_t>(std::popcount(compound.structural));
                tags += compound.tag.empty() ? 0u : 1u;
            }
            selector.specificity =
                (std::min(ids, 1023u) << 20u) | (std::min(classes, 1023u) << 10u) | std::min(tags, 1023u);
            return selector;
        }

        /** @brief Where a block's items are: what may be in it. */
        enum class BlockKind : std::uint8_t
        {
            TopLevel,
            Rule,
            /** @brief Declarations alone: a keyframe's block. */
            Declarations,
            /** @brief Descriptors, kept as written: a font face's block. */
            Descriptors,
            /** @brief Keyframe blocks: a @keyframes rule's. */
            Keyframes,
        };

        class Parser
        {
          public:
            Parser(std::string text, Stylesheet& sheet) : m_text(std::move(text)), m_sheet(sheet)
            {
                m_lineStarts.push_back(0);
                for (std::size_t i = 0; i < m_text.size(); ++i)
                {
                    if (m_text[i] == '\n')
                    {
                        m_lineStarts.push_back(i + 1);
                    }
                }
                BlankComments();
            }

            void Run()
            {
                ParseItems(BlockKind::TopLevel, {}, nullptr, nullptr);
            }

          private:
            std::uint32_t LineAt(std::size_t offset) const
            {
                const auto after = std::upper_bound(m_lineStarts.begin(), m_lineStarts.end(), offset);
                return static_cast<std::uint32_t>(after - m_lineStarts.begin());
            }

            void Diagnose(std::size_t offset, std::string message)
            {
                m_sheet.diagnostics.push_back({LineAt(offset), std::move(message)});
            }

            /** @brief Turns every comment into spaces, keeping its newlines, so offsets keep their lines. */
            void BlankComments()
            {
                char quote = 0;
                for (std::size_t i = 0; i < m_text.size(); ++i)
                {
                    const char c = m_text[i];
                    if (quote != 0)
                    {
                        quote = c == quote || c == '\n' ? 0 : quote;
                        continue;
                    }
                    if (c == '"' || c == '\'')
                    {
                        quote = c;
                        continue;
                    }
                    if (c != '/' || i + 1 >= m_text.size() || m_text[i + 1] != '*')
                    {
                        continue;
                    }
                    const std::size_t end = m_text.find("*/", i + 2);
                    if (end == std::string::npos)
                    {
                        Diagnose(i, "a comment is not closed: the sheet ends inside it");
                    }
                    const std::size_t stop = end == std::string::npos ? m_text.size() : end + 2;
                    for (std::size_t j = i; j < stop; ++j)
                    {
                        if (m_text[j] != '\n')
                        {
                            m_text[j] = ' ';
                        }
                    }
                    i = stop - 1;
                }
            }

            bool AtEnd() const
            {
                return m_pos >= m_text.size();
            }

            void SkipSpace()
            {
                while (!AtEnd() && IsSpace(m_text[m_pos]))
                {
                    ++m_pos;
                }
            }

            /** @brief Reads up to the next top-level `{`, `;` or `}`, which it leaves in place. */
            std::string_view ReadPrelude()
            {
                const std::size_t start = m_pos;
                int depth = 0;
                char quote = 0;
                for (; !AtEnd(); ++m_pos)
                {
                    const char c = m_text[m_pos];
                    if (quote != 0)
                    {
                        quote = c == quote ? 0 : quote;
                    }
                    else if (c == '"' || c == '\'')
                    {
                        quote = c;
                    }
                    else if (c == '(' || c == '[')
                    {
                        ++depth;
                    }
                    else if (c == ')' || c == ']')
                    {
                        depth = std::max(0, depth - 1);
                    }
                    else if (depth == 0 && (c == '{' || c == ';' || c == '}'))
                    {
                        break;
                    }
                }
                return std::string_view(m_text).substr(start, m_pos - start);
            }

            /** @brief From a `{`, past its matching `}`, or to the end. */
            void SkipBlock()
            {
                int depth = 0;
                char quote = 0;
                for (; !AtEnd(); ++m_pos)
                {
                    const char c = m_text[m_pos];
                    if (quote != 0)
                    {
                        quote = c == quote ? 0 : quote;
                    }
                    else if (c == '"' || c == '\'')
                    {
                        quote = c;
                    }
                    else if (c == '{')
                    {
                        ++depth;
                    }
                    else if (c == '}' && --depth == 0)
                    {
                        ++m_pos;
                        return;
                    }
                }
            }

            /** @brief From a `{`: its items, then past its `}`; a block the sheet ends inside is reported at `start`.
             */
            void ParseBlock(BlockKind kind, std::size_t start, std::string_view what,
                            const std::vector<std::string>& selectors, std::vector<StyleDeclaration>* declarations,
                            StyleKeyframes* keyframes)
            {
                ++m_pos;
                ParseItems(kind, selectors, declarations, keyframes);
                if (AtEnd())
                {
                    Diagnose(start, "the block of '" + std::string(what) + "' is not closed: the sheet ends inside it");
                    return;
                }
                ++m_pos;
            }

            void ParseItems(BlockKind kind, const std::vector<std::string>& selectors,
                            std::vector<StyleDeclaration>* declarations, StyleKeyframes* keyframes)
            {
                while (true)
                {
                    SkipSpace();
                    if (AtEnd())
                    {
                        return;
                    }
                    const char c = m_text[m_pos];
                    if (c == '}')
                    {
                        if (kind != BlockKind::TopLevel)
                        {
                            return;
                        }
                        Diagnose(m_pos, "a } closes no block");
                        ++m_pos;
                        continue;
                    }
                    if (c == ';')
                    {
                        ++m_pos;
                        continue;
                    }
                    const std::size_t start = m_pos;
                    if (c == '@')
                    {
                        if (kind == BlockKind::TopLevel)
                        {
                            ParseAtRule(start);
                        }
                        else
                        {
                            const std::string_view prelude = Trim(ReadPrelude());
                            Diagnose(start, "'" + std::string(prelude) + "' is not allowed inside a block");
                            SkipStatementOrBlock();
                        }
                        continue;
                    }
                    const std::string_view prelude = Trim(ReadPrelude());
                    const bool opensBlock = !AtEnd() && m_text[m_pos] == '{';
                    if (opensBlock)
                    {
                        if (kind == BlockKind::TopLevel || kind == BlockKind::Rule)
                        {
                            ParseRule(prelude, start, selectors);
                        }
                        else if (kind == BlockKind::Keyframes)
                        {
                            ParseKeyframe(prelude, start, *keyframes);
                        }
                        else
                        {
                            Diagnose(start, "the rule '" + std::string(prelude) + "' cannot be nested here");
                            SkipBlock();
                        }
                        continue;
                    }
                    if (!AtEnd() && m_text[m_pos] == ';')
                    {
                        ++m_pos;
                    }
                    if (declarations == nullptr)
                    {
                        Diagnose(start, "'" + std::string(prelude) + "' is not a rule: it has no block");
                        continue;
                    }
                    ParseDeclaration(prelude, start, kind == BlockKind::Descriptors, *declarations);
                }
            }

            void SkipStatementOrBlock()
            {
                if (!AtEnd() && m_text[m_pos] == '{')
                {
                    SkipBlock();
                }
                else if (!AtEnd() && m_text[m_pos] == ';')
                {
                    ++m_pos;
                }
            }

            void ParseRule(std::string_view prelude, std::size_t start, const std::vector<std::string>& parents)
            {
                std::vector<std::string> texts;
                for (const std::string_view part : SplitCommas(prelude))
                {
                    if (parents.empty())
                    {
                        texts.emplace_back(part);
                        continue;
                    }
                    // CSS nesting: & is the parent selector; a selector without one is relative to it.
                    for (const std::string& parent : parents)
                    {
                        if (part.find('&') == std::string_view::npos)
                        {
                            texts.push_back(parent + " " + std::string(part));
                            continue;
                        }
                        std::string flattened;
                        for (const char ch : part)
                        {
                            if (ch == '&')
                            {
                                flattened += parent;
                            }
                            else
                            {
                                flattened += ch;
                            }
                        }
                        texts.push_back(std::move(flattened));
                    }
                }
                StyleRule rule;
                rule.line = LineAt(start);
                std::vector<std::string> canonical;
                for (const std::string& text : texts)
                {
                    std::string error;
                    std::optional<Selector> selector = ParseSelector(text, error);
                    if (!selector)
                    {
                        Diagnose(start, "the rule '" + std::string(prelude) + "' was skipped: " + error);
                        SkipBlock();
                        return;
                    }
                    canonical.push_back(selector->ToString());
                    rule.selectors.push_back(std::move(*selector));
                }
                // The rule comes before the rules nested in it, which ParseBlock adds after it.
                const std::size_t index = m_sheet.rules.size();
                m_sheet.rules.push_back(std::move(rule));
                std::vector<StyleDeclaration> declarations;
                ParseBlock(BlockKind::Rule, start, prelude, canonical, &declarations, nullptr);
                m_sheet.rules[index].declarations = std::move(declarations);
            }

            void ParseKeyframe(std::string_view prelude, std::size_t start, StyleKeyframes& keyframes)
            {
                StyleKeyframe keyframe;
                for (const std::string_view part : SplitCommas(prelude))
                {
                    const std::string lower = Lower(part);
                    float percent = -1.0f;
                    if (lower == "from")
                    {
                        percent = 0.0f;
                    }
                    else if (lower == "to")
                    {
                        percent = 100.0f;
                    }
                    else if (!lower.empty() && lower.back() == '%')
                    {
                        const auto [end, error] =
                            std::from_chars(lower.data(), lower.data() + lower.size() - 1, percent);
                        if (error != std::errc{} || end != lower.data() + lower.size() - 1)
                        {
                            percent = -1.0f;
                        }
                    }
                    if (percent < 0.0f || percent > 100.0f)
                    {
                        Diagnose(start, "the keyframe '" + std::string(prelude) +
                                            "' was skipped: it is not from, to or a percentage from 0% to 100%");
                        SkipBlock();
                        return;
                    }
                    keyframe.offsets.push_back(percent / 100.0f);
                }
                ParseBlock(BlockKind::Declarations, start, prelude, {}, &keyframe.declarations, nullptr);
                keyframes.keyframes.push_back(std::move(keyframe));
            }

            void ParseAtRule(std::size_t start)
            {
                ++m_pos;
                const std::size_t nameStart = m_pos;
                while (!AtEnd() && IsNameChar(m_text[m_pos]))
                {
                    ++m_pos;
                }
                const std::string name = Lower(std::string_view(m_text).substr(nameStart, m_pos - nameStart));
                const std::string_view prelude = Trim(ReadPrelude());
                const bool opensBlock = !AtEnd() && m_text[m_pos] == '{';
                if (name == "import")
                {
                    if (!AtEnd() && m_text[m_pos] == ';')
                    {
                        ++m_pos;
                    }
                    std::string_view url = prelude;
                    if (url.size() >= 5 && Lower(url.substr(0, 4)) == "url(" && url.back() == ')')
                    {
                        url = Trim(url.substr(4, url.size() - 5));
                    }
                    if (url.size() >= 2 && (url.front() == '"' || url.front() == '\'') && url.back() == url.front())
                    {
                        url = url.substr(1, url.size() - 2);
                    }
                    if (url.empty() || opensBlock)
                    {
                        Diagnose(start, "the @import '" + std::string(prelude) + "' names no sheet");
                        SkipStatementOrBlock();
                        return;
                    }
                    m_sheet.imports.push_back({std::string(url), LineAt(start)});
                    return;
                }
                if (name == "keyframes" && opensBlock)
                {
                    std::string_view keyframesName = prelude;
                    if (keyframesName.size() >= 2 && (keyframesName.front() == '"' || keyframesName.front() == '\'') &&
                        keyframesName.back() == keyframesName.front())
                    {
                        keyframesName = keyframesName.substr(1, keyframesName.size() - 2);
                    }
                    if (keyframesName.empty() || !std::all_of(keyframesName.begin(), keyframesName.end(), IsNameChar))
                    {
                        Diagnose(start, "the @keyframes '" + std::string(prelude) + "' was skipped: it has no name");
                        SkipBlock();
                        return;
                    }
                    StyleKeyframes keyframes;
                    keyframes.name = std::string(keyframesName);
                    keyframes.line = LineAt(start);
                    ParseBlock(BlockKind::Keyframes, start, "@keyframes " + keyframes.name, {}, nullptr, &keyframes);
                    m_sheet.keyframes.push_back(std::move(keyframes));
                    return;
                }
                if (name == "font-face" && opensBlock && prelude.empty())
                {
                    StyleFontFace face;
                    face.line = LineAt(start);
                    ParseBlock(BlockKind::Descriptors, start, "@font-face", {}, &face.descriptors, nullptr);
                    m_sheet.fontFaces.push_back(std::move(face));
                    return;
                }
                Diagnose(start, "'@" + name + "' is not an at-rule FenrirUI supports; it was skipped");
                SkipStatementOrBlock();
            }

            /** @brief Adds a declaration, replacing an earlier one of the same name: a rule holds each once. */
            static void Add(std::vector<StyleDeclaration>& declarations, StyleDeclaration declaration)
            {
                std::erase_if(declarations, [&](const StyleDeclaration& d) { return d.name == declaration.name; });
                declarations.push_back(std::move(declaration));
            }

            void ParseDeclaration(std::string_view text, std::size_t start, bool descriptor,
                                  std::vector<StyleDeclaration>& declarations)
            {
                const std::uint32_t line = LineAt(start);
                const std::size_t colon = text.find(':');
                if (colon == std::string_view::npos || Trim(text.substr(0, colon)).empty())
                {
                    Diagnose(start,
                             "the declaration '" + std::string(text) + "' was skipped: it has no name and colon");
                    return;
                }
                const std::string_view rawName = Trim(text.substr(0, colon));
                const std::string_view value = Trim(text.substr(colon + 1));
                if (rawName.size() > 2 && rawName.substr(0, 2) == "--")
                {
                    // A custom property keeps its case and its value whole: it is read only where var() uses it.
                    Add(declarations, {std::string(rawName), std::string(value), line});
                    return;
                }
                const std::string name = Lower(rawName);
                if (value.empty())
                {
                    Diagnose(start, "the declaration '" + name + "' was skipped: it has no value");
                    return;
                }
                if (descriptor)
                {
                    Add(declarations, {name, std::string(value), line});
                    return;
                }
                const auto skip = [&](const std::string& why) {
                    Diagnose(start, "the declaration '" + name + ": " + std::string(value) + "' was skipped: " + why);
                };
                const Detail::PropertyKind kind = Detail::ClassifyProperty(name);
                if (kind == Detail::PropertyKind::Unknown)
                {
                    skip("'" + name + "' is not a property FenrirUI knows");
                    return;
                }
                if (value.find('!') != std::string_view::npos)
                {
                    skip("!important is not supported");
                    return;
                }
                if (Detail::ContainsVar(value))
                {
                    std::string error;
                    if (!Detail::CheckVarSyntax(value, error))
                    {
                        skip(error);
                        return;
                    }
                    // Its value is known only once the cascade has its custom properties, shorthand or not.
                    Add(declarations, {name, std::string(value), line});
                    return;
                }
                Detail::StyleParseContext context;
                context.checkOnly = true;
                StyleDeclarations scratch;
                if (!Detail::ParseStyleDeclaration(name, value, scratch, context))
                {
                    skip(context.error);
                    return;
                }
                if (kind != Detail::PropertyKind::Shorthand)
                {
                    Add(declarations, {name, std::string(value), line});
                    return;
                }
                std::vector<Detail::Longhand> longhands;
                Detail::ExpandShorthand(name, value, longhands, context);
                for (auto& [longhand, part] : longhands)
                {
                    Add(declarations, {std::move(longhand), std::move(part), line});
                }
            }

            std::string m_text;
            Stylesheet& m_sheet;
            std::vector<std::size_t> m_lineStarts;
            std::size_t m_pos = 0;
        };

        void AppendCompound(std::string& out, const CompoundSelector& compound)
        {
            out += compound.tag;
            if (!compound.id.empty())
            {
                out += '#';
                out += compound.id;
            }
            for (const std::string& name : compound.classes)
            {
                out += '.';
                out += name;
            }
            for (const PseudoName& pseudo : PseudoClasses)
            {
                if ((compound.pseudoClasses & pseudo.flag) != 0 || (compound.structural & pseudo.structural) != 0)
                {
                    out += ':';
                    out += pseudo.name;
                }
            }
        }
    } // namespace

    std::string Selector::ToString() const
    {
        std::string out;
        for (std::size_t i = 0; i < compounds.size(); ++i)
        {
            if (i > 0)
            {
                out += combinators[i - 1] == SelectorCombinator::Child ? " > " : " ";
            }
            AppendCompound(out, compounds[i]);
        }
        return out;
    }

    Stylesheet Stylesheet::Parse(std::string_view text, std::string_view name)
    {
        Stylesheet sheet;
        sheet.name = std::string(name);
        Parser parser(std::string(text), sheet);
        parser.Run();
        std::stable_sort(sheet.diagnostics.begin(), sheet.diagnostics.end(),
                         [](const StyleDiagnostic& a, const StyleDiagnostic& b) { return a.line < b.line; });
        return sheet;
    }
} // namespace Fenrir
