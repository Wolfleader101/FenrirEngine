// The marshalling helpers natives use for the wire shapes a generated declaration cannot type: a span from C#'s first
// element and count, text out into a buffer of a capacity, and UTF-8 as C#'s UTF-16.
#include <catch2/catch_test_macros.hpp>

#include "FenrirScripting/NativeMarshalling.hpp"

#include <span>
#include <string>
#include <string_view>

using namespace Fenrir;

TEST_CASE("SpanOf is empty for null and for a non-positive count", "[rules][natives]")
{
    int values[3] = {1, 2, 3};
    {
        INFO("A null first element (an empty C# span) did not give an empty span");
        REQUIRE(SpanOf(static_cast<const int*>(nullptr), 3).empty());
    }
    {
        INFO("A count of zero did not give an empty span");
        REQUIRE(SpanOf(values, 0).empty());
    }
    {
        INFO("A negative count did not give an empty span");
        REQUIRE(SpanOf(values, -1).empty());
    }
    {
        const std::span<int> two = SpanOf(values, 2);
        INFO("A first element and a count did not give that many values from it");
        REQUIRE(two.size() == 2);
        REQUIRE(two.data() == values);
        REQUIRE(two[1] == 2);
    }
}

TEST_CASE("WriteText fills up to capacity and returns the full length in both encodings", "[rules][natives]")
{
    {
        char out[4] = {'x', 'x', 'x', 'x'};
        INFO("A buffer too small did not get the first `capacity` chars and the whole length back");
        REQUIRE(WriteText(out, 3, "hello") == 5);
        REQUIRE(std::string_view(out, 3) == "hel");
        REQUIRE(out[3] == 'x');
    }
    {
        char out[8] = {};
        INFO("A buffer with room did not get the whole text");
        REQUIRE(WriteText(out, 8, "hi") == 2);
        REQUIRE(std::string_view(out, 2) == "hi");
    }
    {
        INFO("A null buffer (an empty C# span) or no capacity did not still answer the whole length");
        REQUIRE(WriteText(static_cast<char*>(nullptr), 8, "hi") == 2);
        char out[1] = {'x'};
        REQUIRE(WriteText(out, 0, "hi") == 2);
        REQUIRE(out[0] == 'x');
    }
    {
        char16_t out[4] = {u'x', u'x', u'x', u'x'};
        INFO("A UTF-16 buffer too small did not get the first `capacity` chars and the whole length back");
        REQUIRE(WriteText(out, 3, u"hello") == 5);
        REQUIRE(std::u16string_view(out, 3) == u"hel");
        REQUIRE(out[3] == u'x');
    }
    {
        char16_t out[8] = {};
        INFO("A UTF-16 buffer with room did not get the whole text");
        REQUIRE(WriteText(out, 8, u"hi") == 2);
        REQUIRE(std::u16string_view(out, 2) == u"hi");
        REQUIRE(WriteText(static_cast<char16_t*>(nullptr), 8, u"hi") == 2);
    }
}

TEST_CASE("Utf8ToUtf16 encodes each codepoint as C#'s chars and replaces what is not UTF-8", "[rules][natives]")
{
    {
        INFO("ASCII, a two-byte sequence and a four-byte sequence did not become one char, one char and a pair");
        REQUIRE(Utf8ToUtf16("a\xC3\xA9\xF0\x9F\x98\x80") == std::u16string(u"aé\U0001F600"));
    }
    {
        INFO("A stray continuation byte did not become U+FFFD");
        REQUIRE(Utf8ToUtf16("\x80z") == std::u16string(u"�z"));
    }
    {
        INFO("A sequence cut short did not become U+FFFD");
        REQUIRE(Utf8ToUtf16("\xC3") == std::u16string(u"�"));
    }
    {
        INFO("An overlong form did not become U+FFFD per byte");
        REQUIRE(Utf8ToUtf16("\xC0\x80") == std::u16string(u"��"));
    }
}
