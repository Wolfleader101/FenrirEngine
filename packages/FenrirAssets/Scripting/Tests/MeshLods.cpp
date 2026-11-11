// A mesh's levels of detail from C# (the culling spec's ticket 09, ADR-0057): GenerateLods from C# gives a mesh the
// levels C++ would under the same settings, C#'s default LodSettings are C++'s, a level built by hand joins the mesh
// under the store's rules, and C# reads the levels' errors and indices back.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>

#include "AssetsScriptingApp.hpp"
#include "FenrirAssets/MeshData.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* LodCallsType = "AssetsScriptFixture.LodCalls";

    // A grid mesh C# added, with no levels yet.
    AssetHandle<Mesh> AddGrid(AssetsScriptingApp& test, int cells)
    {
        const auto mesh = HandleFrom<Mesh>(test.Call<std::uint64_t>(LodCallsType, "AddGrid", cells));
        INFO("The grid should be in the store, drawn at full detail only");
        REQUIRE(test.Store().IsValid(mesh));
        REQUIRE(test.Store().TryGet(mesh)->lods.empty());
        return mesh;
    }

    // Whether the store's mesh has exactly `expected`'s levels and reordered indices.
    void RequireLevels(AssetsScriptingApp& test, AssetHandle<Mesh> mesh, const MeshData& expected)
    {
        const MeshData* actual = test.Store().TryGet(mesh);
        INFO("The store's mesh should hold exactly the levels C++'s GenerateLods gives over the same data, and its "
             "indices reordered the same way");
        REQUIRE(actual != nullptr);
        REQUIRE(actual->indices == expected.indices);
        REQUIRE(actual->lods.size() == expected.lods.size());
        for (std::size_t level = 0; level < expected.lods.size(); ++level)
        {
            CAPTURE(level);
            REQUIRE(actual->lods[level].indices == expected.lods[level].indices);
            REQUIRE(std::equal_to<float>{}(actual->lods[level].error, expected.lods[level].error));
        }
    }
} // namespace

TEST_CASE("GenerateLods from C# gives a mesh the levels C++ would under the settings C# passed",
          "[assets][scripting][lod]")
{
    AssetsScriptingApp test;
    const AssetHandle<Mesh> mesh = AddGrid(test, 16);
    const MeshData before = *test.Store().TryGet(mesh);
    LodSettings settings;
    settings.ratio = 0.4f;
    settings.maxLevels = 3;
    settings.minTriangles = 8;
    settings.lockSeams = false;
    const MeshData expected = GenerateLods(before, settings);
    {
        INFO("The grid should simplify into a chain, or the comparison proves little");
        REQUIRE(expected.lods.size() >= 2);
    }
    {
        INFO("C#'s GenerateLods with those settings should succeed");
        REQUIRE(test.Call<bool>(LodCallsType, "Generate", BitsOf(mesh), 0.4f, 3u, 8u, false));
    }
    RequireLevels(test, mesh, expected);
    {
        INFO("The replace should bump the revision C++ sees");
        REQUIRE(test.Store().Revision(mesh) == 2);
    }
    const int count = static_cast<int>(expected.lods.size());
    {
        INFO("C# reads the levels' count and each level's error back");
        REQUIRE(test.Call<int>(LodCallsType, "LevelCount", BitsOf(mesh)) == count);
        for (int level = 0; level < count; ++level)
        {
            CAPTURE(level);
            REQUIRE(std::equal_to<float>{}(test.Call<float>(LodCallsType, "LevelError", BitsOf(mesh), level),
                                           expected.lods[static_cast<std::size_t>(level)].error));
        }
    }
    INFO("C# reads each level's indices: level 0 the mesh's own, level i its ith coarser one, none past the last");
    REQUIRE(test.Call<int>(LodCallsType, "LevelIndexCount", BitsOf(mesh), 0) ==
            static_cast<int>(expected.indices.size()));
    REQUIRE(test.Call<std::uint32_t>(LodCallsType, "LevelIndex", BitsOf(mesh), 0, 5) == expected.indices[5]);
    for (int level = 1; level <= count; ++level)
    {
        CAPTURE(level);
        const MeshLod& lod = expected.lods[static_cast<std::size_t>(level) - 1];
        REQUIRE(test.Call<int>(LodCallsType, "LevelIndexCount", BitsOf(mesh), level) ==
                static_cast<int>(lod.indices.size()));
        REQUIRE(test.Call<std::uint32_t>(LodCallsType, "LevelIndex", BitsOf(mesh), level, 2) == lod.indices[2]);
    }
    REQUIRE(test.Call<int>(LodCallsType, "LevelIndexCount", BitsOf(mesh), count + 1) == 0);
}

TEST_CASE("C#'s default LodSettings are C++'s", "[assets][scripting][lod]")
{
    AssetsScriptingApp test;
    const AssetHandle<Mesh> mesh = AddGrid(test, 12);
    const MeshData expected = GenerateLods(*test.Store().TryGet(mesh));
    {
        INFO("The grid should simplify under the defaults, or the comparison proves little");
        REQUIRE_FALSE(expected.lods.empty());
    }
    {
        INFO("C#'s GenerateLods with no settings should succeed");
        REQUIRE(test.Call<bool>(LodCallsType, "GenerateDefault", BitsOf(mesh)));
    }
    RequireLevels(test, mesh, expected);
}

TEST_CASE("A level C# builds by hand joins the mesh under the store's rules", "[assets][scripting][lod]")
{
    AssetsScriptingApp test;
    const AssetHandle<Mesh> mesh = AddGrid(test, 4);
    const MeshData before = *test.Store().TryGet(mesh);
    {
        INFO("A level of the grid's first two triangles at error 0.5 should be accepted");
        REQUIRE(test.Call<bool>(LodCallsType, "AddLevel", BitsOf(mesh), 0.5f));
    }
    {
        const MeshData* after = test.Store().TryGet(mesh);
        INFO("The mesh should hold that one level, its own indices untouched, at revision 2, and C# should count one");
        REQUIRE(after->lods.size() == 1);
        REQUIRE(after->lods[0].indices ==
                std::vector<std::uint32_t>(before.indices.begin(), before.indices.begin() + 6));
        REQUIRE(std::equal_to<float>{}(after->lods[0].error, 0.5f));
        REQUIRE(after->indices == before.indices);
        REQUIRE(test.Store().Revision(mesh) == 2);
        REQUIRE(test.Call<int>(LodCallsType, "LevelCount", BitsOf(mesh)) == 1);
    }
    {
        INFO("A second level whose error is not greater than the first's is refused, logged, and changes nothing");
        REQUIRE_FALSE(test.Call<bool>(LodCallsType, "AddLevel", BitsOf(mesh), 0.5f));
        REQUIRE(test.Store().TryGet(mesh)->lods.size() == 1);
        REQUIRE(test.Store().Revision(mesh) == 2);
        REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Error, "is not greater than") == 1);
    }
    INFO("A third at a greater error joins after the first");
    REQUIRE(test.Call<bool>(LodCallsType, "AddLevel", BitsOf(mesh), 0.75f));
    REQUIRE(test.Store().TryGet(mesh)->lods.size() == 2);
    REQUIRE(test.Call<int>(LodCallsType, "LevelCount", BitsOf(mesh)) == 2);
}
