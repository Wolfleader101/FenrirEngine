// Stepping on the App's worker threads through the frame seam: the same results as stepping on one thread, whatever
// the number of workers, and no deadlock however busy the workers are.

#include <atomic>
#include <cstddef>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "PhysicsTestApp.hpp"

using namespace Fenrir;
using namespace PhysicsTest;

namespace
{
    struct BodyOutcome
    {
        Transform pose;
        RigidBody body;
    };

    // A pile of boxes and balls, enough that the backend splits its work into many parallel jobs.
    std::vector<Entity> AddPile(EntityList& entities)
    {
        std::vector<Entity> bodies;
        constexpr int side = 8;
        constexpr int layers = 4;
        for (int layer = 0; layer < layers; ++layer)
        {
            for (int x = 0; x < side; ++x)
            {
                for (int z = 0; z < side; ++z)
                {
                    const Math::Vec3 position(1.1f * static_cast<float>(x - side / 2) +
                                                  0.05f * static_cast<float>(layer),
                                              1.0f + 1.2f * static_cast<float>(layer),
                                              1.1f * static_cast<float>(z - side / 2) - 0.03f * static_cast<float>(x));
                    const bool ball = (x + z + layer) % 3 == 0;
                    Entity body = ball ? AddBody(entities, position, SphereShape{0.45f}) : AddBody(entities, position);
                    body.GetComponent<Transform>().rot = Math::AngleAxis(
                        0.1f * static_cast<float>(x + layer), Math::Normalized(Math::Vec3(1.0f, 0.5f, 0.2f)));
                    bodies.push_back(body);
                }
            }
        }
        return bodies;
    }

    std::vector<BodyOutcome> Outcome(std::vector<Entity>& bodies)
    {
        std::vector<BodyOutcome> outcome;
        for (Entity& body : bodies)
        {
            outcome.push_back(BodyOutcome{body.GetComponent<Transform>(), body.GetComponent<RigidBody>()});
        }
        return outcome;
    }

    // The pile dropped onto the ground and left to tumble for two seconds.
    std::vector<BodyOutcome> Tumble(const PhysicsConfig& config, std::size_t workerThreads)
    {
        PhysicsApp test(config, workerThreads);
        AddGround(test.Entities());
        std::vector<Entity> bodies = AddPile(test.Entities());
        test.Run(2.0);
        return Outcome(bodies);
    }

    template <typename T>
    bool BitIdentical(const T& a, const T& b)
    {
        return std::memcmp(&a, &b, sizeof(T)) == 0;
    }

    bool BitIdentical(const std::vector<BodyOutcome>& a, const std::vector<BodyOutcome>& b)
    {
        if (a.size() != b.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            if (!BitIdentical(a[i].pose.pos, b[i].pose.pos) || !BitIdentical(a[i].pose.rot, b[i].pose.rot) ||
                !BitIdentical(a[i].body.linearVelocity, b[i].body.linearVelocity) ||
                !BitIdentical(a[i].body.angularVelocity, b[i].body.angularVelocity))
            {
                return false;
            }
        }
        return true;
    }

    std::size_t DefaultWorkers()
    {
        return std::thread::hardware_concurrency();
    }
} // namespace

TEST_CASE("Stepping on the worker threads gives bit-identical results to stepping on one thread",
          "[physics][threading]")
{
    const std::vector<BodyOutcome> single = Tumble(PhysicsConfig{.useWorkerThreads = false}, DefaultWorkers());

    INFO("the pile moved, so the comparison is not of bodies that never stepped");
    REQUIRE(single.front().pose.pos.y < 1.0f);

    for (const std::size_t workers : {std::size_t{1}, std::size_t{2}, DefaultWorkers()})
    {
        INFO("with " << workers << " worker threads, every body ends at the same pose and velocities, bit for bit");
        REQUIRE(BitIdentical(single, Tumble(PhysicsConfig{.useWorkerThreads = true}, workers)));
    }
}

TEST_CASE("Stepping on the worker threads does not deadlock while parallel systems share them", "[physics][threading]")
{
    for (const std::size_t workers : {std::size_t{1}, std::size_t{2}, DefaultWorkers()})
    {
        PhysicsApp test(PhysicsConfig{.useWorkerThreads = true}, workers);
        std::atomic<int> runs = 0;
        // Parallel systems before, beside and after physics's PostTick system, in every frame.
        for (const SchedulePriority phase :
             {SchedulePriority::Tick, SchedulePriority::PostTick, SchedulePriority::Update})
        {
            for (int i = 0; i < 4; ++i)
            {
                test.app.AddSystem(phase, [&runs](App&) { ++runs; });
            }
        }
        AddGround(test.Entities());
        std::vector<Entity> bodies = AddPile(test.Entities());

        test.Run(1.0);

        INFO("with " << workers << " worker threads, every frame ran its parallel systems and physics stepped");
        CHECK(runs == 60 * 12);
        CHECK(Outcome(bodies).front().pose.pos.y < 1.0f);
    }
}

TEST_CASE("A step finishes while every worker thread is busy", "[physics][threading]")
{
    std::atomic<bool> release = false;
    std::future<void> busy;
    App app(std::make_unique<SilentLogger>(), 1);
    // Before physics's PostTick system: the only worker is kept busy until after the step, so the step's jobs queue
    // behind work that cannot finish until the step has.
    app.AddSequentialSystems(SchedulePriority::PostTick, {[&](App& running) {
                                 release = false;
                                 busy = running.GetThreadPool().Enqueue([&release] {
                                     while (!release)
                                     {
                                         std::this_thread::yield();
                                     }
                                 });
                             }});
    Physics physics = Physics::Install(app, PhysicsConfig{.useWorkerThreads = true});
    app.AddSequentialSystems(SchedulePriority::PostTick, {[&](App&) {
                                 release = true;
                                 busy.wait();
                             }});
    EntityList& entities = app.GetActiveScene().GetEntityList();
    AddGround(entities);
    std::vector<Entity> bodies = AddPile(entities);

    for (int frame = 0; frame < 30; ++frame)
    {
        app.RunFrame(tick);
    }

    INFO("the stepping thread ran the step's jobs itself, so the bodies fell");
    CHECK(Outcome(bodies).front().pose.pos.y < 1.0f);
}
