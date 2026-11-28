// Tree-backed data assets (ADR-0083) and the store's by-name doors. A tree-backed type is one only the running engine
// describes, as a C# data asset type is: the store holds its record as a value tree, and, when it extends a C++ type,
// a materialised struct of that base, which C++ TryGet and Edit read and change. These tests register such a type by
// hand, `AssetsTest::Dagger` extending `AssetsTest::WeaponData`, and drive it through the doors scripting and tools
// use, which take and give trees in the tree encoding, beside a C++ type through the same doors.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DataAssetsTest.hpp"
#include "FenrirSerialisation/Encoding.hpp"
#include "FenrirSerialisation/Upgrade.hpp"
#include "FenrirSerialisation/Value.hpp"
#include "FenrirTypes/FieldInfo.hpp"
#include "FenrirTypes/TypeDescription.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

using AssetsTest::CountContaining;
using AssetsTest::DataApp;
using AssetsTest::FindDiagnostic;
using AssetsTest::Fixtures;
using AssetsTest::ReadFile;
using AssetsTest::Sword;
using AssetsTest::TempRoot;
using AssetsTest::WeaponData;

namespace
{
    namespace Ser = Fenrir::Serialisation;

    constexpr std::string_view DaggerPath = "data/dagger.asset.json5";
    constexpr std::string_view OldDaggerPath = "data/old-dagger.asset.json5";
    constexpr std::string_view SwordPath = "data/sword.asset.json5";
    constexpr std::string_view DaggerName = "AssetsTest::Dagger";
    constexpr std::string_view CharmName = "AssetsTest::Charm";

    // `{}` in the tree encoding: its tag, its version, then an object of no members. Every default here is empty, so
    // an absent field keeps the default a script's class gives it.
    constexpr std::array<std::byte, 7> EmptyObject{std::byte{0x00}, std::byte{0x46}, std::byte{0x56}, std::byte{0x54},
                                                   std::byte{0x01}, std::byte{0x09}, std::byte{0x00}};

    constexpr Fenrir::ValueType RuneType{.kind = Fenrir::Kind::String};
    constexpr std::string_view ReachFormerly[] = {"length"};
    constexpr Fenrir::FieldInfo DaggerFields[] = {
        {.name = "reach", .type = {.kind = Fenrir::Kind::F32}, .formerly = ReachFormerly},
        {.name = "runes", .type = {.kind = Fenrir::Kind::List, .element = &RuneType}},
        {.name = "companion", .type = {.kind = Fenrir::Kind::Reference, .target = "AssetsTest::WeaponData"}},
    };

    // A dagger, as a C# class extending WeaponData would be described: no layout, its fields named and kinded. Version
    // 1 held one `rune`, which its upgrader makes a list, and called `reach` `length`.
    constexpr Fenrir::TypeDescription Dagger{.name = DaggerName,
                                             .package = "FenrirAssetsTests",
                                             .role = Fenrir::Role::DataAsset,
                                             .version = 2,
                                             .base = "AssetsTest::WeaponData",
                                             .fields = DaggerFields,
                                             .treeBacked = true,
                                             .defaultTree = EmptyObject};

    constexpr Fenrir::FieldInfo CharmFields[] = {{.name = "glow", .type = {.kind = Fenrir::Kind::F32}}};

    // A charm: a tree-backed type extending nothing, so no C++ type reads it.
    constexpr Fenrir::TypeDescription Charm{.name = CharmName,
                                            .package = "FenrirAssetsTests",
                                            .role = Fenrir::Role::DataAsset,
                                            .fields = CharmFields,
                                            .treeBacked = true,
                                            .defaultTree = EmptyObject};

    // A data app with the dagger and the charm registered beside the test package's types, as admission registers a
    // script's, and the dagger's upgrader.
    struct TreeApp : DataApp
    {
        Fenrir::TypeRegistry& registry = app.GetResource<Fenrir::TypeRegistry>();
        bool treeRegistered = registry.Register(Dagger) && registry.Register(Charm) &&
                              Ser::RegisterUpgrader(
                                  registry, DaggerName, 1,
                                  [](std::uint32_t, Ser::Value& tree) {
                                      Ser::Object& data = tree.AsObject();
                                      for (Ser::Member& member : data.members)
                                      {
                                          if (member.key == "rune")
                                          {
                                              Ser::Array runes;
                                              runes.Add(std::move(member.value));
                                              member.key = "runes";
                                              member.value = Ser::Value(std::move(runes));
                                          }
                                      }
                                  },
                                  store.Logger());

