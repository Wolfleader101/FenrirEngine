// The App's type registry once packages are installed (ADR-0074): each Install registers its package's types first, so
// a headless App with assets, skeletons, animation, physics and (when it is built) the renderer on its null backend
// knows their components, resources and the types they hold by name. Its own executable, since it links the packages
// above FenrirApp.
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirAnimation/Animation.hpp"
#include "FenrirApp/App.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirECS/ComponentType.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirPhysics/Components.hpp"
#include "FenrirPhysics/Physics.hpp"
#include "FenrirScene/Scene.hpp"
#include "FenrirSkeleton/Skeletons.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "RecordingLogger.hpp"

#if FENRIR_APP_TESTS_RENDERER
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Generated/FenrirRendererTypes.hpp"
#include "FenrirRenderer/Renderer.hpp"
#endif

namespace
{
#if FENRIR_APP_TESTS_RENDERER
    Fenrir::RendererConfig NullBackend()
    {
        Fenrir::RendererConfig config;
        config.backend = Fenrir::GraphicsBackend::Null;
        return config;
    }
#endif

    // A headless App with every package this test links installed, in the order a game installs them.
    struct InstalledApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<AppTest::RecordingLogger>(errors), 1};
        Fenrir::Assets assets = Fenrir::Assets::Install(app);
        Fenrir::Skeletons skeletons = Fenrir::Skeletons::Install(app);
        Fenrir::Animation animation = Fenrir::Animation::Install(app, skeletons);
        Fenrir::Physics physics = Fenrir::Physics::Install(app);
#if FENRIR_APP_TESTS_RENDERER
        Fenrir::Renderer renderer = Fenrir::Renderer::Install(app, NullBackend());
#endif

        [[nodiscard]] const Fenrir::TypeRegistry& Registry() const
        {
            return app.GetResource<Fenrir::TypeRegistry>();
        }
    };

    // Every described component of the packages installed: FenrirECS's, FenrirSkeleton's, FenrirAnimation's,
    // FenrirPhysics's and, with the renderer, FenrirRenderer's.
    std::vector<std::string_view> DescribedComponents()
    {
        std::vector<std::string_view> components{
            "Fenrir::EntityId",
            "Fenrir::Instance",
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
        INFO(name << " is described by an installed package, but the App's registry does not know it");
        REQUIRE(installed.Registry().Find(name) != nullptr);
    }
}

TEST_CASE("The Installs register their packages' types with no diagnostic", "[types]")
{
    InstalledApp installed;

    INFO("the registry refused " << AppTest::RegistryErrors(*installed.errors) << " of the installed packages' types");
    REQUIRE(AppTest::RegistryErrors(*installed.errors) == 0);
}

TEST_CASE("A joint's bodies are registered as entities", "[types]")
{
    InstalledApp installed;
    const Fenrir::TypeDescription* joint = installed.Registry().Find("Fenrir::Joint");

    INFO("Physics::Install should register Fenrir::Joint");
    REQUIRE(joint != nullptr);
    INFO("Joint's first field is its first body, bodyA");
    REQUIRE(joint->fields[0].name == "bodyA");
    INFO("bodyA is a Fenrir::Entity, which the registry knows as the entity kind");
    REQUIRE(joint->fields[0].type.kind == Fenrir::Kind::Entity);
}

#if FENRIR_APP_TESTS_RENDERER
TEST_CASE("After Renderer::Install the registry knows the sky as a scene resource", "[types]")
{
    InstalledApp installed;
    const Fenrir::TypeDescription* sky = installed.Registry().Find("Fenrir::Sky");

    INFO("Renderer::Install should register Fenrir::Sky");
    REQUIRE(sky != nullptr);
    INFO("the sky is a scene resource by FenrirRenderer's selection");
    REQUIRE(sky->role == Fenrir::Role::SceneResource);
}

TEST_CASE("Describe<RendererConfig> is the description Renderer::Install registers", "[types]")
{
    InstalledApp installed;

    INFO("Describe<T> and the registration should yield the same description object, so a config filled before "
         "Install is the type the registry knows");
    REQUIRE(installed.Registry().Find("Fenrir::RendererConfig") ==
            &Fenrir::Describe<Fenrir::RendererConfig>::Description());
}
#endif

TEST_CASE("Iterating the component role lists every described component of the installed packages", "[types]")
{
    InstalledApp installed;

    std::vector<std::string_view> components;
    for (const Fenrir::TypeDescription* component : installed.Registry().Types(Fenrir::Role::Component))
        components.push_back(component->name);
    std::ranges::sort(components);

    INFO("the registry's components should be exactly the installed packages' described components");
    REQUIRE(components == DescribedComponents());
}

TEST_CASE("Installing a package twice registers its types once", "[types]")
{
    InstalledApp installed;
    const std::size_t components = installed.Registry().Types(Fenrir::Role::Component).size();

    Fenrir::Physics second = Fenrir::Physics::Install(installed.app);

    INFO("the second Physics::Install should add no component");
    REQUIRE(installed.Registry().Types(Fenrir::Role::Component).size() == components);
}

