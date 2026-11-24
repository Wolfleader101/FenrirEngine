// Asset paths (ADR-0076) and the asset source seam (ADR-0085): a data asset's envelope is read through a source, an
// asset's identity is its root-relative path, a path that leaves the root is refused, and a handle answers its path.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetSource.hpp"
#include "AssetsTest.hpp"
#include "FenrirTypes/TypeRegistry.hpp"
#include "Generated/DataAssetsTestPackageTypes.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::Fixtures;
using AssetsTest::Sword;

namespace
{
    // An App with assets installed and the test package's data asset types registered, as a game registers its own.
    struct DataApp : AssetsApp
    {
        bool registered =
            Fenrir::Generated::RegisterDataAssetsTestPackageTypes(app.GetResource<Fenrir::TypeRegistry>());

        DataApp() = default;
        explicit DataApp(Fenrir::AssetsConfig config) : AssetsApp(std::move(config))
        {
        }
    };

    // A source holding its files in memory, by asset path: what a pack source will be, with no file on disk.
    class MemorySource : public Fenrir::Detail::AssetSource
    {
      public:
        explicit MemorySource(std::unordered_map<std::string, std::string> files) : m_files(std::move(files))
        {
        }

        bool Exists(std::string_view path) override
        {
            return m_files.contains(std::string(path));
        }

        std::optional<std::vector<std::byte>> Read(std::string_view path) override
        {
            const auto found = m_files.find(std::string(path));
            if (found == m_files.end())
            {
                return std::nullopt;
            }
            std::vector<std::byte> bytes(found->second.size());
            std::ranges::transform(found->second, bytes.begin(), [](char c) { return static_cast<std::byte>(c); });
            return bytes;
        }

      private:
        std::unordered_map<std::string, std::string> m_files;
    };

    // How many of `messages` contain `part`.
    std::size_t CountContaining(const std::vector<std::string>& messages, std::string_view part)
    {
        return static_cast<std::size_t>(std::ranges::count_if(
            messages, [&](const std::string& message) { return message.find(part) != std::string::npos; }));
    }

    // The diagnostic whose message contains `part`, or null.
    const Fenrir::Serialisation::Diagnostic* FindDiagnostic(const std::vector<Fenrir::Serialisation::Diagnostic>& all,
                                                            std::string_view part)
    {
        const auto found = std::ranges::find_if(all, [&](const Fenrir::Serialisation::Diagnostic& diagnostic) {
            return diagnostic.message.find(part) != std::string::npos;
        });
        return found != all.end() ? &*found : nullptr;
    }
} // namespace

TEST_CASE("A data asset loads through the directory source over the asset root", "[assets][paths][source]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const std::unique_ptr<Fenrir::Detail::AssetSource> source =
        Fenrir::Detail::MakeDirectorySource(Fixtures(), test.store.Logger());

    INFO("the directory source finds a file by its root-relative path, and serves its bytes");
    REQUIRE(source->Exists("data/sword.asset.json5"));
    const std::optional<std::vector<std::byte>> bytes = source->Read("data/sword.asset.json5");
    REQUIRE(bytes.has_value());
    REQUIRE(bytes->size() == std::filesystem::file_size(Fixtures() / "data/sword.asset.json5"));
    INFO("a path with no file is not there");
    REQUIRE_FALSE(source->Exists("data/no-such.asset.json5"));
    REQUIRE_FALSE(source->Read("data/no-such.asset.json5").has_value());
}

TEST_CASE("A data asset loads from a source's bytes with no file on disk", "[assets][paths][source]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Detail::UseAssetSource(
        test.store, std::make_unique<MemorySource>(std::unordered_map<std::string, std::string>{
                        {"packed/dagger.asset.json5",
                         R"({ format: 1, type: "AssetsTest::Sword", version: 2, data: { displayName: "Dagger" } })"}}));

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("packed/dagger.asset.json5", Fenrir::AssetOwner::App());

    INFO("the store asks the source for the bytes, though no such file is under the root: "
         << (loaded.diagnostic ? loaded.diagnostic->importerMessage : std::string("ok")));
    REQUIRE_FALSE(std::filesystem::exists(Fixtures() / "packed"));
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Succeeded);
    const Sword* dagger = test.store.TryGet(loaded.handle);
    REQUIRE(dagger != nullptr);
    REQUIRE(dagger->displayName == "Dagger");
}

TEST_CASE("A data asset path its source lacks fails with FileNotFound", "[assets][paths][source]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    Fenrir::Detail::UseAssetSource(test.store,
                                   std::make_unique<MemorySource>(std::unordered_map<std::string, std::string>{}));

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("the file on disk is not the source's, so the store does not find it");
    REQUIRE_FALSE(loaded.Ok());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::FileNotFound);
}

TEST_CASE("A data asset's absolute path under the root and its relative path give one handle", "[assets][paths]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> relative =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Sword> absolute =
        test.store.Load<Sword>((Fixtures() / "data/sword.asset.json5").string(), Fenrir::AssetOwner::App());

    INFO("the absolute path is keyed by its root-relative form, so both name one asset");
    REQUIRE(relative.Ok());
    REQUIRE(absolute.handle == relative.handle);
    REQUIRE(test.store.PathOf(absolute.handle) == "data/sword.asset.json5");
}

