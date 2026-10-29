#include <cmath>
#include <cstdint>
#include <limits>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::Triangle;
using Fenrir::BoneQuad;
using Fenrir::BoneWeightOne;

namespace
{
    std::uint32_t Sum(const BoneQuad& weights)
    {
        return std::uint32_t{weights[0]} + weights[1] + weights[2] + weights[3];
    }

    // The triangle skinned: vertex 0 to bone 0, vertex 1 split between bones 1 and 2, vertex 2 unweighted.
    Fenrir::MeshData SkinnedTriangle()
    {
        Fenrir::MeshData mesh = Triangle();
        Fenrir::MeshSkin skin;
        skin.joints = {BoneQuad{0, 0, 0, 0}, BoneQuad{1, 2, 0, 0}, BoneQuad{0, 0, 0, 0}};
        skin.weights = {*Fenrir::QuantiseBoneWeights({1.0f, 0.0f, 0.0f, 0.0f}),
                        *Fenrir::QuantiseBoneWeights({0.5f, 0.5f, 0.0f, 0.0f}), BoneQuad{0, 0, 0, 0}};
        mesh.skin = skin;
        return mesh;
    }
} // namespace

TEST_CASE("Quantised bone weights sum to exactly one in unorm16", "[assets][skins]")
{
    INFO("a single weight is all of it, whatever its scale");
    REQUIRE(*Fenrir::QuantiseBoneWeights({3.0f, 0.0f, 0.0f, 0.0f}) == BoneQuad{65535, 0, 0, 0});

    INFO("thirds cannot split evenly, so the remainder goes a unit at a time, ties to the lower slot");
    const BoneQuad thirds = *Fenrir::QuantiseBoneWeights({1.0f, 1.0f, 1.0f, 0.0f});
    REQUIRE(thirds == BoneQuad{21845, 21845, 21845, 0});
    const BoneQuad quarters = *Fenrir::QuantiseBoneWeights({1.0f, 1.0f, 1.0f, 1.0f});
    REQUIRE(Sum(quarters) == BoneWeightOne);
    REQUIRE(quarters == BoneQuad{16384, 16384, 16384, 16383});

    INFO("the largest fractional parts get the remainder");
    // A half and two quarters of 65535 are 32767.5, 16383.75 and 16383.75: the floors are two units short, and the
    // quarters' .75 beat the half's .5.
    REQUIRE(*Fenrir::QuantiseBoneWeights({0.5f, 0.25f, 0.25f, 0.0f}) == BoneQuad{32767, 16384, 16384, 0});
    INFO("equal fractions go to the lower slot");
    REQUIRE(*Fenrir::QuantiseBoneWeights({0.5f, 0.5f, 0.0f, 0.0f}) == BoneQuad{32768, 32767, 0, 0});

    INFO("every mix sums exactly");
    for (float a = 0.0f; a <= 1.0f; a += 0.137f)
    {
        for (float b = 0.0f; b <= 1.0f; b += 0.211f)
        {
            const BoneQuad mixed = *Fenrir::QuantiseBoneWeights({a, b, 0.3f * a, 1e-6f});
            REQUIRE(Sum(mixed) == BoneWeightOne);
        }
    }

    INFO("all-zero weights stay zero");
    REQUIRE(*Fenrir::QuantiseBoneWeights({0.0f, 0.0f, 0.0f, 0.0f}) == BoneQuad{0, 0, 0, 0});

    INFO("negative and non-finite weights are refused");
    REQUIRE_FALSE(Fenrir::QuantiseBoneWeights({1.0f, -0.1f, 0.0f, 0.0f}).has_value());
    REQUIRE_FALSE(Fenrir::QuantiseBoneWeights({std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f}).has_value());
    REQUIRE_FALSE(Fenrir::QuantiseBoneWeights({std::numeric_limits<float>::infinity(), 1.0f, 0.0f, 0.0f}).has_value());
}

TEST_CASE("A skinned mesh gets its bone count and per-bone boxes when added", "[assets][skins]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Mesh>(SkinnedTriangle(), Fenrir::AssetOwner::App());
    REQUIRE(test.store.IsValid(handle));
    const Fenrir::MeshSkin& skin = *test.store.TryGet(handle)->skin;

    INFO("the bone count is the highest index named, plus one");
    REQUIRE(skin.boneCount == 3);
    INFO("an unchecked mesh keeps its zero signature");
    REQUIRE(skin.expectedSignature == 0);

    INFO("each bone's box holds the vertices weighted to it, in bind space");
    REQUIRE(skin.boneBounds.size() == 3);
    REQUIRE(skin.boneBounds[0]->min == Fenrir::Math::Vec3(-1.0f, 0.0f, 0.0f));
    REQUIRE(skin.boneBounds[0]->max == Fenrir::Math::Vec3(-1.0f, 0.0f, 0.0f));
    REQUIRE(skin.boneBounds[1]->min == Fenrir::Math::Vec3(2.0f, 0.0f, 0.0f));
    REQUIRE(skin.boneBounds[2]->max == Fenrir::Math::Vec3(2.0f, 0.0f, 0.0f));
    INFO("the unweighted vertex has its own box, and is warned about");
    REQUIRE(skin.unweightedBounds->min == Fenrir::Math::Vec3(0.0f, 3.0f, 0.0f));
    REQUIRE(test.warnings->size() == 1);
}

