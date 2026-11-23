// The tree encoding (the spec, "The tree encoding and `ReadTree`"): the bytes each rule writes, worked out by hand from
// the layout in Encoding.hpp; the goldens and every number width through a round trip; and the one located diagnostic
// a buffer that is not an encoding gives.
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Text.hpp"

#include "SerialisationTest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Fenrir::Serialisation;
using SerialisationTest::ReadFixture;
using SerialisationTest::RecordingLogger;

namespace
{
    constexpr std::string_view kGoldens[] = {"sword.asset.json5", "level1.scene.json5"};

    std::vector<std::byte> Bytes(std::initializer_list<int> values)
    {
        std::vector<std::byte> bytes;
        for (int value : values)
            bytes.push_back(static_cast<std::byte>(value));
        return bytes;
    }

    // The tag and version every encoding starts with: a zero, `FVT`, then version 1.
    std::vector<std::byte> Header()
    {
        return Bytes({0x00, 'F', 'V', 'T', 0x01});
    }

    // The header followed by `body`.
    std::vector<std::byte> Encoded(std::initializer_list<int> body)
    {
        std::vector<std::byte> bytes = Header();
        for (int value : body)
            bytes.push_back(static_cast<std::byte>(value));
        return bytes;
    }

    // Decodes bytes that must decode, and fails the test when they give no tree.
    Value Decode(const std::vector<std::byte>& bytes)
    {
        RecordingLogger logger;
        ReadTreeResult result = DecodeTree(bytes, "test.bin", logger);
        INFO("the bytes decode: " << (logger.errors.empty() ? std::string("no error") : logger.errors.front()));
        REQUIRE(result.tree.has_value());
        return std::move(*result.tree);
    }

    // The one diagnostic bytes that must fail give.
    Diagnostic Fail(const std::vector<std::byte>& bytes)
    {
        RecordingLogger logger;
        ReadTreeResult result = DecodeTree(bytes, "test.bin", logger);
        INFO("the bytes fail with no tree and one error diagnostic");
        REQUIRE_FALSE(result.tree.has_value());
        REQUIRE(result.diagnostics.size() == 1);
        REQUIRE(result.diagnostics.front().severity == DiagnosticSeverity::Error);
        return result.diagnostics.front();
    }

    // Requires the diagnostic to be about byte `offset`: its message names it, and its column is the offset plus one.
    void RequireAtByte(const Diagnostic& diagnostic, std::size_t offset)
    {
        INFO("the diagnostic '" << diagnostic.message << "' at column " << diagnostic.column << " names byte "
                                << offset);
        REQUIRE(diagnostic.message.find("byte " + std::to_string(offset)) != std::string::npos);
        REQUIRE(diagnostic.line == 1);
        REQUIRE(diagnostic.column == offset + 1);
    }

    // A golden's tree, read from its text.
    Value ReadGolden(std::string_view name)
    {
        RecordingLogger logger;
        ReadTreeResult result = ReadText(ReadFixture(name), name, logger);
        INFO("the golden " << name << " reads");
        REQUIRE(result.tree.has_value());
        return std::move(*result.tree);
    }

    bool HasComments(const Value& value);

    template <typename Slot>
    bool SlotHasComments(const Slot& slot)
    {
        return !slot.leadingComments.empty() || slot.trailingComment.has_value() || HasComments(slot.value);
    }

    // Whether any comment is anywhere in the tree under `value`, its own leading comments included.
    bool HasComments(const Value& value)
    {
        if (!value.leadingComments.empty())
            return true;
        if (value.Kind() == ValueKind::Object)
        {
            const Object& object = value.AsObject();
            if (!object.closingComments.empty())
                return true;
            for (const Member& member : object.members)
                if (SlotHasComments(member))
                    return true;
        }
        if (value.Kind() == ValueKind::Array)
        {
            const Array& array = value.AsArray();
            if (!array.closingComments.empty())
                return true;
            for (const Element& element : array.elements)
                if (SlotHasComments(element))
                    return true;
        }
        return false;
    }

