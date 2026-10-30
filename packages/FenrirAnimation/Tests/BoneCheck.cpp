// The headless bone check (spec, seam 9): the checked-in rig.gltf, imported, spawned and played to known times, gives
// bone world transforms that match values worked out by hand from the rig and its "Swing" clip.
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "AnimationTest.hpp"
#include "FenrirAssets/Model.hpp"

using AnimationTest::AnimationApp;
using AnimationTest::Near;
using Fenrir::Entity;
using Fenrir::Skeletons;

TEST_CASE("the rig played to known times puts its bones where the clip's analytic values say", "[animation][bones]")
{
    AnimationApp test;
    const auto model = test.store.Load<Fenrir::Model>(
        (std::filesystem::path(FENRIR_ANIMATION_FIXTURES) / "rig.gltf").string(), test.level);
    REQUIRE(model.Ok());
    const Fenrir::Model& data = *test.store.TryGet(model.handle);
    const Fenrir::SpawnedNodes spawned = test.skeletons.SpawnModel(test.level, model.handle);
    // The whole model five units along +x, so world reads include the skeleton entity's place.
    spawned.root.GetComponent<Fenrir::Transform>().pos = {5.0f, 0.0f, 0.0f};
    Entity owner = spawned.nodes[static_cast<std::size_t>(data.skeletonOwners[0])];
    Fenrir::Animator animator;
    animator.clip = data.clips[0];
    animator.loop = Fenrir::ClipLoop::Once;
    owner.AddComponent<Fenrir::Animator>(animator);
    const std::int32_t shoulder = Skeletons::FindBone(test.level, owner, "Shoulder");
    const std::int32_t elbow = Skeletons::FindBone(test.level, owner, "Elbow");
    REQUIRE(shoulder != Fenrir::NoBone);
    REQUIRE(elbow != Fenrir::NoBone);

    // At time t "Swing" turns Shoulder by 90t degrees about +Z and sets Elbow's offset to (0, 1 + t, 0). Shoulder sits
    // at (0, 1, 0), so the Elbow is at (0, 1, 0) + Rz(90t)(0, 1 + t, 0) = (-(1 + t) sin 90t, 1 + (1 + t) cos 90t, 0),
    // turned by 90t; in the world, five units along +x.
    struct Expected
    {
        double step;
        float degrees;
        Fenrir::Math::Vec3 elbow;
    };
    const Expected times[] = {
        {0.25, 22.5f, {5.0f - 0.4783543f, 2.1548494f, 0.0f}}, // t = 0.25: 1.25 sin 22.5 and 1 + 1.25 cos 22.5
        {0.25, 45.0f, {5.0f - 1.0606602f, 2.0606602f, 0.0f}}, // t = 0.5: 1.5 sin 45 and 1 + 1.5 cos 45
        {0.5, 90.0f, {3.0f, 1.0f, 0.0f}},                     // t = 1: two units along -x from the shoulder
    };
    for (const Expected& expected : times)
    {
        test.RunFrame(expected.step);
        const Fenrir::Math::Mat4 shoulderWorld = *Skeletons::GetBoneWorldMatrix(test.level, owner, shoulder);
        const Fenrir::Math::Mat4 elbowWorld = *Skeletons::GetBoneWorldMatrix(test.level, owner, elbow);
        INFO("at " << expected.degrees << " degrees");
        REQUIRE(Near(Fenrir::Math::Vec3(shoulderWorld[3]), {5.0f, 1.0f, 0.0f}));
        REQUIRE(Near(Fenrir::Math::Vec3(elbowWorld[3]), expected.elbow));
        // The Elbow inherits the Shoulder's turn: its local +y points along (-sin, cos, 0) of the angle.
        const float radians = Fenrir::Math::DegToRad(expected.degrees);
        REQUIRE(Near(Fenrir::Math::Vec3(elbowWorld[1]), {-std::sin(radians), std::cos(radians), 0.0f}));
    }
}
