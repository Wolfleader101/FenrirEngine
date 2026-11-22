// The JSON5 reader (the spec, "The JSON5 reader"): the grammar, one rule per test, then locations, comments as trivia,
// and the diagnostics. The expected values are worked out by hand from the JSON5 specification.
#include "FenrirSerialisation/Text.hpp"

#include "SerialisationTest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace Fenrir::Serialisation;
using SerialisationTest::RecordingLogger;

namespace
{
    // Reads text that must read, and fails the test when it gives no tree.
    Value Read(std::string_view text)
    {
        RecordingLogger logger;
        ReadTreeResult result = ReadText(text, "test.json5", logger);
        INFO("the text reads: " << (logger.errors.empty() ? std::string("no error") : logger.errors.front()));
        REQUIRE(result.tree.has_value());
        return std::move(*result.tree);
    }

    // The one diagnostic text that must fail gives.
    Diagnostic Fail(std::string_view text)
    {
        RecordingLogger logger;
        ReadTreeResult result = ReadText(text, "test.json5", logger);
        INFO("the text fails with no tree and one diagnostic");
        REQUIRE_FALSE(result.tree.has_value());
        REQUIRE(result.diagnostics.size() == 1);
        return result.diagnostics.front();
    }

    // The string a lone JSON5 string reads as.
    std::string ReadString(std::string_view text)
    {
        return Read(text).AsString();
    }

    // The number a lone JSON5 number reads as.
    Number ReadNumber(std::string_view text)
    {
        return Read(text).AsNumber();
    }

    // A `depth`-deep nest of arrays around a zero.
    std::string NestedArrays(int depth)
    {
        return std::string(depth, '[') + "0" + std::string(depth, ']');
    }
} // namespace

TEST_CASE("A single-quoted string reads as a double-quoted one does", "[serialisation][read]")
{
    INFO("either quote may hold the other unescaped");
    REQUIRE(ReadString("'say \"hi\"'") == "say \"hi\"");
    REQUIRE(ReadString("\"it's\"") == "it's");
}

TEST_CASE("Each string escape reads as its character", "[serialisation][read]")
{
    INFO("the single-character escapes");
    REQUIRE(ReadString("\"\\' \\\" \\\\ \\/ \\b \\f \\n \\r \\t \\v\"") == "' \" \\ / \b \f \n \r \t \v");

    INFO("\\0 is a NUL, and \\xHH a code point below 256 in UTF-8");
    REQUIRE(ReadString("\"a\\0b\"") == std::string("a\0b", 3));
    REQUIRE(ReadString("\"\\x41\\xe9\"") == "A\xC3\xA9");

    INFO("\\uHHHH is a code point in UTF-8, and a surrogate pair one above U+FFFF");
    REQUIRE(ReadString("\"\\u00E9\\u4e2d\"") == "\xC3\xA9\xE4\xB8\xAD");
    REQUIRE(ReadString("\"\\uD83D\\uDE00\"") == "\xF0\x9F\x98\x80");

    INFO("any other character escapes itself");
    REQUIRE(ReadString("\"\\q\\$\"") == "q$");
}

TEST_CASE("A digit escape other than \\0 is a syntax error", "[serialisation][read]")
{
    const Diagnostic diagnostic = Fail("\"a\\1\"");

    INFO("\\1 would be an octal escape, which JSON5 forbids; the error is at the digit");
    REQUIRE(diagnostic.line == 1);
    REQUIRE(diagnostic.column == 4);
}

TEST_CASE("A line continuation in a string reads as nothing", "[serialisation][read]")
{
    INFO("a backslash before LF, CRLF, CR, U+2028 or U+2029 joins the lines");
    REQUIRE(ReadString("'one \\\ntwo'") == "one two");
    REQUIRE(ReadString("'one \\\r\ntwo'") == "one two");
    REQUIRE(ReadString("'one \\\rtwo'") == "one two");
    REQUIRE(ReadString("'one \\\xE2\x80\xA8two'") == "one two");
    REQUIRE(ReadString("'one \\\xE2\x80\xA9two'") == "one two");
}

TEST_CASE("An unescaped line break in a string is a syntax error at the break", "[serialisation][read]")
{
    const Diagnostic diagnostic = Fail("\"one\ntwo\"");

    INFO("the string's line break is the fifth character of line 1");
    REQUIRE(diagnostic.line == 1);
    REQUIRE(diagnostic.column == 5);
}

