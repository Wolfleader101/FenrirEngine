// The App's type registry once packages are installed (ADR-0074): each Install registers its package's types first, so
// a headless App with assets, skeletons, animation, physics and (when it is built) the renderer on its null backend
// knows their components, resources and the types they hold by name. Its own executable, since it links the packages
// above FenrirApp.
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAnimation/Animation.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

#if FENRIR_APP_TESTS_RENDERER
#include "FenrirRenderer/Renderer.hpp"
#endif

namespace
{
    // A logger that keeps the errors it is given: a second ConsoleLogger in one process throws.
    class RecordingLogger : public Fenrir::ILogger
    {
      public:
        explicit RecordingLogger(std::shared_ptr<std::vector<std::string>> errors) : m_errors(std::move(errors))
        {
        }

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
        void WarnImpl(std::string_view) override
        {
        }
        void ErrorImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }
        void FatalImpl(std::string_view message) override
        {
            m_errors->emplace_back(message);
        }

      private:
        std::shared_ptr<std::vector<std::string>> m_errors;
    };

    // A headless App with every package this test links installed, in the order a game installs them.
    struct InstalledApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<RecordingLogger>(errors), 1};
        Fenrir::Assets assets = Fenrir::Assets::Install(app);
        Fenrir::Skeletons skeletons = Fenrir::Skeletons::Install(app);
        Fenrir::Animation animation = Fenrir::Animation::Install(app, skeletons);
        Fenrir::Physics physics = Fenrir::Physics::Install(app);
#if FENRIR_APP_TESTS_RENDERER
        Fenrir::Renderer renderer = Fenrir::Renderer::Install(app, NullBackend());

        static Fenrir::RendererConfig NullBackend()
        {
            Fenrir::RendererConfig config;
            config.backend = Fenrir::GraphicsBackend::Null;
            return config;
        }
#endif

        [[nodiscard]] const Fenrir::TypeRegistry& Registry() const
        {
            return app.GetResource<Fenrir::TypeRegistry>();
        }

        // The errors the type registry reported: each of its refusals starts "cannot register".
        [[nodiscard]] std::size_t RegistryErrors() const
        {
            return static_cast<std::size_t>(std::ranges::count_if(
                *errors, [](const std::string& error) { return error.find("cannot register") != std::string::npos; }));
        }
    };

    // Every described component of the packages installed: FenrirECS's, FenrirSkeleton's, FenrirAnimation's,
    // FenrirPhysics's and, with the renderer, FenrirRenderer's.
    std::vector<std::string_view> DescribedComponents()
    {
        std::vector<std::string_view> components{
            "Fenrir::Name",
            "Fenrir::Relationship",
            "Fenrir::Transform",
            "Fenrir::BoneAttachment",
            "Fenrir::SkeletonPose",
            "Fenrir::AnimationLayer",
            "Fenrir::Animator",
            "Fenrir::CharacterBody",
            "Fenrir::Collider",
            "Fenrir::CollisionFilter",
            "Fenrir::ContactEvents",
            "Fenrir::Joint",
            "Fenrir::PhysicsMaterial",
            "Fenrir::RigidBody",
            "Fenrir::Trigger",
            "Fenrir::Vehicle",
            "Fenrir::VehicleAntiRollBar",
            "Fenrir::VehicleDifferential",
            "Fenrir::VehicleWheel",
        };
#if FENRIR_APP_TESTS_RENDERER
        components.insert(components.end(),
                          {"Fenrir::Camera", "Fenrir::DirectionalLight", "Fenrir::InstancedMeshRenderer",
                           "Fenrir::MeshRenderer", "Fenrir::PointLight", "Fenrir::RectLight", "Fenrir::SkinnedMesh",
                           "Fenrir::SpotLight", "Fenrir::WorldQuad2D"});
#endif
        std::ranges::sort(components);
        return components;
    }
} // namespace

TEST_CASE("After the Installs the registry knows RigidBody, Transform and AnimationLayer", "[types]")
{
    InstalledApp installed;

    for (const std::string_view name : {"Fenrir::RigidBody", "Fenrir::Transform", "Fenrir::AnimationLayer"})
    {
        INFO(name);
        REQUIRE(installed.Registry().Find(name) != nullptr);
    }
    REQUIRE(installed.RegistryErrors() == 0);
}

TEST_CASE("A joint's bodies are registered as entities", "[types]")
{
    InstalledApp installed;
    const Fenrir::TypeDescription* joint = installed.Registry().Find("Fenrir::Joint");

    REQUIRE(joint != nullptr);
    REQUIRE(joint->fields[0].name == "bodyA");
    REQUIRE(joint->fields[0].type.kind == Fenrir::Kind::Entity);
}

#if FENRIR_APP_TESTS_RENDERER
TEST_CASE("After Renderer::Install the registry knows the sky as a scene resource", "[types]")
{
    InstalledApp installed;
    const Fenrir::TypeDescription* sky = installed.Registry().Find("Fenrir::Sky");

    REQUIRE(sky != nullptr);
    REQUIRE(sky->role == Fenrir::Role::SceneResource);
}
#endif

TEST_CASE("Iterating the component role lists every described component of the installed packages", "[types]")
{
    InstalledApp installed;

    std::vector<std::string_view> components;
    for (const Fenrir::TypeDescription* component : installed.Registry().Types(Fenrir::Role::Component))
        components.push_back(component->name);
    std::ranges::sort(components);

    REQUIRE(components == DescribedComponents());
}

TEST_CASE("Installing a package twice registers its types once, with no diagnostic", "[types]")
{
    InstalledApp installed;
    const std::size_t components = installed.Registry().Types(Fenrir::Role::Component).size();

    Fenrir::Physics second = Fenrir::Physics::Install(installed.app);

    REQUIRE(installed.Registry().Types(Fenrir::Role::Component).size() == components);
    REQUIRE(installed.RegistryErrors() == 0);
}
