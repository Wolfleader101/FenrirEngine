#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::Triangle;

TEST_CASE("Add never shares an asset, even for equal data", "[assets][revisions]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> first = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());
    const Fenrir::AssetHandle<Fenrir::Mesh> second =
        test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());

    INFO("two adds of the same data are two assets");
    REQUIRE(first != second);
    REQUIRE(test.store.IsValid(first));
    REQUIRE(test.store.IsValid(second));

    REQUIRE(test.store.Unload(first));
    INFO("freeing one leaves the other");
    REQUIRE(test.store.IsValid(second));
}

TEST_CASE("Replace swaps the data and bumps the revision every reader sees", "[assets][revisions]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());

    INFO("an added asset starts at revision 1");
    REQUIRE(test.store.Revision(mesh) == 1);

    Fenrir::MeshData point;
    point.positions = {{5.0f, 5.0f, 5.0f}};
    INFO("replacing a live asset succeeds");
    REQUIRE(test.store.Replace(mesh, point));

    INFO("the revision moves on by one");
    REQUIRE(test.store.Revision(mesh) == 2);
    INFO("the same handle now reads the new data");
    REQUIRE(test.store.TryGet(mesh)->positions.size() == 1);
    REQUIRE(test.store.TryGet(mesh)->indices.empty());

    REQUIRE(test.store.Replace(mesh, Triangle()));
    INFO("each replace bumps it again");
    REQUIRE(test.store.Revision(mesh) == 3);
}

TEST_CASE("Replace refuses a stale handle", "[assets][revisions]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());
    REQUIRE(test.store.Unload(mesh));

    INFO("replacing a freed asset fails and is logged");
    REQUIRE_FALSE(test.store.Replace(mesh, Triangle()));
    REQUIRE_FALSE(test.errors->empty());
    INFO("and does not bring it back");
    REQUIRE_FALSE(test.store.IsValid(mesh));
}

TEST_CASE("A mesh's bounds are computed when it is added", "[assets][revisions][mesh]")
{
    AssetsApp test;
    Fenrir::MeshData mesh = Triangle();
    mesh.bounds = {Fenrir::Math::Vec3(-100.0f), Fenrir::Math::Vec3(100.0f)}; // wrong on purpose: the store ignores it

    const Fenrir::AssetHandle<Fenrir::Mesh> handle = test.store.Add<Fenrir::Mesh>(mesh, Fenrir::AssetOwner::App());
    const Fenrir::MeshData* stored = test.store.TryGet(handle);
    REQUIRE(stored != nullptr);

    INFO("the box runs from the triangle's lowest corner (-1, 0, 0) to its highest (2, 3, 0)");
    REQUIRE(stored->bounds.min == Fenrir::Math::Vec3(-1.0f, 0.0f, 0.0f));
    REQUIRE(stored->bounds.max == Fenrir::Math::Vec3(2.0f, 3.0f, 0.0f));
}

TEST_CASE("A mesh's bounds are computed again when it is replaced", "[assets][revisions][mesh]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> handle =
        test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());

    Fenrir::MeshData moved;
    moved.positions = {{1.0f, -2.0f, 4.0f}, {3.0f, 2.0f, -4.0f}};
    REQUIRE(test.store.Replace(handle, moved));

    const Fenrir::MeshData* stored = test.store.TryGet(handle);
    INFO("the box now runs from (1, -2, -4) to (3, 2, 4)");
    REQUIRE(stored->bounds.min == Fenrir::Math::Vec3(1.0f, -2.0f, -4.0f));
    REQUIRE(stored->bounds.max == Fenrir::Math::Vec3(3.0f, 2.0f, 4.0f));
}

TEST_CASE("A mesh with no positions has bounds at the origin", "[assets][revisions][mesh]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> handle =
        test.store.Add<Fenrir::Mesh>(Fenrir::MeshData{}, Fenrir::AssetOwner::App());

    const Fenrir::MeshData* stored = test.store.TryGet(handle);
    INFO("an empty mesh is accepted");
    REQUIRE(stored != nullptr);
    INFO("its box is a point at the origin");
    REQUIRE(stored->bounds.min == Fenrir::Math::Vec3(0.0f));
    REQUIRE(stored->bounds.max == Fenrir::Math::Vec3(0.0f));
}

TEST_CASE("A mesh whose streams or indices do not fit together is refused", "[assets][revisions][mesh]")
{
    AssetsApp test;

    Fenrir::MeshData shortNormals = Triangle();
    shortNormals.normals.pop_back();
    INFO("a stream with fewer values than positions is refused");
    REQUIRE(test.store.Add<Fenrir::Mesh>(shortNormals, Fenrir::AssetOwner::App()).IsEmpty());

    Fenrir::MeshData farIndex = Triangle();
    farIndex.indices[2] = 3;
    INFO("an index past the last vertex is refused");
    REQUIRE(test.store.Add<Fenrir::Mesh>(farIndex, Fenrir::AssetOwner::App()).IsEmpty());

    Fenrir::MeshData partTriangle = Triangle();
    partTriangle.indices.push_back(0);
    INFO("indices that are not whole triangles are refused");
    REQUIRE(test.store.Add<Fenrir::Mesh>(partTriangle, Fenrir::AssetOwner::App()).IsEmpty());

    Fenrir::MeshData longSubmesh = Triangle();
    longSubmesh.submeshes = {{0, 6, 0}};
    INFO("a submesh reaching past the indices is refused");
    REQUIRE(test.store.Add<Fenrir::Mesh>(longSubmesh, Fenrir::AssetOwner::App()).IsEmpty());

    INFO("each refusal is logged");
    REQUIRE(test.errors->size() == 4);
}
