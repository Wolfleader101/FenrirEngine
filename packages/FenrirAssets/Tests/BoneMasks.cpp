#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/BoneMaskData.hpp"

using AssetsTest::AssetsApp;

TEST_CASE("A bone mask starts empty and keeps its entries in order", "[assets][masks]")
{
    AssetsApp test;
    const auto mask = test.store.CreateBoneMask(Fenrir::AssetOwner::App());
    REQUIRE(test.store.IsValid(mask));
    REQUIRE(test.store.TryGet(mask)->entries.empty());
    REQUIRE(test.store.Revision(mask) == 1);

    REQUIRE(test.store.IncludeInBoneMask(mask, "spine", 1.0f, Fenrir::MaskScope::Subtree));
    REQUIRE(test.store.IncludeInBoneMask(mask, "head", 0.25f));
    INFO("each edit bumps the revision");
    REQUIRE(test.store.Revision(mask) == 3);

    const Fenrir::BoneMaskData& data = *test.store.TryGet(mask);
    REQUIRE(data.entries.size() == 2);
    REQUIRE(data.entries[0].boneName == "spine");
    REQUIRE(data.entries[0].scope == Fenrir::MaskScope::Subtree);
    REQUIRE(data.entries[1].boneName == "head");
    REQUIRE(data.entries[1].weight == 0.25f);
    REQUIRE(data.entries[1].scope == Fenrir::MaskScope::Bone);

    REQUIRE(test.store.ClearBoneMask(mask));
    REQUIRE(test.store.TryGet(mask)->entries.empty());
    REQUIRE(test.store.Revision(mask) == 4);
}

TEST_CASE("Bone mask edits refuse a non-finite weight and a stale handle", "[assets][masks]")
{
    AssetsApp test;
    const auto mask = test.store.CreateBoneMask(Fenrir::AssetOwner::App());
    REQUIRE_FALSE(test.store.IncludeInBoneMask(mask, "spine", std::numeric_limits<float>::quiet_NaN()));
    REQUIRE(test.store.Revision(mask) == 1);
    REQUIRE(test.errors->size() == 1);

    REQUIRE(test.store.Unload(mask));
    REQUIRE_FALSE(test.store.IncludeInBoneMask(mask, "spine", 1.0f));
    REQUIRE_FALSE(test.store.ClearBoneMask(mask));
    REQUIRE(test.errors->size() == 3);
}

TEST_CASE("A bone mask added whole is checked as the edits check", "[assets][masks]")
{
    AssetsApp test;
    Fenrir::BoneMaskData data;
    data.entries = {{"arm", std::numeric_limits<float>::infinity(), Fenrir::MaskScope::Bone}};
    REQUIRE(test.store.Add<Fenrir::BoneMask>(data, Fenrir::AssetOwner::App()).IsEmpty());

    data.entries[0].weight = 0.5f;
    REQUIRE(test.store.IsValid(test.store.Add<Fenrir::BoneMask>(data, Fenrir::AssetOwner::App())));
}

TEST_CASE("A bone mask owned by a scene ends with it", "[assets][masks]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const auto mask = test.store.CreateBoneMask(level);
    REQUIRE(test.store.IsValid(mask));
    test.app.DestroyScene("Level");
    REQUIRE_FALSE(test.store.IsValid(mask));
}
