// How Iris's native modules handle what the bridge says, with no .NET runtime: a scripted bridge function table stands
// where the managed bridge stands (see Fake/FakeBridge.hpp). Each case scripts one response, makes a native call, and
// checks what the caller gets and what was given back.
#include "Fake/FakeBridge.hpp"

#include "Iris/Iris.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
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

// ---- Everything below scripts the bridge's answers to calls other than snapshot queries.

namespace
{
    struct Pair
    {
        std::int32_t first;
        std::int32_t second;
    };
} // namespace

template <>
struct Iris::StructMapping<Pair>
{
    static constexpr std::string_view typeName = "Rules.Pair";
    static constexpr auto fields = Iris::StructFields(IRIS_FIELD(Pair, first), IRIS_FIELD(Pair, second));
};

namespace
{
    // Makes CreateArray succeed with this object handle.
    void ArrayCreatedAs(FakeBridge& fake, std::uint64_t handle)
    {
        fake.createArray = [handle](std::uint64_t, std::int32_t, std::int64_t, std::uint64_t* object, std::uint8_t*,
                                    std::int32_t, std::int32_t*, std::int32_t*) {
            *object = handle;
            return 0;
        };
    }

    // Makes GetArrayLength answer `status` with this length and diagnostic metadata.
    void LengthAnswers(FakeBridge& fake, std::int32_t status, std::int64_t length, std::vector<std::uint8_t> text = {},
                       std::int32_t written = 0, std::int32_t truncated = 0)
    {
        fake.arrayLength = [=](std::uint64_t, std::uint64_t, std::int64_t* out, std::uint8_t* bytes,
                               std::int32_t capacity, std::int32_t* writtenOut, std::int32_t* truncatedOut) {
            *out = length;
            std::memcpy(bytes, text.data(), std::min<std::size_t>(text.size(), static_cast<std::size_t>(capacity)));
            *writtenOut = written;
            *truncatedOut = truncated;
            return status;
        };
    }

    Array<std::int32_t> IntArray(FakeBridge& fake, const Component& component)
    {
        ArrayCreatedAs(fake, 5);
        auto array = Array<std::int32_t>::Create(component, 3);
        {
            INFO("The fake should have created an array: " + array.diagnostic);
            REQUIRE(array.Succeeded());
        }
        return std::move(*array.value);
    }

    std::vector<std::uint8_t> Bytes(std::string_view text)
    {
        return {text.begin(), text.end()};
    }
} // namespace

TEST_CASE("an array length the bridge reports is passed on", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto array = IntArray(fake, component);
    LengthAnswers(fake, 0, 3);

    const auto length = array.Length();

    INFO("A good length should be read: " + length.diagnostic);
    REQUIRE(length.Succeeded());
    INFO("The length should be the one the bridge wrote");
    REQUIRE(*length.value == 3);
}

TEST_CASE("a negative array length is an invalid response", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto array = IntArray(fake, component);
    LengthAnswers(fake, 0, -1);

    const auto length = array.Length();

    INFO("A length of -1 should not be handed on as a huge size");
    REQUIRE(!length.Succeeded());
    REQUIRE(length.status == CallStatus::InvalidResponse);
    INFO("The explanation should say the length was negative");
    REQUIRE(length.diagnostic == "GetArrayLength returned a negative length.");
}

TEST_CASE("an array created with the handle zero is an invalid response", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    ArrayCreatedAs(fake, 0);

    const auto array = Array<std::int32_t>::Create(component, 3);

    INFO("A created array with the handle 0 should not be accepted");
    REQUIRE(array.status == CallStatus::InvalidResponse);
    INFO("The explanation should name the call");
    REQUIRE(array.diagnostic == "CreateArray returned the invalid handle 0.");
}

TEST_CASE("an array element call that fails reports the bridge's status and explanation", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto array = IntArray(fake, component);
    fake.arrayGet = [](std::uint64_t, std::uint64_t, std::int32_t, std::int64_t, std::uint64_t*, std::uint8_t* bytes,
                       std::int32_t, std::int32_t* written, std::int32_t* truncated) {
        std::memcpy(bytes, "index out of range", 18);
        *written = 18;
        *truncated = 0;
        return 1;
    };

    const auto element = array.Get(9);

    INFO("A failed element read should fail");
    REQUIRE(element.status == CallStatus::InvalidArgument);
    INFO("The bridge's explanation should reach the caller");
    REQUIRE(element.diagnostic == "index out of range");
}

