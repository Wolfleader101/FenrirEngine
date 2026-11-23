#include "FenrirSerialisation/Encoding.hpp"

#include "FenrirLogger/ILogger.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Fenrir::Serialisation
{
    namespace
    {
        // The layout is documented on EncodeTree in Encoding.hpp; a change to it is a change of Version.
        constexpr std::array<std::uint8_t, 4> EncodingTag{0x00, 'F', 'V', 'T'};
        constexpr std::uint8_t Version = 1;
        constexpr int MaxDepth = 256;

        enum NodeTag : std::uint8_t
        {
            NullTag = 0x01,
            FalseTag = 0x02,
            TrueTag = 0x03,
            SignedTag = 0x04,
            UnsignedTag = 0x05,
            SingleTag = 0x06,
            DoubleTag = 0x07,
            StringTag = 0x08,
            ObjectTag = 0x09,
            ArrayTag = 0x0A,
            TriviaTag = 0x0B,
        };

        // A container's closing comments; null for a scalar, which has none.
        template <typename V>
        auto* ClosingComments(V& value)
        {
            if (value.Kind() == ValueKind::Object)
                return &value.AsObject().closingComments;
            return value.Kind() == ValueKind::Array ? &value.AsArray().closingComments : nullptr;
        }

        // The index of the first byte of `text` that is not well-formed UTF-8, if any. The three-byte form of a
        // surrogate passes, since ReadText keeps an escaped lone surrogate so and WriteText writes it.
        std::optional<std::size_t> FirstInvalidUtf8(std::string_view text)
        {
            for (std::size_t i = 0; i < text.size();)
            {
                const auto lead = static_cast<std::uint8_t>(text[i]);
                std::size_t length = 0;
                std::uint32_t min = 0;
                if (lead < 0x80)
                    length = 1;
                else if (lead >= 0xC2 && lead < 0xE0)
                    length = 2, min = 0x80;
                else if (lead >= 0xE0 && lead < 0xF0)
                    length = 3, min = 0x800;
                else if (lead >= 0xF0 && lead < 0xF5)
                    length = 4, min = 0x10000;
                else
                    return i;
                if (text.size() - i < length)
                    return i;
                std::uint32_t code = length == 1 ? lead : lead & (0xFF >> (length + 1));
                for (std::size_t k = 1; k < length; ++k)
                {
                    const auto next = static_cast<std::uint8_t>(text[i + k]);
                    if ((next & 0xC0) != 0x80)
                        return i;
                    code = (code << 6) | (next & 0x3F);
                }
                if (code < min || code > 0x10FFFF)
                    return i;
                i += length;
            }
            return std::nullopt;
        }

        // What holds a decoded node, so what its trivia may carry: the root has no trailing comment.
        enum class Holder
        {
            Root,
            MemberOrElement,
        };

        class Encoder
        {
          public:
            Encoder(std::vector<std::byte>& out, Trivia trivia) : m_out(out), m_trivia(trivia)
            {
            }

            void Root(const Value& root)
            {
                for (std::uint8_t byte : EncodingTag)
                    Byte(byte);
                Byte(Version);
                WithTrivia(root.leadingComments, std::nullopt, root);
            }

          private:
            void Byte(std::uint8_t byte)
            {
                m_out.push_back(static_cast<std::byte>(byte));
            }

            void Leb128(std::uint64_t value)
            {
                while (value >= 0x80)
                {
                    Byte(static_cast<std::uint8_t>(value | 0x80));
                    value >>= 7;
                }
                Byte(static_cast<std::uint8_t>(value));
            }

            template <std::unsigned_integral U>
            void LittleEndian(U value)
            {
                for (std::size_t i = 0; i < sizeof(U); ++i)
                    Byte(static_cast<std::uint8_t>(value >> (8 * i)));
            }

            void Text(std::string_view text)
            {
                Leb128(text.size());
                for (char c : text)
                    Byte(static_cast<std::uint8_t>(c));
            }

            void Comments(const std::vector<std::string>& comments)
            {
                Leb128(comments.size());
                for (const std::string& comment : comments)
                    Text(comment);
            }

            // A node with the comments of the member or element holding it in front, as one trivia node when there
            // are any to keep.
            void WithTrivia(const std::vector<std::string>& leading, const std::optional<std::string>& trailing,
                            const Value& value)
            {
                const std::vector<std::string>* closing = ClosingComments(value);
                const bool any = !leading.empty() || trailing.has_value() || (closing && !closing->empty());
                if (m_trivia == Trivia::Keep && any)
                {
                    Byte(TriviaTag);
                    Comments(leading);
                    Leb128(trailing.has_value() ? 1 : 0);
                    if (trailing)
                        Text(*trailing);
                    Comments(closing ? *closing : std::vector<std::string>{});
                }
                Node(value);
            }

            void Node(const Value& value)
            {
                switch (value.Kind())
                {
                case ValueKind::Null:
                    Byte(NullTag);
                    return;
                case ValueKind::Bool:
                    Byte(value.AsBool() ? TrueTag : FalseTag);
                    return;
                case ValueKind::Number:
                    Number(value.AsNumber());
                    return;
                case ValueKind::String:
                    Byte(StringTag);
                    Text(value.AsString());
                    return;
                case ValueKind::Object:
                    Byte(ObjectTag);
                    Leb128(value.AsObject().members.size());
                    for (const Member& member : value.AsObject().members)
                    {
                        Text(member.key);
                        WithTrivia(member.leadingComments, member.trailingComment, member.value);
                    }
                    return;
                case ValueKind::Array:
                    Byte(ArrayTag);
                    Leb128(value.AsArray().elements.size());
                    for (const Element& element : value.AsArray().elements)
                        WithTrivia(element.leadingComments, element.trailingComment, element.value);
                    return;
                }
            }

            void Number(const Serialisation::Number& number)
            {
                switch (number.Width())
                {
                case NumberWidth::Signed:
                    Byte(SignedTag);
                    LittleEndian(std::bit_cast<std::uint64_t>(number.AsSigned()));
                    return;
                case NumberWidth::Unsigned:
                    Byte(UnsignedTag);
                    LittleEndian(number.AsUnsigned());
                    return;
                case NumberWidth::Single:
                    Byte(SingleTag);
                    LittleEndian(std::bit_cast<std::uint32_t>(number.AsSingle()));
                    return;
                case NumberWidth::Double:
                    Byte(DoubleTag);
                    LittleEndian(std::bit_cast<std::uint64_t>(number.AsDouble()));
                    return;
                }
            }

            std::vector<std::byte>& m_out;
            Trivia m_trivia;
        };

        // Ends a decode: the byte offset the buffer went wrong at, and what was expected and found there.
        struct DecodeError
        {
            std::size_t offset;
            std::string message;
        };

        // The comments a trivia node gives the member or element holding the node after it.
        struct Item
        {
            std::vector<std::string> leading;
            std::optional<std::string> trailing;
        };

        class Decoder
        {
          public:
            explicit Decoder(std::span<const std::byte> bytes) : m_bytes(bytes)
            {
            }

            Value Root()
            {
                if (m_bytes.size() < EncodingTag.size() ||
                    !std::equal(EncodingTag.begin(), EncodingTag.end(), m_bytes.begin(),
                                [](std::uint8_t a, std::byte b) { return a == static_cast<std::uint8_t>(b); }))
                    throw DecodeError{0, "expected the tree encoding's tag 00 46 56 54"};
                m_position = EncodingTag.size();
                const std::uint8_t version = Byte("a version byte");
                if (version != Version)
                    throw DecodeError{EncodingTag.size(),
                                      std::format("expected version {}, found version {}", Version, version)};

                Item item;
                Value root = WithTrivia(0, item, Holder::Root);
                root.leadingComments = std::move(item.leading);
                if (m_position != m_bytes.size())
                    throw DecodeError{m_position, std::format("expected the end of the buffer after the root node, "
                                                              "found {} more bytes",
                                                              m_bytes.size() - m_position)};
                return root;
            }

          private:
            std::size_t Left() const
            {
                return m_bytes.size() - m_position;
            }

            std::uint8_t Byte(std::string_view what)
            {
                if (Left() == 0)
                    throw DecodeError{m_position, std::format("expected {}, found the end of the buffer", what)};
                return static_cast<std::uint8_t>(m_bytes[m_position++]);
            }

            std::uint64_t Leb128(std::string_view what)
            {
                const std::size_t start = m_position;
                std::uint64_t value = 0;
                for (int shift = 0;; shift += 7)
                {
                    if (Left() == 0)
                        throw DecodeError{start, std::format("the buffer ends inside {}", what)};
                    const auto byte = static_cast<std::uint8_t>(m_bytes[m_position++]);
                    // The tenth byte holds bit 63 alone: anything more, a continuation included, overflows.
                    if (shift == 63 && byte > 1)
                        throw DecodeError{start, std::format("{} overflows 64 bits", what)};
                    value |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
                    if ((byte & 0x80) == 0)
                        return value;
                }
            }

            template <std::unsigned_integral U>
            U LittleEndian(std::string_view what)
            {
                if (Left() < sizeof(U))
                    throw DecodeError{m_position, std::format("the buffer ends inside {} of {} bytes, {} left", what,
                                                              sizeof(U), Left())};
                U value = 0;
                for (std::size_t i = 0; i < sizeof(U); ++i)
                    value |= static_cast<U>(static_cast<std::uint8_t>(m_bytes[m_position++])) << (8 * i);
                return value;
            }

            std::string Text(std::string_view what)
            {
                const std::uint64_t length = Leb128(std::format("the length of {}", what));
                if (length > Left())
                    throw DecodeError{m_position, std::format("{} of {} bytes runs past the end of the buffer, {} left",
                                                              what, length, Left())};
                const std::string text(reinterpret_cast<const char*>(m_bytes.data() + m_position),
                                       static_cast<std::size_t>(length));
                if (const std::optional<std::size_t> bad = FirstInvalidUtf8(text))
                    throw DecodeError{m_position + *bad, std::format("expected UTF-8 in {}, found the byte {:02X}",
                                                                     what, static_cast<std::uint8_t>(text[*bad]))};
                m_position += text.size();
                return text;
            }

            std::vector<std::string> Comments(std::string_view what)
            {
                const std::uint64_t count = Leb128(std::format("a count of {} comments", what));
                std::vector<std::string> comments;
                // No reserve: the count is untrusted, and each comment takes at least a byte to read.
                for (std::uint64_t i = 0; i < count; ++i)
                    comments.push_back(Text(std::format("a {} comment", what)));
                return comments;
            }

            // A node, with the trivia node in front of it if there is one: the trivia's leading and trailing comments
            // go to `item`, and its closing comments to the node.
            Value WithTrivia(int depth, Item& item, Holder holder)
            {
                std::size_t tagOffset = m_position;
                std::uint8_t tag = Byte("a node");
                if (tag != TriviaTag)
                    return Node(tag, tagOffset, depth);

                const std::size_t triviaOffset = tagOffset;
                item.leading = Comments("leading");
                const std::size_t trailingOffset = m_position;
                const std::uint64_t trailingCount = Leb128("a count of trailing comments");
                if (trailingCount > 1)
                    throw DecodeError{trailingOffset,
                                      std::format("expected at most one trailing comment, found {}", trailingCount)};
                if (trailingCount == 1)
                {
                    if (holder == Holder::Root)
                        throw DecodeError{triviaOffset, "expected no trailing comment on the root, found one"};
                    item.trailing = Text("a trailing comment");
                }
                std::vector<std::string> closing = Comments("closing");

                tagOffset = m_position;
                tag = Byte("the node the trivia is in front of");
                if (tag == TriviaTag)
                    throw DecodeError{tagOffset, "expected the node the trivia is in front of, found a second trivia "
                                                 "node"};
                if (!closing.empty() && tag != ObjectTag && tag != ArrayTag)
                    throw DecodeError{triviaOffset, std::format("expected closing comments only in front of an object "
                                                                "or an array, found them in front of node tag {:02X}",
                                                                tag)};
                Value value = Node(tag, tagOffset, depth);
                if (auto* comments = ClosingComments(value))
                    *comments = std::move(closing);
                return value;
            }

            Value Node(std::uint8_t tag, std::size_t tagOffset, int depth)
            {
                switch (tag)
                {
                case NullTag:
                    return Value(nullptr);
                case FalseTag:
                    return Value(false);
                case TrueTag:
                    return Value(true);
                case SignedTag:
                    return Value(Number(std::bit_cast<std::int64_t>(LittleEndian<std::uint64_t>("a signed integer"))));
                case UnsignedTag:
                    return Value(Number(LittleEndian<std::uint64_t>("an unsigned integer")));
                case SingleTag:
                    return Value(Number(std::bit_cast<float>(LittleEndian<std::uint32_t>("a single float"))));
                case DoubleTag:
                    return Value(Number(std::bit_cast<double>(LittleEndian<std::uint64_t>("a double float"))));
                case StringTag:
                    return Value(Text("a string"));
                case ObjectTag: {
                    Deeper(tagOffset, depth);
                    const std::uint64_t count = Leb128("a member count");
                    Object object;
                    for (std::uint64_t i = 0; i < count; ++i)
                    {
                        std::string key = Text("a key");
                        Item item;
                        Value value = WithTrivia(depth + 1, item, Holder::MemberOrElement);
                        object.members.push_back(Member{std::move(key), std::move(value), std::move(item.leading),
                                                        std::move(item.trailing)});
                    }
                    return Value(std::move(object));
                }
                case ArrayTag: {
                    Deeper(tagOffset, depth);
                    const std::uint64_t count = Leb128("an element count");
                    Array array;
                    for (std::uint64_t i = 0; i < count; ++i)
                    {
                        Item item;
                        Value value = WithTrivia(depth + 1, item, Holder::MemberOrElement);
                        array.elements.push_back(
                            Element{std::move(value), std::move(item.leading), std::move(item.trailing)});
                    }
                    return Value(std::move(array));
                }
                default:
                    throw DecodeError{tagOffset, std::format("expected a node tag, found the unknown tag {:02X}", tag)};
                }
            }

            static void Deeper(std::size_t tagOffset, int depth)
            {
                if (depth + 1 > MaxDepth)
                    throw DecodeError{tagOffset, std::format("expected nesting of at most {} objects and arrays, "
                                                             "found deeper",
                                                             MaxDepth)};
            }

            std::span<const std::byte> m_bytes;
            std::size_t m_position = 0;
        };
    } // namespace

    std::vector<std::byte> EncodeTree(const Value& root, Trivia trivia)
    {
        std::vector<std::byte> out;
        Encoder(out, trivia).Root(root);
        return out;
    }

    ReadTreeResult DecodeTree(std::span<const std::byte> bytes, std::string_view path, ILogger& logger)
    {
        ReadTreeResult result;
        try
        {
            result.tree = Decoder(bytes).Root();
        }
        catch (const DecodeError& error)
        {
            constexpr std::size_t MaxColumn = std::numeric_limits<std::uint32_t>::max();
            Diagnostic diagnostic;
            diagnostic.severity = DiagnosticSeverity::Error;
            diagnostic.path = std::string(path);
            diagnostic.line = 1;
            diagnostic.column = static_cast<std::uint32_t>(std::min(error.offset, MaxColumn - 1) + 1);
            diagnostic.message = std::format("at byte {}, {}", error.offset, error.message);
            logger.Error("{}", FormatDiagnostic(diagnostic));
            result.diagnostics.push_back(std::move(diagnostic));
        }
        return result;
    }
} // namespace Fenrir::Serialisation
