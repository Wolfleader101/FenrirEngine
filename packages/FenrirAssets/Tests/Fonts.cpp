#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;

TEST_CASE("A font file loads as its bytes, at revision 1", "[assets][fonts]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Font> loaded =
        test.store.Load<Fenrir::Font>("fonts/tagged.ttf", Fenrir::AssetOwner::App());

    INFO("a .ttf file starting with the TrueType tag loads");
    REQUIRE(loaded.Ok());
    const Fenrir::FontData* font = test.store.TryGet(loaded.handle);
    REQUIRE(font != nullptr);
    INFO("the data is the file's 57 bytes, from its 00 01 00 00 tag");
    REQUIRE(font->bytes.size() == 57);
    REQUIRE(font->bytes[1] == std::byte{1});
    INFO("a loaded font starts at revision 1, like any asset");
    REQUIRE(test.store.Revision(loaded.handle) == 1);

    INFO("loading the path again gives the same font");
    REQUIRE(test.store.Load<Fenrir::Font>("fonts/tagged.ttf", Fenrir::AssetOwner::App()).handle == loaded.handle);
}

TEST_CASE("A file that does not start as a font does fails to load as one", "[assets][fonts]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Font> loaded =
        test.store.Load<Fenrir::Font>("fonts/plain-text.otf", Fenrir::AssetOwner::App());

    INFO("an .otf file of plain text is refused as an import failure");
    REQUIRE_FALSE(loaded.Ok());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::ImportFailed);
    REQUIRE_FALSE(test.errors->empty());
}

TEST_CASE("Only font files load as fonts", "[assets][fonts]")
{
    AssetsApp test;
    const Fenrir::LoadResult<Fenrir::Font> loaded =
        test.store.Load<Fenrir::Font>("fourbytwo.png", Fenrir::AssetOwner::App());

    INFO("a PNG has no font importer");
    REQUIRE_FALSE(loaded.Ok());
    REQUIRE(loaded.diagnostic->reason == Fenrir::LoadFailure::NoImporter);
}

TEST_CASE("Added font bytes are checked for a font's tag", "[assets][fonts]")
{
    AssetsApp test;
    Fenrir::FontData otto;
    otto.bytes = {std::byte{'O'}, std::byte{'T'}, std::byte{'T'}, std::byte{'O'}, std::byte{0}};
    INFO("bytes tagged OTTO, a CFF OpenType font, are taken");
    REQUIRE_FALSE(test.store.Add<Fenrir::Font>(otto, Fenrir::AssetOwner::App()).IsEmpty());

    Fenrir::FontData empty;
    INFO("no bytes at all are refused, with a logged error");
    REQUIRE(test.store.Add<Fenrir::Font>(empty, Fenrir::AssetOwner::App()).IsEmpty());
    REQUIRE_FALSE(test.errors->empty());
}