TEST_CASE("a struct handle outside the range the native side encodes is an invalid response", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    ArrayCreatedAs(fake, 5);
    const auto id = GENERATE(std::uint64_t{0}, std::uint64_t{1} << 22, std::uint64_t{0xFFFFFFFF});
    fake.registerStruct = [id](std::uint64_t, const std::uint8_t*, std::int32_t, std::uint64_t* out, std::uint8_t*,
                               std::int32_t, std::int32_t*, std::int32_t*) {
        *out = id;
        return 0;
    };

    const auto array = Array<Pair>::Create(component, 1);

    INFO("The struct handle " << id << " should not be accepted");
    REQUIRE(array.status == CallStatus::InvalidResponse);
    INFO("The explanation should name the call");
    REQUIRE(array.diagnostic == "RegisterStruct returned an invalid handle.");
}

TEST_CASE("the largest struct handle the native side encodes is accepted", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    ArrayCreatedAs(fake, 5);
    fake.registerStruct = [](std::uint64_t, const std::uint8_t*, std::int32_t, std::uint64_t* out, std::uint8_t*,
                             std::int32_t, std::int32_t*, std::int32_t*) {
        *out = (std::uint64_t{1} << 22) - 1;
        return 0;
    };

    const auto array = Array<Pair>::Create(component, 1);

    INFO("A handle just inside the range should be accepted: " + array.diagnostic);
    REQUIRE(array.Succeeded());
}

TEST_CASE("a struct mapping the bridge refuses fails the call that needed it", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    fake.registerStruct = [](std::uint64_t, const std::uint8_t*, std::int32_t, std::uint64_t*, std::uint8_t*,
                             std::int32_t, std::int32_t*, std::int32_t*) { return 6; };

    const auto array = Array<Pair>::Create(component, 1);

    INFO("A refused registration should fail the array");
    REQUIRE(array.status == CallStatus::NotFound);
    INFO("The explanation should name the call");
    REQUIRE(array.diagnostic == "RegisterStruct found nothing.");
    INFO("The bridge should not have been asked to create the array");
    REQUIRE(fake.Calls("CreateArray") == 0);
}

namespace
{
    // Makes the slot answer Success and write the handle 0 (the one handle the bridge never hands out).
    void AnswerWithHandleZero(FakeBridge& fake)
    {
        fake.createObject = [](std::uint64_t, std::uint64_t, const std::int32_t*, const std::uint64_t*, std::int32_t,
                               std::uint64_t* id, std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*) {
            *id = 0;
            return 0;
        };
        fake.findMethod = [](std::uint64_t, std::uint64_t, const std::uint8_t*, std::int32_t, std::int32_t,
                             const std::int32_t*, std::int32_t, std::uint64_t* id, std::uint8_t*, std::int32_t,
                             std::int32_t*, std::int32_t*) {
            *id = 0;
            return 0;
        };
        fake.findMember = [](std::uint64_t, std::uint64_t, const std::uint8_t*, std::int32_t, std::int32_t,
                             std::int32_t, std::uint64_t* id, std::uint8_t*, std::int32_t, std::int32_t*,
                             std::int32_t*) {
            *id = 0;
            return 0;
        };
    }
} // namespace

TEST_CASE("an object handle of zero from CreateObject is an invalid response", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    AnswerWithHandleZero(fake);

    const auto object = type.CreateObject();

    INFO("A created object with the handle 0 should not be accepted");
    REQUIRE(object.status == CallStatus::InvalidResponse);
    INFO("The explanation should name the call");
    REQUIRE(object.diagnostic == "CreateObject returned the invalid handle 0.");
}

TEST_CASE("a method handle of zero from FindMethod is an invalid response", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    AnswerWithHandleZero(fake);

    const auto method = type.FindMethod<void()>("Run");

    INFO("A found method with the handle 0 should not be accepted");
    REQUIRE(method.status == CallStatus::InvalidResponse);
    INFO("The explanation should name the call");
    REQUIRE(method.diagnostic == "FindMethod returned the invalid handle 0.");
}

