// How Iris's native modules handle what the bridge says, with no .NET runtime: a scripted bridge function table stands
// where the managed bridge stands (see Fake/FakeBridge.hpp). Each case scripts one response, makes a native call, and
// checks what the caller gets and what was given back.
#include "Fake/FakeBridge.hpp"

#include "Iris/Iris.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <functional>
#include <string_view>
#include <vector>

namespace
{
    using namespace Iris;
    using Testing::FakeBridge;
    using Testing::SnapshotWriter;

    // The type handle every case queries: the fake answers FindType with a type.
    Type TypeOf(const Component& component)
    {
        const auto type = component.FindType("Rules.Subject");
        {
            INFO("The fake should have found a type: " + type.diagnostic);
            REQUIRE(type.Succeeded());
        }
        return *type.value;
    }

    // A type reference as the snapshots write it: the full name, then the assembly's name.
    SnapshotWriter& TypeRef(SnapshotWriter& writer, std::string_view name = "System.Object")
    {
        return writer.String(name).String("System.Private.CoreLib");
    }
} // namespace

TEST_CASE("a well formed snapshot is read, and its block is given back", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    SnapshotWriter writer;
    writer.Int32(1);
    TypeRef(writer, "Rules.Base");
    fake.AnswerQueryTypeWith(writer.Bytes());

    const auto base = type.GetBaseType();

    INFO("A well formed base type snapshot should be read: " + base.diagnostic);
    REQUIRE(base.Succeeded());
    REQUIRE(base.value->has_value());
    INFO("The base type's name should be the one in the snapshot");
    REQUIRE((*base.value)->fullName == "Rules.Base");
}

TEST_CASE("the snapshot's block is given back once, after the bridge was asked once", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    SnapshotWriter writer;
    writer.Int32(1);
    TypeRef(writer, "Rules.Base");
    fake.AnswerQueryTypeWith(writer.Bytes());

    (void)type.GetBaseType();

    INFO("The bridge should have been asked once");
    REQUIRE(fake.Calls("QueryType") == 1);
    INFO("The block should have been given back");
    REQUIRE(fake.OutstandingBlocks() == 0);
    INFO("It should have been given back only once, and nothing else with it");
    REQUIRE(fake.BadFrees() == 0);
}

TEST_CASE("a snapshot with no block is an invalid response", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    fake.AnswerQueryTypeWithNoBlock();

    const auto base = type.GetBaseType();

    INFO("A success with no block should not be accepted");
    REQUIRE(!base.Succeeded());
    REQUIRE(base.status == CallStatus::InvalidResponse);
    INFO("The explanation should say there was no snapshot");
    REQUIRE(base.diagnostic == "The bridge returned no type snapshot.");
}

TEST_CASE("a snapshot with a length that cannot be right is an invalid response and its block is given back", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    SnapshotWriter writer;
    writer.Int32(0);
    const auto claimed = GENERATE(std::int64_t{-1}, std::int64_t{0x80000000LL}, std::int64_t{0x7FFFFFFFFFFFFFFFLL});
    fake.AnswerQueryTypeWithLength(claimed, writer.Bytes());

    const auto base = type.GetBaseType();

    INFO("A length of " << claimed << " should not be accepted");
    REQUIRE(!base.Succeeded());
    REQUIRE(base.status == CallStatus::InvalidResponse);
    INFO("The explanation should say the snapshot is malformed");
    REQUIRE(base.diagnostic == "The bridge returned a malformed type snapshot.");
    INFO("The block should still have been given back");
    REQUIRE(fake.OutstandingBlocks() == 0);
    REQUIRE(fake.BadFrees() == 0);
}

namespace
{
    // One way a snapshot can be wrong, and the call that reads it.
    struct MalformedSnapshot
    {
        const char* what;
        std::function<void(SnapshotWriter&)> write;
        std::function<CallStatus(const Type&)> read;
    };

    CallStatus ReadBaseType(const Type& type)
    {
        return type.GetBaseType().status;
    }
    CallStatus ReadFields(const Type& type)
    {
        return type.GetFields().status;
    }
    CallStatus ReadAttributes(const Type& type)
    {
        return type.GetAttributes().status;
    }

    // A field entry up to its kind, with the rest of the entry for a case to write.
    void FieldUpTo(SnapshotWriter& writer)
    {
        writer.Int32(1).String("Health");
        TypeRef(writer);
    }

    const std::vector<MalformedSnapshot>& MalformedSnapshots()
    {
        static const std::vector<MalformedSnapshot> cases = {
            {"a string cut off by the end of the snapshot",
             [](SnapshotWriter& w) { w.Int32(1).LyingString(50, "abc"); }, ReadBaseType},
            {"a string with a negative length", [](SnapshotWriter& w) { w.Int32(1).Int32(-5); }, ReadBaseType},
            {"bytes left over after the last value",
             [](SnapshotWriter& w) {
                 w.Int32(1);
                 TypeRef(w);
                 w.Raw(0x7F);
             },
             ReadBaseType},
            {"a snapshot that ends before the first count", [](SnapshotWriter&) {}, ReadBaseType},
            {"a type kind that does not exist",
             [](SnapshotWriter& w) {
                 FieldUpTo(w);
                 w.Int32(99);
             },
             ReadFields},
            {"a visibility that does not exist",
             [](SnapshotWriter& w) {
                 FieldUpTo(w);
                 w.Int32(0).Int32(0).Int32(99);
             },
             ReadFields},
            {"an attribute argument tag that does not exist",
             [](SnapshotWriter& w) {
                 w.Int32(1);
                 TypeRef(w, "Rules.Marker");
                 w.Int32(1).String("Level").String("System.Int32").Int32(99);
             },
             ReadAttributes},
        };
        return cases;
    }
} // namespace

TEST_CASE("a malformed snapshot is an invalid response, and its block is given back", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    const auto& malformed = GENERATE_REF(from_range(MalformedSnapshots()));
    SnapshotWriter writer;
    malformed.write(writer);
    fake.AnswerQueryTypeWith(writer.Bytes());

    const auto status = malformed.read(type);

    INFO("Reading " << malformed.what << " should not succeed");
    REQUIRE(status == CallStatus::InvalidResponse);
    INFO("The block should have been given back, once");
    REQUIRE(fake.OutstandingBlocks() == 0);
    REQUIRE(fake.BadFrees() == 0);
}

TEST_CASE("a malformed snapshot explains itself", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    SnapshotWriter writer;
    writer.Int32(1).LyingString(50, "abc");
    fake.AnswerQueryTypeWith(writer.Bytes());

    const auto base = type.GetBaseType();

    INFO("The explanation should say the snapshot is malformed");
    REQUIRE(base.diagnostic == "The bridge returned a malformed type snapshot.");
}
