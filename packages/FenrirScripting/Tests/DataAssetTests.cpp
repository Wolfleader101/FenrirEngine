// C# data asset types (ADR-0083) and the tree encoding in C# (ADR-0085 §2): the fixture's TreeReader and TreeWriter
// against FenrirSerialisation's EncodeTree and DecodeTree over the shared fixtures in TreeEncoding/, byte for byte; and
// the generated codecs of the fixture's data asset classes, their trees and their description entries; and their
// admission to the type registry.

#include "FixtureCalls.hpp"
#include "HeadlessApp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "FenrirScripting/Scripting.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Text.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    using namespace Fenrir::Testing::ScriptFixture;
    using namespace Fenrir::Serialisation;

    constexpr const char* TreesType = "Fenrir.ScriptFixture.Trees";
    constexpr const char* DataAssetsType = "Fenrir.ScriptFixture.DataAssets";

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        {
            INFO("The fixture " + path.string() + " could not be read");
            REQUIRE(file.good());
        }
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    std::filesystem::path Fixture(std::string_view name)
    {
        return std::filesystem::path(FENRIR_TREE_ENCODING_FIXTURES) / name;
    }

    std::string Hex(std::span<const std::byte> bytes)
    {
        static constexpr char Digits[] = "0123456789ABCDEF";
        std::string hex;
        for (std::byte byte : bytes)
        {
            hex.push_back(Digits[std::to_integer<unsigned>(byte) >> 4]);
            hex.push_back(Digits[std::to_integer<unsigned>(byte) & 0xF]);
        }
        return hex;
    }

    std::vector<std::byte> FromHex(std::string_view hex)
    {
        std::vector<std::byte> bytes;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
            bytes.push_back(static_cast<std::byte>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
        return bytes;
    }

    // UTF-8, the three-byte form of a lone surrogate included, as big-endian UTF-16 code units in hex: how the
    // fixture's Trees.Dump shows a string, so a lone surrogate shows as itself on both sides.
    std::string Utf16Hex(std::string_view text)
    {
        std::vector<std::byte> units;
        const auto unit = [&](std::uint32_t code) {
            units.push_back(static_cast<std::byte>(code >> 8));
            units.push_back(static_cast<std::byte>(code & 0xFF));
        };
        for (std::size_t i = 0; i < text.size();)
        {
            const auto lead = static_cast<std::uint8_t>(text[i]);
            const std::size_t length = lead < 0x80 ? 1 : lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
            std::uint32_t code = length == 1 ? lead : lead & (0xFF >> (length + 1));
            for (std::size_t k = 1; k < length; ++k)
                code = (code << 6) | (static_cast<std::uint8_t>(text[i + k]) & 0x3F);
            if (code >= 0x10000)
            {
                code -= 0x10000;
                unit(0xD800 + (code >> 10));
                unit(0xDC00 + (code & 0x3FF));
            }
            else
            {
                unit(code);
            }
            i += length;
        }
        return Hex(units);
    }

    // A value tree in the form the fixture's Trees.Dump writes a TreeNode: numbers by width, floats by their bits.
    void Dump(const Value& value, std::string& out)
    {
        char buffer[32];
        switch (value.Kind())
        {
        case ValueKind::Null:
            out += "null";
            return;
        case ValueKind::Bool:
            out += value.AsBool() ? "true" : "false";
            return;
        case ValueKind::Number: {
            const Number& number = value.AsNumber();
            switch (number.Width())
            {
            case NumberWidth::Signed:
                out += "i:" + std::to_string(number.AsSigned());
                return;
            case NumberWidth::Unsigned:
                out += "u:" + std::to_string(number.AsUnsigned());
                return;
            case NumberWidth::Single:
                std::snprintf(buffer, sizeof buffer, "f:%08X", std::bit_cast<std::uint32_t>(number.AsSingle()));
                out += buffer;
                return;
            case NumberWidth::Double:
                std::snprintf(buffer, sizeof buffer, "d:%016llX",
                              static_cast<unsigned long long>(std::bit_cast<std::uint64_t>(number.AsDouble())));
                out += buffer;
                return;
            }
            return;
        }
        case ValueKind::String:
            out += "s:" + Utf16Hex(value.AsString());
            return;
        case ValueKind::Object:
            out += '{';
            for (const Member& member : value.AsObject().members)
            {
                out += Utf16Hex(member.key) + "=";
                Dump(member.value, out);
                out += ';';
            }
            out += '}';
            return;
        case ValueKind::Array:
            out += '[';
            for (const Element& element : value.AsArray().elements)
            {
                Dump(element.value, out);
                out += ';';
            }
            out += ']';
            return;
        }
    }

    std::string Dump(const Value& value)
    {
        std::string out;
        Dump(value, out);
        return out;
    }

    // A headless app running the fixture script assembly.
    struct FixtureApp
    {
        HeadlessApp headless;
        Scripting scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));

        FixtureApp()
        {
            INFO("The fixture did not load: " + Errors(headless));
            REQUIRE(scripting.HasScripts());
        }

        std::string Trees(const char* method, std::string_view argument)
        {
            return CallIn<std::optional<std::string>>(TreesType, scripting, method, argument).value_or("");
        }

        // A data asset check: null when it holds, else what went wrong.
        void Check(const char* method)
        {
            const auto failure = CallIn<std::optional<std::string>>(DataAssetsType, scripting, method);
            INFO(std::string(method) + ": " + failure.value_or(""));
            REQUIRE(!failure.has_value());
        }
    };

    Value ReadFixture(HeadlessApp& headless, std::string_view name)
    {
        const std::string text = ReadFile(Fixture(name));
        ReadTreeResult read = ReadText(text, name, const_cast<RecordingLogger&>(headless.Logger()));
        {
            INFO("The fixture " + std::string(name) + " is not JSON5");
            REQUIRE(read.tree.has_value());
        }
        return std::move(*read.tree);
    }

    void TheCSharpReaderAndWriterMatchTheEncodingOnTheSharedFixtures()
    {
        FixtureApp app;
        for (const char* name : {"numbers.json5", "strings.json5", "containers.json5", "commented.json5"})
        {
            const Value tree = ReadFixture(app.headless, name);
            const std::string encoded = Hex(EncodeTree(tree, Trivia::Drop));
            {
                INFO(std::string(name) + ": TreeReader then TreeWriter did not give EncodeTree's bytes back");
                REQUIRE(app.Trees("Rewrite", encoded) == encoded);
            }
            {
                INFO(std::string(name) + ": TreeReader did not read the tree EncodeTree wrote");
                REQUIRE(app.Trees("Dump", encoded) == Dump(tree));
            }
            // With its comments kept, the encoding has trivia nodes, which TreeReader skips: written back, it is the
            // encoding with none.
            const std::string withTrivia = Hex(EncodeTree(tree, Trivia::Keep));
            {
                INFO(std::string(name) + ": TreeReader did not drop the trivia of EncodeTree's bytes");
                REQUIRE(app.Trees("Rewrite", withTrivia) == encoded);
            }
        }
    }

    void ATreeTheCSharpWriterBuildsDecodesAsBuilt()
    {
        FixtureApp app;
        const std::vector<std::byte> bytes =
            FromHex(CallIn<std::optional<std::string>>(TreesType, app.scripting, "Built").value_or(""));
        ReadTreeResult decoded = DecodeTree(bytes, "built", const_cast<RecordingLogger&>(app.headless.Logger()));
        {
            INFO("DecodeTree refused the bytes TreeWriter wrote: " + Errors(app.headless));
            REQUIRE(decoded.tree.has_value());
        }
        {
            INFO("EncodeTree did not write TreeWriter's bytes back");
            REQUIRE(EncodeTree(*decoded.tree, Trivia::Drop) == bytes);
        }
        const std::string expected = "{" + Utf16Hex("single") + "=f:3F3504F4;" + Utf16Hex("negativeZero") +
                                     "=f:80000000;" + Utf16Hex("nan") + "=f:7FC00001;" + Utf16Hex("double") +
                                     "=d:3FD5555555555555;" + Utf16Hex("unsigned") + "=u:18446744073709551615;" +
                                     Utf16Hex("signed") +
                                     "=i:-9223372036854775808;=[s:D800;s:D83DDE00;null;true;false;{};];}";
        INFO("The tree TreeWriter wrote decoded otherwise: " + Dump(*decoded.tree));
        REQUIRE(Dump(*decoded.tree) == expected);
    }

    void TheCSharpReaderRefusesWhatDecodeTreeRefusesAtTheSameByte()
    {
        FixtureApp app;
        // Each a buffer DecodeTree refuses: a wrong tag, a wrong version, no root, an unknown node tag, a short number,
        // a string past the end, bad UTF-8, an overflowing LEB128, two trivia nodes, two trailing comments, a trailing
        // comment on the root, closing comments in front of a scalar, bytes after the root, and nesting too deep.
        std::vector<std::string> refused = {
            "0146565401",
            "004656540201",
            "0046565401",
            "00465654010C",
            "00465654010401",
            "0046565401080541",
            "00465654010802C328",
            "004656540108808080808080808080807F",
            "00465654010B0000000B00000001",
            "00465654010A010B000201610162000001",
            "00465654010B000101610001",
            "00465654010B000001016101",
            "004656540101FF",
        };
        std::string deep = "0046565401";
        for (int i = 0; i < 257; ++i)
            deep += "0A01";
        refused.push_back(deep + "01");
        for (const std::string& hex : refused)
        {
            const std::vector<std::byte> bytes = FromHex(hex);
            ReadTreeResult decoded = DecodeTree(bytes, "refused", const_cast<RecordingLogger&>(app.headless.Logger()));
            {
                INFO(hex.substr(0, 40) + " was not refused by DecodeTree");
                REQUIRE((!decoded.tree.has_value() && decoded.diagnostics.size() == 1));
            }
            const auto offset = CallIn<std::int64_t>(TreesType, app.scripting, "RefusedAt", std::string_view(hex));
            INFO(hex.substr(0, 40) + ": DecodeTree refused it at byte " +
                 std::to_string(decoded.diagnostics[0].column - 1) + ", TreeReader at " + std::to_string(offset));
            REQUIRE(offset == static_cast<std::int64_t>(decoded.diagnostics[0].column - 1));
        }
    }

    void ADataAssetRoundTripsThroughItsCodecs()
    {
        FixtureApp app;
        app.Check("RoundTrip");
        app.Check("MissingFieldsDefault");
        app.Check("DefaultsLeftOut");
        app.Check("Upgrades");
    }

    // The tree in `hex` as the JSON5 the golden `name` holds.
    void ExpectGolden(FixtureApp& app, const std::string& hex, std::string_view name)
    {
        ReadTreeResult decoded = DecodeTree(FromHex(hex), name, const_cast<RecordingLogger&>(app.headless.Logger()));
        {
            INFO(std::string(name) + ": DecodeTree refused the C# bytes: " + Errors(app.headless));
            REQUIRE(decoded.tree.has_value());
        }
        const std::string written = WriteText(*decoded.tree);
        INFO(std::string(name) + " differs; the C# tree is:\n" + written);
        REQUIRE(written == ReadFile(Fixture(name)));
    }

    void ADataAssetsTreeAndEntryAreWhatCPlusPlusReads()
    {
        FixtureApp app;
        ExpectGolden(app, CallIn<std::optional<std::string>>(DataAssetsType, app.scripting, "SampleTree").value_or(""),
                     "weapon.tree.json5");
        ExpectGolden(
            app,
            CallIn<std::optional<std::string>>(DataAssetsType, app.scripting, "Entry", std::string_view("item"))
                .value_or(""),
            "item.entry.json5");
        ExpectGolden(
            app,
            CallIn<std::optional<std::string>>(DataAssetsType, app.scripting, "Entry", std::string_view("weapon"))
                .value_or(""),
            "weapon.entry.json5");
    }
} // namespace