TEST_CASE("A hexadecimal number reads as its integer value", "[serialisation][read]")
{
    INFO("either case of x and of the digits, and a sign");
    REQUIRE(ReadNumber("0xDEADbeef") == Number(std::int64_t{0xDEADBEEF}));
    REQUIRE(ReadNumber("0X1f") == Number(std::int64_t{31}));
    REQUIRE(ReadNumber("-0x10") == Number(std::int64_t{-16}));
    REQUIRE(ReadNumber("+0xFFFFFFFFFFFFFFFF") == Number(std::uint64_t{0xFFFFFFFFFFFFFFFF}));
}

TEST_CASE("A leading or trailing decimal point reads as a double", "[serialisation][read]")
{
    INFO(".5, 5., a signed .5 and 5.e1 are all decimal numbers");
    REQUIRE(ReadNumber(".5") == Number(0.5));
    REQUIRE(ReadNumber("5.") == Number(5.0));
    REQUIRE(ReadNumber("-.5") == Number(-0.5));
    REQUIRE(ReadNumber("+5.e1") == Number(50.0));
}

TEST_CASE("A sign reads on integers and decimals", "[serialisation][read]")
{
    INFO("+ is allowed as - is");
    REQUIRE(ReadNumber("+10") == Number(std::int64_t{10}));
    REQUIRE(ReadNumber("-10") == Number(std::int64_t{-10}));
    REQUIRE(ReadNumber("+1.5") == Number(1.5));
    REQUIRE(ReadNumber("-1.5e-1") == Number(-0.15));
}