        TreeApp() = default;
        explicit TreeApp(Fenrir::AssetsConfig config) : DataApp(std::move(config))
        {
        }
    };

    // The tree a door gives for `handle`, decoded; null when it gives none.
    Ser::Value TreeOf(TreeApp& test, Fenrir::DataAssetHandle handle)
    {
        const std::optional<Fenrir::DataTree> tree = test.store.DataAssetTree(handle);
        if (!tree)
        {
            return {};
        }
        Ser::ReadTreeResult decoded = Ser::DecodeTree(tree->bytes, "test", test.store.Logger());
        return decoded.tree ? std::move(*decoded.tree) : Ser::Value();
    }

    // `tree` in the tree encoding, as a door takes it.
    std::vector<std::byte> Encoded(const Ser::Value& tree)
    {
        return Ser::EncodeTree(tree, Ser::Trivia::Drop);
    }

    // The member `key` of the object `tree`, or null.
    const Ser::Value* MemberOf(const Ser::Value& tree, std::string_view key)
    {
        return tree.Kind() == Ser::ValueKind::Object ? tree.AsObject().Find(key) : nullptr;
    }

    // Sets the member `key` of the object `tree` to `value`, adding it when it has none.
    void Set(Ser::Value& tree, std::string_view key, Ser::Value value)
    {
        if (Ser::Value* member = tree.AsObject().Find(key))
        {
            *member = std::move(value);
            return;
        }
        tree.AsObject().Add(std::string(key), std::move(value));
    }

    // A temporary root holding the dagger and the sword it names.
    void CopyDagger(const TempRoot& root)
    {
        root.Copy(Fixtures() / DaggerPath, DaggerPath);
        root.Copy(Fixtures() / SwordPath, SwordPath);
    }

    // A config whose asset root is `root`, watching with no interval and no debounce.
    Fenrir::AssetsConfig Watching(const TempRoot& root)
    {
        Fenrir::AssetsConfig config{root.Path()};
        config.reloadInterval = 0;
        config.reloadDebounce = 0;
        return config;
    }
} // namespace

TEST_CASE("A tree-backed data asset loads from its envelope and saves back byte-identical", "[assets][data][tree]")
{
    TempRoot root;
    CopyDagger(root);
    TreeApp test(Fenrir::AssetsConfig{root.Path()});
    REQUIRE(test.treeRegistered);

    const Fenrir::DataLoadResult loaded = test.store.LoadDataAsset(DaggerPath, DaggerName, Fenrir::AssetOwner::App());
    INFO("the dagger loads whole, its reference to the sword loaded with it");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    REQUIRE(test.errors->empty());

    const Ser::Value tree = TreeOf(test, loaded.handle);
    INFO("its tree holds its base's fields and its own, its reference as the sword's path");
    REQUIRE(MemberOf(tree, "displayName")->AsString() == "Kris");
    REQUIRE(MemberOf(tree, "runes")->AsArray().elements.size() == 2);
    REQUIRE(MemberOf(tree, "companion")->AsString() == SwordPath);
    REQUIRE(test.store.DataAssetTree(loaded.handle)->type == DaggerName);

    INFO("a load as its C++ base gives the same handle, and reads the materialised base");
    const Fenrir::LoadResult<WeaponData> asBase = test.store.Load<WeaponData>(DaggerPath, Fenrir::AssetOwner::App());
    REQUIRE(asBase.handle.index == loaded.handle.index);
    const WeaponData* weapon = test.store.TryGet(asBase.handle);
    REQUIRE(weapon != nullptr);
    REQUIRE(weapon->displayName == "Kris");
    REQUIRE(weapon->stats.damage == 7.0f);

    const std::string original = ReadFile(Fixtures() / DaggerPath);
    INFO("saving it through the door writes back the bytes it was read from, comments and all");
    REQUIRE(test.store.SaveDataAsset(loaded.handle).status == Fenrir::SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / DaggerPath) == original);
    INFO("and so does saving it as its C++ base");
    REQUIRE(test.store.Save(asBase.handle).status == Fenrir::SaveStatus::Succeeded);
    REQUIRE(ReadFile(root.Path() / DaggerPath) == original);
}

