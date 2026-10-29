// The no-op rules (spec: "Scripting: the extensions", no-op rules; seam 7: a missing package no-oping), with no .NET:
// through NativeCallHarness, which runs the renderer extension as shipped (Renderer::ScriptingExtension()), each native
// function is called from C++ by its registered name through the registration's own thunk. With no renderer, no camera,
// a stale entity or model, or no asset store, each does nothing and returns false, zero or no entity, leaves its output
// at the default the registration gave it (ADR-0045), nothing throws, and a debug build warns once per native and
// reason, however often it is called, in the words RendererScripting and the native guard name. SpawnModel's handle
// rule is Assets' (AssetResolve.hpp), so its warning is in Assets' words under the renderer's native.

#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirRendererScripting/RendererScripting.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "RendererScriptingState.hpp"

namespace
{
    using namespace Fenrir;

    // C#'s Entity.None: a default Entity.
    const Entity noEntity;

    EntityList& Entities(Testing::NativeCallHarness& harness)
    {
        return harness.GetApp().GetActiveScene().GetEntityList();
    }

    // Assets installed in the harness's app, its asset root FenrirRenderer's test fixtures.
    Assets InstallAssets(Testing::NativeCallHarness& harness)
    {
        return Assets::Install(harness.GetApp(), {std::filesystem::absolute(FENRIR_RENDERER_FIXTURES)});
    }

    // The renderer on the null backend, which needs no window.
    Renderer InstallRenderer(Testing::NativeCallHarness& harness)
    {
        RendererConfig config;
        config.backend = GraphicsBackend::Null;
        return Renderer::Install(harness.GetApp(), config);
    }

    // The fixture model, two_parts.gltf, loaded into `store`.
    AssetHandle<Model> LoadModel(AssetStore& store)
    {
        const AssetHandle<Model> model = store.Load<Model>("two_parts.gltf", AssetOwner::App()).handle;
        INFO("The fixture model did not load");
        REQUIRE(store.IsValid(model));
        return model;
    }

    const Math::Vec2 centre(640.0f, 360.0f);
    const Math::Vec3 origin(0.0f);
} // namespace

TEST_CASE("With no renderer, picking, bounds and stats do nothing, leave their outputs zeroed and say so once",
          "[renderer][scripting][noops]")
{
    Testing::NativeCallHarness harness({Renderer::ScriptingExtension()});
    Entity made = Entities(harness).CreateEntity();
    made.AddComponent<Camera>();
    // The natives take the entity by pointer to const, as C# passes it.
    const Entity camera = made;

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": each native should return false, zero or no entity and write nothing");
        // Each output is set to something else first: the registration zeroes it before the native runs (ADR-0045),
        // and the native then writes nothing.
        Math::Ray ray;
        ray.origin = Math::Vec3(3.0f);
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.ScreenPointToRay", &camera, &centre, &ray));
        REQUIRE(ray.origin == Math::Ray{}.origin);
        Math::Vec2 screenPoint(3.0f);
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.WorldToScreenPoint", &camera, &origin, &screenPoint));
        REQUIRE(screenPoint == Math::Vec2(0.0f));
        Entity found;
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.CameraAt", WindowId{1}, &centre, &found));
        REQUIRE(found == noEntity);
        Math::AABB bounds{};
        bounds.max = Math::Vec3(3.0f);
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.GetWorldBounds", &camera, &bounds));
        REQUIRE(bounds.max == Math::AABB{}.max);
        RendererStats stats;
        stats.draws = 3;
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.Stats", &stats));
        REQUIRE(stats.draws == 0);
    }
    INFO("Each native should warn once in a debug build, and never in a release build, that there is no renderer");
    for (const char* native : {"RendererNative.ScreenPointToRay", "RendererNative.WorldToScreenPoint",
                               "RendererNative.CameraAt", "RendererNative.GetWorldBounds", "RendererNative.Stats"})
    {
        INFO(native);
        REQUIRE(harness.WarnedOnce(native, RendererScripting::NoRenderer));
    }
    {
        INFO("A camera entity should not be checked when there is no renderer to pick with");
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, std::string(NativeGuard::NotAlive)));
        REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, std::string(RendererScripting::NoCamera)));
    }
}

