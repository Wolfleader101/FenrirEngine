#include "FenrirSerialisation/Text.hpp"

#include "FenrirLogger/ILogger.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Fenrir::Serialisation
{
    namespace
    {
        // The spec's limit, so a hostile file cannot overflow the stack of this recursive reader.
        constexpr int MaxDepth = 256;

        // Code points no text holds, standing for the end of the text and for a byte that does not decode as UTF-8.
        constexpr char32_t EndOfText = 0xFFFFFFFF;
        constexpr char32_t NotUtf8 = 0xFFFFFFFE;

        constexpr char32_t LineSeparator = 0x2028;
        constexpr char32_t ParagraphSeparator = 0x2029;

        struct Decoded
        {
            char32_t codePoint;
            std::size_t length;
        };

        bool IsContinuation(unsigned char byte)
        {
            return (byte & 0xC0) == 0x80;
        }

        // One code point of UTF-8 at `offset`, refusing overlong forms and anything past U+10FFFF. A surrogate's
        // three-byte form is read, since that is how a lone surrogate from a `\u` escape is held and written.
        Decoded Decode(std::string_view text, std::size_t offset)
        {
            if (offset >= text.size())
                return {EndOfText, 0};
            const auto lead = static_cast<unsigned char>(text[offset]);
            if (lead < 0x80)
                return {lead, 1};

            std::size_t length = 0;
            char32_t codePoint = 0;
            char32_t smallest = 0;
            if (lead >= 0xC2 && lead <= 0xDF)
            {
                length = 2;
                codePoint = lead & 0x1F;
                smallest = 0x80;
            }
            else if (lead >= 0xE0 && lead <= 0xEF)
            {
                length = 3;
                codePoint = lead & 0x0F;
                smallest = 0x800;
            }
            else if (lead >= 0xF0 && lead <= 0xF4)
            {
                length = 4;
                codePoint = lead & 0x07;
                smallest = 0x10000;
            }
            else
            {
                return {NotUtf8, 1};
            }
            if (offset + length > text.size())
                return {NotUtf8, 1};
            for (std::size_t i = 1; i < length; ++i)
            {
                const auto byte = static_cast<unsigned char>(text[offset + i]);
                if (!IsContinuation(byte))
                    return {NotUtf8, 1};
                codePoint = (codePoint << 6) | (byte & 0x3F);
            }
            if (codePoint < smallest || codePoint > 0x10FFFF)
                return {NotUtf8, 1};
            return {codePoint, length};
        }

        // UTF-8 of any code point up to U+10FFFF. A lone surrogate from a `\u` escape is written in the same
        // three-byte form, so a strict JSON file holding one still reads.
        void AppendUtf8(std::string& out, char32_t codePoint)
        {
            if (codePoint < 0x80)
            {
                out += static_cast<char>(codePoint);
            }
            else if (codePoint < 0x800)
            {
                out += static_cast<char>(0xC0 | (codePoint >> 6));
                out += static_cast<char>(0x80 | (codePoint & 0x3F));
            }
            else if (codePoint < 0x10000)
            {
                out += static_cast<char>(0xE0 | (codePoint >> 12));
                out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codePoint & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xF0 | (codePoint >> 18));
                out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (codePoint & 0x3F));
            }
        }

        bool IsLineTerminator(char32_t c)
        {
            return c == '\n' || c == '\r' || c == LineSeparator || c == ParagraphSeparator;
        }

        // JSON5's WhiteSpace and LineTerminator: tab, vertical tab, form feed, space, NBSP, U+FEFF, the Zs spaces,
        // LF, CR, U+2028 and U+2029.
        bool IsWhitespace(char32_t c)
        {
            return c == '\t' || c == '\v' || c == '\f' || c == ' ' || c == 0xA0 || c == 0xFEFF || c == 0x1680 ||
                   (c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000 || IsLineTerminator(c);
        }

        bool IsDigit(char32_t c)
        {
            return c >= '0' && c <= '9';
        }

        bool IsHexDigit(char32_t c)
        {
            return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        std::uint32_t HexValue(char32_t c)
        {
            if (IsDigit(c))
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            return c - 'A' + 10;
        }

        bool IsSurrogate(char32_t c)
        {
            return c >= 0xD800 && c <= 0xDFFF;
        }

        // A code point Unicode keeps out of text: U+FDD0 to U+FDEF, and the last two of every plane.
        bool IsNonCharacter(char32_t c)
        {
            return (c >= 0xFDD0 && c <= 0xFDEF) || (c & 0xFFFE) == 0xFFFE;
        }

        // ECMAScript's IdentifierStart in ASCII, and every code point above ASCII that is not whitespace, a C1
        // control, a surrogate or a noncharacter, which stands in for Unicode's ID_Start and ID_Continue so the
        // reader carries no tables.
        bool IsIdentifierStart(char32_t c)
        {
            if (c < 0x80)
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
            return c > 0x9F && c <= 0x10FFFF && !IsWhitespace(c) && !IsSurrogate(c) && !IsNonCharacter(c);
        }

        bool IsIdentifierPart(char32_t c)
        {
            return IsIdentifierStart(c) || IsDigit(c);
        }

        struct Position
        {
            std::size_t offset = 0;
            std::uint32_t line = 1;
            std::uint32_t column = 1;
        };

        struct Comment
        {
            std::string text;
            // Whether a line break came between the last token and this comment: one that did not trails it.
            bool afterLineBreak;
        };

        struct SyntaxError
        {
            Position at;
            std::string message;
        };

        std::vector<std::string> Texts(std::vector<Comment>&& comments)
        {
            std::vector<std::string> texts;
            texts.reserve(comments.size());
            for (Comment& comment : comments)
                texts.push_back(std::move(comment.text));
            return texts;
        }

        // Splits the comments read after an item: those before the first line break join into its trailing
        // comment, and the rest lead whatever comes next.
        std::optional<std::string> SplitTrailing(std::vector<Comment>& comments, std::vector<std::string>& next)
        {
            std::optional<std::string> trailing;
            for (Comment& comment : comments)
            {
                if (comment.afterLineBreak)
                {
                    next.push_back(std::move(comment.text));
                }
                else if (trailing)
                {
                    *trailing += ' ';
                    *trailing += comment.text;
                }
                else
                {
                    trailing = std::move(comment.text);
                }
            }
            return trailing;
        }

        class Parser
        {
          public:
            Parser(std::string_view text, std::vector<Diagnostic>& warnings, std::string_view path)
                : m_text(text), m_warnings(warnings), m_path(path)
            {
                // A byte order mark reads as if absent: it takes no column.
                if (m_text.starts_with("\xEF\xBB\xBF"))
                    m_position.offset = 3;
            }

            Value ParseRoot()
            {
                std::vector<Comment> leading;
                SkipTrivia(leading);
                Value root = ParseValue();
                std::vector<Comment> after;
                SkipTrivia(after);
                if (Peek() != EndOfText)
                    FailExpected("the end of the text");

                root.leadingComments = Texts(std::move(leading));
                // A comment after the root has no place of its own, so it goes to the nearest one.
                std::vector<std::string>& target = root.Kind() == ValueKind::Object  ? root.AsObject().closingComments
                                                   : root.Kind() == ValueKind::Array ? root.AsArray().closingComments
                                                                                     : root.leadingComments;
                for (std::string& comment : Texts(std::move(after)))
                    target.push_back(std::move(comment));
                return root;
            }

          private:
            std::string_view m_text;
            std::vector<Diagnostic>& m_warnings;
            std::string_view m_path;
            Position m_position;
            bool m_sawLineBreak = false;
            int m_depth = 0;

            char32_t Peek() const
            {
                return Decode(m_text, m_position.offset).codePoint;
            }

            void Advance()
            {
                const Decoded decoded = Decode(m_text, m_position.offset);
                if (decoded.codePoint == EndOfText)
                    return;
                m_position.offset += decoded.length;
                if (decoded.codePoint == '\r' && m_position.offset < m_text.size() && m_text[m_position.offset] == '\n')
                    ++m_position.offset; // CRLF is one line break
                if (IsLineTerminator(decoded.codePoint))
                {
                    ++m_position.line;
                    m_position.column = 1;
                }
                else
                {
                    ++m_position.column;
                }
            }

            std::string Describe(char32_t c) const
            {
                if (c == EndOfText)
                    return "the end of the text";
                if (c == NotUtf8)
                    return std::format("the byte 0x{:02X}, which is not UTF-8",
                                       static_cast<unsigned char>(m_text[m_position.offset]));
                if (IsLineTerminator(c))
                    return "a line break";
                if (c < 0x20 || (c >= 0x7F && c <= 0x9F) || IsWhitespace(c))
                    return std::format("U+{:04X}", static_cast<std::uint32_t>(c));
                std::string text = "'";
                AppendUtf8(text, c);
                text += '\'';
                return text;
            }

            [[noreturn]] void Fail(Position at, std::string message) const
            {
                throw SyntaxError{at, std::move(message)};
            }

            [[noreturn]] void FailExpected(std::string_view expected) const
            {
                Fail(m_position, std::format("expected {}, found {}", expected, Describe(Peek())));
            }

            void Expect(char32_t c, std::string_view expected)
            {
                if (Peek() != c)
                    FailExpected(expected);
                Advance();
            }

            void ExpectWord(std::string_view word)
            {
                for (const char c : word)
                {
                    if (Peek() != static_cast<char32_t>(c))
                        FailExpected(std::format("'{}'", word));
                    Advance();
                }
            }

            // Whitespace and comments, each comment added to `comments` with whether a line break came before it.
            void SkipTrivia(std::vector<Comment>& comments)
            {
                for (;;)
                {
                    const char32_t c = Peek();
                    if (IsWhitespace(c))
                    {
                        if (IsLineTerminator(c))
                            m_sawLineBreak = true;
                        Advance();
                    }
                    else if (c == '/' && m_position.offset + 1 < m_text.size() &&
                             (m_text[m_position.offset + 1] == '/' || m_text[m_position.offset + 1] == '*'))
                    {
                        const bool afterLineBreak = m_sawLineBreak;
                        std::string text =
                            m_text[m_position.offset + 1] == '/' ? ReadLineComment() : ReadBlockComment();
                        comments.push_back({std::move(text), afterLineBreak});
                    }
                    else
                    {
                        return;
                    }
                }
            }

            std::string ReadLineComment()
            {
                const std::size_t start = m_position.offset;
                for (char32_t c = Peek(); c != EndOfText && !IsLineTerminator(c); c = Peek())
                {
                    if (c == NotUtf8)
                        FailExpected("UTF-8 text");
                    Advance();
                }
                return std::string(m_text.substr(start, m_position.offset - start));
            }

            std::string ReadBlockComment()
            {
                const Position start = m_position;
                std::string text = "/*";
                Advance();
                Advance();
                for (;;)
                {
                    const char32_t c = Peek();
                    if (c == EndOfText)
                        Fail(start, "expected '*/' to end the block comment begun here, found the end of the text");
                    if (c == NotUtf8)
                        FailExpected("UTF-8 text");
                    if (c == '*' && m_position.offset + 1 < m_text.size() && m_text[m_position.offset + 1] == '/')
                    {
                        Advance();
                        Advance();
                        return text + "*/";
                    }
                    if (c == '\n' || c == '\r')
                    {
                        // CR and CRLF are kept as LF, so a CRLF file reads as its LF form would.
                        text += '\n';
                        m_sawLineBreak = true;
                    }
                    else
                    {
                        if (IsLineTerminator(c))
                            m_sawLineBreak = true;
                        AppendUtf8(text, c);
                    }
                    Advance();
                }
            }

            Value ParseValue()
            {
                const Position start = m_position;
                Value value = ParseValueAt();
                value.location = Location{start.line, start.column};
                return value;
            }

            Value ParseValueAt()
            {
                switch (const char32_t c = Peek())
                {
                case '{':
                    return ParseObject();
                case '[':
                    return ParseArray();
                case '"':
                case '\'':
                    return Value(ParseString(c));
                case 't':
                    ExpectWord("true");
                    return Value(true);
                case 'f':
                    ExpectWord("false");
                    return Value(false);
                case 'n':
                    ExpectWord("null");
                    return Value(nullptr);
                default:
                    if (IsDigit(c) || c == '-' || c == '+' || c == '.' || c == 'I' || c == 'N')
                        return Value(ParseNumber());
                    FailExpected("a value");
                }
            }

            void Enter()
            {
                if (++m_depth > MaxDepth)
                    Fail(m_position,
                         std::format("expected at most {} nested objects and arrays, found one more", MaxDepth));
            }

            // After an item's value: the comma or the closing bracket. Comments on the item's line become its
            // trailing comment, and the others go to `next`. Returns whether a comma was read.
            bool ReadItemEnd(char32_t close, std::optional<std::string>& trailing, std::vector<std::string>& next)
            {
                m_sawLineBreak = false;
                std::vector<Comment> comments;
                SkipTrivia(comments);
                bool comma = false;
                if (Peek() == ',')
                {
                    Advance();
                    comma = true;
                    SkipTrivia(comments);
                }
                else if (Peek() != close)
                {
                    FailExpected(close == '}' ? "',' or '}'" : "',' or ']'");
                }
                trailing = SplitTrailing(comments, next);
                return comma;
            }

            std::vector<std::string> LeadingComments()
            {
                std::vector<Comment> comments;
                SkipTrivia(comments);
                return Texts(std::move(comments));
            }

            void AppendComments(std::vector<std::string>& to)
            {
                for (std::string& comment : LeadingComments())
                    to.push_back(std::move(comment));
            }

            Value ParseObject()
            {
                Enter();
                Advance();
                Object object;
                std::unordered_map<std::string, Location> keys;
                std::vector<std::string> pending = LeadingComments();
                for (;;)
                {
                    if (Peek() == '}')
                        break;
                    const Location keyAt{m_position.line, m_position.column};
                    Member member;
                    member.key = ParseKey();
                    AppendComments(pending);
                    Expect(':', "':'");
                    AppendComments(pending);
                    member.value = ParseValue();
                    member.leadingComments = std::move(pending);
                    pending.clear();
                    const bool comma = ReadItemEnd('}', member.trailingComment, pending);

                    if (const auto [first, added] = keys.try_emplace(member.key, keyAt); !added)
                        Warn(keyAt,
                             std::format("duplicate key '{}'; the first, at {}:{}, stands and this one is dropped",
                                         member.key, first->second.line, first->second.column));
                    else
                        object.members.push_back(std::move(member));
                    if (!comma)
                        break;
                }
                Advance();
                object.closingComments = std::move(pending);
                --m_depth;
                return Value(std::move(object));
            }

            Value ParseArray()
            {
                Enter();
                Advance();
                Array array;
                std::vector<std::string> pending = LeadingComments();
                for (;;)
                {
                    if (Peek() == ']')
                        break;
                    Element element;
                    element.value = ParseValue();
                    element.leadingComments = std::move(pending);
                    pending.clear();
                    const bool comma = ReadItemEnd(']', element.trailingComment, pending);
                    array.elements.push_back(std::move(element));
                    if (!comma)
                        break;
                }
                Advance();
                array.closingComments = std::move(pending);
                --m_depth;
                return Value(std::move(array));
            }

            void Warn(Location at, std::string message)
            {
                m_warnings.push_back(
                    {DiagnosticSeverity::Warning, std::string(m_path), at.line, at.column, std::move(message)});
            }

            std::string ParseKey()
            {
                const char32_t c = Peek();
                if (c == '"' || c == '\'')
                    return ParseString(c);
                if (!IsIdentifierStart(c) && c != '\\')
                    FailExpected("a key");
                std::string key;
                for (bool first = true;; first = false)
                {
                    const char32_t part = Peek();
                    if (part == '\\')
                    {
                        const Position escapeAt = m_position;
                        Advance();
                        Expect('u', "'u' to begin a Unicode escape in a key");
                        const char32_t escaped = ReadUnicodeEscape();
                        if (first ? !IsIdentifierStart(escaped) : !IsIdentifierPart(escaped))
                            Fail(escapeAt,
                                 std::format("expected an escape of a character a key may hold, found U+{:04X}",
                                             static_cast<std::uint32_t>(escaped)));
                        AppendUtf8(key, escaped);
                    }
                    else if (first ? IsIdentifierStart(part) : IsIdentifierPart(part))
                    {
                        AppendUtf8(key, part);
                        Advance();
                    }
                    else
                    {
                        return key;
                    }
                }
            }

            char32_t ReadHex(int digits)
            {
                char32_t value = 0;
                for (int i = 0; i < digits; ++i)
                {
                    if (!IsHexDigit(Peek()))
                        FailExpected("a hexadecimal digit");
                    value = value * 16 + HexValue(Peek());
                    Advance();
                }
                return value;
            }

            bool AtUnicodeEscape() const
            {
                return m_position.offset + 1 < m_text.size() && m_text[m_position.offset] == '\\' &&
                       m_text[m_position.offset + 1] == 'u';
            }

            std::string ParseString(char32_t quote)
            {
                Advance();
                std::string text;
                for (;;)
                {
                    const char32_t c = Peek();
                    if (c == quote)
                    {
                        Advance();
                        return text;
                    }
                    if (c == EndOfText)
                        FailExpected("the closing quote");
                    if (c == '\n' || c == '\r')
                        FailExpected("the closing quote or an escape before the line break");
                    if (c == NotUtf8)
                        FailExpected("UTF-8 text");
                    if (c == '\\')
                    {
                        Advance();
                        ReadEscape(text);
                        continue;
                    }
                    AppendUtf8(text, c);
                    Advance();
                }
            }

            void ReadEscape(std::string& text)
            {
                const char32_t c = Peek();
                if (IsLineTerminator(c))
                {
                    Advance(); // a line continuation: the backslash and the line break read as nothing
                    return;
                }
                switch (c)
                {
                case 'b':
                    text += '\b';
                    break;
                case 'f':
                    text += '\f';
                    break;
                case 'n':
                    text += '\n';
                    break;
                case 'r':
                    text += '\r';
                    break;
                case 't':
                    text += '\t';
                    break;
                case 'v':
                    text += '\v';
                    break;
                case '0':
                    Advance();
                    if (IsDigit(Peek()))
                        FailExpected("no digit after the escape \\0");
                    text += '\0';
                    return;
                case 'x':
                    Advance();
                    AppendUtf8(text, ReadHex(2));
                    return;
                case 'u':
                    Advance();
                    AppendUtf8(text, ReadUnicodeEscape());
                    return;
                case EndOfText:
                case NotUtf8:
                    FailExpected("an escape");
                default:
                    if (IsDigit(c))
                        FailExpected("an escape other than a digit");
                    AppendUtf8(text, c); // any other character escapes itself
                    break;
                }
                Advance();
            }

            // The four digits after `\u`, joined with a following low surrogate's escape when they are a high one.
            char32_t ReadUnicodeEscape()
            {
                const char32_t unit = ReadHex(4);
                if (unit < 0xD800 || unit > 0xDBFF || !AtUnicodeEscape())
                    return unit;
                const Position low = m_position;
                Advance();
                Advance();
                const char32_t next = ReadHex(4);
                if (next >= 0xDC00 && next <= 0xDFFF)
                    return 0x10000 + ((unit - 0xD800) << 10) + (next - 0xDC00);
                // Not a pair: the high surrogate stands alone, and the second escape is read again on its own.
                m_position = low;
                return unit;
            }

            Number ParseNumber()
            {
                bool negative = false;
                if (Peek() == '+' || Peek() == '-')
                {
                    negative = Peek() == '-';
                    Advance();
                }
                if (Peek() == 'I')
                {
                    ExpectWord("Infinity");
                    const double infinity = std::numeric_limits<double>::infinity();
                    return Number(negative ? -infinity : infinity);
                }
                if (Peek() == 'N')
                {
                    ExpectWord("NaN");
                    return Number(std::numeric_limits<double>::quiet_NaN());
                }
                if (Peek() == '0' && m_position.offset + 1 < m_text.size() &&
                    (m_text[m_position.offset + 1] == 'x' || m_text[m_position.offset + 1] == 'X'))
                {
                    Advance();
                    Advance();
                    return ParseHex(negative);
                }

                std::string integer;
                if (Peek() == '0')
                {
                    integer = "0";
                    Advance();
                    if (IsDigit(Peek()))
                        FailExpected("no digit after a leading zero");
                }
                while (IsDigit(Peek()))
                {
                    integer += static_cast<char>(Peek());
                    Advance();
                }

                std::string fraction;
                bool point = false;
                if (Peek() == '.')
                {
                    point = true;
                    Advance();
                    while (IsDigit(Peek()))
                    {
                        fraction += static_cast<char>(Peek());
                        Advance();
                    }
                    if (integer.empty() && fraction.empty())
                        FailExpected("a digit after the decimal point");
                }
                else if (integer.empty())
                {
                    FailExpected("a digit, 'Infinity' or 'NaN'");
                }

                std::string exponent;
                if (Peek() == 'e' || Peek() == 'E')
                {
                    Advance();
                    if (Peek() == '+' || Peek() == '-')
                    {
                        if (Peek() == '-')
                            exponent = "-";
                        Advance();
                    }
                    if (!IsDigit(Peek()))
                        FailExpected("a digit in the exponent");
                    while (IsDigit(Peek()))
                    {
                        exponent += static_cast<char>(Peek());
                        Advance();
                    }
                }

                if (!point && exponent.empty())
                    return IntegerNumber(integer, negative);
                return DecimalNumber(integer, fraction, exponent, negative);
            }

            Number ParseHex(bool negative)
            {
                if (!IsHexDigit(Peek()))
                    FailExpected("a hexadecimal digit");
                std::uint64_t magnitude = 0;
                double wide = 0.0;
                bool overflowed = false;
                while (IsHexDigit(Peek()))
                {
                    const std::uint32_t digit = HexValue(Peek());
                    if (!overflowed && magnitude > (std::numeric_limits<std::uint64_t>::max() >> 4))
                    {
                        overflowed = true;
                        wide = static_cast<double>(magnitude);
                    }
                    if (overflowed)
                        wide = wide * 16.0 + digit;
                    else
                        magnitude = magnitude * 16 + digit;
                    Advance();
                }
                if (overflowed)
                    return Number(negative ? -wide : wide);
                return FromMagnitude(magnitude, negative);
            }

            // The width rule: signed when it fits std::int64_t, unsigned when only std::uint64_t holds it. Negative
            // zero is JSON5's -0, which no integer holds, so it is a double.
            static Number FromMagnitude(std::uint64_t magnitude, bool negative)
            {
                constexpr std::uint64_t signedMax = std::numeric_limits<std::int64_t>::max();
                if (negative && magnitude == 0)
                    return Number(-0.0);
                if (!negative)
                    return magnitude <= signedMax ? Number(static_cast<std::int64_t>(magnitude)) : Number(magnitude);
                if (magnitude <= signedMax)
                    return Number(-static_cast<std::int64_t>(magnitude));
                if (magnitude == signedMax + 1)
                    return Number(std::numeric_limits<std::int64_t>::min());
                return Number(-static_cast<double>(magnitude));
            }

            static Number IntegerNumber(const std::string& digits, bool negative)
            {
                std::uint64_t magnitude = 0;
                const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), magnitude);
                if (error == std::errc{})
                    return FromMagnitude(magnitude, negative);
                // Wider than 64 bits: a double, as a number with a point would be.
                return DecimalNumber(digits, "", "", negative);
            }

            static Number DecimalNumber(const std::string& integer, const std::string& fraction,
                                        const std::string& exponent, bool negative)
            {
                std::string text = integer.empty() ? "0" : integer;
                if (!fraction.empty())
                    text += "." + fraction;
                if (!exponent.empty())
                    text += "e" + exponent;
                double value = 0.0;
                const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                if (error == std::errc::result_out_of_range)
                {
                    // from_chars leaves the value alone when out of range, so whether it was too large or too small
                    // is worked out here: the power of ten of the first significant digit decides.
                    value = Magnitude(integer, fraction, exponent) > 0 ? std::numeric_limits<double>::infinity() : 0.0;
                }
                return Number(negative ? -value : value);
            }

            // Where the first significant digit sits, as a power of ten plus one: positive when the number is at
            // least 1, and no more than zero when it is below 1. Only its sign matters, so the sums saturate.
            static std::int64_t Magnitude(const std::string& integer, const std::string& fraction,
                                          const std::string& exponent)
            {
                constexpr std::int64_t saturated = std::int64_t{1} << 40;
                std::int64_t power = 0;
                const bool exponentNegative = !exponent.empty() && exponent.front() == '-';
                for (const char digit : std::string_view(exponent).substr(exponentNegative ? 1 : 0))
                    power = std::min(power * 10 + (digit - '0'), saturated);
                if (exponentNegative)
                    power = -power;

                const std::size_t integerStart = integer.find_first_not_of('0');
                if (integerStart != std::string::npos)
                    return power + static_cast<std::int64_t>(integer.size() - integerStart);
                const std::size_t fractionStart = fraction.find_first_not_of('0');
                return power - static_cast<std::int64_t>(fractionStart == std::string::npos ? 0 : fractionStart);
            }
        };
    } // namespace

    std::string FormatDiagnostic(const Diagnostic& diagnostic)
    {
        return std::format("{}:{}:{}: {}", diagnostic.path, diagnostic.line, diagnostic.column, diagnostic.message);
    }

    ReadTreeResult ReadText(std::string_view text, std::string_view path, ILogger& logger)
    {
        ReadTreeResult result;
        try
        {
            Parser parser(text, result.diagnostics, path);
            result.tree = parser.ParseRoot();
        }
        catch (const SyntaxError& error)
        {
            // A syntax error ends the read, and is its one diagnostic: warnings found before it are dropped with
            // the tree they were about.
            result.diagnostics.assign(1, Diagnostic{DiagnosticSeverity::Error, std::string(path), error.at.line,
                                                    error.at.column, error.message});
        }
        for (const Diagnostic& diagnostic : result.diagnostics)
        {
            if (diagnostic.severity == DiagnosticSeverity::Error)
                logger.Error("{}", FormatDiagnostic(diagnostic));
            else
                logger.Warn("{}", FormatDiagnostic(diagnostic));
        }
        return result;
    }
} // namespace Fenrir::Serialisation
