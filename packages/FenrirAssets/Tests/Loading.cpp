#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::Fixtures;

namespace
{
    bool Contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    std::string Generic(const std::filesystem::path& path)
    {
        return path.lexically_normal().generic_string();
    }
} // namespace

TEST_CASE("Loading a path already loaded gives the same handle and adds the owner", "[assets][loading]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");

    const Fenrir::LoadResult<Fenrir::Image> forLevel = test.store.Load<Fenrir::Image>("fourbytwo.png", level);
    const Fenrir::LoadResult<Fenrir::Image> forApp =
        test.store.Load<Fenrir::Image>("fourbytwo.png", Fenrir::AssetOwner::App());
    REQUIRE(forLevel.Ok());
    REQUIRE(forApp.Ok());

    INFO("the second load gives back the first one's handle");
    REQUIRE(forApp.handle == forLevel.handle);

    test.app.DestroyScene("Level");
    INFO("the app became an owner too, so the image outlives the scene that loaded it first");
    REQUIRE(test.store.IsValid(forLevel.handle));
}

TEST_CASE("The same file named two ways is loaded once", "[assets][loading]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> relative =
        test.store.Load<Fenrir::Image>("fourbytwo.png", Fenrir::AssetOwner::App());
    const Fenrir::LoadResult<Fenrir::Image> absolute =
        test.store.Load<Fenrir::Image>((Fixtures() / "fourbytwo.png").string(), Fenrir::AssetOwner::App());

    INFO("a relative path and the absolute path it resolves to give one asset");
    REQUIRE(relative.Ok());
    REQUIRE(absolute.handle == relative.handle);
}

TEST_CASE("A path loaded again after Unload is a new asset", "[assets][loading]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> first =
        test.store.Load<Fenrir::Image>("fourbytwo.png", Fenrir::AssetOwner::App());
    REQUIRE(test.store.Unload(first.handle));

    const Fenrir::LoadResult<Fenrir::Image> again =
        test.store.Load<Fenrir::Image>("fourbytwo.png", Fenrir::AssetOwner::App());
    INFO("the file is read again into a new asset");
    REQUIRE(again.Ok());
    REQUIRE(again.handle != first.handle);
    REQUIRE_FALSE(test.store.IsValid(first.handle));
}

TEST_CASE("Relative paths resolve against the asset root", "[assets][loading][paths]")
{
    AssetsApp test({Fixtures()});
    const Fenrir::LoadResult<Fenrir::Image> image =
        test.store.Load<Fenrir::Image>("fourbytwo.png", Fenrir::AssetOwner::App());

    INFO("a file in the asset root loads by its name");
    REQUIRE(image.Ok());
    REQUIRE(test.store.IsValid(image.handle));
}

TEST_CASE("Absolute paths are taken as given", "[assets][loading][paths]")
{
    AssetsApp test({Fixtures() / "no-such-root"});
    const Fenrir::LoadResult<Fenrir::Image> image =
        test.store.Load<Fenrir::Image>((Fixtures() / "fourbytwo.png").string(), Fenrir::AssetOwner::App());

    INFO("an absolute path loads whatever the asset root is");
    REQUIRE(image.Ok());
}

TEST_CASE("The default asset root is assets beside the executable", "[assets][loading][paths]")
{
    AssetsApp test(Fenrir::AssetsConfig{});

    INFO("with no root given, the root is an absolute directory called assets");
    REQUIRE(test.store.Root().is_absolute());
    REQUIRE(test.store.Root().filename() == "assets");
}

TEST_CASE("A missing file's diagnostic names the path and the reason", "[assets][loading][diagnostics]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> missing =
        test.store.Load<Fenrir::Image>("missing.png", Fenrir::AssetOwner::App());

    INFO("the load fails with no handle");
    REQUIRE_FALSE(missing.Ok());
    REQUIRE(missing.handle.IsEmpty());
    INFO("the diagnostic names the path resolved against the asset root");
    REQUIRE(missing.diagnostic->path == Generic(Fixtures() / "missing.png"));
    INFO("the reason is that there is no file");
    REQUIRE(missing.diagnostic->reason == Fenrir::LoadFailure::FileNotFound);
    REQUIRE_FALSE(missing.diagnostic->importerMessage.empty());
    INFO("the failure is logged with its path");
    REQUIRE(test.errors->size() == 1);
    REQUIRE(Contains(test.errors->front(), "missing.png"));
}

TEST_CASE("A corrupt PNG's diagnostic carries the importer's message", "[assets][loading][diagnostics]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> corrupt =
        test.store.Load<Fenrir::Image>("corrupt.png", Fenrir::AssetOwner::App());

    INFO("the load fails with no handle");
    REQUIRE_FALSE(corrupt.Ok());
    REQUIRE(corrupt.handle.IsEmpty());
    INFO("the diagnostic names the file");
    REQUIRE(corrupt.diagnostic->path == Generic(Fixtures() / "corrupt.png"));
    INFO("the reason is that the importer could not read it, in the importer's words");
    REQUIRE(corrupt.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
    REQUIRE_FALSE(corrupt.diagnostic->importerMessage.empty());
    INFO("the logged error carries the importer's message");
    REQUIRE(test.errors->size() == 1);
    REQUIRE(Contains(test.errors->front(), corrupt.diagnostic->importerMessage));
}

TEST_CASE("A label after # names a sub-asset of the file", "[assets][loading][labels]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> labelled =
        test.store.Load<Fenrir::Image>("fourbytwo.png#Face", Fenrir::AssetOwner::App());

    INFO("an image file has no sub-assets, so the label is not found");
    REQUIRE_FALSE(labelled.Ok());
    REQUIRE(labelled.diagnostic->reason == Fenrir::LoadFailure::LabelNotFound);
    INFO("the diagnostic names the file and the label");
    REQUIRE(labelled.diagnostic->path == Generic(Fixtures() / "fourbytwo.png") + "#Face");
}

TEST_CASE("A label on a missing file reports the missing file", "[assets][loading][labels]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Image> missing =
        test.store.Load<Fenrir::Image>("missing.png#Face", Fenrir::AssetOwner::App());

    INFO("the file part is looked for first");
    REQUIRE(missing.diagnostic->reason == Fenrir::LoadFailure::FileNotFound);
    REQUIRE(missing.diagnostic->path == Generic(Fixtures() / "missing.png") + "#Face");
}
