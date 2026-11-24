// ReadTree (the spec, "The tree encoding and `ReadTree`"): a file's bytes read as JSON5 text or as the tree encoding,
// told apart by their first byte, so a golden's text and its encoding give the same tree.
#include "FenrirSerialisation/ReadTree.hpp"
#include "FenrirSerialisation/Diagnostic.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "SerialisationTest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace Fenrir::Serialisation;
using SerialisationTest::BytesOf;
using SerialisationTest::FirstError;
using SerialisationTest::ReadFixture;
using SerialisationTest::RecordingLogger;

namespace
{
    constexpr std::string_view kGoldens[] = {"sword.asset.json5", "level1.scene.json5"};

    // Reads bytes that must read, and fails the test when they give no tree.
    Value Read(std::span<const std::byte> bytes, std::string_view path)
    {
        RecordingLogger logger;
        ReadTreeResult result = ReadTree(bytes, path, logger);
        INFO(path << " reads: " << FirstError(logger));
        REQUIRE(result.tree.has_value());
        return std::move(*result.tree);
    }

    // The one diagnostic bytes that must fail give.
    Diagnostic Fail(std::span<const std::byte> bytes)
    {
        RecordingLogger logger;
        ReadTreeResult result = ReadTree(bytes, "save.dat", logger);
        INFO("the bytes fail with no tree and one error diagnostic");
        REQUIRE_FALSE(result.tree.has_value());
        REQUIRE(result.diagnostics.size() == 1);
        REQUIRE(result.diagnostics.front().severity == DiagnosticSeverity::Error);
        return result.diagnostics.front();
    }
} // namespace

TEST_CASE("ReadTree reads a golden's text bytes and its encoded bytes as equal trees", "[serialisation][readtree]")
{
    for (std::string_view name : kGoldens)
    {
        const std::string text = ReadFixture(name);
        const Value fromText = Read(BytesOf(text), name);
        const std::vector<std::byte> encoded = EncodeTree(fromText, Trivia::Drop);
        const Value fromEncoding = Read(encoded, name);
        INFO("the tree read from " << name << "'s text equals the one read from its encoding, trivia aside");
        REQUIRE(Equals(fromText, fromEncoding));
    }
}

TEST_CASE("ReadTree reads text bytes as ReadText reads the text, comments and locations included",
          "[serialisation][readtree]")
{
    for (std::string_view name : kGoldens)
    {
        const std::string text = ReadFixture(name);
        RecordingLogger logger;
        const Value expected = ReadText(text, name, logger).tree.value_or(Value{});
        const Value read = Read(BytesOf(text), name);
        INFO(name << "'s bytes read as its text does, comment for comment");
        REQUIRE(Equals(read, expected, Trivia::Keep));
        INFO("the root keeps the location ReadText gives it");
        REQUIRE(read.location == expected.location);
    }
}

TEST_CASE("ReadTree reads an encoding kept with its comments back with them", "[serialisation][readtree]")
{
    const Value tree = SerialisationTest::TreeOf("// the root\n{\n  // a\n  a: 1, // one\n}\n");
    const Value read = Read(EncodeTree(tree, Trivia::Keep), "commented.bin");
    INFO("the encoded tree reads back comment for comment");
    REQUIRE(Equals(read, tree, Trivia::Keep));
}

TEST_CASE("ReadTree reads bytes whose first is zero as the tree encoding, with its diagnostics",
          "[serialisation][readtree]")
{
    // A zero and then nothing: the start of an encoding's tag, cut short.
    const std::byte bytes[] = {std::byte{0x00}};
    const Diagnostic diagnostic = Fail(bytes);
    INFO("the diagnostic '" << diagnostic.message << "' is DecodeTree's, naming the byte it is about");
    REQUIRE(diagnostic.message.starts_with("at byte 0, "));
}

TEST_CASE("ReadTree reads bytes whose first is not zero as JSON5 text, with its diagnostics",
          "[serialisation][readtree]")
{
    const Diagnostic diagnostic = Fail(BytesOf("{\n  a: ,\n}\n"));
    INFO("the diagnostic '" << diagnostic.message << "' is ReadText's, on the line and column of the bad character");
    REQUIRE(diagnostic.line == 2);
    REQUIRE(diagnostic.column == 6);
}

TEST_CASE("ReadTree skips a byte order mark at the start of text", "[serialisation][readtree]")
{
    const Value read = Read(BytesOf("\xEF\xBB\xBF{a: 1}"), "bom.json5");
    INFO("the text after the mark reads");
    REQUIRE(Equals(read, SerialisationTest::TreeOf("{a: 1}")));
    INFO("the mark takes no column, so the root begins at column 1");
    REQUIRE(read.location == Location{1, 1});
}

TEST_CASE("ReadTree gives an empty span one error diagnostic at its start", "[serialisation][readtree]")
{
    const Diagnostic diagnostic = Fail({});
    INFO("the diagnostic names the file and is at line 1, column 1");
    REQUIRE(diagnostic.path == "save.dat");
    REQUIRE(diagnostic.line == 1);
    REQUIRE(diagnostic.column == 1);
}

TEST_CASE("ReadTree logs an empty span's diagnostic once, through Error", "[serialisation][readtree]")
{
    RecordingLogger logger;
    const ReadTreeResult result = ReadTree({}, "save.dat", logger);
    INFO("the empty span gives one diagnostic");
    REQUIRE(result.diagnostics.size() == 1);
    INFO("it is logged once, through Error, as path:line:column: message");
    REQUIRE(logger.errors == std::vector<std::string>{FormatDiagnostic(result.diagnostics.front())});
    REQUIRE(logger.warnings.empty());
}