TEST_CASE("a member handle of zero from FindMember is an invalid response", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    AnswerWithHandleZero(fake);

    const auto field = type.FindField<std::int32_t>("Health");

    INFO("A found field with the handle 0 should not be accepted");
    REQUIRE(field.status == CallStatus::InvalidResponse);
    INFO("The explanation should name the call");
    REQUIRE(field.diagnostic == "FindMember returned the invalid handle 0.");
}

TEST_CASE("an object handle of zero from a method that may return nothing is an empty object", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto type = TypeOf(component);
    fake.findMethod = [](std::uint64_t, std::uint64_t, const std::uint8_t*, std::int32_t, std::int32_t,
                         const std::int32_t*, std::int32_t, std::uint64_t* id, std::uint8_t*, std::int32_t,
                         std::int32_t*, std::int32_t*) {
        *id = 7;
        return 0;
    };
    const auto method = type.FindMethod<std::optional<Object>()>("Find");
    REQUIRE(method.Succeeded());
    fake.invokeMethod = [](std::uint64_t, std::uint64_t, const std::uint64_t*, std::int32_t, std::uint64_t* result,
                           std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*) {
        *result = 0;
        return 0;
    };

    const auto result = method.value->Invoke();

    INFO("A zero handle from a nullable object return should succeed: " + result.diagnostic);
    REQUIRE(result.Succeeded());
    INFO("It should be an empty object");
    REQUIRE(!result.value->has_value());
}

namespace
{
    struct TextCase
    {
        const char* what;
        std::string text;
    };
} // namespace

TEST_CASE("a string element is copied out whole and its block is given back once", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    ArrayCreatedAs(fake, 5);
    auto array = Array<std::string>::Create(component, 1);
    REQUIRE(array.Succeeded());
    const auto text = GENERATE(TextCase{"plain text", "hello"}, TextCase{"empty text", ""},
                               TextCase{"text with an embedded NUL", std::string("a\0b", 3)},
                               TextCase{"bytes that are not valid UTF-8", "\xFF\xFE"});
    fake.arrayGet = [&fake, bytes = Bytes(text.text)](std::uint64_t, std::uint64_t, std::int32_t, std::int64_t,
                                                      std::uint64_t* slot, std::uint8_t*, std::int32_t, std::int32_t*,
                                                      std::int32_t*) {
        *slot = fake.Block(bytes);
        return 0;
    };

    const auto element = array.value->Get(0);

    INFO("Reading " << text.what << " should succeed: " + element.diagnostic);
    REQUIRE(element.Succeeded());
    INFO("The string is not null");
    REQUIRE(element.value->has_value());
    INFO("The text should arrive as the bridge wrote it");
    REQUIRE(**element.value == text.text);
    INFO("The block should have been given back, once");
    REQUIRE(fake.OutstandingBlocks() == 0);
    REQUIRE(fake.BadFrees() == 0);
}

TEST_CASE("a string element the bridge reports as null is an empty optional and nothing is given back", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    ArrayCreatedAs(fake, 5);
    auto array = Array<std::string>::Create(component, 1);
    REQUIRE(array.Succeeded());
    fake.arrayGet = [](std::uint64_t, std::uint64_t, std::int32_t, std::int64_t, std::uint64_t* slot, std::uint8_t*,
                       std::int32_t, std::int32_t*, std::int32_t*) {
        *slot = 0;
        return 0;
    };

    const auto element = array.value->Get(0);

    INFO("A null string should be a success: " + element.diagnostic);
    REQUIRE(element.Succeeded());
    INFO("It should be null, not empty");
    REQUIRE(!element.value->has_value());
    INFO("Nothing was handed out, so nothing should be given back");
    REQUIRE(fake.OutstandingBlocks() == 0);
    REQUIRE(fake.BadFrees() == 0);
}

TEST_CASE("a string element the bridge rejects is an error with no block to give back", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    ArrayCreatedAs(fake, 5);
    auto array = Array<std::string>::Create(component, 1);
    REQUIRE(array.Succeeded());
    fake.arrayGet = [](std::uint64_t, std::uint64_t, std::int32_t, std::int64_t, std::uint64_t* slot, std::uint8_t*,
                       std::int32_t, std::int32_t*, std::int32_t*) {
        *slot = 0;
        return 1;
    };

    const auto element = array.value->Get(0);

    INFO("A rejected read should fail");
    REQUIRE(element.status == CallStatus::InvalidArgument);
    INFO("A failure should carry no value");
    REQUIRE(!element.value.has_value());
    INFO("Nothing was handed out, so nothing should be given back");
    REQUIRE(fake.OutstandingBlocks() == 0);
    REQUIRE(fake.BadFrees() == 0);
}