TEST_CASE("A data asset's absolute path outside the root fails with OutsideRoot", "[assets][paths]")
{
    DataApp test({Fixtures() / "models"});
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> outside =
        test.store.Load<Sword>((Fixtures() / "data/sword.asset.json5").string(), Fenrir::AssetOwner::App());

    INFO("the file is there, but not under the root, so it has no asset path");
    REQUIRE(std::filesystem::exists(Fixtures() / "data/sword.asset.json5"));
    REQUIRE_FALSE(outside.Ok());
    REQUIRE(outside.handle.IsEmpty());
    REQUIRE(outside.diagnostic->reason == Fenrir::LoadFailure::OutsideRoot);
    INFO("the failure is logged once");
    REQUIRE(test.errors->size() == 1);
}

TEST_CASE("A data asset's relative path climbing out of the root fails with OutsideRoot", "[assets][paths]")
{
    DataApp test({Fixtures() / "models"});
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> outside =
        test.store.Load<Sword>("../data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("`..` resolves beside the root, not under it");
    REQUIRE_FALSE(outside.Ok());
    REQUIRE(outside.diagnostic->reason == Fenrir::LoadFailure::OutsideRoot);
}

TEST_CASE("A path in a file climbing through .. is refused with a located diagnostic", "[assets][paths][files]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/escaping-icon.asset.json5", Fenrir::AssetOwner::App());

    INFO("the sword loads in part, its icon left empty");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Partial);
    const Sword* sword = test.store.TryGet(loaded.handle);
    REQUIRE(sword != nullptr);
    REQUIRE(sword->displayName == "Wandering sword");
    REQUIRE(sword->icon.IsEmpty());
    INFO("one diagnostic names the path and why, at the string's line and column");
    const Fenrir::Serialisation::Diagnostic* refused = FindDiagnostic(loaded.diagnostics, "../fourbytwo.png");
    REQUIRE(refused != nullptr);
    REQUIRE(refused->severity == Fenrir::Serialisation::DiagnosticSeverity::Error);
    REQUIRE(refused->message.find("..") != std::string::npos);
    REQUIRE(refused->line == 8);
    REQUIRE(refused->column == 11);
    REQUIRE(loaded.diagnostics.size() == 1);
    INFO("it is logged once");
    REQUIRE(CountContaining(*test.errors, "../fourbytwo.png") == 1);
}

TEST_CASE("An absolute path in a file is refused with a located diagnostic", "[assets][paths][files]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/absolute-icon.asset.json5", Fenrir::AssetOwner::App());

    INFO("the sword loads in part, its icon left empty, with one diagnostic at the string");
    REQUIRE(loaded.Status() == Fenrir::LoadStatus::Partial);
    const Fenrir::Serialisation::Diagnostic* refused = FindDiagnostic(loaded.diagnostics, "/icons/sword.png");
    REQUIRE(refused != nullptr);
    REQUIRE(refused->message.find("absolute") != std::string::npos);
    REQUIRE(refused->line == 8);
    REQUIRE(loaded.diagnostics.size() == 1);
}

#ifndef NDEBUG
TEST_CASE("A path matching a file only when case is ignored warns once in a development build", "[assets][paths][case]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> first = test.store.Load<Sword>("data/SWORD.asset.json5", Fenrir::AssetOwner::App());
    REQUIRE(test.store.Unload(first.handle));
    const Fenrir::LoadResult<Sword> again = test.store.Load<Sword>("data/SWORD.asset.json5", Fenrir::AssetOwner::App());

    INFO("the file is found, and read each time");
    REQUIRE(first.Ok());
    REQUIRE(again.Ok());
    INFO("the warning names the path, once for the path however often it is read");
    REQUIRE(CountContaining(*test.warnings, "data/SWORD.asset.json5") == 1);
    REQUIRE(CountContaining(*test.warnings, "case") == 1);
}

TEST_CASE("A path matching its file's case exactly warns nothing", "[assets][paths][case]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("no warning for a path spelled as the disk spells it");
    REQUIRE(loaded.Ok());
    REQUIRE(test.warnings->empty());
}
#endif

TEST_CASE("PathOf gives a loaded data asset's path", "[assets][paths][pathof]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);

    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());

    INFO("its asset path: root-relative, forward slashes, extension included");
    REQUIRE(loaded.Ok());
    REQUIRE(test.store.PathOf(loaded.handle) == "data/sword.asset.json5");
}

TEST_CASE("PathOf gives a loaded image's path", "[assets][paths][pathof]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> image =
        test.store.Load<Fenrir::Image>("fourbytwo.png", Fenrir::AssetOwner::App());

    INFO("an imported asset's path is its asset path too");
    REQUIRE(image.Ok());
    REQUIRE(test.store.PathOf(image.handle) == "fourbytwo.png");
}

TEST_CASE("PathOf gives a sub-asset's path with its label", "[assets][paths][pathof]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Mesh> mesh =
        test.store.Load<Fenrir::Mesh>("models/scene.glb#Mesh0", Fenrir::AssetOwner::App());

    INFO("a model's sub-asset is its file's path, then `#` and its label");
    REQUIRE(mesh.Ok());
    REQUIRE(test.store.PathOf(mesh.handle) == "models/scene.glb#Mesh0");
}

TEST_CASE("PathOf is empty for an asset added with no name", "[assets][paths][pathof]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh =
        test.store.Add<Fenrir::Mesh>(AssetsTest::Triangle(), Fenrir::AssetOwner::App());

    INFO("an unnamed Add has no path");
    REQUIRE(test.store.IsValid(mesh));
    REQUIRE_FALSE(test.store.PathOf(mesh).has_value());
}

TEST_CASE("PathOf is empty for a stale handle", "[assets][paths][pathof]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    REQUIRE(test.store.Unload(loaded.handle));

    INFO("a freed asset's handle names no path");
    REQUIRE_FALSE(test.store.PathOf(loaded.handle).has_value());
}