    // An object with one member `a: true` carrying a leading and a trailing comment.
    Value CommentedMember()
    {
        Object object;
        Member& member = object.Add("a", true);
        member.leadingComments.push_back("// a");
        member.trailingComment = "// t";
        return object;
    }
} // namespace

TEST_CASE("An encoding starts with a zero byte, the tag FVT and version 1", "[serialisation][encoding]")
{
    INFO("a null root encodes as the header and the null tag");
    REQUIRE(EncodeTree(Value(nullptr), Trivia::Drop) == Encoded({0x01}));
}

TEST_CASE("Bools encode as their own tags", "[serialisation][encoding]")
{
    INFO("false is tag 02 and true is tag 03");
    REQUIRE(EncodeTree(Value(false), Trivia::Drop) == Encoded({0x02}));
    REQUIRE(EncodeTree(Value(true), Trivia::Drop) == Encoded({0x03}));
}

TEST_CASE("Each number encodes little-endian in its own width", "[serialisation][encoding]")
{
    {
        INFO("a signed integer is tag 04 and eight bytes, low byte first");
        REQUIRE(EncodeTree(Value(std::int64_t{0x0102030405060708}), Trivia::Drop) ==
                Encoded({0x04, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01}));
    }
    {
        INFO("an unsigned integer is tag 05 and eight bytes, low byte first");
        REQUIRE(EncodeTree(Value(std::uint64_t{0xF102030405060708}), Trivia::Drop) ==
                Encoded({0x05, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0xF1}));
    }
    {
        INFO("a single float is tag 06 and its four bytes: 1.0f is 3F800000");
        REQUIRE(EncodeTree(Value(1.0f), Trivia::Drop) == Encoded({0x06, 0x00, 0x00, 0x80, 0x3F}));
    }
    {
        INFO("a double float is tag 07 and its eight bytes: 1.0 is 3FF0000000000000");
        REQUIRE(EncodeTree(Value(1.0), Trivia::Drop) ==
                Encoded({0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F}));
    }
}

TEST_CASE("A string encodes as its byte length and its UTF-8", "[serialisation][encoding]")
{
    INFO("\"h\\u00e9\" is tag 08, length 3, then 68 C3 A9");
    REQUIRE(EncodeTree(Value("h\xC3\xA9"), Trivia::Drop) == Encoded({0x08, 0x03, 0x68, 0xC3, 0xA9}));
}

TEST_CASE("A length of 128 or more is several LEB128 bytes", "[serialisation][encoding]")
{
    const std::vector<std::byte> bytes = EncodeTree(Value(std::string(200, 'x')), Trivia::Drop);
    INFO("200 is C8 01 in LEB128, so 200 x's follow three bytes after the header");
    REQUIRE(bytes.size() == Header().size() + 3 + 200);
    REQUIRE(std::vector<std::byte>(bytes.begin(), bytes.begin() + 8) == Encoded({0x08, 0xC8, 0x01}));
}

TEST_CASE("An object encodes its member count, then each key and value in order", "[serialisation][encoding]")
{
    Object object;
    object.Add("b", true);
    object.Add("a", false);
    INFO("{b: true, a: false} is tag 09, count 2, then b's key and true, then a's key and false");
    REQUIRE(EncodeTree(Value(std::move(object)), Trivia::Drop) ==
            Encoded({0x09, 0x02, 0x01, 'b', 0x03, 0x01, 'a', 0x02}));
}

TEST_CASE("An array encodes its element count, then each element in order", "[serialisation][encoding]")
{
    Array array;
    array.Add("a");
    array.Add(nullptr);
    INFO("[\"a\", null] is tag 0A, count 2, then the string and the null");
    REQUIRE(EncodeTree(Value(std::move(array)), Trivia::Drop) == Encoded({0x0A, 0x02, 0x08, 0x01, 'a', 0x01}));
}

