// A loaded mesh's positions from C# (the C# reach spec's ticket 01): Assets.GetMeshPositions copies the positions of
// the CPU copy the store keeps, in the shape of the mesh LOD readers (MeshLods.cpp): an empty span asks the count, a
// short span takes as many as fit and still gives the count, and a mesh whose CPU copy is gone reads as unreadable, not
// as a mesh of no positions.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "AssetsScriptingApp.hpp"
#include "FenrirAssets/MeshData.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;

namespace
{
    constexpr const char* PositionCallsType = "AssetsScriptFixture.PositionCalls";

    // The fixture model's first mesh, loaded from C#, with its CPU copy in the store.
    AssetHandle<Mesh> LoadMesh(AssetsScriptingApp& test)
    {
        const auto mesh = HandleFrom<Mesh>(test.Call<std::uint64_t>(
            AssetCallsType, "LoadMesh", std::string_view("models/scene.gltf#Mesh0"), appOwner));
        INFO("The fixture model's mesh should load, with positions to read");
        REQUIRE(test.Store().IsValid(mesh));
        REQUIRE(test.Store().TryGet(mesh)->positions.size() >= 3);
        return mesh;
    }

    // Whether C#'s component `axis` of position `at`, read as `read` does, is the store's.
    template <typename Read>
    void RequireComponents(const Math::Vec3& expected, Read read)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            CAPTURE(axis);
            REQUIRE(std::equal_to<float>{}(read(axis), expected[axis]));
        }
    }
} // namespace

TEST_CASE("C# reads a loaded mesh's positions, sizing the span by the count", "[assets][scripting][positions]")
{
    AssetsScriptingApp test;
    const AssetHandle<Mesh> mesh = LoadMesh(test);
    const MeshData& data = *test.Store().TryGet(mesh);
    {
        INFO("An empty span should ask how many positions the mesh holds");
        REQUIRE(test.Call<int>(PositionCallsType, "Count", BitsOf(mesh)) == static_cast<int>(data.positions.size()));
    }
    INFO("Every position C# reads into a span of that size should be the store's, in vertex order");
    for (std::size_t at = 0; at < data.positions.size(); ++at)
    {
        CAPTURE(at);
        RequireComponents(data.positions[at], [&](int axis) {
            return test.Call<float>(PositionCallsType, "At", BitsOf(mesh), static_cast<int>(at), axis);
        });
    }
}

TEST_CASE("A span too short for a mesh's positions takes as many as fit and still gives the count",
          "[assets][scripting][positions]")
{
    AssetsScriptingApp test;
    const AssetHandle<Mesh> mesh = LoadMesh(test);
    const MeshData& data = *test.Store().TryGet(mesh);
    const int count = static_cast<int>(data.positions.size());
    {
        INFO("A span of two should still be told the whole count, as the LOD readers tell it");
        REQUIRE(test.Call<int>(PositionCallsType, "ReadInto", BitsOf(mesh), 2) == count);
    }
    {
        INFO("The two entries that fit should hold the first two positions");
        RequireComponents(data.positions[0], [&](int axis) {
            return test.Call<float>(PositionCallsType, "EntryAfterRead", BitsOf(mesh), 2, 0, axis);
        });
        RequireComponents(data.positions[1], [&](int axis) {
            return test.Call<float>(PositionCallsType, "EntryAfterRead", BitsOf(mesh), 2, 1, axis);
        });
    }
    INFO("A span longer than the positions should be written only as far as the count");
    RequireComponents(Math::Vec3(-7.0f), [&](int axis) {
        return test.Call<float>(PositionCallsType, "EntryAfterRead", BitsOf(mesh), count + 1, count, axis);
    });
}

TEST_CASE("A mesh whose CPU copy is gone reads as unreadable, not as a mesh of no positions",
          "[assets][scripting][positions]")
{
    AssetsScriptingApp test;
    const AssetHandle<Mesh> mesh = LoadMesh(test);
    REQUIRE(test.Store().Unload(mesh));
    {
        INFO("An unloaded mesh has no CPU copy: the count and a read should both say unreadable, writing nothing");
        REQUIRE(test.Call<int>(PositionCallsType, "Count", BitsOf(mesh)) == -1);
        REQUIRE(test.Call<int>(PositionCallsType, "ReadInto", BitsOf(mesh), 4) == -1);
        RequireComponents(Math::Vec3(-7.0f), [&](int axis) {
            return test.Call<float>(PositionCallsType, "EntryAfterRead", BitsOf(mesh), 4, 0, axis);
        });
    }
    INFO("A live mesh of no positions is readable, and reads as none");
    const AssetHandle<Mesh> empty = test.Store().Add<Mesh>(MeshData{}, test.Get().GetActiveScene());
    REQUIRE(test.Store().IsValid(empty));
    REQUIRE(test.Call<int>(PositionCallsType, "Count", BitsOf(empty)) == 0);
}