TEST_CASE("Replacing a skinned mesh rebuilds its boxes", "[assets][skins]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Mesh>(SkinnedTriangle(), Fenrir::AssetOwner::App());

    Fenrir::MeshData moved = SkinnedTriangle();
    moved.positions[0] = {-5.0f, 1.0f, 0.0f};
    moved.skin->weights[2] = BoneQuad{BoneWeightOne, 0, 0, 0};
    // Stale boxes in the data handed over are overwritten.
    moved.skin->boneBounds.assign(7, Fenrir::Math::AABB(Fenrir::Math::Vec3(9.0f), Fenrir::Math::Vec3(9.0f)));
    REQUIRE(test.store.Replace(handle, moved));

    const Fenrir::MeshSkin& skin = *test.store.TryGet(handle)->skin;
    REQUIRE(skin.boneBounds.size() == 3);
    REQUIRE(skin.boneBounds[0]->min == Fenrir::Math::Vec3(-5.0f, 1.0f, 0.0f));
    REQUIRE(skin.boneBounds[0]->max == Fenrir::Math::Vec3(0.0f, 3.0f, 0.0f));
    REQUIRE_FALSE(skin.unweightedBounds.has_value());
    REQUIRE_FALSE(skin.boneBounds[2]->min == Fenrir::Math::Vec3(9.0f));
}

TEST_CASE("A bone no vertex is weighted to has no box", "[assets][skins]")
{
    AssetsApp test;
    Fenrir::MeshData mesh = SkinnedTriangle();
    mesh.skin->boneCount = 5;
    const auto handle = test.store.Add<Fenrir::Mesh>(mesh, Fenrir::AssetOwner::App());
    const Fenrir::MeshSkin& skin = *test.store.TryGet(handle)->skin;

    INFO("a bone count that was set is kept");
    REQUIRE(skin.boneCount == 5);
    REQUIRE(skin.boneBounds.size() == 5);
    REQUIRE_FALSE(skin.boneBounds[3].has_value());
    REQUIRE_FALSE(skin.boneBounds[4].has_value());
}

TEST_CASE("Adding a mesh refuses a malformed skin", "[assets][skins]")
{
    const auto refused = [](Fenrir::MeshData mesh) {
        AssetsApp test;
        const auto handle = test.store.Add<Fenrir::Mesh>(std::move(mesh), Fenrir::AssetOwner::App());
        return handle.IsEmpty() && !test.errors->empty();
    };

    SECTION("a bone index at or past a bone count that was set")
    {
        Fenrir::MeshData mesh = SkinnedTriangle();
        mesh.skin->boneCount = 2;
        REQUIRE(refused(mesh));
    }
    SECTION("weights that do not sum to one")
    {
        Fenrir::MeshData mesh = SkinnedTriangle();
        mesh.skin->weights[1] = BoneQuad{30000, 30000, 0, 0};
        REQUIRE(refused(mesh));
    }
    SECTION("fewer joint quads than positions")
    {
        Fenrir::MeshData mesh = SkinnedTriangle();
        mesh.skin->joints.pop_back();
        REQUIRE(refused(mesh));
    }
    SECTION("fewer weight quads than positions")
    {
        Fenrir::MeshData mesh = SkinnedTriangle();
        mesh.skin->weights.pop_back();
        REQUIRE(refused(mesh));
    }
}

TEST_CASE("Replacing a mesh with a malformed skin leaves it unchanged", "[assets][skins]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Mesh>(SkinnedTriangle(), Fenrir::AssetOwner::App());
    Fenrir::MeshData broken = SkinnedTriangle();
    broken.skin->weights[0] = BoneQuad{1, 0, 0, 0};
    REQUIRE_FALSE(test.store.Replace(handle, broken));
    REQUIRE(test.store.Revision(handle) == 1);
    REQUIRE(test.store.TryGet(handle)->skin->weights[0] == BoneQuad{BoneWeightOne, 0, 0, 0});
}

TEST_CASE("A mesh without a skin is unchanged by the skin rules", "[assets][skins]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());
    REQUIRE_FALSE(test.store.TryGet(handle)->skin.has_value());
    REQUIRE(test.warnings->empty());
}
