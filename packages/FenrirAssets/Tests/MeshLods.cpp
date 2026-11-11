// Levels of detail (ADR-0057): GenerateLods's chain through meshoptimizer, and the store's checks on MeshData::lods.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::Triangle;
using Fenrir::BoneQuad;
using Fenrir::BoneWeightOne;
using Fenrir::LodSettings;
using Fenrir::MeshData;
using Fenrir::MeshLod;

namespace
{
    // A unit sphere: one vertex at each pole, then `rings - 1` latitude rings of `segments + 1` columns, with normals
    // and a UV seam. A ring's last column copies its first column's position with u of 1 instead of 0, so the seam's
    // vertices share a position exactly, as an exported model's do; the poles are single vertices, so no triangle is
    // degenerate.
    MeshData UvSphere(std::uint32_t rings, std::uint32_t segments)
    {
        MeshData mesh;
        const auto add = [&](const Fenrir::Math::Vec3& direction, float u, float v) {
            mesh.positions.push_back(direction);
            mesh.normals.push_back(direction);
            mesh.uv0.emplace_back(u, v);
        };
        add({0.0f, 1.0f, 0.0f}, 0.5f, 0.0f);
        for (std::uint32_t ring = 1; ring < rings; ++ring)
        {
            const float v = static_cast<float>(ring) / static_cast<float>(rings);
            const float polar = v * std::numbers::pi_v<float>;
            const std::size_t first = mesh.positions.size();
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segments);
                const float azimuth = u * 2.0f * std::numbers::pi_v<float>;
                add({std::sin(polar) * std::cos(azimuth), std::cos(polar), std::sin(polar) * std::sin(azimuth)}, u, v);
            }
            add(mesh.positions[first], 1.0f, v);
        }
        add({0.0f, -1.0f, 0.0f}, 0.5f, 1.0f);

        const std::uint32_t columns = segments + 1;
        const auto ringStart = [&](std::uint32_t ring) { return 1 + (ring - 1) * columns; };
        const auto bottom = static_cast<std::uint32_t>(mesh.positions.size() - 1);
        for (std::uint32_t segment = 0; segment < segments; ++segment)
        {
            mesh.indices.insert(mesh.indices.end(), {0, ringStart(1) + segment, ringStart(1) + segment + 1});
            mesh.indices.insert(mesh.indices.end(),
                                {ringStart(rings - 1) + segment + 1, ringStart(rings - 1) + segment, bottom});
        }
        for (std::uint32_t ring = 1; ring + 1 < rings; ++ring)
        {
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const std::uint32_t a = ringStart(ring) + segment;
                const std::uint32_t b = a + columns;
                mesh.indices.insert(mesh.indices.end(), {a + 1, a, b, a + 1, b, b + 1});
            }
        }
        mesh.submeshes = {{0, static_cast<std::uint32_t>(mesh.indices.size()), 0}};
        return mesh;
    }

    MeshData PositionsOnly(MeshData mesh)
    {
        mesh.normals.clear();
        mesh.uv0.clear();
        return mesh;
    }

    // A unit cube of eight corners and twelve triangles, positions only.
    MeshData Cube()
    {
        MeshData mesh;
        for (std::uint32_t corner = 0; corner < 8; ++corner)
        {
            mesh.positions.emplace_back((corner & 1) != 0 ? 1.0f : -1.0f, (corner & 2) != 0 ? 1.0f : -1.0f,
                                        (corner & 4) != 0 ? 1.0f : -1.0f);
        }
        mesh.indices = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                        2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};
        return mesh;
    }

    // The cube with a vertex per face corner and flat normals: every position is shared by three vertices.
    MeshData FlatCube()
    {
        const MeshData shared = Cube();
        MeshData mesh;
        for (std::size_t triangle = 0; triangle < shared.indices.size() / 3; ++triangle)
        {
            const Fenrir::Math::Vec3 a = shared.positions[shared.indices[triangle * 3]];
            const Fenrir::Math::Vec3 b = shared.positions[shared.indices[triangle * 3 + 1]];
            const Fenrir::Math::Vec3 c = shared.positions[shared.indices[triangle * 3 + 2]];
            const Fenrir::Math::Vec3 normal = glm::normalize(glm::cross(b - a, c - a));
            for (const Fenrir::Math::Vec3& position : {a, b, c})
            {
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.positions.size()));
                mesh.positions.push_back(position);
                mesh.normals.push_back(normal);
            }
        }
        return mesh;
    }

    std::size_t Triangles(const std::vector<std::uint32_t>& indices)
    {
        return indices.size() / 3;
    }

    bool InRange(const std::vector<std::uint32_t>& indices, std::size_t vertices)
    {
        return std::all_of(indices.begin(), indices.end(), [&](std::uint32_t index) { return index < vertices; });
    }

    // Each level has fewer triangles than the last, a larger error, and only indices into the mesh's vertices.
    void RequireWellFormedChain(const MeshData& mesh)
    {
        std::size_t previousTriangles = Triangles(mesh.indices);
        float previousError = 0.0f;
        for (const MeshLod& lod : mesh.lods)
        {
            INFO("each level is whole triangles, fewer than the level before, with a larger error, all in range");
            REQUIRE(lod.indices.size() % 3 == 0);
            REQUIRE(Triangles(lod.indices) < previousTriangles);
            REQUIRE(lod.error > previousError);
            REQUIRE(InRange(lod.indices, mesh.positions.size()));
            previousTriangles = Triangles(lod.indices);
            previousError = lod.error;
        }
    }

    // The vertices a triangle list names whose position another vertex of the mesh shares.
    std::set<std::uint32_t> SeamVertices(const MeshData& mesh, const std::vector<std::uint32_t>& indices)
    {
        std::set<std::uint32_t> seam;
        for (const std::uint32_t index : indices)
        {
            const Fenrir::Math::Vec3& position = mesh.positions[index];
            const auto shared = std::count(mesh.positions.begin(), mesh.positions.end(), position);
            if (shared > 1)
            {
                seam.insert(index);
            }
        }
        return seam;
    }

    // The sphere with two levels of detail that pass the store's checks.
    MeshData SphereWithLevels()
    {
        MeshData mesh = UvSphere(8, 16);
        MeshLod coarse;
        coarse.indices = {0, 17, 1, 1, 17, 18};
        coarse.error = 0.5f;
        MeshLod coarser;
        coarser.indices = {0, 17, 1};
        coarser.error = 1.0f;
        mesh.lods = {coarse, coarser};
        return mesh;
    }
} // namespace