TEST_CASE("A tree-backed data asset reloads in place, its revision bumped and its base refreshed",
          "[assets][data][tree][reload]")
{
    TempRoot root;
    CopyDagger(root);
    TreeApp test(Watching(root));
    const Fenrir::DataLoadResult loaded = test.store.LoadDataAsset(DaggerPath, DaggerName, Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());
    test.app.RunFrame(1.0 / 60.0);
    const std::uint32_t revision = test.store.Revision(loaded.handle);
    const WeaponData* before = test.store.TryGet(loaded.handle.As<WeaponData>());
    REQUIRE(before != nullptr);

    std::string text = ReadFile(Fixtures() / DaggerPath);
    text.replace(text.find("Kris"), 4, "Keris");
    text.replace(text.find("0.25"), 4, "0.75");
    root.Write(DaggerPath, text);
    test.app.RunFrame(1.0 / 60.0);
    test.app.RunFrame(1.0 / 60.0);

    INFO("the same handle reads the new tree, one revision on");
    REQUIRE(test.store.Revision(loaded.handle) == revision + 1);
    const Ser::Value tree = TreeOf(test, loaded.handle);
    REQUIRE(MemberOf(tree, "reach")->AsNumber() == Ser::Number(0.75f));
    INFO("and its materialised base is refreshed where it was, so a pointer TryGet gave reads the reload");
    REQUIRE(test.store.TryGet(loaded.handle.As<WeaponData>()) == before);
    REQUIRE(before->displayName == "Keris");
    REQUIRE(test.errors->empty());
}

TEST_CASE("TryGet and Edit of a tree-backed asset's C++ base read and change its tree", "[assets][data][tree]")
{
    TreeApp test;
    const Fenrir::DataLoadResult loaded = test.store.LoadDataAsset(DaggerPath, DaggerName, Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());
    const Fenrir::AssetHandle<WeaponData> weapon = loaded.handle.As<WeaponData>();
    const std::uint32_t revision = test.store.Revision(weapon);

    WeaponData* edited = test.store.Edit(weapon, "rename the dagger");
    REQUIRE(edited != nullptr);
    edited->displayName = "Kampilan";
    edited->stats.weight = 2.0f;

    INFO("the edit bumped the revision, and is in the record's tree, beside its own fields");
    REQUIRE(test.store.Revision(weapon) == revision + 1);
    const Ser::Value tree = TreeOf(test, loaded.handle);
    REQUIRE(MemberOf(tree, "displayName")->AsString() == "Kampilan");
    REQUIRE(MemberOf(*MemberOf(tree, "stats"), "weight")->AsNumber() == Ser::Number(2.0f));
    REQUIRE(MemberOf(tree, "runes")->AsArray().elements.size() == 2);

    INFO("a typed Replace of a C++ type never replaces a tree-backed record, which is no C++ type exactly");
    REQUIRE_FALSE(test.store.Replace(weapon, WeaponData{}));
    REQUIRE(CountContaining(*test.errors, "only a value of exactly its type replaces it") == 1);
}

TEST_CASE("Each by-name door round-trips a C++ type and a tree-backed type through tree bytes", "[assets][data][tree]")
{
    TempRoot root;
    CopyDagger(root);
    TreeApp test(Fenrir::AssetsConfig{root.Path()});

    for (const auto& [path, typeName, field, value] :
         {std::tuple{SwordPath, std::string_view("AssetsTest::Sword"), std::string_view("edge"), 0.125f},
          std::tuple{DaggerPath, DaggerName, std::string_view("reach"), 0.125f}})
    {
        CAPTURE(typeName);
        const Fenrir::DataLoadResult loaded = test.store.LoadDataAsset(path, typeName, Fenrir::AssetOwner::App());
        REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
        const std::uint32_t revision = test.store.Revision(loaded.handle);

        INFO("replace from a changed tree: the revision moves and the tree reads the change");
        Ser::Value tree = TreeOf(test, loaded.handle);
        Set(tree, field, Ser::Value(value));
        Set(tree, "displayName", Ser::Value("Renamed"));
        REQUIRE(test.store.ReplaceDataAsset(loaded.handle, Encoded(tree)));
        REQUIRE(test.store.Revision(loaded.handle) == revision + 1);
        const Ser::Value replaced = TreeOf(test, loaded.handle);
        REQUIRE(MemberOf(replaced, field)->AsNumber() == Ser::Number(value));
        REQUIRE(test.store.TryGet(loaded.handle.As<WeaponData>())->displayName == "Renamed");

        INFO("add a named record from the tree, save it, and load it again in a fresh store");
        const std::string name = std::format("made/{}.asset.json5", field);
        const Fenrir::DataAssetHandle added =
            test.store.AddDataAsset(typeName, Encoded(replaced), Fenrir::AssetOwner::App(), name);
        REQUIRE_FALSE(added.IsEmpty());
        REQUIRE(Ser::Equals(TreeOf(test, added), replaced));
        REQUIRE(test.store.SaveDataAsset(added).status == Fenrir::SaveStatus::Succeeded);
        {
            TreeApp fresh(Fenrir::AssetsConfig{root.Path()});
            const Fenrir::DataLoadResult again = fresh.store.LoadDataAsset(name, typeName, Fenrir::AssetOwner::App());
            REQUIRE(again.Status() == Fenrir::LoadStatus::Succeeded);
            REQUIRE(Ser::Equals(TreeOf(fresh, again.handle), replaced));
        }

        INFO("save to another path writes a copy that reads the same");
        const std::string copy = std::format("copies/{}.asset.json5", field);
        REQUIRE(test.store.SaveDataAsset(loaded.handle, copy).status == Fenrir::SaveStatus::Succeeded);
        REQUIRE(std::filesystem::exists(root.Path() / copy));
    }
    REQUIRE(test.errors->empty());
}

