// Meshes from C# (spec: "Scripting: the extensions", Fenrir.Assets): AddMesh and ReplaceMesh take spans and the store
// computes the bounds; a replace bumps the revision C++ sees.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "AssetsScriptingApp.hpp"
#include "FenrirAssets/MeshData.hpp"

using namespace Fenrir;
using namespace Fenrir::Testing;
using Math::Vec2;
using Math::Vec3;

TEST_CASE("A C# system adds a mesh with computed bounds, then replaces it, bumping the revision C++ sees",
          "[assets][scripting]")
{
    AssetsScriptingApp test;
    test.Call<void>(MeshBuilderType, "Arm");
    test.Get().RunFrame(oneTick);

    const auto mesh = HandleFrom<Mesh>(test.Call<std::uint64_t>(MeshBuilderType, "AddedMeshHandle"));
    {
        const MeshData* added = test.Store().TryGet(mesh);
        INFO("The C# system's triangle was not added with its streams, one submesh and bounds from (-1, 0, 0) to "
             "(2, 3, 0)");
        REQUIRE(added != nullptr);
        REQUIRE(added->positions == std::vector<Vec3>{{-1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 3.0f, 0.0f}});
        REQUIRE(added->normals == std::vector<Vec3>(3, Vec3(0.0f, 0.0f, 1.0f)));
        REQUIRE(added->uv0 == std::vector<Vec2>{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}});
        REQUIRE(added->indices == std::vector<std::uint32_t>{0, 1, 2});
        REQUIRE(added->submeshes.size() == 1);
        REQUIRE(added->submeshes[0].indexOffset == 0);
        REQUIRE(added->submeshes[0].indexCount == 3);
        REQUIRE(added->bounds.min == Vec3(-1.0f, 0.0f, 0.0f));
        REQUIRE(added->bounds.max == Vec3(2.0f, 3.0f, 0.0f));
        REQUIRE(test.Store().Revision(mesh) == 1);
    }

    test.Get().RunFrame(oneTick);
    {
        INFO("The C# system's replace did not succeed");
        REQUIRE(test.Call<bool>(MeshBuilderType, "Replaced"));
    }
    const MeshData* replaced = test.Store().TryGet(mesh);
    INFO("The replaced mesh is not the quad, with no normals or UVs, bounds from (-2, -1, 0) to (4, 5, 1), and its "
         "revision bumped to 2");
    REQUIRE(replaced != nullptr);
    REQUIRE(replaced->positions.size() == 4);
    REQUIRE(replaced->normals.empty());
    REQUIRE(replaced->uv0.empty());
    REQUIRE(replaced->indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    REQUIRE(replaced->submeshes.size() == 1);
    REQUIRE(replaced->submeshes[0].indexCount == 6);
    REQUIRE(replaced->bounds.min == Vec3(-2.0f, -1.0f, 0.0f));
    REQUIRE(replaced->bounds.max == Vec3(4.0f, 5.0f, 1.0f));
    REQUIRE(test.Store().Revision(mesh) == 2);
}

TEST_CASE("A mesh the store refuses gives C# the empty handle", "[assets][scripting]")
{
    AssetsScriptingApp test;
    {
        INFO("A mesh with an index past its vertices was added");
        REQUIRE(test.Call<std::uint64_t>(AssetCallsType, "AddBrokenMesh") == 0);
    }
    INFO("The store did not log why it refused the mesh");
    REQUIRE(LoggedWith(test.headless, Testing::LogLevel::Error, "past its 3 vertices") == 1);
}
