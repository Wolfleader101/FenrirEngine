// UAX#29 segments of plain UTF-8 text, outside any paragraph: the grapheme boundaries and the words a text entry moves
// its caret by (seam 3).
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirText/TextSegments.hpp"

using Fenrir::TextRange;

TEST_CASE("Grapheme boundaries of plain text keep a combining mark with its base", "[text][segments]")
{
    // e, a combining acute accent (two bytes), then x.
    const std::string text = "e\xCC\x81x";
    INFO("the boundaries are 0, after the accented e, and the end");
    REQUIRE(Fenrir::GraphemeBoundaries(text) == std::vector<std::uint32_t>{0, 3, 4});
    INFO("empty text has the one boundary at 0");
    REQUIRE(Fenrir::GraphemeBoundaries("") == std::vector<std::uint32_t>{0});
}

TEST_CASE("Words are the UAX#29 segments holding letters, digits or ideographs", "[text][segments]")
{
    const std::vector<TextRange> words = Fenrir::Words("hello, brave world 42!");
    INFO("the comma, the spaces and the exclamation mark are not words");
    REQUIRE(words == std::vector<TextRange>{{0, 5}, {7, 12}, {13, 18}, {19, 21}});

    INFO("an apostrophe inside a word does not split it, as UAX#29 says");
    REQUIRE(Fenrir::Words("don't stop") == std::vector<TextRange>{{0, 5}, {6, 10}});

    INFO("a word keeps its combining marks");
    REQUIRE(Fenrir::Words("caf\x65\xCC\x81 au") == std::vector<TextRange>{{0, 6}, {7, 9}});

    INFO("text of only spaces and punctuation has no words");
    REQUIRE(Fenrir::Words(" .,; ").empty());
    REQUIRE(Fenrir::Words("").empty());
}
