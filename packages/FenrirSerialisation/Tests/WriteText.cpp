// The JSON5 writer's emit rules (the spec, "The JSON5 writer"), one per test: keys, escapes, the flow rule's branches,
// number widths and literals, comments, and determinism. The expected text is written out from the spec's rules.
#include "FenrirSerialisation/Text.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

using namespace Fenrir::Serialisation;

namespace
{
    /** @brief The text of a root object with one member, `key: value`, in block style, as most rules are seen. */
    std::string Block(std::string_view key, Value value)
    {
        Object object;
        object.Add(std::string(key), std::move(value));
        object.Add("z", 0);
        return WriteText(Value(std::move(object)));
    }

    /** @brief The text `WriteText` gives for a lone string, without its trailing newline. */
    std::string StringText(std::string text)
    {
        std::string written = WriteText(Value(std::move(text)));
        return written.substr(0, written.size() - 1);
    }

    /** @brief The text `WriteText` gives for a lone number, without its trailing newline. */
    std::string NumberText(Number number)
    {
        std::string written = WriteText(Value(number));
        return written.substr(0, written.size() - 1);
    }
} // namespace

TEST_CASE("A key that is an identifier is written bare", "[serialisation][write]")
{
    INFO("cubemap, _hidden and $ref are identifiers");
    REQUIRE(Block("cubemap", 1) == "{\n  cubemap: 1,\n  z: 0,\n}\n");
    REQUIRE(Block("_hidden2", 1) == "{\n  _hidden2: 1,\n  z: 0,\n}\n");
    REQUIRE(Block("$ref", 1) == "{\n  $ref: 1,\n  z: 0,\n}\n");
}

TEST_CASE("A key that is not an identifier is double-quoted and escaped", "[serialisation][write]")
{
    INFO("a namespaced type name has colons, so it is quoted");
    REQUIRE(Block("Fenrir::Sky", 1) == "{\n  \"Fenrir::Sky\": 1,\n  z: 0,\n}\n");

    INFO("a key starting with a digit, an empty key and a key with a space are quoted");
    REQUIRE(Block("1st", 1) == "{\n  \"1st\": 1,\n  z: 0,\n}\n");
    REQUIRE(Block("", 1) == "{\n  \"\": 1,\n  z: 0,\n}\n");
    REQUIRE(Block("hand r", 1) == "{\n  \"hand r\": 1,\n  z: 0,\n}\n");

    INFO("a quoted key is escaped as a string is");
    REQUIRE(Block("say \"hi\"", 1) == "{\n  \"say \\\"hi\\\"\": 1,\n  z: 0,\n}\n");
}

TEST_CASE("A string is double-quoted and plain text is written as it is", "[serialisation][write]")
{
    INFO("a path with a hash and a single quote needs no escape");
    REQUIRE(StringText("models/sword.glb#Mesh0 'x'") == "\"models/sword.glb#Mesh0 'x'\"");
}

TEST_CASE("A double quote and a backslash in a string are escaped", "[serialisation][write]")
{
    INFO("\" becomes \\\" and \\ becomes \\\\");
    REQUIRE(StringText("a\"b") == "\"a\\\"b\"");
    REQUIRE(StringText("a\\b") == "\"a\\\\b\"");
}

TEST_CASE("The named controls in a string are written as their short escapes", "[serialisation][write]")
{
    INFO("newline, tab, carriage return, backspace and form feed each have a short escape");
    REQUIRE(StringText("\n") == "\"\\n\"");
    REQUIRE(StringText("\t") == "\"\\t\"");
    REQUIRE(StringText("\r") == "\"\\r\"");
    REQUIRE(StringText("\b") == "\"\\b\"");
    REQUIRE(StringText("\f") == "\"\\f\"");
}

TEST_CASE("Any other control in a string is written as \\u00XX", "[serialisation][write]")
{
    INFO("U+0000, U+0001 and U+001F take the four-digit escape");
    REQUIRE(StringText(std::string("\0", 1)) == "\"\\u0000\"");
    REQUIRE(StringText("\x01") == "\"\\u0001\"");
    REQUIRE(StringText("\x1f") == "\"\\u001f\"");
}

TEST_CASE("UTF-8 beyond ASCII in a string is written raw", "[serialisation][write]")
{
    INFO("é, a CJK character and an emoji are copied byte for byte");
    REQUIRE(StringText("\xc3\xa9 \xe5\x89\xa3 \xf0\x9f\x97\xa1") == "\"\xc3\xa9 \xe5\x89\xa3 \xf0\x9f\x97\xa1\"");
}