namespace
{
    struct StatusCase
    {
        std::int32_t wire;
        CallStatus expected;
        const char* diagnostic;
    };
} // namespace

TEST_CASE("every status the bridge can answer maps to the documented result", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto array = IntArray(fake, component);
    const auto answer = GENERATE(
        StatusCase{1, CallStatus::InvalidArgument, "GetArrayLength rejected an argument."},
        StatusCase{3, CallStatus::ManagedException, "GetArrayLength caught a managed exception."},
        StatusCase{5, CallStatus::InvalidHandle, "GetArrayLength received an invalid or released handle."},
        StatusCase{6, CallStatus::NotFound, "GetArrayLength found nothing."},
        StatusCase{7, CallStatus::InUse, "GetArrayLength was refused because the component has live objects."},
        StatusCase{4, CallStatus::InvalidResponse,
                   "GetArrayLength reported a buffer too small, but the buffer was sized from its own answer."},
        StatusCase{2, CallStatus::UnexpectedStatus, "GetArrayLength returned an unknown status: 2."},
        StatusCase{99, CallStatus::UnexpectedStatus, "GetArrayLength returned an unknown status: 99."},
        StatusCase{-1, CallStatus::UnexpectedStatus, "GetArrayLength returned an unknown status: -1."});
    LengthAnswers(fake, answer.wire, 3);

    const auto length = array.Length();

    INFO("The wire status " << answer.wire << " should map to the documented status");
    REQUIRE(length.status == answer.expected);
    INFO("The explanation should name the operation and the status");
    REQUIRE(length.diagnostic == answer.diagnostic);
    INFO("A failure should carry no value");
    REQUIRE(!length.value.has_value());
}

namespace
{
    struct DiagnosticCase
    {
        const char* what;
        std::int32_t status;
        std::vector<std::uint8_t> text;
        std::int32_t written;
        std::int32_t truncated;
        CallStatus expected;
        std::string diagnostic;
        bool diagnosticTruncated;
    };
} // namespace

TEST_CASE("diagnostics the bridge writes badly are reported without reading out of range", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto array = IntArray(fake, component);
    const std::string fallback = "GetArrayLength caught a managed exception.";
    const std::string invalidMetadata = fallback + " Managed diagnostic metadata was invalid.";
    const auto written = GENERATE_REF(
        DiagnosticCase{"valid text", 3, Bytes("boom"), 4, 0, CallStatus::ManagedException, "boom", false},
        DiagnosticCase{"valid text cut by the bridge", 3, Bytes("boom"), 4, 1, CallStatus::ManagedException, "boom",
                       true},
        DiagnosticCase{"no text", 3, {}, 0, 0, CallStatus::ManagedException, fallback, false},
        DiagnosticCase{"a negative count", 3, Bytes("boom"), -1, 0, CallStatus::ManagedException, invalidMetadata,
                       false},
        DiagnosticCase{"a count larger than the buffer", 3, Bytes("boom"), 4097, 0, CallStatus::ManagedException,
                       invalidMetadata, false},
        DiagnosticCase{"a count of INT32_MAX", 3, Bytes("boom"), 2147483647, 0, CallStatus::ManagedException,
                       invalidMetadata, false},
        DiagnosticCase{"a negative count on a success", 0, Bytes("boom"), -1, 0, CallStatus::InvalidResponse,
                       "A successful managed call returned diagnostic metadata.", false},
        DiagnosticCase{"a truncation flag of 2", 3, Bytes("boom"), 4, 2, CallStatus::ManagedException, invalidMetadata,
                       false},
        DiagnosticCase{"text that is not UTF-8",
                       3,
                       {0xFF, 0xFE},
                       2,
                       0,
                       CallStatus::ManagedException,
                       fallback + " Managed diagnostic text was not valid UTF-8.",
                       false},
        DiagnosticCase{"details on a success", 0, Bytes("boom"), 4, 0, CallStatus::InvalidResponse,
                       "A successful managed call returned diagnostic metadata.", false});
    LengthAnswers(fake, written.status, 3, written.text, written.written, written.truncated);

    const auto length = array.Length();

    INFO("Reading " << written.what << " should give the documented status");
    REQUIRE(length.status == written.expected);
    INFO("The diagnostic should be the documented text");
    REQUIRE(length.diagnostic == written.diagnostic);
    INFO("The truncation flag should follow the bridge's, when it was valid");
    REQUIRE(length.diagnosticTruncated == written.diagnosticTruncated);
}

