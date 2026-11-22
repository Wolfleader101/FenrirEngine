// The value tree (the spec, "The value tree"): its kinds, an object's order and lookup, a number's four widths, and
// equality with and without trivia.
#include "FenrirSerialisation/Value.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>

using namespace Fenrir::Serialisation;

TEST_CASE("Each kind of value reports its kind and its contents", "[serialisation][value]")
{
    INFO("a string holds its text");
    REQUIRE(Value("iron").Kind() == ValueKind::String);
    REQUIRE(Value("iron").AsString() == "iron");

    INFO("a bool holds its flag");
    REQUIRE(Value(true).Kind() == ValueKind::Bool);
    REQUIRE(Value(true).AsBool());

    INFO("a null is a null");
    REQUIRE(Value(nullptr).Kind() == ValueKind::Null);

    INFO("an integer is a number");
    REQUIRE(Value(24).Kind() == ValueKind::Number);
    REQUIRE(Value(24).AsNumber().AsSigned() == 24);

    INFO("an object and an array are their kinds");
    REQUIRE(Value(Object{}).Kind() == ValueKind::Object);
    REQUIRE(Value(Array{}).Kind() == ValueKind::Array);
}

TEST_CASE("An object keeps its members in the order they were added", "[serialisation][value]")
{
    Object object;
    object.Add("type", "MyGame::Sword");
    object.Add("format", 1);
    object.Add("version", 2);

    INFO("members are kept in insertion order, not sorted");
    REQUIRE(object.members.size() == 3);
    REQUIRE(object.members[0].key == "type");
    REQUIRE(object.members[1].key == "format");
    REQUIRE(object.members[2].key == "version");
}

TEST_CASE("Find returns a member's value by key, and null for a key the object lacks", "[serialisation][value]")
{
    Object object;
    object.Add("count", 24);
    object.Add("lifetime", 0.4f);

    const Value* count = object.Find("count");
    INFO("a present key finds its value");
    REQUIRE(count != nullptr);
    REQUIRE(count->AsNumber().AsSigned() == 24);

    INFO("an absent key finds nothing");
    REQUIRE(object.Find("texture") == nullptr);
}

TEST_CASE("A number keeps the width it was built in", "[serialisation][value]")
{
    const Number signedMin(std::numeric_limits<std::int64_t>::min());
    const Number unsignedMax(std::numeric_limits<std::uint64_t>::max());
    const Number single(0.7071068f);
    const Number dbl(1.2);

    INFO("a signed integer is Signed and keeps all 64 bits");
    REQUIRE(signedMin.Width() == NumberWidth::Signed);
    REQUIRE(signedMin.AsSigned() == std::numeric_limits<std::int64_t>::min());

    INFO("an unsigned integer is Unsigned and keeps all 64 bits");
    REQUIRE(unsignedMax.Width() == NumberWidth::Unsigned);
    REQUIRE(unsignedMax.AsUnsigned() == std::numeric_limits<std::uint64_t>::max());

    INFO("a float is Single");
    REQUIRE(single.Width() == NumberWidth::Single);
    REQUIRE(single.AsSingle() == 0.7071068f);

    INFO("a double is Double");
    REQUIRE(dbl.Width() == NumberWidth::Double);
    REQUIRE(dbl.AsDouble() == 1.2);
}

TEST_CASE("Numbers of different widths are not equal, even when their values are", "[serialisation][value]")
{
    INFO("1 signed and 1 unsigned differ in width");
    REQUIRE_FALSE(Value(1) == Value(1u));

    INFO("1.5 as a float and 1.5 as a double differ in width");
    REQUIRE_FALSE(Value(1.5f) == Value(1.5));
}

TEST_CASE("A NaN equals a NaN of its width, so a tree holding one equals itself", "[serialisation][value]")
{
    const Value nan(std::numeric_limits<float>::quiet_NaN());

    INFO("a single NaN equals a copy of itself");
    REQUIRE(nan == Value(std::numeric_limits<float>::quiet_NaN()));
}

namespace
{
    Value LocatedSword(std::uint32_t line)
    {
        Object data;
        data.Add("damage", 12.5f).value.location = Location{line + 1, 13};
        Value root(std::move(data));
        root.location = Location{line, 1};
        return root;
    }

    Value CommentedSword(std::string comment)
    {
        Object data;
        data.Add("damage", 12.5f).leadingComments.push_back(std::move(comment));
        return Value(std::move(data));
    }
} // namespace

TEST_CASE("Equality ignores where the nodes were read from", "[serialisation][value]")
{
    INFO("the same tree read at different lines is equal");
    REQUIRE(LocatedSword(1) == LocatedSword(40));
}

TEST_CASE("Trivia::Keep ignores where the nodes were read from", "[serialisation][value]")
{
    Object built;
    built.Add("damage", 12.5f);

    INFO("a located tree equals the same tree built in code, comments compared too");
    REQUIRE(Equals(LocatedSword(1), Value(std::move(built)), Trivia::Keep));
}

namespace
{
    struct CommentedArrays
    {
        Value bare;
        Value trailing;
        Value closing;
        Value rootComment;
    };

    /** @brief One array of tags, and three copies differing from it only by a comment, each in a different place. */
    CommentedArrays TagArrays()
    {
        Array bare;
        bare.Add("melee");

        Array trailing = bare;
        trailing.elements[0].trailingComment = "// first";

        Array closing = bare;
        closing.closingComments.push_back("// more to come");

        Value rootComment(bare);
        rootComment.leadingComments.push_back("// tags");

        return {Value(bare), Value(trailing), Value(closing), rootComment};
    }
} // namespace

TEST_CASE("Equality ignores comments", "[serialisation][value]")
{
    const CommentedArrays arrays = TagArrays();

    INFO("trees differing only in a leading comment on a member are equal");
    REQUIRE(CommentedSword("// tuned for the first level") == CommentedSword("// tuned for the boss"));

    INFO("trees differing only in a trailing, a closing or a root comment are equal");
    REQUIRE(arrays.bare == arrays.trailing);
    REQUIRE(arrays.bare == arrays.closing);
    REQUIRE(arrays.bare == arrays.rootComment);
}

TEST_CASE("Trivia::Keep finds a tree equal to its copy, comments and all", "[serialisation][value]")
{
    INFO("the same comment in the same place compares equal");
    REQUIRE(Equals(CommentedSword("// tuned"), CommentedSword("// tuned"), Trivia::Keep));
}

TEST_CASE("Trivia::Keep sees a comment wherever it is", "[serialisation][value]")
{
    const CommentedArrays arrays = TagArrays();

    INFO("a different leading comment on a member is seen");
    REQUIRE_FALSE(
        Equals(CommentedSword("// tuned for the first level"), CommentedSword("// tuned for the boss"), Trivia::Keep));

    INFO("a trailing comment is seen");
    REQUIRE_FALSE(Equals(arrays.bare, arrays.trailing, Trivia::Keep));

    INFO("a closing comment is seen");
    REQUIRE_FALSE(Equals(arrays.bare, arrays.closing, Trivia::Keep));

    INFO("the root's leading comment is seen");
    REQUIRE_FALSE(Equals(arrays.bare, arrays.rootComment, Trivia::Keep));
}