TEST_CASE("Trivia::Keep writes a member's comments as a trivia node in front of its value", "[serialisation][encoding]")
{
    INFO("after the key comes tag 0B, one leading comment, one trailing comment, no closing comments, then true");
    REQUIRE(EncodeTree(CommentedMember(), Trivia::Keep) ==
            Encoded({0x09, 0x01, 0x01, 'a', 0x0B, 0x01, 0x04, '/', '/', ' ', 'a', 0x01, 0x04, '/', '/', ' ', 't', 0x00,
                     0x03}));
}

TEST_CASE("Trivia::Drop writes no trivia node", "[serialisation][encoding]")
{
    INFO("the commented member encodes as if it had no comments");
    REQUIRE(EncodeTree(CommentedMember(), Trivia::Drop) == Encoded({0x09, 0x01, 0x01, 'a', 0x03}));
}

TEST_CASE("Each golden encoded with Trivia::Keep decodes to its tree, comments included", "[serialisation][encoding]")
{
    for (std::string_view name : kGoldens)
    {
        const Value tree = ReadGolden(name);
        const Value decoded = Decode(EncodeTree(tree, Trivia::Keep));
        INFO("the decoded " << name << " equals its tree, comment for comment");
        REQUIRE(Equals(decoded, tree, Trivia::Keep));
    }
}

TEST_CASE("Each golden encoded with Trivia::Keep decodes to a tree that writes the golden's bytes",
          "[serialisation][encoding]")
{
    for (std::string_view name : kGoldens)
    {
        const Value decoded = Decode(EncodeTree(ReadGolden(name), Trivia::Keep));
        INFO("WriteText of the decoded " << name << " gives the golden byte for byte");
        REQUIRE(WriteText(decoded) == ReadFixture(name));
    }
}

TEST_CASE("Each golden encoded with Trivia::Drop decodes to its values with no comments", "[serialisation][encoding]")
{
    for (std::string_view name : kGoldens)
    {
        const Value tree = ReadGolden(name);
        const Value decoded = Decode(EncodeTree(tree, Trivia::Drop));
        {
            INFO("the decoded " << name << " holds the golden's values");
            REQUIRE(Equals(decoded, tree, Trivia::Drop));
        }
        {
            INFO("the decoded " << name << " holds no comment");
            REQUIRE_FALSE(HasComments(decoded));
        }
    }
}

TEST_CASE("Each golden's encoding is smaller than its text", "[serialisation][encoding]")
{
    for (std::string_view name : kGoldens)
    {
        const std::string text = ReadFixture(name);
        const std::vector<std::byte> bytes = EncodeTree(ReadGolden(name), Trivia::Keep);
        INFO(name << " is " << text.size() << " bytes of text and " << bytes.size() << " bytes encoded, comments kept");
        REQUIRE(bytes.size() < text.size());
    }
}

TEST_CASE("Every number width survives the round trip, bit for bit at its limits", "[serialisation][encoding]")
{
    Array numbers;
    numbers.Add(std::numeric_limits<std::int64_t>::min());
    numbers.Add(std::numeric_limits<std::int64_t>::max());
    numbers.Add(std::numeric_limits<std::uint64_t>::max());
    numbers.Add(std::numeric_limits<float>::denorm_min());
    numbers.Add(-0.0f);
    numbers.Add(std::numeric_limits<double>::denorm_min());
    numbers.Add(-0.0);
    const Value decoded = Decode(EncodeTree(Value(numbers), Trivia::Drop));
    const Array& out = decoded.AsArray();
    INFO("the decoded array holds seven numbers");
    REQUIRE(out.elements.size() == 7);

    INFO("each integer keeps its width and value, and each float its width and bits");
    REQUIRE(out.elements[0].value.AsNumber().AsSigned() == std::numeric_limits<std::int64_t>::min());
    REQUIRE(out.elements[1].value.AsNumber().AsSigned() == std::numeric_limits<std::int64_t>::max());
    REQUIRE(out.elements[2].value.AsNumber().AsUnsigned() == std::numeric_limits<std::uint64_t>::max());
    REQUIRE(std::bit_cast<std::uint32_t>(out.elements[3].value.AsNumber().AsSingle()) == 0x00000001u);
    REQUIRE(std::bit_cast<std::uint32_t>(out.elements[4].value.AsNumber().AsSingle()) == 0x80000000u);
    REQUIRE(std::bit_cast<std::uint64_t>(out.elements[5].value.AsNumber().AsDouble()) == 0x0000000000000001u);
    REQUIRE(std::bit_cast<std::uint64_t>(out.elements[6].value.AsNumber().AsDouble()) == 0x8000000000000000u);
}

