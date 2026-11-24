// ToTree with a prior tree (the spec, "The bridge"): a value read from a tree and written again takes that tree's
// comments by key path, so a tool's rewrite keeps a file's comments although a struct holds none (ADR-0077 §1). Each
// test reads JSON5 text, bridges it into a test type, changes it and writes it with the text's tree as `prior`.
#include "FenrirSerialisation/Bridge.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Value.hpp"

#include "BridgeTypes.hpp"
#include "SerialisationTest.hpp"

#include "FenrirTypes/TypeRegistry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using namespace Fenrir::Serialisation;
using namespace BridgeTest;
using SerialisationTest::RecordingLogger;
using SerialisationTest::TreeOf;

namespace
{
    // An `Owned` with a comment in every place trivia has: the root's, a member's leading and trailing, an element's
    // leading and trailing, and a container's closing comments, the root object's among them.
    constexpr std::string_view Commented = R"(// An owned value, every comment kept
{
  // who owns it
  name: "Brand", // a trailing comment
  motto: "Forward",
  tags: [
    // the first tag
    1,
    2, // the second tag
    // before the tags close
  ],
  ranges: [
    {
      // the low end
      min: 0.5,
    },
  ],
  // before the root closes
}
)";

    // `text` with its first `from` replaced by `to`.
    std::string Replaced(std::string_view text, std::string_view from, std::string_view to)
    {
        std::string result(text);
        const std::size_t at = result.find(from);
        if (at != std::string::npos)
            result.replace(at, from.size(), to);
        return result;
    }

    // An `Owned` read from `prior`, against a registry holding the bridge's test types.
    Owned ReadOwned(const Value& prior, const Fenrir::TypeRegistry& registry)
    {
        Owned owned;
        RecordingLogger logger;
        (void)FromTree(prior, OwnedDescription, registry, Resolvers{}, &owned, "test.json5", logger);
        return owned;
    }
} // namespace

TEST_CASE("A value read, changed in one field and written with its prior tree writes the text back with only that "
          "value changed",
          "[serialisation][totree][prior]")
{
    Fenrir::TypeRegistry registry;
    RegisterBridgeTypes(registry);
    const Value prior = TreeOf(Commented);
    Owned owned = ReadOwned(prior, registry);
    owned.name = "Banner";

    INFO("every comment of the text it was read from is where it was, and only `name` differs");
    REQUIRE(WriteText(ToTree(&owned, OwnedDescription, registry, Resolvers{}, &prior)) ==
            Replaced(Commented, R"(name: "Brand")", R"(name: "Banner")"));
}

TEST_CASE("Array elements take the comments of the prior element at the same index", "[serialisation][totree][prior]")
{
    Fenrir::TypeRegistry registry;
    RegisterBridgeTypes(registry);
    const Value prior = TreeOf(R"({
  tags: [
    // the first tag
    1,
    2, // the second tag
  ],
}
)");
    Owned owned = ReadOwned(prior, registry);
    owned.tags = {7, 8, 9};

    INFO("elements 0 and 1 keep their comments by index, whatever their values, and element 2 has none");
    REQUIRE(WriteText(ToTree(&owned, OwnedDescription, registry, Resolvers{}, &prior)) == R"({
  tags: [
    // the first tag
    7,
    8, // the second tag
    9,
  ],
}
)");
}

TEST_CASE("A comment on a field now at its default is dropped with the field, and the other comments stay",
          "[serialisation][totree][prior]")
{
    Fenrir::TypeRegistry registry;
    RegisterBridgeTypes(registry);
    const Value prior = TreeOf(R"({
  name: "Brand", // kept
  // dropped with the field
  motto: "Forward", // dropped too
  // closing, kept
}
)");
    Owned owned = ReadOwned(prior, registry);
    owned.motto = Owned{}.motto;

    INFO("`motto` is at its default, so it is left out with both its comments, and the rest keep theirs");
    REQUIRE(WriteText(ToTree(&owned, OwnedDescription, registry, Resolvers{}, &prior)) == R"({
  name: "Brand", // kept
  // closing, kept
}
)");
}

TEST_CASE("With no prior tree the written tree has no comments", "[serialisation][totree][prior]")
{
    Fenrir::TypeRegistry registry;
    RegisterBridgeTypes(registry);
    const Owned owned = ReadOwned(TreeOf(Commented), registry);
    const Value tree = ToTree(&owned, OwnedDescription, registry, Resolvers{});

    INFO("the value read from the commented text writes in flow style wherever it can, with no comment anywhere");
    REQUIRE(WriteText(tree) == R"({
  name: "Brand",
  motto: "Forward",
  tags: [1, 2],
  ranges: [
    {min: 0.5},
  ],
}
)");
}

TEST_CASE("A prior node of another kind gives its member's comments but none of its own",
          "[serialisation][totree][prior]")
{
    Fenrir::TypeRegistry registry;
    RegisterBridgeTypes(registry);
    const Value prior = TreeOf(R"({
  // tags were once an object
  tags: {
    // never an element's
    first: 1,
    // nor the array's closing comment
  },
}
)");
    Owned owned;
    owned.tags = {1};

    INFO("the `tags` member keeps its comment, and the array takes nothing from the object it replaced");
    REQUIRE(WriteText(ToTree(&owned, OwnedDescription, registry, Resolvers{}, &prior)) == R"({
  // tags were once an object
  tags: [1],
}
)");
}
