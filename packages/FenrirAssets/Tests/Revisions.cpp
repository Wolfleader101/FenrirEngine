#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"
#include "DataAssetsTest.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::CountContaining;
using AssetsTest::DataApp;
using AssetsTest::Sword;
using AssetsTest::Triangle;
using AssetsTest::WeaponData;

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

TEST_CASE("Edit changes a data asset in place and bumps the revision every reader sees", "[assets][revisions][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    INFO("the sword loads");
    REQUIRE(loaded.Ok());
    const Sword* before = test.store.TryGet(loaded.handle);

    Sword* sword = test.store.Edit(loaded.handle, "sharpen a sword");
    INFO("Edit gives the one shared instance, mutable, and moves the revision on by one");
    REQUIRE(sword != nullptr);
    REQUIRE(sword == before);
    REQUIRE(test.store.Revision(loaded.handle) == 2);

    sword->edge = 1.0f;
    INFO("the same handle reads the change");
    REQUIRE(test.store.TryGet(loaded.handle)->edge == 1.0f);

    REQUIRE(test.store.Edit(loaded.handle, "sharpen a sword") != nullptr);
    INFO("each edit bumps it again");
    REQUIRE(test.store.Revision(loaded.handle) == 3);
}

TEST_CASE("Edit through a base type's handle edits the base part of a data asset", "[assets][revisions][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    INFO("the sword loads");
    REQUIRE(loaded.Ok());

    WeaponData* weapon = test.store.Edit(loaded.handle, "rename a weapon");
    INFO("a Sword edits as a WeaponData, bumping its revision");
    REQUIRE(weapon != nullptr);
    weapon->displayName = "Blunted";
    REQUIRE(test.store.TryGet(loaded.handle)->displayName == "Blunted");
    REQUIRE(test.store.Revision(loaded.handle) == 2);
}

TEST_CASE("Edit of a stale data asset handle gives null and logs its action", "[assets][revisions][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::AssetHandle<Sword> sword = test.store.Add<Sword>(Sword{}, Fenrir::AssetOwner::App());
    INFO("the added sword unloads, so its handle is stale");
    REQUIRE(test.store.Unload(sword));

    INFO("editing a freed data asset gives nothing, and one error says what was tried");
    REQUIRE(test.store.Edit(sword, "sharpen a sword") == nullptr);
    REQUIRE(test.errors->size() == 1);
    REQUIRE(CountContaining(*test.errors, "sharpen a sword") == 1);
}

TEST_CASE("Edit of a data asset as a type it is not gives null and is logged", "[assets][revisions][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::AssetHandle<WeaponData> weapon = test.store.Add<WeaponData>(WeaponData{}, Fenrir::AssetOwner::App());
    const Fenrir::AssetHandle<Sword> asSword{weapon.index, weapon.generation};

    INFO("a WeaponData record does not edit as a Sword, and its revision stays");
    REQUIRE(test.store.Edit(asSword, "sharpen a sword") == nullptr);
    REQUIRE(CountContaining(*test.errors, "sharpen a sword") == 1);
    REQUIRE(test.store.Revision(weapon) == 1);
}

TEST_CASE("Replace swaps a data asset's value and bumps its revision", "[assets][revisions][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<Sword> loaded =
        test.store.Load<Sword>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    INFO("the sword loads");
    REQUIRE(loaded.Ok());

    Sword blunt;
    blunt.displayName = "Blunt";
    blunt.edge = 0.1f;
    INFO("replacing a live sword with a Sword succeeds and moves the revision on by one");
    REQUIRE(test.store.Replace(loaded.handle, blunt));
    REQUIRE(test.store.Revision(loaded.handle) == 2);
    INFO("the same handle reads the new value, path and all");
    REQUIRE(test.store.TryGet(loaded.handle)->displayName == "Blunt");
    REQUIRE(test.store.TryGet(loaded.handle)->edge == 0.1f);
    REQUIRE(test.store.PathOf(loaded.handle) == std::optional<std::string>("data/sword.asset.json5"));
}

TEST_CASE("Replace of a data asset through a base type's handle is refused and logged", "[assets][revisions][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::LoadResult<WeaponData> loaded =
        test.store.Load<WeaponData>("data/sword.asset.json5", Fenrir::AssetOwner::App());
    INFO("the sword loads");
    REQUIRE(loaded.Ok());

    WeaponData plain;
    plain.displayName = "Plain";
    INFO("a WeaponData does not replace a Sword record: Replace takes exactly the record's type");
    REQUIRE_FALSE(test.store.Replace(loaded.handle, plain));
    REQUIRE(CountContaining(*test.errors, "AssetsTest::Sword") == 1);
    INFO("the sword is unchanged, at its first revision");
    REQUIRE(test.store.TryGet(loaded.handle)->displayName == "Longsword");
    REQUIRE(test.store.Revision(loaded.handle) == 1);
}

TEST_CASE("Replace of a stale data asset handle is refused and logged", "[assets][revisions][data]")
{
    DataApp test;
    INFO("the test package's data asset types register");
    REQUIRE(test.registered);
    const Fenrir::AssetHandle<Sword> sword = test.store.Add<Sword>(Sword{}, Fenrir::AssetOwner::App());
    INFO("the added sword unloads, so its handle is stale");
    REQUIRE(test.store.Unload(sword));

    INFO("replacing a freed data asset fails with one error, and does not bring it back");
    REQUIRE_FALSE(test.store.Replace(sword, Sword{}));
    REQUIRE(test.errors->size() == 1);
    REQUIRE_FALSE(test.store.IsValid(sword));
}