TEST_CASE("the C# tree reader and writer give EncodeTree's bytes on the shared fixtures", "[scripting][data-assets]")
{
    TheCSharpReaderAndWriterMatchTheEncodingOnTheSharedFixtures();
}

TEST_CASE("a tree the C# writer builds decodes in C++ as built", "[scripting][data-assets]")
{
    ATreeTheCSharpWriterBuildsDecodesAsBuilt();
}

TEST_CASE("the C# tree reader refuses what DecodeTree refuses, at the same byte", "[scripting][data-assets]")
{
    TheCSharpReaderRefusesWhatDecodeTreeRefusesAtTheSameByte();
}

TEST_CASE("a C# data asset round-trips through its generated codecs", "[scripting][data-assets]")
{
    ADataAssetRoundTripsThroughItsCodecs();
}

TEST_CASE("a C# data asset's tree and description entry are what C++ reads", "[scripting][data-assets]")
{
    ADataAssetsTreeAndEntryAreWhatCPlusPlusReads();
}

// Admission of the fixture's data asset types (ADR-0083): the engine finds the generated DataAssetTable, its Register,
// Upgrade and Forget by the names GeneratedNames.hpp spells, so a drift from the generator's GeneratedNames.cs fails
// here.
namespace
{
    constexpr const char* ItemName = "Fenrir.ScriptFixture.ItemData";
    constexpr const char* WeaponName = "Fenrir.ScriptFixture.WeaponData";

