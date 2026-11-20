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

    struct HeadlessApp
    {
        std::shared_ptr<std::vector<std::string>> errors = std::make_shared<std::vector<std::string>>();
        Fenrir::App app{std::make_unique<RecordingLogger>(errors), 1};
    };
} // namespace

TEST_CASE("A headless App holds a type registry as an app resource", "[types]")
{
    HeadlessApp headless;

    REQUIRE(headless.app.TryGetResource<Fenrir::TypeRegistry>() != nullptr);
}

TEST_CASE("Before any Install the registry knows FenrirECS's components and FenrirMath's types", "[types]")
{
    HeadlessApp headless;
    const auto& registry = headless.app.GetResource<Fenrir::TypeRegistry>();

    constexpr std::array names{
        "Fenrir::Transform",     "Fenrir::Name",          "Fenrir::Relationship", "Fenrir::Entity",
        "Fenrir::Math::Vec2",    "Fenrir::Math::Vec3",    "Fenrir::Math::Vec4",   "Fenrir::Math::Quat",
        "Fenrir::Math::Mat4",    "Fenrir::Colour",        "Fenrir::Math::Ray",    "Fenrir::Math::AABB",
        "Fenrir::Math::Frustum", "Fenrir::RawEventRange",
    };
    for (const std::string_view name : names)
    {
        INFO(name);
        REQUIRE(registry.Find(name) != nullptr);
    }
    REQUIRE(headless.errors->empty());
}

TEST_CASE("The registry holds the descriptions Describe<T> yields", "[types]")
{
    HeadlessApp headless;
    const auto& registry = headless.app.GetResource<Fenrir::TypeRegistry>();

    REQUIRE(registry.Find("Fenrir::Transform") == &Fenrir::Describe<Fenrir::Transform>::Description());
    REQUIRE(registry.Find("Fenrir::RawEventRange") == &Fenrir::Describe<Fenrir::RawEventRange>::Description());
}

TEST_CASE("Transform's position is FenrirMath's Vec3, not a second description of it", "[types]")
{
    HeadlessApp headless;
    const auto& registry = headless.app.GetResource<Fenrir::TypeRegistry>();
    const Fenrir::TypeDescription& transform = *registry.Find("Fenrir::Transform");

    REQUIRE(transform.fields[0].name == "pos");
    REQUIRE(transform.fields[0].type.type == registry.Find("Fenrir::Math::Vec3"));
    REQUIRE(transform.fields[0].type.type == &Fenrir::Describe<Fenrir::Math::Vec3>::Description());
}

TEST_CASE("Running a registration the App ran again registers nothing, with no diagnostic", "[types]")
{
    HeadlessApp headless;
    auto& registry = headless.app.GetResource<Fenrir::TypeRegistry>();
    const std::size_t components = registry.Types(Fenrir::Role::Component).size();

    REQUIRE(Fenrir::Generated::RegisterFenrirECSTypes(registry));

    REQUIRE(registry.Types(Fenrir::Role::Component).size() == components);
    REQUIRE(headless.errors->empty());
}
