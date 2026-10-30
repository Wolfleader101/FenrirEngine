// The pose generation and the lazy skinning palette (ADR-0033, ADR-0035).
#include <cstdint>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "SkeletonTest.hpp"

using Fenrir::Entity;
using Fenrir::Skeletons;
using SkeletonTest::Near;
using SkeletonTest::SkeletonApp;
using SkeletonTest::Translation;

TEST_CASE("the pose generation advances when the final pose changes, and not on a frame with no change",
          "[skeleton][generation]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();
    const std::uint64_t built = Skeletons::GetPoseGeneration(test.level, skeleton);
    REQUIRE(built != 0);

    test.RunFrame();
    INFO("a clean instance keeps its generation");
    REQUIRE(Skeletons::GetPoseGeneration(test.level, skeleton) == built);

    INFO("EditBasePose alone does not advance it: the final pose has not changed yet");
    Skeletons::EditBasePose(test.level, skeleton)[0].pos = {1.0f, 0.0f, 0.0f};
    REQUIRE(Skeletons::GetPoseGeneration(test.level, skeleton) == built);

    INFO("finalising the changed pose does");
    test.RunFrame();
    const std::uint64_t posed = Skeletons::GetPoseGeneration(test.level, skeleton);
    REQUIRE(posed > built);

    INFO("EditBasePose marks the pose dirty even when no value changes");
    (void)Skeletons::EditBasePose(test.level, skeleton);
    test.RunFrame();
    REQUIRE(Skeletons::GetPoseGeneration(test.level, skeleton) > posed);
}

TEST_CASE("moving the skeleton entity does not advance the pose generation", "[skeleton][generation]")
{
    SkeletonApp test;
    Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();
    const std::uint64_t before = Skeletons::GetPoseGeneration(test.level, skeleton);

    skeleton.GetComponent<Fenrir::Transform>().pos = {3.0f, 0.0f, 0.0f};
    test.RunFrame();

    INFO("a move affects world reads only");
    REQUIRE(Skeletons::GetPoseGeneration(test.level, skeleton) == before);
    REQUIRE(Near(Translation(*Skeletons::GetBoneWorldMatrix(test.level, skeleton, 2)), {3.0f, 3.0f, 0.0f}));
}

TEST_CASE("a rebuild advances the pose generation", "[skeleton][generation]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, chain);
    test.RunFrame();
    const std::uint64_t before = Skeletons::GetPoseGeneration(test.level, skeleton);

    REQUIRE(test.store.Replace(chain, SkeletonTest::ChainData()));
    test.RunFrame();

    INFO("a new revision rebuilds the instance with a new generation");
    REQUIRE(Skeletons::GetPoseGeneration(test.level, skeleton) > before);
}

TEST_CASE("an instance under a recycled entity never repeats a pose generation", "[skeleton][generation]")
{
    SkeletonApp test;
    const Fenrir::AssetHandle<Fenrir::Skeleton> chain = SkeletonTest::AddChain(test);
    std::set<std::uint64_t> seen;
    for (int round = 0; round < 4; ++round)
    {
        const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, chain);
        REQUIRE(test.skeletons.Ensure(test.level, skeleton) == Fenrir::PoseState::Ready);
        INFO("each new instance, whatever entity id it reuses, starts at a generation never seen");
        REQUIRE(seen.insert(Skeletons::GetPoseGeneration(test.level, skeleton)).second);
        test.level.GetEntityList().DestroyEntity(skeleton);
        test.RunFrame();
    }
}

TEST_CASE("the palette is the model matrices times the inverse binds, built for the current pose",
          "[skeleton][palette]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();

    INFO("at the rest pose, which is the chain's bind pose, every palette matrix is the identity");
    const std::span<const Fenrir::Math::Mat4> rest = Skeletons::GetPalette(test.level, skeleton);
    REQUIRE(rest.size() == 3);
    for (const Fenrir::Math::Mat4& matrix : rest)
    {
        REQUIRE(Near(Translation(matrix), {0.0f, 0.0f, 0.0f}));
        REQUIRE(Near(Fenrir::Math::Vec3(matrix[0]), {1.0f, 0.0f, 0.0f}));
    }

    // Worked by hand: the tip's model matrix is a quarter turn about +Z at (-1, 2, 0), its inverse bind a move by
    // (0, -3, 0). Turned, (0, -3, 0) is (3, 0, 0), so the palette matrix moves the origin to (2, 2, 0).
    Skeletons::EditBasePose(test.level, skeleton)[1].rot = SkeletonTest::QuarterTurnZ();
    test.RunFrame();
    INFO("after a pose change the palette follows it");
    REQUIRE(Near(Translation(Skeletons::GetPalette(test.level, skeleton)[2]), {2.0f, 2.0f, 0.0f}));
}

TEST_CASE("the palette is cached with its pose generation, and reading it changes nothing", "[skeleton][palette]")
{
    SkeletonApp test;
    const Entity skeleton = SkeletonTest::SkeletonEntity(test.level, SkeletonTest::AddChain(test));
    test.RunFrame();
    const std::uint64_t generation = Skeletons::GetPoseGeneration(test.level, skeleton);

    const Fenrir::Math::Mat4* first = Skeletons::GetPalette(test.level, skeleton).data();
    test.RunFrame();
    INFO("a second read for the same generation gives the cached palette");
    REQUIRE(Skeletons::GetPalette(test.level, skeleton).data() == first);
    INFO("reading it does not advance the generation");
    REQUIRE(Skeletons::GetPoseGeneration(test.level, skeleton) == generation);

    INFO("a palette read only after two pose changes gives the latest pose");
    Skeletons::EditBasePose(test.level, skeleton)[0].pos = {1.0f, 1.0f, 0.0f};
    test.RunFrame();
    Skeletons::EditBasePose(test.level, skeleton)[0].pos = {2.0f, 1.0f, 0.0f};
    test.RunFrame();
    REQUIRE(Near(Translation(Skeletons::GetPalette(test.level, skeleton)[0]), {2.0f, 0.0f, 0.0f}));
}
