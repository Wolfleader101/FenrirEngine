// The json5-tests cases (the spec, "Testing"), committed under fixtures/json5-tests/ from github.com/json5/json5-tests.
// A case's extension says what it is: `.json` and `.json5` are valid and must read; `.js` and `.txt` are invalid and
// must fail with one diagnostic. Each invalid case's line and column below is read off the case: the character that
// breaks the grammar, or the end of the text. They agree with the case's `.errorSpec` where it has one, but for two:
// unescaped-multi-line-string's points at the line break itself (1:5) where json5's reference reader says 2:0, and
// top-level-inline-comment's is at the end of its 65 bytes (1:66), where its `.errorSpec` says 1:67.
#include "FenrirSerialisation/Text.hpp"

#include "SerialisationTest.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace Fenrir::Serialisation;

namespace
{
    struct InvalidCase
    {
        const char* name;
        std::uint32_t line;
        std::uint32_t column;
    };

    // Every invalid case, with where its one diagnostic must point.
    constexpr InvalidCase InvalidCases[] = {
        {"arrays/leading-comma-array.js", 2, 5},
        {"arrays/lone-trailing-comma-array.js", 2, 5},
        {"arrays/no-comma-array.txt", 3, 5},
        {"comments/top-level-block-comment.txt", 4, 3},
        {"comments/top-level-inline-comment.txt", 1, 66},
        // The block comment that never ends is reported where it begins.
        {"comments/unterminated-block-comment.txt", 2, 1},
        {"misc/empty.txt", 1, 1},
        {"numbers/hexadecimal-empty.txt", 1, 3},
        {"numbers/integer-with-float-exponent.txt", 1, 4},
        {"numbers/integer-with-hexadecimal-exponent.txt", 1, 4},
        {"numbers/integer-with-negative-float-exponent.txt", 1, 5},
        {"numbers/integer-with-negative-hexadecimal-exponent.txt", 1, 5},
        {"numbers/integer-with-positive-float-exponent.txt", 1, 5},
        {"numbers/integer-with-positive-hexadecimal-exponent.txt", 1, 5},
        {"numbers/lone-decimal-point.txt", 1, 2},
        {"numbers/negative-noctal.js", 1, 3},
        {"numbers/negative-octal.txt", 1, 3},
        {"numbers/negative-zero-octal.txt", 1, 3},
        {"numbers/noctal-with-leading-octal-digit.js", 1, 2},
        {"numbers/noctal.js", 1, 2},
        {"numbers/octal.txt", 1, 2},
        {"numbers/positive-noctal.js", 1, 3},
        {"numbers/positive-octal.txt", 1, 3},
        {"numbers/positive-zero-octal.txt", 1, 3},
        {"numbers/zero-octal.txt", 1, 2},
        {"objects/illegal-unquoted-key-number.txt", 2, 5},
        {"objects/illegal-unquoted-key-symbol.txt", 2, 10},
        {"objects/leading-comma-object.txt", 2, 5},
        {"objects/lone-trailing-comma-object.txt", 2, 5},
        {"objects/no-comma-object.txt", 3, 5},
        {"strings/unescaped-multi-line-string.txt", 1, 5},
    };

    std::filesystem::path CasesRoot()
    {
        return SerialisationTest::Fixtures() / "json5-tests";
    }

    // Every case of one validity, by its path under json5-tests/ with forward slashes, in a stable order.
    std::vector<std::string> Cases(bool valid)
    {
        std::vector<std::string> cases;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(CasesRoot()))
        {
            if (!entry.is_regular_file())
                continue;
            const std::filesystem::path extension = entry.path().extension();
            const bool isValid = extension == ".json" || extension == ".json5";
            const bool isInvalid = extension == ".js" || extension == ".txt";
            if ((valid && isValid) || (!valid && isInvalid))
                cases.push_back(entry.path().lexically_relative(CasesRoot()).generic_string());
        }
        std::sort(cases.begin(), cases.end());
        return cases;
    }
} // namespace

TEST_CASE("Every valid json5-tests case reads into a tree with no error", "[serialisation][read][json5-tests]")
{
    const std::vector<std::string> cases = Cases(true);

    INFO("the committed copy holds all 83 valid cases");
    REQUIRE(cases.size() == 83);

    for (const std::string& name : cases)
    {
        SerialisationTest::RecordingLogger logger;
        const ReadTreeResult result = ReadText(SerialisationTest::ReadBytes(CasesRoot() / name), name, logger);

        INFO(name << " is valid JSON5, so it reads; first error logged: "
                  << (logger.errors.empty() ? std::string("none") : logger.errors.front()));
        REQUIRE(result.tree.has_value());
        REQUIRE(logger.errors.empty());
    }
}

TEST_CASE("Every invalid json5-tests case fails with one diagnostic at its line and column",
          "[serialisation][read][json5-tests]")
{
    std::vector<std::string> tabled;
    for (const InvalidCase& invalid : InvalidCases)
        tabled.emplace_back(invalid.name);
    std::sort(tabled.begin(), tabled.end());

    INFO("the table above names every invalid case in the committed copy, so none goes unchecked");
    REQUIRE(tabled == Cases(false));

    for (const InvalidCase& invalid : InvalidCases)
    {
        SerialisationTest::RecordingLogger logger;
        const ReadTreeResult result =
            ReadText(SerialisationTest::ReadBytes(CasesRoot() / invalid.name), invalid.name, logger);

        INFO(invalid.name << " is not JSON5, so it gives no tree and one error diagnostic");
        REQUIRE_FALSE(result.tree.has_value());
        REQUIRE(result.diagnostics.size() == 1);
        REQUIRE(result.diagnostics.front().severity == DiagnosticSeverity::Error);

        INFO(invalid.name << "'s diagnostic, \"" << result.diagnostics.front().message << "\", is at "
                          << result.diagnostics.front().line << ":" << result.diagnostics.front().column
                          << ", expected " << invalid.line << ":" << invalid.column);
        REQUIRE(result.diagnostics.front().line == invalid.line);
        REQUIRE(result.diagnostics.front().column == invalid.column);
    }
}