TEST_CASE("A subdivided sphere's levels fall in triangles and rise in error", "[assets][lods]")
{
    const MeshData sphere = UvSphere(24, 48);
    const std::size_t full = Triangles(sphere.indices);
    REQUIRE(full == 2 * 48 + 22 * 48 * 2);

    SECTION("with normals and texture coordinates")
    {
        const MeshData result = Fenrir::GenerateLods(sphere);
        INFO("the defaults make four levels: 2208 -> 1104 -> 552 -> 276 -> 138 triangles, each at most 55 % of the "
             "last to leave the simplifier room to stop a few triangles short");
        REQUIRE(result.lods.size() == 4);
        RequireWellFormedChain(result);
        REQUIRE(Triangles(result.lods[0].indices) <= full * 11 / 20);
        REQUIRE(Triangles(result.lods[3].indices) >= 32);

        INFO("the error is in the mesh's units: a unit sphere departs by less than its radius");
        REQUIRE(result.lods.back().error < 1.0f);
        REQUIRE(result.lods.back().error > 0.0f);
    }

    SECTION("with positions alone")
    {
        const MeshData result = Fenrir::GenerateLods(PositionsOnly(sphere));
        INFO("without attributes the chain goes through meshopt_simplify and has the same shape");
        REQUIRE(result.lods.size() == 4);
        RequireWellFormedChain(result);
        REQUIRE(Triangles(result.lods[0].indices) <= full * 11 / 20);
    }
}

TEST_CASE("A level's error is a distance in the mesh's units", "[assets][lods]")
{
    const MeshData sphere = UvSphere(24, 48);
    MeshData larger = sphere;
    for (Fenrir::Math::Vec3& position : larger.positions)
    {
        position *= 10.0f;
    }
    const MeshData result = Fenrir::GenerateLods(sphere);
    const MeshData largerResult = Fenrir::GenerateLods(larger);
    REQUIRE(result.lods.size() == largerResult.lods.size());
    for (std::size_t level = 0; level < result.lods.size(); ++level)
    {
        INFO("the same sphere ten times the size departs ten times as far, at level "
             << level + 1 << " (within 5 %: float rounding can change the simplifier's collapse order)");
        REQUIRE(largerResult.lods[level].error > 9.5f * result.lods[level].error);
        REQUIRE(largerResult.lods[level].error < 10.5f * result.lods[level].error);
    }
}

