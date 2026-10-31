#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "FenrirAssets/SkeletonData.hpp"

using AssetsTest::AssetsApp;

namespace
{
    Fenrir::SkeletonBone Bone(std::string name, std::int32_t parent)
    {
        Fenrir::SkeletonBone bone;
        bone.sourceName = std::move(name);
        bone.parent = parent;
        return bone;
    }

    // root -> spine -> (head, arm -> hand), and a second root, prop.
    Fenrir::SkeletonData Rig()
    {
        Fenrir::SkeletonData skeleton;
        skeleton.bones = {Bone("root", -1), Bone("spine", 0), Bone("head", 1),
                          Bone("arm", 1),   Bone("hand", 3),  Bone("prop", -1)};
        return skeleton;
    }

    const Fenrir::SkeletonData& Added(AssetsApp& test, Fenrir::SkeletonData skeleton)
    {
        const auto handle = test.store.Add<Fenrir::Skeleton>(std::move(skeleton), Fenrir::AssetOwner::App());
        REQUIRE(test.store.IsValid(handle));
        return *test.store.TryGet(handle);
    }

    bool Refused(AssetsApp& test, Fenrir::SkeletonData skeleton)
    {
        const auto handle = test.store.Add<Fenrir::Skeleton>(std::move(skeleton), Fenrir::AssetOwner::App());
        return handle.IsEmpty() && !test.errors->empty();
    }
} // namespace

TEST_CASE("A skeleton keeps its bones, parents first, and answers parent and name queries", "[assets][skeletons]")
{
    AssetsApp test;
    const Fenrir::SkeletonData& skeleton = Added(test, Rig());

    REQUIRE(skeleton.BoneCount() == 6);
    INFO("parents are earlier bones, roots have none, and several roots are allowed");
    REQUIRE(skeleton.Parent(0) == Fenrir::NoBone);
    REQUIRE(skeleton.Parent(4) == 3);
    REQUIRE(skeleton.Parent(5) == Fenrir::NoBone);
    REQUIRE(skeleton.Parent(6) == Fenrir::NoBone);
    REQUIRE(skeleton.Parent(-1) == Fenrir::NoBone);

    INFO("unique source names are the engine names, and bones are found by either");
    REQUIRE(skeleton.EngineName(4) == "hand");
    REQUIRE(skeleton.FindBone("hand") == 4);
    REQUIRE(skeleton.FindBoneBySourceName("hand") == 4);
    REQUIRE(skeleton.FindBone("tail") == Fenrir::NoBone);
    REQUIRE(skeleton.EngineName(9).empty());
    REQUIRE(test.warnings->empty());
}

TEST_CASE("A skeleton answers descendant and subtree queries", "[assets][skeletons]")
{
    AssetsApp test;
    const Fenrir::SkeletonData& skeleton = Added(test, Rig());

    REQUIRE(skeleton.IsDescendant(4, 0));
    REQUIRE(skeleton.IsDescendant(4, 3));
    REQUIRE(skeleton.IsDescendant(2, 1));
    INFO("a bone is not its own descendant, nor a sibling's or another root's");
    REQUIRE_FALSE(skeleton.IsDescendant(3, 3));
    REQUIRE_FALSE(skeleton.IsDescendant(4, 2));
    REQUIRE_FALSE(skeleton.IsDescendant(5, 0));
    REQUIRE_FALSE(skeleton.IsDescendant(0, 4));
    REQUIRE_FALSE(skeleton.IsDescendant(10, 0));

    INFO("a subtree is the bone and everything below it, in index order");
    REQUIRE(skeleton.Subtree(1) == std::vector<std::int32_t>{1, 2, 3, 4});
    REQUIRE(skeleton.Subtree(3) == std::vector<std::int32_t>{3, 4});
    REQUIRE(skeleton.Subtree(5) == std::vector<std::int32_t>{5});
    REQUIRE(skeleton.Subtree(6).empty());
}

TEST_CASE("Repeated bone names get suffixed engine names, each with a warning", "[assets][skeletons]")
{
    AssetsApp test;
    Fenrir::SkeletonData rig;
    rig.bones = {Bone("Bone", -1), Bone("Bone", 0), Bone("Bone#2", 1), Bone("Bone", 2), Bone("", 0), Bone("", 0)};
    const Fenrir::SkeletonData& skeleton = Added(test, std::move(rig));

    INFO("the first keeps its name, and each repeat takes the next free suffix");
    REQUIRE(skeleton.EngineName(0) == "Bone");
    REQUIRE(skeleton.EngineName(1) == "Bone#2");
    REQUIRE(skeleton.EngineName(2) == "Bone#2#2");
    REQUIRE(skeleton.EngineName(3) == "Bone#3");
    REQUIRE(skeleton.EngineName(4).empty());
    REQUIRE(skeleton.EngineName(5) == "#2");

    INFO("engine names are exact; a source name finds its first bone");
    REQUIRE(skeleton.FindBone("Bone#3") == 3);
    REQUIRE(skeleton.FindBoneBySourceName("Bone") == 0);
    REQUIRE(skeleton.FindBoneBySourceName("Bone#2") == 2);

    INFO("one warning per renamed bone");
    REQUIRE(test.warnings->size() == 4);
}