TEST_CASE("The by-name doors refuse a stale handle and bytes that are no tree, each with an error",
          "[assets][data][tree]")
{
    TreeApp test;
    INFO("a stale handle has no tree, and is neither replaced nor saved");
    REQUIRE_FALSE(test.store.DataAssetTree({}).has_value());
    REQUIRE_FALSE(test.store.ReplaceDataAsset({}, Encoded(Ser::Value(Ser::Object{}))));
    REQUIRE(CountContaining(*test.errors, "Could not replace a data asset: its handle is empty or stale") == 1);
    REQUIRE(test.store.SaveDataAsset({}).status == Fenrir::SaveStatus::Failed);
    REQUIRE(CountContaining(*test.errors, "the data asset handle is empty or stale") == 1);

    INFO("bytes that are not the tree encoding add nothing");
    const std::array<std::byte, 2> junk{std::byte{1}, std::byte{2}};
    REQUIRE(test.store.AddDataAsset(DaggerName, junk, Fenrir::AssetOwner::App()).IsEmpty());
    REQUIRE(CountContaining(*test.errors, "not a value tree in the tree encoding") == 1);

    INFO("nor does a type the registry has as no data asset type");
    REQUIRE(test.store
                .AddDataAsset("AssetsTest::WeaponStats", Encoded(Ser::Value(Ser::Object{})), Fenrir::AssetOwner::App())
                .IsEmpty());
    REQUIRE(CountContaining(*test.errors, "`AssetsTest::WeaponStats` is a type, but no data asset type") == 1);
}

TEST_CASE("Loading by type name checks the file's type is the one named or extends it", "[assets][data][tree]")
{
    TreeApp test;
    INFO("the dagger is a WeaponData, but no Sword");
    REQUIRE(test.store.LoadDataAsset(DaggerPath, "AssetsTest::WeaponData", Fenrir::AssetOwner::App()).Ok());
    const Fenrir::DataLoadResult asSword =
        test.store.LoadDataAsset(DaggerPath, "AssetsTest::Sword", Fenrir::AssetOwner::App());
    REQUIRE(asSword.diagnostic->reason == Fenrir::LoadFailure::WrongType);
    INFO("a sword file is no Dagger");
    const Fenrir::DataLoadResult swordAsDagger =
        test.store.LoadDataAsset(SwordPath, DaggerName, Fenrir::AssetOwner::App());
    REQUIRE(swordAsDagger.diagnostic->reason == Fenrir::LoadFailure::WrongType);
    INFO("a name the registry lacks, or one of a type that is no data asset, is NotADataAsset");
    REQUIRE(test.store.LoadDataAsset(DaggerPath, "AssetsTest::Nothing", Fenrir::AssetOwner::App()).diagnostic->reason ==
            Fenrir::LoadFailure::NotADataAsset);
    REQUIRE(
        test.store.LoadDataAsset(DaggerPath, "AssetsTest::WeaponStats", Fenrir::AssetOwner::App()).diagnostic->reason ==
        Fenrir::LoadFailure::NotADataAsset);
}