TEST_CASE("Installing a package twice reports nothing from the registry", "[types]")
{
    InstalledApp installed;

    Fenrir::Physics second = Fenrir::Physics::Install(installed.app);

    INFO("the second Physics::Install should leave the registry nothing to refuse");
    REQUIRE(AppTest::RegistryErrors(*installed.errors) == 0);
}

TEST_CASE("ComponentType::ForName finds RigidBody as a component an entity accepts", "[types][components]")
{
    InstalledApp installed;
    Fenrir::EntityList& entities = installed.app.GetActiveScene().GetEntityList();
    const auto entity = entities.CreateEntity().GetId();

    const std::optional<Fenrir::ComponentType> body =
        Fenrir::ComponentType::ForName(installed.Registry(), "Fenrir::RigidBody");
    INFO("Physics::Install's registration should hang a ComponentType on Fenrir::RigidBody");
    REQUIRE(body.has_value());
    INFO("the entity should take a RigidBody through the ComponentType found by name");
    REQUIRE(entities.Add(*body, entity));
    INFO("the RigidBody added by name should be the one the C++ type sees");
    REQUIRE(entities.Has(Fenrir::ComponentType::Native<Fenrir::RigidBody>(), entity));
}

TEST_CASE("ComponentType::NameOf gives RigidBody's name back", "[types][components]")
{
    InstalledApp installed;

    const std::optional<Fenrir::ComponentType> body =
        Fenrir::ComponentType::ForName(installed.Registry(), "Fenrir::RigidBody");
    INFO("Physics::Install's registration should hang a ComponentType on Fenrir::RigidBody");
    REQUIRE(body.has_value());
    INFO("NameOf should give back the name ForName found it by");
    REQUIRE(Fenrir::ComponentType::NameOf(installed.Registry(), *body) == "Fenrir::RigidBody");
}

TEST_CASE("ComponentType::ForName finds Transform, which the App registers itself", "[types][components]")
{
    InstalledApp installed;

    INFO("the App's constructor registers FenrirECS, whose registration hangs a ComponentType on Fenrir::Transform");
    REQUIRE(Fenrir::ComponentType::ForName(installed.Registry(), "Fenrir::Transform").has_value());
}

TEST_CASE("ComponentType::ForName finds no component for an unknown name", "[types][components]")
{
    InstalledApp installed;

    INFO("no installed package describes Fenrir::NoSuchComponent");
    REQUIRE(!Fenrir::ComponentType::ForName(installed.Registry(), "Fenrir::NoSuchComponent").has_value());
}

TEST_CASE("Every registered component has a ComponentType by its name", "[types][components]")
{
    InstalledApp installed;

    for (const Fenrir::TypeDescription* component : installed.Registry().Types(Fenrir::Role::Component))
    {
        INFO(component->name << " is a registered component, so its package's registration should have attached it");
        REQUIRE(Fenrir::ComponentType::ForName(installed.Registry(), component->name).has_value());
    }
}

#if FENRIR_APP_TESTS_RENDERER
TEST_CASE("Scene::AddResource by name makes a default Sky the C++ type finds", "[types][resources]")
{
    InstalledApp installed;
    Fenrir::Scene& scene = installed.app.GetActiveScene();
    scene.RemoveResource<Fenrir::Sky>();

    void* added = scene.AddResource(installed.Registry(), "Fenrir::Sky");

    INFO("Renderer::Install's registration should hang the sky's scene doors, so adding it by name gives a Sky");
    REQUIRE(added != nullptr);
    INFO("GetResource<Sky> should find the Sky added by name");
    REQUIRE(scene.GetResource<Fenrir::Sky>() == added);
}

TEST_CASE("Scene::FindResource by name finds the scene's Sky", "[types][resources]")
{
    InstalledApp installed;
    Fenrir::Scene& scene = installed.app.GetActiveScene();
    Fenrir::Sky& sky = scene.AddResource<Fenrir::Sky>();

    INFO("FindResource by Fenrir::Sky should give the Sky AddResource<Sky> made");
    REQUIRE(scene.FindResource(installed.Registry(), "Fenrir::Sky") == &sky);
}

TEST_CASE("Scene::RemoveResource by name removes the scene's Sky", "[types][resources]")
{
    InstalledApp installed;
    Fenrir::Scene& scene = installed.app.GetActiveScene();
    scene.AddResource<Fenrir::Sky>();

    scene.RemoveResource(installed.Registry(), "Fenrir::Sky");

    INFO("the scene should have no Sky once it is removed by name");
    REQUIRE(scene.GetResource<Fenrir::Sky>() == nullptr);
}

TEST_CASE("Scene's named doors open nothing for a name that is not a scene resource's", "[types][resources]")
{
    InstalledApp installed;
    Fenrir::Scene& scene = installed.app.GetActiveScene();
    const std::size_t errors = installed.errors->size();

    INFO("Fenrir::Camera is a component, not a scene resource, so adding it by name gives nothing");
    REQUIRE(scene.AddResource(installed.Registry(), "Fenrir::Camera") == nullptr);
    INFO("and finding it by name gives nothing");
    REQUIRE(scene.FindResource(installed.Registry(), "Fenrir::Camera") == nullptr);
    INFO("and the registry reports nothing about it");
    REQUIRE(installed.errors->size() == errors);
}
#endif