TEST_CASE("A name from elsewhere matches a bone only when its source name is unique", "[assets][skeletons]")
{
    AssetsApp test;
    Fenrir::SkeletonData repeated;
    repeated.bones = {Bone("Bone", -1), Bone("Bone", 0), Bone("Bone#2", 1), Bone("hand", 2)};
    const Fenrir::SkeletonData& skeleton = Added(test, std::move(repeated));
    using Outcome = Fenrir::BoneNameMatch::Outcome;

    INFO("a unique source name matches its bone, under its engine name");
    REQUIRE(skeleton.MatchBoneName("hand").outcome == Outcome::Matched);
    REQUIRE(skeleton.MatchBoneName("hand").bone == 3);
    REQUIRE(skeleton.MatchBoneName("Bone#2#2").outcome == Outcome::Matched);
    REQUIRE(skeleton.MatchBoneName("Bone#2#2").bone == 2);

    INFO("every bone of a repeated source name is ambiguous, suffixed or not, and gives no bone");
    REQUIRE(skeleton.MatchBoneName("Bone").outcome == Outcome::Ambiguous);
    REQUIRE(skeleton.MatchBoneName("Bone#2").outcome == Outcome::Ambiguous);
    REQUIRE(skeleton.MatchBoneName("Bone#2").bone == Fenrir::NoBone);

    INFO("a name no engine name has matches nothing");
    REQUIRE(skeleton.MatchBoneName("tail").outcome == Outcome::NoMatch);
    REQUIRE(skeleton.MatchBoneName("tail").bone == Fenrir::NoBone);

    INFO("FindBone stays exact");
    REQUIRE(skeleton.FindBone("Bone") == 0);
    REQUIRE(skeleton.FindBone("Bone#2") == 1);
}

TEST_CASE("SubtreeOf walks any parents-first list of parents, as Subtree does", "[assets][skeletons]")
{
    // root -> spine -> (head, arm -> hand), and a second root, prop.
    const std::vector<std::int32_t> parents{-1, 0, 1, 1, 3, -1};
    const auto parentOf = [&parents](std::size_t i) { return parents[i]; };

    REQUIRE(Fenrir::SubtreeOf(parents.size(), 1, parentOf) == std::vector<std::int32_t>{1, 2, 3, 4});
    REQUIRE(Fenrir::SubtreeOf(parents.size(), 3, parentOf) == std::vector<std::int32_t>{3, 4});
    REQUIRE(Fenrir::SubtreeOf(parents.size(), 5, parentOf) == std::vector<std::int32_t>{5});
    INFO("out of range either way is empty");
    REQUIRE(Fenrir::SubtreeOf(parents.size(), 6, parentOf).empty());
    REQUIRE(Fenrir::SubtreeOf(parents.size(), Fenrir::NoBone, parentOf).empty());
}

TEST_CASE("An over-long bone name is cut at a UTF-8 boundary and suffixed", "[assets][skeletons]")
{
    AssetsApp test;
    // 300 bytes of two-byte characters: a cut at an odd byte count would split one.
    std::string longName;
    for (int i = 0; i < 150; ++i)
    {
        longName += "\xC3\xA9";
    }
    Fenrir::SkeletonData rig;
    rig.bones = {Bone(longName, -1)};
    const Fenrir::SkeletonData& skeleton = Added(test, std::move(rig));

    const std::string_view name = skeleton.EngineName(0);
    INFO("it fits a bone name, ends in its suffix, and splits no character");
    REQUIRE(name.size() <= Fenrir::MaxBoneNameBytes);
    REQUIRE(name.ends_with("#2"));
    const std::string_view kept = name.substr(0, name.size() - 2);
    REQUIRE(kept.size() % 2 == 0);
    REQUIRE(longName.starts_with(kept));
    REQUIRE(skeleton.FindBone(name) == 0);
    REQUIRE(skeleton.FindBoneBySourceName(longName) == 0);
    REQUIRE(test.warnings->size() == 1);
}

