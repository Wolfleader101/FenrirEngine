// Same inputs, same ragdoll (the spec's user story 45): a humanoid walking through a row of boxes for 300 ticks, or
// walking into them and falling onto them, or stumbling into them powered, gives the same bodies, boxes and bone
// overrides, byte for byte, in two runs, on one thread and on several.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirPhysics/Components.hpp"
#include "RagdollTest.hpp"

using Fenrir::Entity;
using Fenrir::Math::Vec3;

namespace
{
    template <typename T>
    void Append(std::vector<std::byte>& bytes, const T& value)
    {
        const auto* begin = reinterpret_cast<const std::byte*>(&value);
        bytes.insert(bytes.end(), begin, begin + sizeof(T));
    }

    // One run: the walk into five boxes, 300 ticks (switched to `mode` after the first 40 unless it is Following),
    // then every body's and box's world transform and velocities, and every body's bone override.
    std::vector<std::byte> Run(bool workerThreads, Fenrir::RagdollMode mode)
    {
        Fenrir::PhysicsConfig config = RagdollTest::StepPosesOnly();
        config.useWorkerThreads = workerThreads;
        RagdollTest::RagdollApp test(config, workerThreads ? 4 : 1);
        test.AddGround();
        const Entity skeleton = test.AddSkeleton(RagdollTest::Humanoid());
        RagdollTest::AddWalk(test, 1.5f, 0.3f);
        const Fenrir::SpawnedRagdoll spawned =
            Fenrir::BuildRagdoll(test.app, test.level, skeleton,
                                 Fenrir::DescribeHumanoid(test.DataOf(skeleton), RagdollTest::HumanoidMap()));
        REQUIRE(spawned.error.empty());
        std::vector<Entity> boxes;
        for (int i = 0; i < 5; ++i)
        {
            Entity box = test.Entities().CreateEntity();
            box.GetComponent<Fenrir::Transform>().pos =
                Vec3(0.8f + 0.5f * static_cast<float>(i), 0.2f, 0.1f * static_cast<float>(i % 2));
            box.AddComponent<Fenrir::RigidBody>(Fenrir::RigidBody{.mass = 2.0f});
            box.AddComponent<Fenrir::Collider>(Fenrir::Collider{Fenrir::BoxShape{Vec3(0.2f)}});
            boxes.push_back(box);
        }
        if (mode != Fenrir::RagdollMode::Following)
        {
            test.Run(40);
            skeleton.GetComponent<Fenrir::Ragdoll>().mode = mode;
            test.Run(260);
        }
        else
        {
            test.Run(300);
        }

        std::vector<std::byte> bytes;
        std::vector<Entity> everything = spawned.bodies;
        everything.insert(everything.end(), boxes.begin(), boxes.end());
        {
            for (const Entity& entity : everything)
            {
                Append(bytes, Fenrir::GetWorldTransform(entity));
                Append(bytes, entity.GetComponent<Fenrir::RigidBody>().linearVelocity);
                Append(bytes, entity.GetComponent<Fenrir::RigidBody>().angularVelocity);
            }
            for (const Entity& body : spawned.bodies)
            {
                const std::int32_t bone = body.GetComponent<Fenrir::RagdollBody>().state.bone;
                const std::optional<Fenrir::Target> entry = Fenrir::Skeletons::GetOverride(test.level, skeleton, bone);
                Append(bytes, entry.has_value());
                if (entry)
                {
                    Append(bytes, entry->trs.pos);
                    Append(bytes, entry->trs.rot);
                }
            }
        }
        return bytes;
    }
} // namespace

TEST_CASE("Two runs of a following ragdoll pushing boxes are bit-identical over 300 ticks", "[ragdoll][determinism]")
{
    for (const bool threads : {false, true})
    {
        const std::vector<std::byte> first = Run(threads, Fenrir::RagdollMode::Following);
        const std::vector<std::byte> second = Run(threads, Fenrir::RagdollMode::Following);
        INFO((threads ? "on worker threads" : "on one thread")
             << ": every body's and box's pose and velocities match byte for byte");
        REQUIRE(first.size() == second.size()); // same entities, so a difference is in the bytes below
        REQUIRE(std::memcmp(first.data(), second.data(), first.size()) == 0);
    }
}

TEST_CASE("Two runs of a ragdoll falling onto boxes are bit-identical over 300 ticks",
          "[ragdoll][determinism][simulate]")
{
    for (const bool threads : {false, true})
    {
        const std::vector<std::byte> first = Run(threads, Fenrir::RagdollMode::Simulating);
        const std::vector<std::byte> second = Run(threads, Fenrir::RagdollMode::Simulating);
        INFO((threads ? "on worker threads" : "on one thread")
             << ": every body's, box's and bone override's values match byte for byte");
        REQUIRE(first.size() == second.size()); // the same entries exist in both runs
        REQUIRE(std::memcmp(first.data(), second.data(), first.size()) == 0);
    }
}

TEST_CASE("Two runs of a powered ragdoll stumbling into boxes are bit-identical over 300 ticks",
          "[ragdoll][determinism][powered]")
{
    for (const bool threads : {false, true})
    {
        const std::vector<std::byte> first = Run(threads, Fenrir::RagdollMode::Powered);
        const std::vector<std::byte> second = Run(threads, Fenrir::RagdollMode::Powered);
        INFO((threads ? "on worker threads" : "on one thread")
             << ": every body's, box's and bone override's values match byte for byte");
        REQUIRE(first.size() == second.size()); // the same entries exist in both runs
        REQUIRE(std::memcmp(first.data(), second.data(), first.size()) == 0);
    }
}