TEST_CASE("GenerateLods keeps the mesh and reorders its indices for the vertex cache", "[assets][lods]")
{
    const MeshData sphere = UvSphere(24, 48);
    const MeshData result = Fenrir::GenerateLods(sphere);

    INFO("the full-detail indices are as many as before, over the same vertices");
    REQUIRE(result.indices.size() == sphere.indices.size());
    REQUIRE(std::set<std::uint32_t>(result.indices.begin(), result.indices.end()) ==
            std::set<std::uint32_t>(sphere.indices.begin(), sphere.indices.end()));

    INFO("the streams, submeshes and bounds are as they were");
    REQUIRE(result.positions == sphere.positions);
    REQUIRE(result.normals == sphere.normals);
    REQUIRE(result.uv0 == sphere.uv0);
    REQUIRE(result.submeshes.size() == 1);
    REQUIRE(result.submeshes[0].indexCount == sphere.submeshes[0].indexCount);
}

TEST_CASE("A generated chain passes the store's checks and is kept", "[assets][lods]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Mesh>(Fenrir::GenerateLods(UvSphere(24, 48)), Fenrir::AssetOwner::App());
    INFO("the store accepts what GenerateLods makes, with no error logged");
    REQUIRE(test.store.IsValid(handle));
    REQUIRE(test.errors->empty());
    REQUIRE(test.store.TryGet(handle)->lods.size() == 4);
}

TEST_CASE("The chain stops at each limit", "[assets][lods]")
{
    const MeshData sphere = UvSphere(24, 48);

    SECTION("maxLevels caps the count")
    {
        LodSettings settings;
        settings.maxLevels = 2;
        const MeshData result = Fenrir::GenerateLods(sphere, settings);
        INFO("two levels asked for, two made, though the sphere could go further");
        REQUIRE(result.lods.size() == 2);
        RequireWellFormedChain(result);
    }
    SECTION("no level is coarser than minTriangles, and a level at it is the last")
    {
        LodSettings settings;
        settings.maxLevels = 10;
        settings.minTriangles = 400;
        const MeshData result = Fenrir::GenerateLods(sphere, settings);
        INFO("2208 -> 1104 -> 552 -> 400 triangles, then the chain stops at the floor; the last level may stop a "
             "tenth short of its target, never below it");
        REQUIRE(result.lods.size() == 3);
        RequireWellFormedChain(result);
        REQUIRE(Triangles(result.lods.back().indices) >= 400);
        REQUIRE(Triangles(result.lods.back().indices) <= 440);
    }
    SECTION("minTriangles at or above the mesh makes no levels")
    {
        LodSettings settings;
        settings.minTriangles = static_cast<std::uint32_t>(Triangles(sphere.indices));
        INFO("the mesh is already at the floor, so there is no level to make");
        REQUIRE(Fenrir::GenerateLods(sphere, settings).lods.empty());
    }
    SECTION("a ratio that saves less than a tenth makes no levels")
    {
        LodSettings settings;
        settings.ratio = 0.95f;
        INFO("a level aiming at 95 % of the triangles cannot save a tenth, so it is not worth a draw");
        REQUIRE(Fenrir::GenerateLods(sphere, settings).lods.empty());
    }
    SECTION("zero levels asked for is zero levels")
    {
        LodSettings settings;
        settings.maxLevels = 0;
        INFO("maxLevels of 0 asks for full detail only");
        REQUIRE(Fenrir::GenerateLods(sphere, settings).lods.empty());
    }
}

TEST_CASE("A cube has nothing to remove and gets no levels", "[assets][lods]")
{
    INFO("twelve triangles are already under the default floor");
    REQUIRE(Fenrir::GenerateLods(Cube()).lods.empty());

    INFO("asked to go further, a cube's corners are all it has: no level saves a tenth without one");
    LodSettings settings;
    settings.minTriangles = 1;
    const MeshData flat = Fenrir::GenerateLods(FlatCube(), settings);
    REQUIRE(flat.lods.empty());
    REQUIRE(Triangles(flat.indices) == 12);
}