namespace
{
    struct ReleaseCase
    {
        std::int32_t wire;
        CallStatus expected;
        bool stillLoaded;
    };
} // namespace

TEST_CASE("a refused component release leaves the component usable and any other answer ends the owner", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto answer =
        GENERATE(ReleaseCase{0, CallStatus::Success, false}, ReleaseCase{7, CallStatus::InUse, true},
                 ReleaseCase{5, CallStatus::InvalidHandle, false}, ReleaseCase{3, CallStatus::ManagedException, false});
    fake.release = [answer](std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*) {
        return answer.wire;
    };

    const auto released = component.Release();

    INFO("The release answered " << answer.wire);
    REQUIRE(released.status == answer.expected);
    INFO("Whether the owner still holds the component should follow the answer");
    REQUIRE(component.IsLoaded() == answer.stillLoaded);
}

namespace
{
    struct ObjectReleaseCase
    {
        std::int32_t wire;
        CallStatus expected;
    };

    Object ObjectFrom(FakeBridge& fake, const Component& component)
    {
        fake.createObject = [](std::uint64_t, std::uint64_t, const std::int32_t*, const std::uint64_t*, std::int32_t,
                               std::uint64_t* id, std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*) {
            *id = 9;
            return 0;
        };
        auto object = TypeOf(component).CreateObject();
        {
            INFO("The fake should have created an object: " + object.diagnostic);
            REQUIRE(object.Succeeded());
        }
        return std::move(*object.value);
    }
} // namespace

TEST_CASE("an object release answered with InvalidHandle or InUse reports it and ends the owner", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    auto object = ObjectFrom(fake, component);
    const auto answer =
        GENERATE(ObjectReleaseCase{5, CallStatus::InvalidHandle}, ObjectReleaseCase{7, CallStatus::InUse});
    fake.releaseObject = [answer](std::uint64_t, std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*,
                                  std::int32_t*) { return answer.wire; };

    const auto released = object.Release();

    INFO("The release answered " << answer.wire);
    REQUIRE(released.status == answer.expected);
    INFO("The owner should not hold the object any more");
    REQUIRE(!object.IsValid());
}

TEST_CASE("an object is not released a second time after a release the bridge refused", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    auto object = ObjectFrom(fake, component);
    fake.releaseObject = [](std::uint64_t, std::uint64_t, std::uint8_t*, std::int32_t, std::int32_t*, std::int32_t*) {
        return 7;
    };
    (void)object.Release();

    const auto again = object.Release();

    INFO("Releasing an owner that holds nothing should succeed");
    REQUIRE(again.Succeeded());
    INFO("The bridge should have been asked once");
    REQUIRE(fake.Calls("ReleaseObject") == 1);
}

TEST_CASE("a copy out of an array that fails reports the bridge's status and explanation", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    const auto array = IntArray(fake, component);
    fake.arrayCopy = [](std::uint64_t, std::uint64_t, std::int32_t, std::int64_t, std::int64_t, void*, std::int32_t,
                        std::uint8_t* bytes, std::int32_t, std::int32_t* written, std::int32_t* truncated) {
        std::memcpy(bytes, "range", 5);
        *written = 5;
        *truncated = 0;
        return 1;
    };
    std::array<std::int32_t, 2> out{};

    const auto copied = array.CopyOut(2, out);

    INFO("A failed copy should fail");
    REQUIRE(copied.status == CallStatus::InvalidArgument);
    INFO("The bridge's explanation should reach the caller");
    REQUIRE(copied.diagnostic == "range");
}

namespace
{
    // Makes every lookup of a method or member succeed with a handle, so a case can reach the call that follows.
    void LookupsFindHandles(FakeBridge& fake)
    {
        fake.findMethod = [](std::uint64_t, std::uint64_t, const std::uint8_t*, std::int32_t, std::int32_t,
                             const std::int32_t*, std::int32_t, std::uint64_t* id, std::uint8_t*, std::int32_t,
                             std::int32_t*, std::int32_t*) {
            *id = 7;
            return 0;
        };
        fake.findInstanceMethod = fake.findMethod;
        fake.findMember = [](std::uint64_t, std::uint64_t, const std::uint8_t*, std::int32_t, std::int32_t,
                             std::int32_t, std::uint64_t* id, std::uint8_t*, std::int32_t, std::int32_t*,
                             std::int32_t*) {
            *id = 8;
            return 0;
        };
    }
} // namespace

