// A component a script defines, in a scene file: admission describes it to the App's type registry and attaches its
// ComponentType (FenrirScripting, ADR-0074, ADR-0083), so the scene writer saves it by its full name as it saves a
// native component, leaving its transient field out, and the reader reads it back. Each test saves into a temporary
// asset root of its own, so nothing is written among the fixtures.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "AssetsScriptingApp.hpp"

#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirECS/TypeAttachments.hpp"
#include "FenrirTypes/TypeRegistry.hpp"

namespace
{
    using namespace Fenrir;
    using namespace Fenrir::Testing;
    namespace fs = std::filesystem;

    constexpr std::string_view TaggedName = "AssetsScriptFixture.Tagged";
    constexpr std::string_view ScenePath = "levels/tagged.scene.json5";

    // A temporary asset root holding one image, removed afterwards.
    struct ImageRoot
    {
        fs::path path;

        ImageRoot()
        {
            static int counter = 0;
            path = fs::temp_directory_path() / ("fenrir-assets-scripting-scenes-" + std::to_string(++counter));
            std::error_code ignored;
            fs::remove_all(path, ignored);
            fs::create_directories(path / "icons");
            fs::copy_file(fs::path(FENRIR_ASSETS_FIXTURES) / "fourbytwo.png", path / "icons" / "flag.png");
        }

        ~ImageRoot()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    };

    // Assets over `root`, for scripting with assets' extension running the fixture.
    struct RootedAssets
    {
        RootedAssets(App& app, const fs::path& root) : assets(Assets::Install(app, {root}))
        {
        }

        Assets assets;
    };

    struct SceneApp : ExtensionTestApp<RootedAssets>
    {
        explicit SceneApp(const fs::path& root)
            : ExtensionTestApp(FixtureFromEnvironment("FENRIR_TEST_ASSETS_FIXTURE"), {Assets::ScriptingExtension()},
                               root)
        {
        }

        AssetStore& Store()
        {
            return assets.Store();
        }
    };

    std::string ReadText(const fs::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    // The offset of the field `name` of `type`.
    std::size_t OffsetOf(const TypeDescription& type, std::string_view name)
    {
        for (const FieldInfo& field : type.fields)
        {
            if (field.name == name)
            {
                return field.offset;
            }
        }
        FAIL("The description has no field " << name);
        return 0;
    }
} // namespace

TEST_CASE("the scene writer saves a C# component by its full name, leaving its transient field out",
          "[assets-scripting][scenes]")
{
    ImageRoot root;
    SceneApp test(root.path);
    const auto& types = test.Get().GetResource<TypeRegistry>();
    const TypeDescription* tagged = types.Find(TaggedName);
    {
        INFO("Admission should describe the C# component, with its four fields");
        REQUIRE(tagged != nullptr);
        REQUIRE(tagged->fields.size() == 4);
        CHECK(tagged->fields[0].type.kind == Kind::Entity);
        CHECK((tagged->fields[1].type.kind == Kind::Reference && tagged->fields[1].type.target == "Fenrir::Image"));
        CHECK((tagged->fields[2].type.kind == Kind::FixedString && tagged->fields[2].type.count == 16));
        CHECK((tagged->fields[3].name == "Frames" && tagged->fields[3].transient));
    }
    const std::optional<ComponentType> component = ComponentType::ForName(types, TaggedName);
    {
        INFO("ComponentType::ForName should find it");
        REQUIRE(component.has_value());
    }

    const AssetHandle<Image> icon = test.Store().Load<Image>("icons/flag.png", AssetOwner::App()).handle;
    REQUIRE(test.Store().IsValid(icon));
    Scene& scene = test.Get().CreateScene("Tagged");
    EntityList& entities = scene.GetEntityList();
    Entity target = entities.CreateEntity();
    Entity holder = entities.CreateEntity();
    REQUIRE(entities.Add(*component, holder.GetId()));
    auto* bytes = static_cast<std::byte*>(entities.Get(*component, holder.GetId()));
    REQUIRE(bytes != nullptr);
    std::memcpy(bytes + OffsetOf(*tagged, "Target"), &target, sizeof(target));
    std::memcpy(bytes + OffsetOf(*tagged, "Icon"), &icon, sizeof(icon));
    std::memcpy(bytes + OffsetOf(*tagged, "Label"), "banner", 7);
    const std::int32_t frames = 7;
    std::memcpy(bytes + OffsetOf(*tagged, "Frames"), &frames, sizeof(frames));

    const SceneSaveResult saved = test.Store().SaveScene(scene, ScenePath);

    INFO("The save should succeed with nothing to report");
    REQUIRE(saved.status == SaveStatus::Succeeded);
    const std::string text = ReadText(root.path / ScenePath);
    INFO(text);
    INFO("The component should be written under its full name, with its label and the path of its icon");
    CHECK(text.find("\"AssetsScriptFixture.Tagged\"") != std::string::npos);
    CHECK(text.find("banner") != std::string::npos);
    CHECK(text.find("icons/flag.png") != std::string::npos);
    INFO("Its target should be written as the id of the entity it names");
    CHECK(text.find("Target") != std::string::npos);
    INFO("Its transient field should be left out");
    CHECK(text.find("Frames") == std::string::npos);

    const SceneLoadResult loaded = test.Store().LoadScene(test.Get(), ScenePath, "Loaded");
    INFO("The file should load back");
    REQUIRE(loaded.status == LoadStatus::Succeeded);
    REQUIRE(loaded.scene != nullptr);
    EntityList& read = loaded.scene->GetEntityList();
    std::size_t found = 0;
    for (const Entity& entity : loaded.roots)
    {
        const auto* readBytes = static_cast<const std::byte*>(read.Get(*component, entity.GetId()));
        if (readBytes == nullptr)
        {
            continue;
        }
        ++found;
        INFO("The label should read back, and the transient field stay at its default");
        CHECK(std::string(reinterpret_cast<const char*>(readBytes + OffsetOf(*tagged, "Label"))) == "banner");
        std::int32_t readFrames = -1;
        std::memcpy(&readFrames, readBytes + OffsetOf(*tagged, "Frames"), sizeof(readFrames));
        CHECK(readFrames == 0);
    }
    INFO("One entity should have read the component back");
    CHECK(found == 1);
}