TEST_CASE("A mesh the store would refuse levels on comes back unchanged", "[assets][lods]")
{
    LodSettings settings;
    settings.minTriangles = 1;

    INFO("a mesh with no triangles has no levels");
    MeshData empty;
    REQUIRE(Fenrir::GenerateLods(empty).lods.empty());

    INFO("a mesh with more than one submesh gets no levels and keeps its index order");
    MeshData twoSubmeshes = UvSphere(24, 48);
    twoSubmeshes.submeshes = {{0, 6, 0}, {6, 6, 1}};
    REQUIRE(Fenrir::GenerateLods(twoSubmeshes, settings).lods.empty());
    REQUIRE(Fenrir::GenerateLods(twoSubmeshes, settings).indices == twoSubmeshes.indices);

    INFO("so does a lone submesh that covers only some of the indices, which reordering them would scramble");
    MeshData partial = UvSphere(24, 48);
    partial.submeshes = {{0, 6, 0}};
    REQUIRE(Fenrir::GenerateLods(partial, settings).lods.empty());
    REQUIRE(Fenrir::GenerateLods(partial, settings).indices == partial.indices);

    INFO("and a mesh with an index past its positions, which the simplifier must never read");
    MeshData broken = UvSphere(24, 48);
    broken.indices[5] = static_cast<std::uint32_t>(broken.positions.size());
    REQUIRE(Fenrir::GenerateLods(broken, settings).lods.empty());
    REQUIRE(Fenrir::GenerateLods(broken, settings).indices == broken.indices);
}

TEST_CASE("A skinned mesh keeps its skin, streams and bone bounds through its levels", "[assets][lods][skins]")
{
    MeshData sphere = UvSphere(12, 24);
    Fenrir::MeshSkin skin;
    for (std::size_t vertex = 0; vertex < sphere.positions.size(); ++vertex)
    {
        // The top half rides bone 0, the bottom half bone 1.
        const std::uint16_t bone = sphere.positions[vertex].y >= 0.0f ? 0 : 1;
        skin.joints.push_back(BoneQuad{bone, 0, 0, 0});
        skin.weights.push_back(BoneQuad{BoneWeightOne, 0, 0, 0});
    }
    sphere.skin = skin;

    const MeshData result = Fenrir::GenerateLods(sphere);
    INFO("the skinned sphere simplifies like any other");
    REQUIRE_FALSE(result.lods.empty());
    RequireWellFormedChain(result);
    INFO("the skin is the one given: a level indexes the same vertices, so it needs no skin of its own");
    REQUIRE(result.skin.has_value());
    REQUIRE(result.skin->joints == skin.joints);
    REQUIRE(result.skin->weights == skin.weights);
    REQUIRE(result.positions == sphere.positions);

    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Mesh>(result, Fenrir::AssetOwner::App());
    INFO("the store takes the skinned mesh with its levels");
    REQUIRE(test.store.IsValid(handle));
    const Fenrir::MeshSkin& stored = *test.store.TryGet(handle)->skin;
    INFO("the store computes the bone bounds over every vertex, as for a mesh with no levels");
    REQUIRE(stored.boneCount == 2);
    REQUIRE(stored.boneBounds.size() == 2);
    REQUIRE(stored.boneBounds[0]->max.y > 0.99f);
    REQUIRE(stored.boneBounds[1]->min.y < -0.99f);
    REQUIRE(test.store.TryGet(handle)->lods.size() == result.lods.size());
}

TEST_CASE("UV seams stay where they were", "[assets][lods]")
{
    const MeshData sphere = UvSphere(24, 48);
    const MeshData result = Fenrir::GenerateLods(sphere);
    INFO("the sphere simplifies, so there are levels to look at");
    REQUIRE_FALSE(result.lods.empty());

    INFO("the sphere's seam is the first and last column of each of its 23 rings");
    const std::set<std::uint32_t> seam = SeamVertices(sphere, sphere.indices);
    REQUIRE(seam.size() == 2 * 23);

    INFO("every level names every seam vertex, so no triangle was collapsed across the seam");
    for (const MeshLod& lod : result.lods)
    {
        REQUIRE(SeamVertices(sphere, lod.indices) == seam);
    }
    INFO("and the levels did simplify away from the seam");
    REQUIRE(Triangles(result.lods.back().indices) < Triangles(sphere.indices) / 4);
}

TEST_CASE("With lockSeams off a seam collapses along itself and the chain goes further", "[assets][lods]")
{
    const MeshData sphere = UvSphere(24, 48);
    Fenrir::LodSettings settings;
    settings.maxLevels = 8;
    settings.minTriangles = 4;
    const MeshData locked = Fenrir::GenerateLods(sphere, settings);
    settings.lockSeams = false;
    const MeshData unlocked = Fenrir::GenerateLods(sphere, settings);
    REQUIRE_FALSE(locked.lods.empty());
    REQUIRE_FALSE(unlocked.lods.empty());

    const std::set<std::uint32_t> seam = SeamVertices(sphere, sphere.indices);
    INFO("the unlocked chain's last level names fewer of the seam's 46 vertices: the seam shortened along itself");
    const std::set<std::uint32_t> left = SeamVertices(sphere, unlocked.lods.back().indices);
    REQUIRE(left.size() < seam.size());
    INFO("but only seam vertices, since a seam's two sides collapse together: no vertex crossed to the other side");
    REQUIRE(std::includes(seam.begin(), seam.end(), left.begin(), left.end()));
    INFO("and with the seam free the chain reaches fewer triangles than the locked chain, which stalls on it");
    REQUIRE(Triangles(unlocked.lods.back().indices) < Triangles(locked.lods.back().indices));
    for (const MeshLod& lod : unlocked.lods)
    {
        REQUIRE(std::all_of(lod.indices.begin(), lod.indices.end(),
                            [&](std::uint32_t index) { return index < sphere.positions.size(); }));
    }
}