TEST_CASE("Infinity and NaN survive the round trip bit for bit in both float widths", "[serialisation][encoding]")
{
    // A NaN with a payload, so a decoder that wrote a NaN of its own would be seen.
    const std::uint32_t singleNaN = 0x7FC00123u;
    const std::uint64_t doubleNaN = 0x7FF8000000000123u;
    Array numbers;
    numbers.Add(std::numeric_limits<float>::infinity());
    numbers.Add(-std::numeric_limits<float>::infinity());
    numbers.Add(std::bit_cast<float>(singleNaN));
    numbers.Add(std::numeric_limits<double>::infinity());
    numbers.Add(-std::numeric_limits<double>::infinity());
    numbers.Add(std::bit_cast<double>(doubleNaN));
    const Value decoded = Decode(EncodeTree(Value(numbers), Trivia::Drop));
    const Array& out = decoded.AsArray();
    INFO("the decoded array holds six numbers");
    REQUIRE(out.elements.size() == 6);

    INFO("each infinity keeps its sign and each NaN its payload");
    REQUIRE(std::bit_cast<std::uint32_t>(out.elements[0].value.AsNumber().AsSingle()) == 0x7F800000u);
    REQUIRE(std::bit_cast<std::uint32_t>(out.elements[1].value.AsNumber().AsSingle()) == 0xFF800000u);
    REQUIRE(std::bit_cast<std::uint32_t>(out.elements[2].value.AsNumber().AsSingle()) == singleNaN);
    REQUIRE(std::bit_cast<std::uint64_t>(out.elements[3].value.AsNumber().AsDouble()) == 0x7FF0000000000000u);
    REQUIRE(std::bit_cast<std::uint64_t>(out.elements[4].value.AsNumber().AsDouble()) == 0xFFF0000000000000u);
    REQUIRE(std::bit_cast<std::uint64_t>(out.elements[5].value.AsNumber().AsDouble()) == doubleNaN);
}

TEST_CASE("A comment in every place a tree holds one survives the round trip", "[serialisation][encoding]")
{
    // The goldens hold no trailing comment, so this tree puts one comment in each slot.
    Array inner;
    Element& element = inner.Add(1);
    element.leadingComments.push_back("// element above");
    element.trailingComment = "// element after";
    inner.closingComments.push_back("// inner end");
    Object object;
    Member& member = object.Add("list", Value(std::move(inner)));
    member.leadingComments.push_back("// member above");
    member.trailingComment = "/* member after */";
    object.closingComments.push_back("/* end */");
    Value root(std::move(object));
    root.leadingComments.push_back("// top");
    INFO("the decoded tree equals the built one, comment for comment");
    REQUIRE(Equals(Decode(EncodeTree(root, Trivia::Keep)), root, Trivia::Keep));
}

TEST_CASE("Every truncation of an encoding gives one diagnostic and no tree", "[serialisation][encoding]")
{
    const std::vector<std::byte> bytes = EncodeTree(ReadGolden("sword.asset.json5"), Trivia::Keep);
    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
        RecordingLogger logger;
        ReadTreeResult result = DecodeTree(std::span<const std::byte>(bytes.data(), length), "sword.bin", logger);
        INFO("the encoding cut to " << length << " of " << bytes.size() << " bytes is refused with one diagnostic");
        REQUIRE_FALSE(result.tree.has_value());
        REQUIRE(result.diagnostics.size() == 1);
    }
}