TEST_CASE("The bind signature is deterministic and changes with what it covers", "[assets][skeletons]")
{
    AssetsApp test;
    const std::uint64_t signature = Added(test, Rig()).BindSignature();

    INFO("never the unchecked value, and the same for the same bones");
    REQUIRE(signature != 0);
    REQUIRE(Added(test, Rig()).BindSignature() == signature);

    INFO("a different name, parent or inverse bind changes it");
    Fenrir::SkeletonData renamed = Rig();
    renamed.bones[4].sourceName = "palm";
    REQUIRE(Added(test, std::move(renamed)).BindSignature() != signature);
    Fenrir::SkeletonData reparented = Rig();
    reparented.bones[4].parent = 1;
    REQUIRE(Added(test, std::move(reparented)).BindSignature() != signature);
    Fenrir::SkeletonData rebound = Rig();
    rebound.bones[2].inverseBind[3][1] = -1.5f;
    REQUIRE(Added(test, std::move(rebound)).BindSignature() != signature);

    INFO("the rest pose and source names behind unchanged engine names do not");
    Fenrir::SkeletonData moved = Rig();
    moved.bones[2].rest.pos = {0.0f, 2.0f, 0.0f};
    REQUIRE(Added(test, std::move(moved)).BindSignature() == signature);

    INFO("-0 and +0 in an inverse bind hash alike");
    Fenrir::SkeletonData negativeZero = Rig();
    negativeZero.bones[0].inverseBind[3][0] = -0.0f;
    REQUIRE(Added(test, std::move(negativeZero)).BindSignature() == signature);
}

TEST_CASE("The bind signature follows ADR-0035's encoding", "[assets][skeletons]")
{
    // One root bone named "a" with the identity inverse bind, hashed by hand: version 1, count 1, parent -1, the name's
    // length and byte, then sixteen floats.
    std::uint64_t hash = 14695981039346656037ull;
    const auto byte = [&](std::uint8_t b) {
        hash ^= b;
        hash *= 1099511628211ull;
    };
    const auto u32 = [&](std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8)
        {
            byte(static_cast<std::uint8_t>(v >> shift));
        }
    };
    u32(1);
    u32(1);
    u32(0xFFFFFFFFu);
    u32(1);
    byte('a');
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            u32(column == row ? 0x3F800000u : 0u);
        }
    }

    Fenrir::SkeletonBone bone;
    bone.parent = -1;
    REQUIRE(Fenrir::ComputeBindSignature({bone}, {"a"}) == hash);
}

TEST_CASE("Adding a skeleton refuses bones the importer would refuse", "[assets][skeletons]")
{
    SECTION("no bones")
    {
        AssetsApp test;
        REQUIRE(Refused(test, Fenrir::SkeletonData{}));
    }
    SECTION("a parent after its child")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton;
        skeleton.bones = {Bone("a", 1), Bone("b", -1)};
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("a bone its own parent, the smallest cycle")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton;
        skeleton.bones = {Bone("a", 0)};
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("a parent below -1")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton;
        skeleton.bones = {Bone("a", -2)};
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("a rest transform that is not finite")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton = Rig();
        skeleton.bones[2].rest.pos.y = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("a zero rest scale")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton = Rig();
        skeleton.bones[3].rest.scale = {1.0f, 0.0f, 1.0f};
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("a zero rest rotation")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton = Rig();
        skeleton.bones[1].rest.rot = Fenrir::Math::Quat(0.0f, 0.0f, 0.0f, 0.0f);
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("an inverse bind that is not finite")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton = Rig();
        skeleton.bones[1].inverseBind[0][0] = std::numeric_limits<float>::infinity();
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("an inverse bind that cannot be inverted")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton = Rig();
        skeleton.bones[1].inverseBind[2] = Fenrir::Math::Vec4(0.0f);
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("one source node used as two bones")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton = Rig();
        skeleton.bones[1].sourceId = 7;
        skeleton.bones[4].sourceId = 7;
        REQUIRE(Refused(test, std::move(skeleton)));
    }
    SECTION("one model node used as two bones")
    {
        AssetsApp test;
        Fenrir::SkeletonData skeleton = Rig();
        skeleton.bones[1].modelNode = 3;
        skeleton.bones[2].modelNode = 3;
        REQUIRE(Refused(test, std::move(skeleton)));
    }
}

TEST_CASE("A skeleton's rest rotations are kept unit length", "[assets][skeletons]")
{
    AssetsApp test;
    Fenrir::SkeletonData rig = Rig();
    rig.bones[1].rest.rot = Fenrir::Math::Quat(2.0f, 0.0f, 0.0f, 0.0f);
    const Fenrir::SkeletonData& skeleton = Added(test, std::move(rig));
    REQUIRE(std::abs(skeleton.bones[1].rest.rot.w - 1.0f) < 1e-6f);
}

TEST_CASE("Replacing a skeleton rederives its names and signature and bumps its revision", "[assets][skeletons]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Skeleton>(Rig(), Fenrir::AssetOwner::App());
    const std::uint64_t before = test.store.TryGet(handle)->BindSignature();

    Fenrir::SkeletonData renamed = Rig();
    renamed.bones[2].sourceName = "skull";
    REQUIRE(test.store.Replace(handle, std::move(renamed)));
    REQUIRE(test.store.Revision(handle) == 2);
    REQUIRE(test.store.TryGet(handle)->FindBone("skull") == 2);
    REQUIRE(test.store.TryGet(handle)->FindBone("head") == Fenrir::NoBone);
    REQUIRE(test.store.TryGet(handle)->BindSignature() != before);
}