TEST_CASE("Adding a mesh refuses a malformed level of detail, naming it", "[assets][lods]")
{
    const auto refusal = [](MeshData mesh) {
        AssetsApp test;
        const auto handle = test.store.Add<Fenrir::Mesh>(std::move(mesh), Fenrir::AssetOwner::App());
        INFO("the mesh is refused with one error");
        REQUIRE(handle.IsEmpty());
        REQUIRE(test.errors->size() == 1);
        return test.errors->front();
    };

    SECTION("an index past the vertices")
    {
        MeshData mesh = SphereWithLevels();
        INFO("the message names the level and the overrun");
        mesh.lods[1].indices[2] = static_cast<std::uint32_t>(mesh.positions.size());
        const std::string message = refusal(mesh);
        REQUIRE(message.find("level of detail 2") != std::string::npos);
        REQUIRE(message.find("past its") != std::string::npos);
    }
    SECTION("a count that is not whole triangles")
    {
        MeshData mesh = SphereWithLevels();
        INFO("the message names the level and the count");
        mesh.lods[0].indices.push_back(0);
        const std::string message = refusal(mesh);
        REQUIRE(message.find("level of detail 1") != std::string::npos);
        REQUIRE(message.find("whole triangles") != std::string::npos);
    }
    SECTION("an error that does not grow")
    {
        MeshData mesh = SphereWithLevels();
        INFO("the message names both levels' errors");
        mesh.lods[1].error = mesh.lods[0].error;
        const std::string message = refusal(mesh);
        REQUIRE(message.find("level of detail 2") != std::string::npos);
        REQUIRE(message.find("not greater than level 1") != std::string::npos);
    }
    SECTION("a first level with no error")
    {
        MeshData mesh = SphereWithLevels();
        INFO("level 1 must exceed level 0's error of 0");
        mesh.lods[0].error = 0.0f;
        REQUIRE(refusal(mesh).find("level of detail 1") != std::string::npos);
    }
    SECTION("levels on a mesh with more than one submesh")
    {
        MeshData mesh = SphereWithLevels();
        INFO("the message counts the levels and the submeshes");
        mesh.submeshes = {{0, 6, 0}, {6, 6, 1}};
        const std::string message = refusal(mesh);
        REQUIRE(message.find("2 levels of detail") != std::string::npos);
        REQUIRE(message.find("2 submeshes") != std::string::npos);
    }
}

TEST_CASE("Replace keeps good levels and refuses bad ones", "[assets][lods]")
{
    AssetsApp test;
    const auto handle = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());
    INFO("a mesh added with no levels has none");
    REQUIRE(test.store.TryGet(handle)->lods.empty());

    INFO("good levels replace the mesh and are kept as given");
    const MeshData withLevels = SphereWithLevels();
    REQUIRE(test.store.Replace(handle, withLevels));
    REQUIRE(test.store.Revision(handle) == 2);
    REQUIRE(test.store.TryGet(handle)->lods.size() == 2);
    REQUIRE(test.store.TryGet(handle)->lods[1].indices == withLevels.lods[1].indices);
    REQUIRE(test.store.TryGet(handle)->lods[1].error == 1.0f);

    INFO("a malformed level leaves the mesh as it was");
    MeshData broken = SphereWithLevels();
    broken.lods[0].indices.pop_back();
    REQUIRE_FALSE(test.store.Replace(handle, broken));
    REQUIRE(test.errors->size() == 1);
    REQUIRE(test.store.Revision(handle) == 2);
    REQUIRE(test.store.TryGet(handle)->lods.size() == 2);

    INFO("a mesh with no levels replaces one with levels");
    REQUIRE(test.store.Replace(handle, Triangle()));
    REQUIRE(test.store.TryGet(handle)->lods.empty());
}
