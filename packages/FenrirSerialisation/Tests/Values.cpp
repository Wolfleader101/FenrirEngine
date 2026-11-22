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

    Object built;
    built.Add("damage", 12.5f);

    INFO("a located tree equals the same tree built in code, comments compared too");
    REQUIRE(Equals(LocatedSword(1), Value(std::move(built)), Trivia::Compare));
}

TEST_CASE("Equality ignores comments, and Trivia::Compare does not", "[serialisation][value]")
{
    const Value a = CommentedSword("// tuned for the first level");
    const Value b = CommentedSword("// tuned for the boss");

    INFO("trees differing only in a comment are equal");
    REQUIRE(a == b);

    INFO("Trivia::Compare sees the different comment");
    REQUIRE_FALSE(Equals(a, b, Trivia::Compare));

    INFO("Trivia::Compare finds a tree equal to its own copy");
    REQUIRE(Equals(a, CommentedSword("// tuned for the first level"), Trivia::Compare));
}

TEST_CASE("Trivia::Compare sees trailing, closing and root comments", "[serialisation][value]")
{
    Array bare;
    bare.Add("melee");
    Value root(bare);

    Array trailing = bare;
    trailing.elements[0].trailingComment = "// first";

    Array closing = bare;
    closing.closingComments.push_back("// more to come");

    Value withRootComment(bare);
    withRootComment.leadingComments.push_back("// tags");

    INFO("a trailing comment is trivia");
    REQUIRE_FALSE(Equals(root, Value(trailing), Trivia::Compare));

    INFO("a closing comment is trivia");
    REQUIRE_FALSE(Equals(root, Value(closing), Trivia::Compare));

    INFO("the root's leading comment is trivia");
    REQUIRE_FALSE(Equals(root, withRootComment, Trivia::Compare));

    INFO("none of them changes plain equality");
    REQUIRE(root == Value(trailing));
    REQUIRE(root == Value(closing));
    REQUIRE(root == withRootComment);
}
