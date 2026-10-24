#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "AssetsTest.hpp"

using AssetsTest::AssetsApp;
using AssetsTest::Triangle;

TEST_CASE("An asset owned by the app outlives the scenes", "[assets][owners]")
{
    AssetsApp test;
    test.app.CreateScene("Level");
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());

    test.app.DestroyScene("Level");

    INFO("an app-owned mesh still resolves after a scene ends");
    REQUIRE(test.store.IsValid(mesh));
    REQUIRE(test.store.TryGet(mesh) != nullptr);
}

TEST_CASE("An asset owned by a scene is freed with the scene", "[assets][owners]")
{
    AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.store.Add<Fenrir::Mesh>(Triangle(), level);

    INFO("the mesh resolves while its scene lives");
    REQUIRE(test.store.IsValid(mesh));

    test.app.DestroyScene("Level");

    INFO("the mesh's handle is stale once its scene ends");
    REQUIRE_FALSE(test.store.IsValid(mesh));
    REQUIRE(test.store.TryGet(mesh) == nullptr);
}

TEST_CASE("An asset owned by two scenes lives until both end", "[assets][owners]")
{
    AssetsApp test;
    Fenrir::Scene& first = test.app.CreateScene("First");
    Fenrir::Scene& second = test.app.CreateScene("Second");
    const Fenrir::LoadResult<Fenrir::Image> fromFirst = test.store.Load<Fenrir::Image>("fourbytwo.png", first);
    const Fenrir::LoadResult<Fenrir::Image> fromSecond = test.store.Load<Fenrir::Image>("fourbytwo.png", second);
    REQUIRE(fromFirst.Ok());
    REQUIRE(fromSecond.Ok());

    test.app.DestroyScene("First");

    INFO("the image lives on while the second scene still owns it");
    REQUIRE(test.store.IsValid(fromFirst.handle));

    test.app.DestroyScene("Second");

    INFO("the image is freed once its last owner ends");
    REQUIRE_FALSE(test.store.IsValid(fromFirst.handle));
}

TEST_CASE("Unload frees an asset and its old handle resolves as stale", "[assets][owners]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());

    INFO("unloading a live asset succeeds");
    REQUIRE(test.store.Unload(mesh));

    INFO("the unloaded mesh's handle no longer resolves");
    REQUIRE_FALSE(test.store.IsValid(mesh));
    REQUIRE(test.store.TryGet(mesh) == nullptr);
    REQUIRE(test.store.Revision(mesh) == 0);

    INFO("unloading it again finds nothing");
    REQUIRE_FALSE(test.store.Unload(mesh));

    const Fenrir::AssetHandle<Fenrir::Mesh> next = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());
    INFO("a new asset never brings an old handle back to life");
    REQUIRE(next != mesh);
    REQUIRE(test.store.IsValid(next));
    REQUIRE_FALSE(test.store.IsValid(mesh));
}

TEST_CASE("An empty handle never resolves", "[assets][handles]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> empty;
    REQUIRE(test.store.IsValid(test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App())));

    INFO("the default handle names no asset, even when the store holds one");
    REQUIRE_FALSE(test.store.IsValid(empty));
    REQUIRE(test.store.TryGet(empty) == nullptr);
}

TEST_CASE("A handle of one kind does not resolve an asset of another", "[assets][handles]")
{
    AssetsApp test;
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.store.Add<Fenrir::Mesh>(Triangle(), Fenrir::AssetOwner::App());
    const Fenrir::AssetHandle<Fenrir::Image> sameNumbers{mesh.index, mesh.generation};

    INFO("each kind of asset has its own slots");
    REQUIRE_FALSE(test.store.IsValid(sameNumbers));
}

TEST_CASE("Destroying the owner frees every asset, and scenes that end later free nothing", "[assets][owners]")
{
    AssetsTest::AssetsApp test;
    Fenrir::Scene& level = test.app.CreateScene("Level");
    const Fenrir::AssetHandle<Fenrir::Mesh> mesh = test.store.Add<Fenrir::Mesh>(Triangle(), level);

    {
        Fenrir::Assets ended = std::move(test.assets);
    }

    INFO("the moved-from owner is no longer valid");
    REQUIRE_FALSE(test.assets.IsValid());
    INFO("the asset store is gone with its owner");
    REQUIRE(test.app.TryGetResource<Fenrir::AssetStore>() == nullptr);

    // The scene that owned the mesh ends after the store: it must find nothing to free.
    test.app.DestroyScene("Level");
    (void)mesh;
}

TEST_CASE("A stale handle is logged once per handle", "[assets][handles]")
{
    Fenrir::StaleHandleLog log;
    const Fenrir::AssetHandle<Fenrir::Mesh> stale{3, 2};
    const Fenrir::AssetHandle<Fenrir::Mesh> older{3, 1};
    const Fenrir::AssetHandle<Fenrir::Image> image{3, 2};

    INFO("the first meeting with a handle is logged");
    REQUIRE(log.ShouldLog(stale));
    INFO("meeting it again is not");
    REQUIRE_FALSE(log.ShouldLog(stale));
    INFO("another generation of the same slot is another handle");
    REQUIRE(log.ShouldLog(older));
    INFO("the same numbers for another kind of asset are another handle");
    REQUIRE(log.ShouldLog(image));

    log.Clear();
    INFO("after Clear, every handle is logged again");
    REQUIRE(log.ShouldLog(stale));
}