    void TheFixturesDataAssetTypesAreRegisteredWhileItRuns()
    {
        HeadlessApp headless;
        const auto& types = headless.Get().GetResource<TypeRegistry>();
        {
            Scripting scripting = Scripting::Install(headless.Get(), ConfigFor(fixture));
            INFO("The fixture did not load: " + Errors(headless));
            REQUIRE(scripting.HasScripts());

            const TypeDescription* weapon = types.Find(WeaponName);
            const TypeDescription* item = types.Find(ItemName);
            INFO("Both data asset types should be tree-backed descriptions, the subtype is-a its C# base");
            REQUIRE((weapon != nullptr && item != nullptr));
            REQUIRE((weapon->treeBacked && weapon->role == Role::DataAsset && weapon->version == 3 &&
                     weapon->base == ItemName));
            REQUIRE(types.IsA(WeaponName, ItemName));
            REQUIRE(types.Find("Fenrir.ScriptFixture.Thing") == item);
            INFO("Its struct and enums should be described beside it");
            REQUIRE((types.Find("Fenrir.ScriptFixture.Stats") != nullptr &&
                     types.Find("Fenrir.ScriptFixture.Stats")->treeBacked));
            REQUIRE((types.Find("Fenrir.ScriptFixture.Element") != nullptr &&
                     types.Find("Fenrir.ScriptFixture.Element")->role == Role::Enum));

            INFO("Its upgraders should run the script's, through the table's Upgrade");
            const auto* upgraders = types.Attachment<UpgradersTag>(WeaponName);
            REQUIRE((upgraders != nullptr && upgraders->size() == 2));
            Value tree = Object{};
            tree.AsObject().Add("Dmg", 8);
            const auto upgraded = Upgrade(types, WeaponName, 1, tree, "weapon.asset.json5",
                                          const_cast<RecordingLogger&>(headless.Logger()));
            REQUIRE(upgraded.status == UpgradeStatus::Succeeded);
            REQUIRE((tree.AsObject().Find("I32") != nullptr && tree.AsObject().Find("Dmg") == nullptr));
            REQUIRE(tree.AsObject().Find("I32")->AsNumber().AsSigned() == 8);
        }
        INFO("Stopping should unregister the types and their upgraders");
        REQUIRE((types.Find(WeaponName) == nullptr && types.Find(ItemName) == nullptr &&
                 types.Find("Fenrir.ScriptFixture.Stats") == nullptr));
    }
} // namespace

TEST_CASE("the fixture's C# data asset types are registered while it runs, with upgraders, and go when it stops",
          "[scripting][data-assets]")
{
    TheFixturesDataAssetTypesAreRegisteredWhileItRuns();
}