TEST_CASE("an object argument that was released is refused before the call, naming its position", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent();
    LookupsFindHandles(fake);
    auto object = ObjectFrom(fake, component);
    REQUIRE(object.Release().Succeeded());
    const auto method = TypeOf(component).FindMethod<std::int32_t(const Object&, const Object&)>("Total");
    REQUIRE(method.Succeeded());
    auto fresh = ObjectFrom(fake, component);

    const auto result = method.value->Invoke(fresh, object);

    INFO("A released object should be refused");
    REQUIRE(result.status == CallStatus::InvalidHandle);
    INFO("The explanation should name the argument");
    REQUIRE(result.diagnostic == "Argument 1 is an object that has been released or moved.");
    INFO("The bridge should not have been asked to invoke");
    REQUIRE(fake.Calls("InvokeMethod") == 0);
}

TEST_CASE("an object argument from another component is refused before the call, naming its position", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent(1);
    auto other = fake.MakeComponent(2);
    LookupsFindHandles(fake);
    auto foreign = ObjectFrom(fake, other);
    const auto method = TypeOf(component).FindMethod<std::int32_t(const Object&, const Object&)>("Total");
    REQUIRE(method.Succeeded());
    auto own = ObjectFrom(fake, component);

    const auto result = method.value->Invoke(foreign, own);

    INFO("An object of another component should be refused");
    REQUIRE(result.status == CallStatus::InvalidArgument);
    INFO("The explanation should name the argument");
    REQUIRE(result.diagnostic == "Argument 0 is an object from another component.");
    INFO("The bridge should not have been asked to invoke");
    REQUIRE(fake.Calls("InvokeMethod") == 0);
}

TEST_CASE("an instance method refuses a released target and one from another component", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent(1);
    auto other = fake.MakeComponent(2);
    LookupsFindHandles(fake);
    const auto method = TypeOf(component).FindInstanceMethod<std::int32_t()>("Value");
    REQUIRE(method.Succeeded());
    auto released = ObjectFrom(fake, component);
    REQUIRE(released.Release().Succeeded());
    auto foreign = ObjectFrom(fake, other);

    const auto releasedResult = method.value->Invoke(released);
    const auto foreignResult = method.value->Invoke(foreign);

    INFO("A released target should be refused");
    REQUIRE(releasedResult.status == CallStatus::InvalidHandle);
    REQUIRE(releasedResult.diagnostic == "The object has been released or moved.");
    INFO("A target of another component should be refused");
    REQUIRE(foreignResult.status == CallStatus::InvalidArgument);
    REQUIRE(foreignResult.diagnostic == "The object belongs to another component.");
    INFO("The bridge should not have been asked to invoke either");
    REQUIRE(fake.Calls("InvokeInstanceMethod") == 0);
}

TEST_CASE("a member refuses a released target and one from another component with the same words", "[fake]")
{
    FakeBridge fake;
    auto component = fake.MakeComponent(1);
    auto other = fake.MakeComponent(2);
    LookupsFindHandles(fake);
    const auto field = TypeOf(component).FindField<std::int32_t>("Health");
    REQUIRE(field.Succeeded());
    auto released = ObjectFrom(fake, component);
    REQUIRE(released.Release().Succeeded());
    auto foreign = ObjectFrom(fake, other);

    const auto releasedResult = field.value->Get(released);
    const auto foreignResult = field.value->Get(foreign);

    INFO("A released target should be refused");
    REQUIRE(releasedResult.status == CallStatus::InvalidHandle);
    REQUIRE(releasedResult.diagnostic == "The object has been released or moved.");
    INFO("A target of another component should be refused");
    REQUIRE(foreignResult.status == CallStatus::InvalidArgument);
    REQUIRE(foreignResult.diagnostic == "The object belongs to another component.");
    INFO("The bridge should not have been asked to read");
    REQUIRE(fake.Calls("GetMember") == 0);
}