TEST_CASE("SpawnModel needs no renderer", "[renderer][scripting][noops]")
{
    Testing::NativeCallHarness harness({Renderer::ScriptingExtension()});
    const Assets installed = InstallAssets(harness);
    const Entity parent = Entities(harness).CreateEntity();
    const AssetHandle<Model> model = LoadModel(installed.Store());

    Entity root;
    const bool spawned = harness.Call<bool>("RendererNative.SpawnModel", &model, &parent, &root);
    (void)harness.ApplyChanges();
    INFO("The model should spawn under the parent with no renderer installed");
    REQUIRE(spawned);
    REQUIRE(root != noEntity);
    REQUIRE(Entities(harness).HasEntity(root.GetId()));
    REQUIRE(Entities(harness).GetEntity(root.GetId()).GetComponent<Relationship>().parent == parent);
    REQUIRE_FALSE(harness.Logger().Contains(Testing::LogLevel::Warn, "RendererNative.SpawnModel"));
}

TEST_CASE("SpawnModel with no asset store spawns nothing and says so once, in Assets' words under its own name",
          "[renderer][scripting][noops]")
{
    Testing::NativeCallHarness harness({Renderer::ScriptingExtension()});
    // The handle that would name the first model: slot 0, generation 1.
    const AssetHandle<Model> first{0, 1};

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": with no store there is no model to spawn");
        Entity root;
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.SpawnModel", &first, &noEntity, &root));
        REQUIRE(root == noEntity);
    }
    {
        INFO("Nothing should have been recorded to spawn");
        REQUIRE(harness.PendingChanges() == 0);
    }
    INFO("Two spawns with no store should warn once in a debug build, under the renderer's native, in Assets' words");
    REQUIRE(harness.WarnedOnce("RendererNative.SpawnModel", AssetResolve::NoStore));
}

TEST_CASE("A dead entity, no Camera, a stale or empty model or a dead parent do nothing and say so once",
          "[renderer][scripting][noops]")
{
    Testing::NativeCallHarness harness({Renderer::ScriptingExtension()});
    const Assets installed = InstallAssets(harness);
    const Renderer withRenderer = InstallRenderer(harness);
    const std::uint32_t dead = Entities(harness).CreateEntity().GetId();
    Entities(harness).DestroyEntity(dead);
    const Entity deadEntity = Entities(harness).HandleOf(dead);
    const Entity plain = Entities(harness).CreateEntity();
    // Loading a path again gives the same handle, so the stale one is made first: loaded, unloaded, and the live one
    // loaded after it under a new generation.
    const AssetHandle<Model> unloaded = LoadModel(installed.Store());
    REQUIRE(installed.Store().Unload(unloaded));
    const AssetHandle<Model> model = LoadModel(installed.Store());
    const AssetHandle<Model> empty;
    {
        INFO("Reloading the model should leave the unloaded handle stale");
        REQUIRE_FALSE(installed.Store().IsValid(unloaded));
    }

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": each native should return false or no entity");
        Math::Ray out;
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.ScreenPointToRay", &deadEntity, &centre, &out));
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.ScreenPointToRay", &plain, &centre, &out));
        Math::AABB box{};
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.GetWorldBounds", &deadEntity, &box));
        Entity root;
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.SpawnModel", &unloaded, &noEntity, &root));
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.SpawnModel", &empty, &noEntity, &root));
        REQUIRE_FALSE(harness.Call<bool>("RendererNative.SpawnModel", &model, &deadEntity, &root));
        REQUIRE(root == noEntity);
    }
    {
        INFO("Nothing should have been recorded to spawn");
        REQUIRE(harness.PendingChanges() == 0);
    }
    INFO("Each reason should warn once in a debug build, and never in a release build, under the native's name");
    REQUIRE(harness.WarnedOnce("RendererNative.ScreenPointToRay", NativeGuard::NotAlive));
    REQUIRE(harness.WarnedOnce("RendererNative.ScreenPointToRay", RendererScripting::NoCamera));
    REQUIRE(harness.WarnedOnce("RendererNative.GetWorldBounds", NativeGuard::NotAlive));
    {
        INFO("A stale and the empty model handle should warn once from the one place SpawnModel resolves them, in "
             "Assets' words");
        REQUIRE(harness.WarnedOnce("RendererNative.SpawnModel", AssetResolve::StaleHandle("model")));
    }
    REQUIRE(harness.WarnedOnce("RendererNative.SpawnModel", NativeGuard::NotAlive));
    REQUIRE(harness.Warnings("RendererNative.SpawnModel", AssetResolve::NoStore) == 0);
}
