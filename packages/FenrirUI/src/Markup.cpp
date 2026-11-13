// Markup documents (ADR-0064): a parser of FenrirUI's own for the XML-like grammar into a tree of nodes, then a loader
// that makes the nodes' elements in a root through the tag registry. Parsing first and building after keeps the
// grammar's recovery (tags left open, closing tags that close none) apart from what the elements make of it.
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "AttributeValues.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirUI/UI.hpp"
#include "FenrirUI/UIRoot.hpp"
#include "StyleFiles.hpp"
#include "StyleValues.hpp"
#include "UIRootState.hpp"

namespace Fenrir
{
    namespace
    {
        bool IsMarkupNameStart(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        // A colon reads as part of a name, so a namespaced tag or attribute is refused as unknown, never split.
        bool IsMarkupNameChar(char c)
        {
            return IsMarkupNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == ':';
        }

        void AppendUtf8(std::string& out, std::uint32_t code)
        {
            if (code < 0x80)
            {
                out += static_cast<char>(code);
            }
            else if (code < 0x800)
            {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
            else if (code < 0x10000)
            {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
        }

        /** @brief The text an entity's name (between `&` and `;`) stands for; false for no entity. */
        bool DecodeEntity(std::string_view name, std::string& out)
        {
            if (name == "amp")
            {
                out += '&';
            }
            else if (name == "lt")
            {
                out += '<';
            }
            else if (name == "gt")
            {
                out += '>';
            }
            else if (name == "quot")
            {
                out += '"';
            }
            else if (name == "apos")
            {
                out += '\'';
            }
            else if (name.starts_with('#'))
            {
                std::string_view digits = name.substr(1);
                int base = 10;
                if (digits.starts_with('x') || digits.starts_with('X'))
                {
                    digits.remove_prefix(1);
                    base = 16;
                }
                std::uint32_t code = 0;
                const char* end = digits.data() + digits.size();
                const auto [at, error] = std::from_chars(digits.data(), end, code, base);
                if (digits.empty() || error != std::errc() || at != end || code == 0 || code > 0x10FFFF ||
                    (code >= 0xD800 && code <= 0xDFFF))
                {
                    return false;
                }
                AppendUtf8(out, code);
            }
            else
            {
                return false;
            }
            return true;
        }

        /** @brief An attribute as written, its value's entities decoded. */
        struct MarkupAttribute
        {
            std::string name;
            std::string value;
            std::uint32_t line = 0;
            /** @brief The line its value starts on. */
            std::uint32_t valueLine = 0;
        };

        /** @brief One element tag of a document with what is in it, or one run of text between tags. */
        struct MarkupNode
        {
            bool isText = false;
            std::string tag;
            /** @brief The tag's line, or the line of a text run's first character that is not whitespace. */
            std::uint32_t line = 0;
            std::vector<MarkupAttribute> attributes;
            std::vector<MarkupNode> children;
            /** @brief A text run's decoded text, or an inline `<style>`'s CSS as written. */
            std::string text;
            /** @brief The line an inline `<style>`'s CSS starts on. */
            std::uint32_t contentLine = 0;
        };

        using Detail::MarkupErrors;
        using Detail::OrderedError;

        void AddError(MarkupErrors& errors, std::string_view file, std::uint32_t line, std::string message)
        {
            errors.push_back({line, MarkupError{std::string(file), line, std::move(message)}});
        }

        /** @brief A document being loaded: its name, and the errors found in it so far. */
        struct MarkupDocument
        {
            std::string_view file;
            MarkupErrors errors;

            void Error(std::uint32_t line, std::string message)
            {
                AddError(errors, file, line, std::move(message));
            }

            /** @brief The error for an attribute its element did not take, when it did not. */
            void Refused(std::uint32_t line, AttributeResult result, const std::string& tag, std::string_view name,
                         std::string_view value)
            {
                if (result == AttributeResult::Unknown)
                {
                    Error(line, "<" + tag + "> has no attribute '" + std::string(name) + "'");
                }
                else if (result == AttributeResult::Invalid)
                {
                    Error(line,
                          "<" + tag + "> cannot read '" + std::string(value) + "' as its '" + std::string(name) + "'");
                }
            }
        };

        /**
         * @brief Reads a document into nodes. It never fails as a whole: each thing it refuses is an error with its
         * line, and it reads on after it.
         */
        class MarkupParser
        {
          public:
            MarkupParser(std::string_view text, std::string_view file, MarkupErrors& errors)
                : m_text(text), m_file(file), m_errors(errors)
            {
            }

            /** @brief The document's top-level nodes. */
            std::vector<MarkupNode> Parse()
            {
                MarkupNode top;
                // The tags open now, innermost last. Only the innermost gains children, so these stay valid.
                std::vector<MarkupNode*> open{&top};
                if (StartsWith("\xEF\xBB\xBF"))
                {
                    m_at = 3;
                }
                while (m_at < m_text.size())
                {
                    if (m_text[m_at] != '<')
                    {
                        ParseText(*open.back());
                    }
                    else if (StartsWith("<!--"))
                    {
                        const std::uint32_t line = m_line;
                        Advance(4);
                        if (!SkipPast("-->"))
                        {
                            Error(line, "a comment is not closed with '-->'");
                        }
                    }
                    else if (StartsWith("<?"))
                    {
                        Error(m_line, "processing instructions are not markup");
                        SkipPast("?>");
                    }
                    else if (StartsWith("<![CDATA["))
                    {
                        Error(m_line, "CDATA sections are not markup: write the text with entities");
                        SkipPast("]]>");
                    }
                    else if (StartsWith("<!"))
                    {
                        Error(m_line, "DTDs and declarations are not markup");
                        SkipPast(">");
                    }
                    else if (StartsWith("</"))
                    {
                        ParseEnd(open);
                    }
                    else
                    {
                        ParseStart(open);
                    }
                }
                for (std::size_t i = open.size(); i-- > 1;)
                {
                    NotClosed(*open[i]);
                }
                return std::move(top.children);
            }

          private:
            static constexpr std::size_t MaxEntityLength = 32;

            void Error(std::uint32_t line, std::string message)
            {
                AddError(m_errors, m_file, line, std::move(message));
            }

            void NotClosed(const MarkupNode& node)
            {
                Error(node.line, "<" + node.tag + "> is not closed");
            }

            [[nodiscard]] bool StartsWith(std::string_view prefix) const
            {
                return m_text.substr(m_at).starts_with(prefix);
            }

            void Advance(std::size_t count)
            {
                for (const std::size_t end = std::min(m_at + count, m_text.size()); m_at < end; ++m_at)
                {
                    if (m_text[m_at] == '\n')
                    {
                        ++m_line;
                    }
                }
            }

            /** @brief Moves past the next `end`; false, at the end of the text, when there is none. */
            bool SkipPast(std::string_view end)
            {
                const std::size_t found = m_text.find(end, m_at);
                if (found == std::string_view::npos)
                {
                    Advance(m_text.size() - m_at);
                    return false;
                }
                Advance(found + end.size() - m_at);
                return true;
            }

            void SkipSpace()
            {
                while (m_at < m_text.size() && Detail::IsSpace(m_text[m_at]))
                {
                    Advance(1);
                }
            }

            std::string ReadName()
            {
                const std::size_t start = m_at;
                if (m_at < m_text.size() && IsMarkupNameStart(m_text[m_at]))
                {
                    while (m_at < m_text.size() && IsMarkupNameChar(m_text[m_at]))
                    {
                        ++m_at;
                    }
                }
                return std::string(m_text.substr(start, m_at - start));
            }

            /** @brief `raw` with its entities decoded; `line` is the line it starts on. */
            std::string Decode(std::string_view raw, std::uint32_t line)
            {
                std::string out;
                out.reserve(raw.size());
                for (std::size_t i = 0; i < raw.size(); ++i)
                {
                    const char c = raw[i];
                    if (c == '\n')
                    {
                        ++line;
                    }
                    if (c != '&')
                    {
                        out += c;
                        continue;
                    }
                    // Far enough for any entity, leading zeros and all, and short of a stray `;` further on.
                    const std::size_t semicolon = raw.find(';', i + 1);
                    if (semicolon == std::string_view::npos || semicolon - i > MaxEntityLength)
                    {
                        Error(line, "'&' starts no entity: write it as &amp;");
                        out += c;
                        continue;
                    }
                    const std::string_view name = raw.substr(i + 1, semicolon - i - 1);
                    if (!DecodeEntity(name, out))
                    {
                        Error(line, "'&" + std::string(name) + ";' is not an entity");
                        out += c;
                        continue;
                    }
                    i = semicolon;
                }
                return out;
            }

            void ParseText(MarkupNode& parent)
            {
                const std::uint32_t startLine = m_line;
                std::size_t end = m_text.find('<', m_at);
                if (end == std::string_view::npos)
                {
                    end = m_text.size();
                }
                const std::string_view raw = m_text.substr(m_at, end - m_at);
                Advance(end - m_at);
                const auto first = std::find_if(raw.begin(), raw.end(), [](char c) { return !Detail::IsSpace(c); });
                if (first == raw.end())
                {
                    return;
                }
                MarkupNode& text = parent.children.emplace_back();
                text.isText = true;
                text.line = startLine + static_cast<std::uint32_t>(std::count(raw.begin(), first, '\n'));
                text.text = Decode(raw, startLine);
            }

            void ParseEnd(std::vector<MarkupNode*>& open)
            {
                const std::uint32_t line = m_line;
                Advance(2);
                const std::string name = ReadName();
                SkipSpace();
                if (m_at < m_text.size() && m_text[m_at] == '>')
                {
                    Advance(1);
                }
                else
                {
                    Error(line, "the closing tag </" + name + "> is not finished with '>'");
                    SkipPast(">");
                }
                std::size_t match = open.size();
                while (--match > 0 && open[match]->tag != name)
                {
                }
                if (match == 0)
                {
                    Error(line, "</" + name + "> closes no open tag");
                    return;
                }
                for (std::size_t i = open.size() - 1; i > match; --i)
                {
                    NotClosed(*open[i]);
                }
                open.resize(match);
            }

            void ParseStart(std::vector<MarkupNode*>& open)
            {
                const std::uint32_t line = m_line;
                Advance(1);
                MarkupNode node;
                node.tag = ReadName();
                node.line = line;
                if (node.tag.empty())
                {
                    Error(line, "'<' starts no tag: write it as &lt;");
                    return;
                }
                bool selfClosing = false;
                bool spaced = true;
                for (;;)
                {
                    const std::size_t before = m_at;
                    SkipSpace();
                    spaced = spaced || m_at != before;
                    if (m_at >= m_text.size())
                    {
                        Error(line, "the tag <" + node.tag + "> is not finished with '>'");
                        return;
                    }
                    if (m_text[m_at] == '<')
                    {
                        // A tag is starting: this one is open, as if its `>` were there.
                        Error(line, "the tag <" + node.tag + "> is not finished with '>'");
                        break;
                    }
                    if (m_text[m_at] == '>')
                    {
                        Advance(1);
                        break;
                    }
                    if (StartsWith("/>"))
                    {
                        Advance(2);
                        selfClosing = true;
                        break;
                    }
                    if (!spaced)
                    {
                        Error(m_line, "the attributes of <" + node.tag + "> need whitespace between them");
                    }
                    ParseAttribute(node);
                    spaced = false;
                }
                MarkupNode& added = open.back()->children.emplace_back(std::move(node));
                if (added.tag == "style" && !selfClosing)
                {
                    // CSS is kept as written, `>` combinators and all: it ends only at its closing tag.
                    added.contentLine = m_line;
                    const auto [close, closeEnd] = FindStyleEnd();
                    if (close == std::string_view::npos)
                    {
                        NotClosed(added);
                        added.text = std::string(m_text.substr(m_at));
                        Advance(m_text.size() - m_at);
                        return;
                    }
                    added.text = std::string(m_text.substr(m_at, close - m_at));
                    Advance(closeEnd - m_at);
                    return;
                }
                if (!selfClosing)
                {
                    open.push_back(&added);
                }
            }

            /** @brief Where the next `</style>` (whitespace before its `>` allowed) starts and ends; npos for none. */
            [[nodiscard]] std::pair<std::size_t, std::size_t> FindStyleEnd() const
            {
                constexpr std::string_view closing = "</style";
                for (std::size_t at = m_text.find(closing, m_at); at != std::string_view::npos;
                     at = m_text.find(closing, at + 1))
                {
                    std::size_t end = at + closing.size();
                    while (end < m_text.size() && Detail::IsSpace(m_text[end]))
                    {
                        ++end;
                    }
                    if (end < m_text.size() && m_text[end] == '>')
                    {
                        return {at, end + 1};
                    }
                }
                return {std::string_view::npos, std::string_view::npos};
            }

            void ParseAttribute(MarkupNode& node)
            {
                const std::uint32_t line = m_line;
                std::string name = ReadName();
                if (name.empty())
                {
                    Error(line, "'" + std::string(1, m_text[m_at]) + "' is not an attribute of <" + node.tag + ">");
                    Advance(1);
                    return;
                }
                SkipSpace();
                std::string value;
                std::uint32_t valueLine = line;
                if (m_at < m_text.size() && m_text[m_at] == '=')
                {
                    Advance(1);
                    SkipSpace();
                    const char quote = m_at < m_text.size() ? m_text[m_at] : '\0';
                    if (quote != '"' && quote != '\'')
                    {
                        Error(line, "the value of '" + name + "' is not in quotes");
                        while (m_at < m_text.size() && !Detail::IsSpace(m_text[m_at]) && m_text[m_at] != '>' &&
                               !StartsWith("/>"))
                        {
                            Advance(1);
                        }
                        return;
                    }
                    Advance(1);
                    valueLine = m_line;
                    const std::size_t close = m_text.find(quote, m_at);
                    if (close == std::string_view::npos)
                    {
                        Error(line, "the value of '" + name + "' is not closed");
                        Advance(m_text.size() - m_at);
                        return;
                    }
                    const std::string_view raw = m_text.substr(m_at, close - m_at);
                    Advance(close + 1 - m_at);
                    value = Decode(raw, valueLine);
                }
                const bool repeated = std::any_of(node.attributes.begin(), node.attributes.end(),
                                                  [&name](const MarkupAttribute& seen) { return seen.name == name; });
                if (repeated)
                {
                    Error(line, "<" + node.tag + "> has '" + name + "' twice");
                    return;
                }
                node.attributes.push_back({std::move(name), std::move(value), line, valueLine});
            }

            std::string_view m_text;
            std::string_view m_file;
            MarkupErrors& m_errors;
            std::size_t m_at = 0;
            std::uint32_t m_line = 1;
        };

        /** @brief Whether the node holds anything: an element, text, or an inline `<style>`'s CSS. */
        bool HoldsAnything(const MarkupNode& node)
        {
            return !node.children.empty() || !Detail::Trim(node.text).empty();
        }
    } // namespace

    /** @brief Makes the elements of a document's nodes in the root, and of the documents it includes. */
    struct Detail::MarkupLoader
    {
        UIRoot& root;
        const UIRoots& roots;
        const SourceReader& reader;
        AssetStore* store;
        MarkupLoad& load;
        /** @brief Where the next top-level element goes among the parent's children; UIRoot::End for after them. */
        std::size_t insertAt = UIRoot::End;
        /** @brief Where the sheets that read files go, for hot reload; null when nothing is read from files. */
        FileLoad* files = nullptr;
        /** @brief The documents being loaded, outermost first: an include of one of them recurses. */
        std::vector<std::string> documents;

        /** @brief Loads `text`, named `name`, under `parent`; its errors in its order. */
        MarkupErrors LoadDocument(std::string_view text, std::string_view name, ElementId parent, bool topLevel)
        {
            MarkupDocument document{name, {}};
            documents.emplace_back(name);
            const std::vector<MarkupNode> nodes = MarkupParser(text, name, document.errors).Parse();
            for (const MarkupNode& node : nodes)
            {
                if (node.isText)
                {
                    document.Error(node.line, "text is outside every element");
                }
            }
            BuildChildren(document, nodes, parent, topLevel);
            documents.pop_back();
            std::stable_sort(document.errors.begin(), document.errors.end(),
                             [](const OrderedError& a, const OrderedError& b) { return a.at < b.at; });
            return std::move(document.errors);
        }

        void BuildChildren(MarkupDocument& document, const std::vector<MarkupNode>& nodes, ElementId parent,
                           bool topLevel)
        {
            for (const MarkupNode& node : nodes)
            {
                if (node.isText)
                {
                    continue;
                }
                if (node.tag == "include")
                {
                    Include(document, node, parent, topLevel);
                }
                else if (node.tag == "style")
                {
                    Style(document, node);
                }
                else
                {
                    Build(document, node, parent, topLevel);
                }
            }
        }

        void Build(MarkupDocument& document, const MarkupNode& node, ElementId parent, bool topLevel)
        {
            if (!roots.IsRegistered(node.tag))
            {
                document.Error(node.line, "<" + node.tag + "> is not an element tag: none is registered");
                return;
            }
            const ElementId element = root.Create(parent, node.tag, topLevel ? insertAt : UIRoot::End);
            if (topLevel)
            {
                load.elements.push_back(element);
                if (insertAt != UIRoot::End)
                {
                    ++insertAt;
                }
            }
            // `focusable` comes last, so it wins over what `disabled` does to the focus wherever it is written.
            const MarkupAttribute* focusable = nullptr;
            for (const MarkupAttribute& attribute : node.attributes)
            {
                if (attribute.name == "focusable")
                {
                    focusable = &attribute;
                    continue;
                }
                Attribute(document, node.tag, element, attribute);
            }
            if (focusable != nullptr)
            {
                Attribute(document, node.tag, element, *focusable);
            }
            BuildChildren(document, node.children, element, false);
            Text(document, node, element);
        }

        /** @brief The node's text content, as its element's `text` attribute. */
        void Text(MarkupDocument& document, const MarkupNode& node, ElementId element)
        {
            std::string text;
            std::uint32_t textLine = 0;
            for (const MarkupNode& child : node.children)
            {
                if (child.isText)
                {
                    textLine = textLine == 0 ? child.line : textLine;
                    text += child.text;
                }
            }
            const std::string_view trimmed = Detail::Trim(text);
            if (trimmed.empty())
            {
                return;
            }
            if (std::any_of(node.attributes.begin(), node.attributes.end(),
                            [](const MarkupAttribute& attribute) { return attribute.name == "text"; }))
            {
                document.Error(textLine, "<" + node.tag + "> has both a text attribute and text content");
                return;
            }
            const AttributeResult result = root.GetElement(element)->SetAttribute("text", trimmed);
            if (result == AttributeResult::Unknown)
            {
                document.Error(textLine, "<" + node.tag + "> takes no text");
            }
            else
            {
                document.Refused(textLine, result, node.tag, "text", trimmed);
            }
        }

        void Attribute(MarkupDocument& document, const std::string& tag, ElementId element,
                       const MarkupAttribute& attribute)
        {
            const std::string_view value = attribute.value;
            AttributeResult result = AttributeResult::Set;
            if (attribute.name == "id")
            {
                root.SetIdString(element, value);
            }
            else if (attribute.name == "class")
            {
                std::size_t at = 0;
                while (at < value.size())
                {
                    const auto start = std::find_if(value.begin() + static_cast<std::ptrdiff_t>(at), value.end(),
                                                    [](char c) { return !Detail::IsSpace(c); });
                    const auto end = std::find_if(start, value.end(), [](char c) { return Detail::IsSpace(c); });
                    if (start != end)
                    {
                        root.AddClass(element, std::string_view(&*start, static_cast<std::size_t>(end - start)));
                    }
                    at = static_cast<std::size_t>(end - value.begin());
                }
            }
            else if (attribute.name == "style")
            {
                InlineStyle(document, tag, element, attribute);
            }
            else if (attribute.name == "focusable")
            {
                const std::optional<bool> focusable = Detail::ParseBoolAttribute(value);
                result = focusable.has_value() ? AttributeResult::Set : AttributeResult::Invalid;
                if (focusable.has_value())
                {
                    root.SetFocusable(element, *focusable);
                }
            }
            else
            {
                result = root.GetElement(element)->SetAttribute(attribute.name, value);
            }
            document.Refused(attribute.line, result, tag, attribute.name, value);
        }

        /**
         * @brief A `style` attribute's declarations, split as a stylesheet's rule body is, its lines the value's,
         * then each set as SetStyleProperty sets it.
         */
        void InlineStyle(MarkupDocument& document, const std::string& tag, ElementId element,
                         const MarkupAttribute& attribute)
        {
            std::string wrapped(attribute.valueLine - 1, '\n');
            wrapped += "style{";
            wrapped += attribute.value;
            wrapped += "}";
            const Stylesheet sheet = Stylesheet::Parse(wrapped, document.file);
            for (const StyleDiagnostic& diagnostic : sheet.diagnostics)
            {
                document.Error(diagnostic.line, "<" + tag + ">'s style: " + diagnostic.message);
            }
            if (sheet.rules.size() > 1 || !sheet.keyframes.empty() || !sheet.fontFaces.empty() ||
                !sheet.imports.empty())
            {
                document.Error(attribute.valueLine, "<" + tag + ">'s style holds declarations only, not rules");
            }
            if (sheet.rules.empty())
            {
                return;
            }
            for (const StyleDeclaration& declaration : sheet.rules.front().declarations)
            {
                std::string why;
                if (!root.ApplyStyleProperty(element, declaration.name, declaration.value, why))
                {
                    document.Error(declaration.line, "<" + tag + ">'s style property '" + declaration.name + ": " +
                                                         declaration.value + "' was not set: " + why);
                }
            }
        }

        /** @brief The node's `src`, refusing its other attributes and anything in it; nothing when it has none. */
        std::optional<std::string> Source(MarkupDocument& document, const MarkupNode& node, bool required)
        {
            std::optional<std::string> src;
            for (const MarkupAttribute& attribute : node.attributes)
            {
                if (attribute.name == "src")
                {
                    src = attribute.value;
                }
                else
                {
                    document.Refused(attribute.line, AttributeResult::Unknown, node.tag, attribute.name,
                                     attribute.value);
                }
            }
            if (src.has_value() && HoldsAnything(node))
            {
                document.Error(node.line, "<" + node.tag + " src> holds nothing: what it loads is its src");
            }
            if (!src.has_value() && required)
            {
                document.Error(node.line, "<" + node.tag + "> has no src");
            }
            return src;
        }

        void Include(MarkupDocument& document, const MarkupNode& node, ElementId parent, bool topLevel)
        {
            const std::optional<std::string> src = Source(document, node, true);
            if (!src.has_value())
            {
                return;
            }
            const std::string name = reader.name(*src, document.file);
            if (std::find(documents.begin(), documents.end(), name) != documents.end())
            {
                document.Error(node.line, "the include of '" + *src + "' recurses: it is being loaded");
                return;
            }
            const std::optional<std::string> text = reader.read(name);
            if (!text.has_value())
            {
                document.Error(node.line, "the document '" + *src + "' was not found");
                return;
            }
            for (OrderedError& error : LoadDocument(*text, name, parent, topLevel))
            {
                document.errors.push_back({node.line, std::move(error.error)});
            }
        }

        void Style(MarkupDocument& document, const MarkupNode& node)
        {
            const std::optional<std::string> src = Source(document, node, false);
            // The files read from here on, the src sheet's and its imports', are the sheet's, not the document's.
            const std::size_t firstFile = files != nullptr ? files->files.size() : 0;
            std::string name;
            std::string text;
            if (src.has_value())
            {
                name = reader.name(*src, document.file);
                std::optional<std::string> read = reader.read(name);
                if (!read.has_value())
                {
                    document.Error(node.line, "the stylesheet '" + *src + "' was not found");
                    return;
                }
                text = std::move(*read);
            }
            else if (Detail::Trim(node.text).empty())
            {
                document.Error(node.line, "<style> holds no stylesheet and has no src");
                return;
            }
            else
            {
                // Blank lines before the CSS put its lines, and so its errors', at the document's.
                name = std::string(document.file);
                text = std::string(node.contentLine - 1, '\n');
                text += node.text;
            }
            // A src sheet's errors are ordered at its <style>, an inline one's at their own lines.
            const auto order = [&](std::uint32_t line) { return src.has_value() ? node.line : line; };
            MarkupErrors errors;
            std::vector<std::string> loading;
            Stylesheet sheet = BuildStylesheet(text, name, reader, store, loading, errors);
            for (OrderedError& error : errors)
            {
                document.errors.push_back({order(error.at), std::move(error.error)});
            }
            std::vector<MarkupError> attachErrors;
            const StylesheetId id = root.AttachStylesheet(std::move(sheet), attachErrors);
            for (MarkupError& error : attachErrors)
            {
                const std::uint32_t at = order(error.line);
                document.errors.push_back({at, std::move(error)});
            }
            load.stylesheets.push_back(id);
            if (files != nullptr && files->files.size() > firstFile)
            {
                // The files this sheet read are its own, for hot reload to parse it again alone.
                FileSheet fileSheet{id, name, src.has_value() ? std::string() : text, {}};
                fileSheet.files.assign(std::make_move_iterator(files->files.begin() + std::ptrdiff_t(firstFile)),
                                       std::make_move_iterator(files->files.end()));
                files->files.resize(firstFile);
                files->sheets.push_back(std::move(fileSheet));
            }
        }
    };

    MarkupLoad Detail::LoadMarkupDocument(UIRoot& root, const UIRoots& roots, ElementId parent, std::string_view text,
                                          std::string_view name, const SourceReader& reader, std::size_t index,
                                          FileLoad* files)
    {
        MarkupLoad load;
        if (!root.IsAlive(parent))
        {
            load.errors.push_back({std::string(name), 0, "the parent to load into is not an element of the root"});
        }
        else
        {
            MarkupLoader loader{root, roots, reader, root.GetApp().TryGetResource<AssetStore>(),
                                load, index, files,  {}};
            for (OrderedError& error : loader.LoadDocument(text, name, parent, true))
            {
                load.errors.push_back(std::move(error.error));
            }
        }
        if (!load.elements.empty())
        {
            load.element = load.elements.front();
        }
        return load;
    }

    MarkupLoad UIRoot::LoadMarkup(ElementId parent, std::string_view text, std::string_view name,
                                  const MarkupResolver& resolver)
    {
        const Detail::SourceReader reader = Detail::ResolverReader(resolver);
        MarkupLoad load = Detail::LoadMarkupDocument(*this, m_state->roots, parent, text, name, reader, End, nullptr);
        LogErrors(load.errors);
        return load;
    }

    void UIRoot::LogErrors(std::span<const MarkupError> errors) const
    {
        for (const MarkupError& error : errors)
        {
            m_state->app.Logger()->Warn("FenrirUI: {}:{}: {}", error.file, error.line, error.message);
        }
    }
} // namespace Fenrir