TEST_CASE("An array whose every element is a scalar is written in flow style", "[serialisation][write]")
{
    Array tags;
    tags.Add("melee");
    tags.Add(1);
    tags.Add(true);
    tags.Add(nullptr);

    INFO("one line, comma and space between, no trailing comma");
    REQUIRE(Block("tags", tags) == "{\n  tags: [\"melee\", 1, true, null],\n  z: 0,\n}\n");
}

TEST_CASE("An array holding a container is written in block style", "[serialisation][write]")
{
    Array rows;
    rows.Add(Array{});
    rows.Add(2);

    INFO("one element per line with a trailing comma");
    REQUIRE(WriteText(Value(rows)) == "[\n  [],\n  2,\n]\n");
}

TEST_CASE("An all-scalar object that is an array element is written in flow style", "[serialisation][write]")
{
    Object row;
    row.Add("level", 1);
    row.Add("cost", 100);
    Array upgrades;
    upgrades.Add(row);

    INFO("a table row is one line");
    REQUIRE(WriteText(Value(upgrades)) == "[\n  {level: 1, cost: 100},\n]\n");
}

TEST_CASE("An all-scalar object with one member is written in flow style", "[serialisation][write]")
{
    Object name;
    name.Add("value", "Player");

    INFO("a one-member object is one line");
    REQUIRE(Block("Fenrir::Name", name) == "{\n  \"Fenrir::Name\": {value: \"Player\"},\n  z: 0,\n}\n");
}

TEST_CASE("An all-scalar object with two members, not in an array, is written in block style", "[serialisation][write]")
{
    Object sky;
    sky.Add("cubemap", "day.ktx2");
    sky.Add("exposure", 1.2f);

    INFO("each member on its own line, one level deeper");
    REQUIRE(Block("sky", sky) == "{\n  sky: {\n    cubemap: \"day.ktx2\",\n    exposure: 1.2,\n  },\n  z: 0,\n}\n");
}

TEST_CASE("An object holding a container is written in block style, even with one member", "[serialisation][write]")
{
    Array rotation;
    rotation.Add(0);
    rotation.Add(1);
    Object transform;
    transform.Add("rotation", rotation);
    Array entities;
    entities.Add(transform);

    INFO("neither one member nor being an array element makes it flow");
    REQUIRE(WriteText(Value(entities)) == "[\n  {\n    rotation: [0, 1],\n  },\n]\n");
}

TEST_CASE("An empty object and an empty array are written as {} and []", "[serialisation][write]")
{
    INFO("an empty container has nothing to put on its own lines");
    REQUIRE(Block("data", Object{}) == "{\n  data: {},\n  z: 0,\n}\n");
    REQUIRE(Block("tags", Array{}) == "{\n  tags: [],\n  z: 0,\n}\n");
}

TEST_CASE("A signed integer is written without a point", "[serialisation][write]")
{
    INFO("small, negative and the 64-bit extremes");
    REQUIRE(NumberText(24) == "24");
    REQUIRE(NumberText(-3) == "-3");
    REQUIRE(NumberText(std::numeric_limits<std::int64_t>::min()) == "-9223372036854775808");
    REQUIRE(NumberText(std::numeric_limits<std::int64_t>::max()) == "9223372036854775807");
}

TEST_CASE("An unsigned integer is written in full, without passing through a double", "[serialisation][write]")
{
    INFO("2^64 - 1 is not rounded");
    REQUIRE(NumberText(std::numeric_limits<std::uint64_t>::max()) == "18446744073709551615");
}

TEST_CASE("A single float is written in its own shortest round-trip form", "[serialisation][write]")
{
    INFO("0.7071068f is not widened to a double's digits, and 6.0f has no point");
    REQUIRE(NumberText(0.7071068f) == "0.7071068");
    REQUIRE(NumberText(6.0f) == "6");
    REQUIRE(NumberText(-0.3826834f) == "-0.3826834");
}

TEST_CASE("A double is written in its own shortest round-trip form", "[serialisation][write]")
{
    INFO("0.1 and 12.5 as doubles, and 6.0 with no point");
    REQUIRE(NumberText(0.1) == "0.1");
    REQUIRE(NumberText(12.5) == "12.5");
    REQUIRE(NumberText(6.0) == "6");
}