TEST_CASE("A tree-backed asset's tree is upgraded and checked against its description", "[assets][data][tree]")
{
    TreeApp test;

    INFO("a version 1 file is upgraded, and a former field name read as the current one");
    const Fenrir::DataLoadResult old = test.store.LoadDataAsset(OldDaggerPath, DaggerName, Fenrir::AssetOwner::App());
    REQUIRE(old.Status() == Fenrir::LoadStatus::Succeeded);
    const Ser::Value upgraded = TreeOf(test, old.handle);
    REQUIRE(MemberOf(upgraded, "runes")->AsArray().elements.front().value.AsString() == "ember");
    REQUIRE(MemberOf(upgraded, "reach") != nullptr);
    REQUIRE(MemberOf(upgraded, "length") == nullptr);

    INFO("a replace with an unknown key, a value of the wrong kind and a missing target keeps the rest");
    Ser::Value tree = upgraded;
    Set(tree, "colour", Ser::Value("red"));
    Set(tree, "reach", Ser::Value("far"));
    Set(tree, "runes", Ser::Value(Ser::Array{}));
    tree.AsObject().Find("runes")->AsArray().Add(Ser::Value(3));
    Set(tree, "companion", Ser::Value("data/nowhere.asset.json5"));
    REQUIRE(test.store.ReplaceDataAsset(old.handle, Encoded(tree)));
    const Ser::Value checked = TreeOf(test, old.handle);
    REQUIRE(MemberOf(checked, "colour") == nullptr);
    REQUIRE(MemberOf(checked, "reach") == nullptr);
    REQUIRE(MemberOf(checked, "runes") == nullptr);
    REQUIRE(MemberOf(checked, "companion")->Kind() == Ser::ValueKind::Null);
    REQUIRE(MemberOf(checked, "displayName")->AsString() == "Old kris");
    REQUIRE(CountContaining(*test.warnings, "`colour` names no field, so it is dropped") == 1);
    REQUIRE(CountContaining(*test.errors, "`reach` expected a number (f32), found \"far\"") == 1);
    REQUIRE(CountContaining(*test.errors, "`runes[0]` expected a string") == 1);
    REQUIRE(CountContaining(*test.errors, "`companion` names \"data/nowhere.asset.json5\"") == 1);
}

TEST_CASE("A type live records hold is not unregistered", "[assets][data][tree]")
{
    TreeApp test;
    const Fenrir::DataLoadResult loaded = test.store.LoadDataAsset(DaggerPath, DaggerName, Fenrir::AssetOwner::App());
    REQUIRE(loaded.Ok());

    INFO("while the dagger is loaded, its type, and its base, are held");
    REQUIRE(test.store.TypeHeld(DaggerName).has_value());
    REQUIRE(test.store.TypeHeld("AssetsTest::WeaponData").has_value());
    REQUIRE_FALSE(test.store.UnregisterType(test.registry, DaggerName));
    REQUIRE(CountContaining(*test.errors, "Could not unregister the type `AssetsTest::Dagger`") == 1);
    REQUIRE(test.registry.Find(DaggerName) != nullptr);

    INFO("once it is unloaded, nothing holds the type, and it goes");
    REQUIRE(test.store.Unload(loaded.handle.As<WeaponData>()));
    REQUIRE_FALSE(test.store.TypeHeld(DaggerName).has_value());
    REQUIRE(test.store.UnregisterType(test.registry, DaggerName));
    REQUIRE(test.registry.Find(DaggerName) == nullptr);
}

TEST_CASE("A tree-backed type with no C++ base is read through its tree alone", "[assets][data][tree]")
{
    TreeApp test;
    Ser::Value tree{Ser::Object{}};
    tree.AsObject().Add("glow", Ser::Value(0.5f));
    const Fenrir::DataAssetHandle charm = test.store.AddDataAsset(CharmName, Encoded(tree), Fenrir::AssetOwner::App());
    REQUIRE_FALSE(charm.IsEmpty());
    REQUIRE(Ser::Equals(TreeOf(test, charm), tree));

    INFO("TryGet of a C++ type gives null, logged once for the handle");
    REQUIRE(test.store.TryGet(charm.As<WeaponData>()) == nullptr);
    REQUIRE(test.store.TryGet(charm.As<WeaponData>()) == nullptr);
    REQUIRE(CountContaining(*test.errors, "tree-backed with no C++ base") == 1);
}

TEST_CASE("Moving a file rewrites a tree-backed asset's references to it", "[assets][data][tree][move]")
{
    TempRoot root;
    CopyDagger(root);
    TreeApp test(Fenrir::AssetsConfig{root.Path()});
    constexpr std::string_view BladePath = "data/blade.asset.json5";

    const Fenrir::MoveResult moved = Fenrir::Assets::Move(root.Path(), SwordPath, BladePath, test.registry);
    REQUIRE(moved.status == Fenrir::MoveStatus::Succeeded);
    INFO("the dagger's own `companion` field, which only its description names, now names the sword's new path");
    REQUIRE(moved.references == 1);
    REQUIRE(ReadFile(root.Path() / DaggerPath).find("companion: \"data/blade.asset.json5\"") != std::string::npos);
    REQUIRE(moved.unscanned.empty());
}