TEST_CASE("A string cut short names the byte its text began at", "[serialisation][encoding]")
{
    INFO("\"abc\" cut after its first character: the text begins at byte 7");
    RequireAtByte(Fail(Encoded({0x08, 0x03, 'a'})), 7);
}

TEST_CASE("A buffer that ends where a node should be names the offset of its end", "[serialisation][encoding]")
{
    INFO("an array of two with one element: the second is missing at byte 8");
    RequireAtByte(Fail(Encoded({0x0A, 0x02, 0x01})), 8);
}

TEST_CASE("An unknown node tag names the byte it is at", "[serialisation][encoding]")
{
    INFO("tag 7F after the header is at byte 5");
    RequireAtByte(Fail(Encoded({0x7F})), 5);
}

TEST_CASE("A buffer without the encoding's tag is refused at byte 0", "[serialisation][encoding]")
{
    INFO("text bytes are not an encoding");
    RequireAtByte(Fail(Bytes({'{', '}', 0x00, 0x00, 0x01, 0x01})), 0);
}

TEST_CASE("An encoding of an unknown version is refused at its version byte", "[serialisation][encoding]")
{
    INFO("version 2 is at byte 4");
    RequireAtByte(Fail(Bytes({0x00, 'F', 'V', 'T', 0x02, 0x01})), 4);
}

TEST_CASE("Bytes after the root node are refused at the first of them", "[serialisation][encoding]")
{
    INFO("a null root followed by another null: the extra byte is byte 6");
    RequireAtByte(Fail(Encoded({0x01, 0x01})), 6);
}

TEST_CASE("A LEB128 value wider than 64 bits is refused where it began", "[serialisation][encoding]")
{
    INFO("a string length of eleven continuation bytes begins at byte 6");
    RequireAtByte(Fail(Encoded({0x08, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01})), 6);
}

TEST_CASE("A trivia node followed by another is refused at the second", "[serialisation][encoding]")
{
    INFO("two empty trivia nodes before a null: the second is at byte 9");
    RequireAtByte(Fail(Encoded({0x0B, 0x00, 0x00, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x01})), 9);
}

TEST_CASE("A trivia node with closing comments in front of a scalar is refused at the trivia",
          "[serialisation][encoding]")
{
    INFO("a closing comment has no place on a null; the trivia is at byte 5");
    RequireAtByte(Fail(Encoded({0x0B, 0x00, 0x00, 0x01, 0x02, '/', '/', 0x01})), 5);
}

TEST_CASE("Nesting deeper than 256 containers is refused at the container too deep", "[serialisation][encoding]")
{
    std::vector<std::byte> bytes = Header();
    for (int depth = 0; depth < 257; ++depth)
    {
        bytes.push_back(std::byte{0x0A});
        bytes.push_back(std::byte{0x01});
    }
    bytes.push_back(std::byte{0x01});
    INFO("the 257th array begins at byte " << 5 + 256 * 2);
    RequireAtByte(Fail(bytes), 5 + 256 * 2);
}

TEST_CASE("Nesting 256 containers deep decodes", "[serialisation][encoding]")
{
    std::vector<std::byte> bytes = Header();
    for (int depth = 0; depth < 256; ++depth)
    {
        bytes.push_back(std::byte{0x0A});
        bytes.push_back(std::byte{0x01});
    }
    bytes.push_back(std::byte{0x01});
    INFO("256 nested arrays are within the limit");
    REQUIRE(Decode(bytes).Kind() == ValueKind::Array);
}

TEST_CASE("A refused buffer's diagnostic is logged once through Error, with its path", "[serialisation][encoding]")
{
    RecordingLogger logger;
    ReadTreeResult result = DecodeTree(Encoded({0x7F}), "data/sword.bin", logger);
    INFO("one error is logged as path:line:column: message, and no warning");
    REQUIRE(logger.errors.size() == 1);
    REQUIRE(logger.warnings.empty());
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics.front().path == "data/sword.bin");
    REQUIRE(logger.errors.front() == "data/sword.bin:1:6: " + result.diagnostics.front().message);
}
