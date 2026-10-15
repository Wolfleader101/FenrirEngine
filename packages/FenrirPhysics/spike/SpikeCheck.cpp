// PROTOTYPE (physics spike): a headless check of the spike. Drops a box on the ground and a sphere under a rotated,
// moved parent; prints where they rest; runs twice and compares bit for bit.

#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>

#include "FenrirApp/App.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/WorldTransform.hpp"
#include "FenrirLogger/ILogger.hpp"
#include "FenrirPhysics/Physics.hpp"

using namespace Fenrir;

// ConsoleLogger registers a process-wide spdlog logger named "FENRIR", so a second App with one throws.
class SilentLogger final : public ILogger
{
  public:
    void AddLogger(std::shared_ptr<ILogger>) override
    {
    }
    void RemoveLogger(std::shared_ptr<ILogger>) override
    {
    }

  protected:
    void LogImpl(std::string_view) override
    {
    }
    void InfoImpl(std::string_view) override
    {
    }
    void WarnImpl(std::string_view message) override
    {
        std::fprintf(stderr, "[warn] %.*s\n", static_cast<int>(message.size()), message.data());
    }
    void ErrorImpl(std::string_view message) override
    {
        std::fprintf(stderr, "[error] %.*s\n", static_cast<int>(message.size()), message.data());
    }
    void FatalImpl(std::string_view message) override
    {
        std::fprintf(stderr, "[fatal] %.*s\n", static_cast<int>(message.size()), message.data());
    }
};

struct Result
{
    Math::Vec3 box;
    Math::Vec3 child;
};

static void Progress(const char* where)
{
    std::fprintf(stderr, "[spike] %s\n", where);
    std::fflush(stderr);
}

static Result Run(bool print)
{
    Progress("creating app");
    App app(std::make_unique<SilentLogger>());
    Progress("installing physics");
    Physics physics = Physics::Install(app);
    Scene& scene = app.GetActiveScene();
    EntityList& entities = scene.GetEntityList();
    const Math::Quat identity(1.0f, 0.0f, 0.0f, 0.0f);

    Progress("creating entities");
    Entity ground = entities.CreateEntity();
    ground.GetComponent<Transform>() =
        Transform(Math::Vec3(0.0f, -0.5f, 0.0f), identity, Math::Vec3(20.0f, 1.0f, 20.0f));
    ground.AddComponent<Collider>();

    Entity box = entities.CreateEntity();
    box.GetComponent<Transform>() = Transform(Math::Vec3(0.0f, 5.0f, 0.0f), identity, Math::Vec3(1.0f));
    box.AddComponent<RigidBody>();
    box.AddComponent<Collider>();

    // A dynamic body under a parent that is moved and rotated: write-back must go through SetWorldTransform.
    Entity parent = entities.CreateEntity();
    parent.GetComponent<Transform>() =
        Transform(Math::Vec3(5.0f, 2.0f, 0.0f), Math::AngleAxis(0.7f, Math::Vec3(0.0f, 1.0f, 0.0f)), Math::Vec3(1.0f));
    Entity child = entities.CreateEntity();
    parent.AddChild(child);
    child.GetComponent<Transform>() = Transform(Math::Vec3(1.0f, 3.0f, 0.0f), identity, Math::Vec3(1.0f));
    child.AddComponent<RigidBody>();
    child.AddComponent<Collider>(Collider{SphereShape{0.5f}});

    Progress("creating world");
    PhysicsWorld& world = scene.AddResource<PhysicsWorld>(PhysicsConfig{});
    Progress("stepping");
    for (int step = 0; step < 300; ++step)
    {
        world.Step(scene, 1.0f / 60.0f);
        if (print && step % 60 == 0)
        {
            const double boxY = GetWorldTransform(box).pos.y;
            const double childY = GetWorldTransform(child).pos.y;
            std::printf("t=%.1fs  box y=%.4f  sphere world y=%.4f\n", step / 60.0, boxY, childY);
        }
    }
    return Result{GetWorldTransform(box).pos, GetWorldTransform(child).pos};
}

int main()
{
    const Result a = Run(true);
    const Result b = Run(false);
    std::printf("box rests at y=%.4f (expected 0.5: half extent above the ground's top at 0)\n",
                static_cast<double>(a.box.y));
    std::printf("sphere rests at world y=%.4f (expected 0.5: its radius)\n", static_cast<double>(a.child.y));
    const bool same = std::memcmp(&a, &b, sizeof(Result)) == 0;
    std::printf("two runs bit-identical: %s\n", same ? "yes" : "NO");
    return same ? 0 : 1;
}