TEST_CASE("Infinity is written as the literal Infinity in either width", "[serialisation][write]")
{
    INFO("single and double positive infinity");
    REQUIRE(NumberText(std::numeric_limits<float>::infinity()) == "Infinity");
    REQUIRE(NumberText(std::numeric_limits<double>::infinity()) == "Infinity");
}

TEST_CASE("Negative infinity is written as the literal -Infinity in either width", "[serialisation][write]")
{
    INFO("single and double negative infinity");
    REQUIRE(NumberText(-std::numeric_limits<float>::infinity()) == "-Infinity");
    REQUIRE(NumberText(-std::numeric_limits<double>::infinity()) == "-Infinity");
}

TEST_CASE("NaN is written as the literal NaN in either width, whatever its sign", "[serialisation][write]")
{
    INFO("single, double and negated NaN");
    REQUIRE(NumberText(std::numeric_limits<float>::quiet_NaN()) == "NaN");
    REQUIRE(NumberText(std::numeric_limits<double>::quiet_NaN()) == "NaN");
    REQUIRE(NumberText(-std::numeric_limits<double>::quiet_NaN()) == "NaN");
}

TEST_CASE("A bool and a null are written as their literals", "[serialisation][write]")
{
    INFO("true, false and null");
    REQUIRE(WriteText(Value(true)) == "true\n");
    REQUIRE(WriteText(Value(false)) == "false\n");
    REQUIRE(WriteText(Value(nullptr)) == "null\n");
}

TEST_CASE("A member's leading comments are written on their own lines at its indent", "[serialisation][write]")
{
    Object data;
    Member& damage = data.Add("damage", 12.5f);
    damage.leadingComments = {"// tuned in playtest 3", "/* raised from 10 */"};
    Object root;
    root.Add("data", data);
    root.Add("z", 0);

    INFO("each comment on its own line, two levels deep");
    REQUIRE(
        WriteText(Value(root)) ==
        "{\n  data: {\n    // tuned in playtest 3\n    /* raised from 10 */\n    damage: 12.5,\n  },\n  z: 0,\n}\n");
}

TEST_CASE("A trailing comment is written after the comma", "[serialisation][write]")
{
    Array tags;
    tags.Add("melee").trailingComment = "// the default";
    tags.Add("iron");

    INFO("on the element's line, one space after its comma");
    REQUIRE(WriteText(Value(tags)) == "[\n  \"melee\", // the default\n  \"iron\",\n]\n");
}

TEST_CASE("Closing comments are written before the closing bracket", "[serialisation][write]")
{
    Object sub;
    sub.Add("a", 1);
    sub.Add("b", 2);
    sub.closingComments = {"// sparks next"};

    INFO("at the members' indent, after the last member");
    REQUIRE(WriteText(Value(sub)) == "{\n  a: 1,\n  b: 2,\n  // sparks next\n}\n");
}

TEST_CASE("The root's leading comments are written above it", "[serialisation][write]")
{
    Value root(nullptr);
    root.leadingComments = {"// Level 1: the player's start room"};

    INFO("on its own line at column one");
    REQUIRE(WriteText(root) == "// Level 1: the player's start room\nnull\n");
}

TEST_CASE("A container holding a comment is written in block style so the comment is kept", "[serialisation][write]")
{
    Array position;
    position.Add(0).trailingComment = "// x";
    position.Add(1);

    Object name;
    name.Add("value", "Player");
    name.closingComments = {"// display only"};

    INFO("an all-scalar array with a trailing comment is block style");
    REQUIRE(WriteText(Value(position)) == "[\n  0, // x\n  1,\n]\n");

    INFO("a one-member object with a closing comment is block style");
    REQUIRE(WriteText(Value(name)) == "{\n  value: \"Player\",\n  // display only\n}\n");
}

TEST_CASE("Writing one tree twice gives the same bytes", "[serialisation][write]")
{
    Object data;
    data.Add("damage", 12.5f).leadingComments = {"// tuned"};
    Array tags;
    tags.Add("melee");
    data.Add("tags", tags);
    data.Add("durability", std::numeric_limits<float>::infinity());
    const Value root(data);

    const std::string first = WriteText(root);
    const std::string second = WriteText(root);
    INFO("the output depends on the tree alone");
    REQUIRE(first == second);
}