TEST_CASE("Infinity and NaN read as doubles, with a sign", "[serialisation][read]")
{
    INFO("Infinity with either sign or none");
    REQUIRE(ReadNumber("Infinity") == Number(std::numeric_limits<double>::infinity()));
    REQUIRE(ReadNumber("+Infinity") == Number(std::numeric_limits<double>::infinity()));
    REQUIRE(ReadNumber("-Infinity") == Number(-std::numeric_limits<double>::infinity()));

    INFO("NaN, signed or not, is a double NaN");
    REQUIRE(ReadNumber("NaN") == Number(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE(ReadNumber("-NaN") == Number(std::numeric_limits<double>::quiet_NaN()));
}

TEST_CASE("A number keeps the width its text implies", "[serialisation][read]")
{
    INFO("an integer that fits std::int64_t is signed, at both ends of its range");
    REQUIRE(ReadNumber("9223372036854775807") == Number(std::numeric_limits<std::int64_t>::max()));
    REQUIRE(ReadNumber("-9223372036854775808") == Number(std::numeric_limits<std::int64_t>::min()));

    INFO("a larger one is unsigned");
    REQUIRE(ReadNumber("9223372036854775808") == Number(std::uint64_t{9223372036854775808u}));
    REQUIRE(ReadNumber("18446744073709551615") == Number(std::numeric_limits<std::uint64_t>::max()));

    INFO("one wider than 64 bits, or below the signed range, is a double");
    REQUIRE(ReadNumber("18446744073709551616") == Number(18446744073709551616.0));
    REQUIRE(ReadNumber("-9223372036854775809") == Number(-9223372036854775809.0));

    INFO("a point or an exponent makes a double even for a whole number");
    REQUIRE(ReadNumber("1.0") == Number(1.0));
    REQUIRE(ReadNumber("1e2") == Number(100.0));
    REQUIRE(ReadNumber("0") == Number(std::int64_t{0}));
}

TEST_CASE("A leading zero before a digit is a syntax error", "[serialisation][read]")
{
    const Diagnostic diagnostic = Fail("[1, 012]");

    INFO("an octal-looking number is refused at its second digit");
    REQUIRE(diagnostic.line == 1);
    REQUIRE(diagnostic.column == 6);
}

TEST_CASE("A number run into a letter is a syntax error", "[serialisation][read]")
{
    const Diagnostic diagnostic = Fail("[12px]");

    INFO("the letter straight after the number is the error");
    REQUIRE(diagnostic.line == 1);
    REQUIRE(diagnostic.column == 4);
}

TEST_CASE("true, false and null read as their values", "[serialisation][read]")
{
    INFO("the three literals");
    REQUIRE(Read("true") == Value(true));
    REQUIRE(Read("false") == Value(false));
    REQUIRE(Read("null") == Value(nullptr));
}

TEST_CASE("An identifier key reads as its name", "[serialisation][read]")
{
    const Value value = Read(
        "{plain: 1, _under: 2, $dollar: 3, while: 4, sig\\u03A3ma: 5, \xC3\xBCml: 6, \"quoted key\": 7, 'single': 8}");
    const Object& object = value.AsObject();

    INFO("bare, underscore, dollar, reserved word, escaped, non-ASCII and quoted keys, in order");
    REQUIRE(object.members.size() == 8);
    REQUIRE(object.members[0].key == "plain");
    REQUIRE(object.members[1].key == "_under");
    REQUIRE(object.members[2].key == "$dollar");
    REQUIRE(object.members[3].key == "while");
    REQUIRE(object.members[4].key == "sig\xCE\xA3ma");
    REQUIRE(object.members[5].key == "\xC3\xBCml");
    REQUIRE(object.members[6].key == "quoted key");
    REQUIRE(object.members[7].key == "single");
}

TEST_CASE("Objects and arrays read their members and elements in order", "[serialisation][read]")
{
    const Value value = Read("{b: [1, 'two', [], {}], a: {c: null,},}");

    Object expectedInner;
    expectedInner.Add("c", nullptr);
    Array expectedList;
    expectedList.Add(1);
    expectedList.Add("two");
    expectedList.Add(Array{});
    expectedList.Add(Object{});
    Object expected;
    expected.Add("b", std::move(expectedList));
    expected.Add("a", std::move(expectedInner));

    INFO("the tree is the text's, key order kept and trailing commas allowed");
    REQUIRE(value == Value(std::move(expected)));
}

TEST_CASE("Every node carries the line and column its text began at", "[serialisation][read]")
{
    const Value value = Read("{\n  a: [1,\n\t2],\n  \xC3\xA9: 'x',\r\n  b: -3,\n}");
    const Object& object = value.AsObject();
    const Array& array = object.members[0].value.AsArray();

    INFO("the root begins at 1:1");
    REQUIRE(value.location == Location{1, 1});

    INFO("a's array at 2:6 and its elements at 2:7 and, after a tab counted as one column, 3:2");
    REQUIRE(object.members[0].value.location == Location{2, 6});
    REQUIRE(array.elements[0].value.location == Location{2, 7});
    REQUIRE(array.elements[1].value.location == Location{3, 2});

    INFO("columns count code points, so the value after a two-byte key is at 4:6");
    REQUIRE(object.members[1].value.location == Location{4, 6});

    INFO("CRLF ends one line, and a number's location is its sign's");
    REQUIRE(object.members[2].value.location == Location{5, 6});
}

TEST_CASE("Comments are kept as the trivia the writer puts back", "[serialisation][read]")
{
    const Value value = Read("// root\n"
                             "{\n"
                             "  // above a\n"
                             "  /* and a block */\n"
                             "  a: 1, // after a\n"
                             "  list: [\n"
                             "    // above 1\n"
                             "    1, /* after 1 */\n"
                             "    // closing list\n"
                             "  ],\n"
                             "  // closing root\n"
                             "}\n");
    const Object& object = value.AsObject();
    const Element& element = object.members[1].value.AsArray().elements[0];

    INFO("the root's comment is its leading comment");
    REQUIRE(value.leadingComments == std::vector<std::string>{"// root"});

    INFO("comments above a member lead it and one after it on its line trails it, verbatim");
    REQUIRE(object.members[0].leadingComments == std::vector<std::string>{"// above a", "/* and a block */"});
    REQUIRE(object.members[0].trailingComment == "// after a");

    INFO("an element's comments, and the comment before the array's closing bracket");
    REQUIRE(element.leadingComments == std::vector<std::string>{"// above 1"});
    REQUIRE(element.trailingComment == "/* after 1 */");
    REQUIRE(object.members[1].value.AsArray().closingComments == std::vector<std::string>{"// closing list"});

    INFO("the comment before the root's closing brace");
    REQUIRE(object.closingComments == std::vector<std::string>{"// closing root"});
}

TEST_CASE("A commented tree read then written gives back its text", "[serialisation][read]")
{
    const std::string text = "// root\n"
                             "{\n"
                             "  // above a\n"
                             "  a: 1, // after a\n"
                             "  list: [\n"
                             "    1, /* after 1 */\n"
                             "    // closing list\n"
                             "  ],\n"
                             "  // closing root\n"
                             "}\n";

    INFO("each comment goes back where it was read from");
    REQUIRE(WriteText(Read(text)) == text);
}

TEST_CASE("A trailing comment before the comma trails its member", "[serialisation][read]")
{
    const Value value = Read("{\n  a: 1 /* one */, /* two */\n  b: 2 // last, no comma\n}");
    const Object& object = value.AsObject();

    INFO("both comments on a's line join into its trailing comment");
    REQUIRE(object.members[0].trailingComment == "/* one */ /* two */");

    INFO("a comment after the last member with no comma trails it");
    REQUIRE(object.members[1].trailingComment == "// last, no comma");
}

TEST_CASE("A comment inside a member before its value moves to the member's leading comments", "[serialisation][read]")
{
    const Value value = Read("{\n  // above\n  a /* key */ : /* value */ 1,\n}");

    INFO("the member has no other place for them, so they lead it in the order read");
    REQUIRE(value.AsObject().members[0].leadingComments ==
            std::vector<std::string>{"// above", "/* key */", "/* value */"});
}

TEST_CASE("A comment after the root moves inside the root container, or above a scalar root", "[serialisation][read]")
{
    const Value container = Read("{\n  a: 1,\n}\n// after the root\n");
    const Value scalar = Read("null // after the root");

    INFO("after a container root it is the container's closing comment");
    REQUIRE(container.AsObject().closingComments == std::vector<std::string>{"// after the root"});

    INFO("after a scalar root it is the root's leading comment");
    REQUIRE(scalar.leadingComments == std::vector<std::string>{"// after the root"});
}

TEST_CASE("A block comment's line breaks read as LF", "[serialisation][read]")
{
    const Value value = Read("/* one\r\ntwo\rthree */\nnull");

    INFO("CRLF and CR inside the comment are kept as LF");
    REQUIRE(value.leadingComments == std::vector<std::string>{"/* one\ntwo\nthree */"});
}

TEST_CASE("Comment markers inside a string are text", "[serialisation][read]")
{
    INFO("// and /* in a string are its characters");
    REQUIRE(ReadString("'a // b /* c */'") == "a // b /* c */");
}

TEST_CASE("JSON5's whitespace set separates tokens", "[serialisation][read]")
{
    // Tab, vertical tab, form feed, space, NBSP, U+FEFF, the line terminators U+2028 and U+2029, and the Zs spaces
    // U+1680, U+2003, U+202F, U+205F and U+3000.
    const std::string space = "\t\v\f \xC2\xA0\xEF\xBB\xBF\xE2\x80\xA8\xE2\x80\xA9\xE1\x9A\x80\xE2\x80\x83\xE2\x80\xAF"
                              "\xE2\x81\x9F\xE3\x80\x80";
    const Value value = Read(space + "[" + space + "1" + space + "," + space + "2" + space + "]" + space);

    Array expected;
    expected.Add(1);
    expected.Add(2);

    INFO("every whitespace code point is skipped");
    REQUIRE(value == Value(std::move(expected)));
}

TEST_CASE("A byte order mark reads as if absent", "[serialisation][read]")
{
    const std::string text = "// c\n{a: [1, 2]}\n";
    const Value withBom = Read("\xEF\xBB\xBF" + text);
    const Value without = Read(text);

    INFO("the tree, comments included, is the one without the mark");
    REQUIRE(Equals(withBom, without, Trivia::Keep));

    INFO("the mark takes no column, so the root is still at 2:1");
    REQUIRE(withBom.location == Location{2, 1});
}

TEST_CASE("CRLF line endings read as if they were LF", "[serialisation][read]")
{
    const std::string lf = "// c\n{\n  a: 1, // t\n  /* x\n  y */\n  b: [2],\n}\n";
    std::string crlf;
    for (const char c : lf)
        crlf += c == '\n' ? std::string("\r\n") : std::string(1, c);

    const Value fromCrlf = Read(crlf);
    const Value fromLf = Read(lf);

    INFO("the tree, comments included, is the LF one");
    REQUIRE(Equals(fromCrlf, fromLf, Trivia::Keep));

    INFO("and so are the locations: b's array at 6:6 in both");
    REQUIRE(fromCrlf.AsObject().members[1].value.location == Location{6, 6});
    REQUIRE(fromLf.AsObject().members[1].value.location == Location{6, 6});
}

TEST_CASE("A syntax error gives no tree and one diagnostic saying what was expected and found",
          "[serialisation][read][diagnostic]")
{
    RecordingLogger logger;
    const ReadTreeResult result = ReadText("{\n  a: 1\n  b: 2\n}", "levels/one.json5", logger);

    INFO("no tree, and one error");
    REQUIRE_FALSE(result.tree.has_value());
    REQUIRE(result.diagnostics.size() == 1);
    const Diagnostic& diagnostic = result.diagnostics.front();
    REQUIRE(diagnostic.severity == DiagnosticSeverity::Error);

    INFO("located at b, where a comma or the closing brace was expected, in the file named");
    REQUIRE(diagnostic.path == "levels/one.json5");
    REQUIRE(diagnostic.line == 3);
    REQUIRE(diagnostic.column == 3);
    REQUIRE(diagnostic.message == "expected ',' or '}', found 'b'");
}

TEST_CASE("A diagnostic reaches the logger as path:line:column: message", "[serialisation][read][diagnostic]")
{
    RecordingLogger logger;
    const ReadTreeResult result = ReadText("[1, 2", "data/list.json5", logger);

    INFO("one error, at the end of the text, logged through Error and nothing through Warn");
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(logger.errors ==
            std::vector<std::string>{"data/list.json5:1:6: expected ',' or ']', found the end of the text"});
    REQUIRE(logger.warnings.empty());

    INFO("the logged line is FormatDiagnostic of the diagnostic returned");
    REQUIRE(FormatDiagnostic(result.diagnostics.front()) == logger.errors.front());
}

TEST_CASE("A duplicate key warns at the later key and keeps the first value", "[serialisation][read][diagnostic]")
{
    RecordingLogger logger;
    const ReadTreeResult result = ReadText("{\n  a: 1,\n  a: 2,\n  b: 3,\n}", "dup.json5", logger);

    INFO("the tree is read");
    REQUIRE(result.tree.has_value());

    Object expected;
    expected.Add("a", 1);
    expected.Add("b", 3);
    INFO("the first a stands and the second is dropped");
    REQUIRE(*result.tree == Value(std::move(expected)));

    INFO("one warning at the second a, logged through Warn and nothing through Error");
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics.front().severity == DiagnosticSeverity::Warning);
    REQUIRE(result.diagnostics.front().line == 3);
    REQUIRE(result.diagnostics.front().column == 3);
    REQUIRE(logger.warnings.size() == 1);
    REQUIRE(logger.warnings.front().starts_with("dup.json5:3:3: "));
    REQUIRE(logger.errors.empty());
}

TEST_CASE("Nesting 256 deep reads", "[serialisation][read][diagnostic]")
{
    INFO("256 arrays inside one another are within the limit");
    REQUIRE(Read(NestedArrays(256)).Kind() == ValueKind::Array);
}

TEST_CASE("Nesting 257 deep is a syntax error at the 257th bracket", "[serialisation][read][diagnostic]")
{
    const Diagnostic arrays = Fail(NestedArrays(257));
    const Diagnostic objects = Fail(std::string(256, '[') + "{}" + std::string(256, ']'));

    INFO("the 257th opening bracket is where the limit is passed");
    REQUIRE(arrays.line == 1);
    REQUIRE(arrays.column == 257);

    INFO("objects count towards the depth as arrays do");
    REQUIRE(objects.column == 257);
}

TEST_CASE("Text after the root value is a syntax error", "[serialisation][read][diagnostic]")
{
    const Diagnostic diagnostic = Fail("{} []");

    INFO("one value per text: the second is the error");
    REQUIRE(diagnostic.column == 4);
    REQUIRE(diagnostic.message == "expected the end of the text, found '['");
}

TEST_CASE("A block comment that never ends is a syntax error where it begins", "[serialisation][read][diagnostic]")
{
    const Diagnostic diagnostic = Fail("[1,\n  /* open");

    INFO("the error points at the comment's opening");
    REQUIRE(diagnostic.line == 2);
    REQUIRE(diagnostic.column == 3);
}

TEST_CASE("Bytes that are not UTF-8 are a syntax error", "[serialisation][read][diagnostic]")
{
    const Diagnostic diagnostic = Fail("['a\xFF']");

    INFO("the stray byte is the error");
    REQUIRE(diagnostic.column == 4);
}
