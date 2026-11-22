// The App's type registry (ADR-0074): the app resource its constructor makes, holding the types of the packages beneath
// Install (FenrirMath, FenrirECS and FenrirApp; FenrirScene describes none) before any package is installed.
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FenrirApp/App.hpp"
#include "FenrirApp/Generated/FenrirAppTypes.hpp"
#include "FenrirECS/Generated/FenrirECSTypes.hpp"
#include "FenrirMath/Generated/FenrirMathTypes.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "RecordingLogger.hpp"

namespace
{
    struct HeadlessApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<AppTest::RecordingLogger>(errors), 1};

        [[nodiscard]] Fenrir::TypeRegistry& Registry()
        {
            return app.GetResource<Fenrir::TypeRegistry>();
        }
    };
} // namespace

TEST_CASE("A headless App holds a type registry as an app resource", "[types]")
{
    HeadlessApp headless;

    INFO("App's constructor makes the TypeRegistry app resource, so TryGetResource finds it");
    REQUIRE(headless.app.TryGetResource<Fenrir::TypeRegistry>() != nullptr);
}

TEST_CASE("Before any Install the registry knows FenrirECS's components and FenrirMath's and FenrirApp's types",
          "[types]")
{
    HeadlessApp headless;

    constexpr std::array names{
        "Fenrir::Transform",     "Fenrir::Name",          "Fenrir::Relationship", "Fenrir::Entity",
        "Fenrir::Math::Vec2",    "Fenrir::Math::Vec3",    "Fenrir::Math::Vec4",   "Fenrir::Math::Quat",
        "Fenrir::Math::Mat4",    "Fenrir::Colour",        "Fenrir::Math::Ray",    "Fenrir::Math::AABB",
        "Fenrir::Math::Frustum", "Fenrir::RawEventRange",
    };
    for (const std::string_view name : names)
    {
        INFO(name << " is described by a package beneath Install, but the App's registry does not know it");
        REQUIRE(headless.Registry().Find(name) != nullptr);
    }
}

TEST_CASE("App's constructor registers its packages' types with no diagnostic", "[types]")
{
    HeadlessApp headless;

    INFO("the registry refused a type the constructor registered: " << AppTest::RegistryErrors(*headless.errors));
    REQUIRE(AppTest::RegistryErrors(*headless.errors) == 0);
}

TEST_CASE("The registry holds the descriptions Describe<T> yields", "[types]")
{
    HeadlessApp headless;

    INFO("the registered Transform is not the object Describe<Fenrir::Transform> yields");
    REQUIRE(headless.Registry().Find("Fenrir::Transform") == &Fenrir::Describe<Fenrir::Transform>::Description());
    INFO("the registered RawEventRange is not the object Describe<Fenrir::RawEventRange> yields");
    REQUIRE(headless.Registry().Find("Fenrir::RawEventRange") ==
            &Fenrir::Describe<Fenrir::RawEventRange>::Description());
}

TEST_CASE("Transform's position is FenrirMath's Vec3, not a second description of it", "[types]")
{
    HeadlessApp headless;
    const Fenrir::TypeDescription& transform = *headless.Registry().Find("Fenrir::Transform");

    INFO("Transform's first field is its position, pos");
    REQUIRE(transform.fields[0].name == "pos");
    INFO("pos's type should be the Vec3 FenrirMath registers, which FenrirECS's registration uses");
    REQUIRE(transform.fields[0].type.type == headless.Registry().Find("Fenrir::Math::Vec3"));
}

TEST_CASE("Running a registration the App ran again registers nothing", "[types]")
{
    HeadlessApp headless;
    const std::size_t components = headless.Registry().Types(Fenrir::Role::Component).size();

    INFO("a second run of FenrirECS's registration should succeed, every type already registered as itself");
    REQUIRE(Fenrir::Generated::RegisterFenrirECSTypes(headless.Registry()));
    INFO("the second run should add no component");
    REQUIRE(headless.Registry().Types(Fenrir::Role::Component).size() == components);
}

TEST_CASE("Running a registration the App ran again reports nothing", "[types]")
{
    HeadlessApp headless;

    static_cast<void>(Fenrir::Generated::RegisterFenrirECSTypes(headless.Registry()));

    INFO("the second run should leave the registry nothing to refuse");
    REQUIRE(AppTest::RegistryErrors(*headless.errors) == 0);
}
