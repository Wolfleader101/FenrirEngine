// The no-op rules (spec: "Scripting: the extensions", no-op rules; seam 7: a missing package no-oping), with no .NET:
// each native function is called from C++ through NativeCallHarness with a RendererScripting state the test makes.
// With no renderer, no camera, a stale entity or model, or no asset store, each does nothing and returns false, zero or
// no entity, leaves its output at the default the registration gave it (ADR-0045), nothing throws, and a debug build
// warns once per native and reason, however often it is called. SpawnModel's handle rule is Assets' (AssetResolve.hpp),
// so its warning is in Assets' words under the renderer's native.

#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "FenrirAssets/AssetStore.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssetsScripting/AssetHandleBits.hpp"
#include "FenrirECS/DefaultComponents.hpp"
#include "FenrirECS/Entity.hpp"
#include "FenrirECS/EntityList.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirRenderer/Components.hpp"
#include "FenrirRenderer/Renderer.hpp"
#include "FenrirScripting/ScriptingExtension.hpp"
#include "FenrirScripting/ZeroingOutputs.hpp"
#include "Generated/FenrirRendererNatives.hpp"
#include "RendererScriptingState.hpp"

namespace
{
    using namespace Fenrir;

    // A release build warns about nothing (WarnOnceRecord, ADR-0043); a debug build once per place and message.
#ifdef NDEBUG
    constexpr std::size_t once = 0;
#else
    constexpr std::size_t once = 1;
#endif

    constexpr const char* NoRenderer = "the app has no renderer (FenrirRenderer is not installed), so it does nothing";
    constexpr const char* NotAlive = "the entity is not alive, so it does nothing";
    constexpr const char* NoCamera = "the entity has no Camera, so it does nothing";
    // Assets' words (AssetResolve.hpp), which SpawnModel reuses.
    constexpr const char* NoStore = "the app has no asset store (FenrirAssets is not installed), so it does nothing";
    constexpr const char* StaleModel = "the model handle is empty or stale, so it does nothing";

    // C#'s Entity.None: a default Entity.
    const Entity noEntity;

    // How many warnings the app logged that read `text`.
    std::size_t Warnings(const Testing::NativeCallHarness& harness, const std::string& text)
    {
        const auto messages = harness.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const Testing::LoggedMessage& message) {
                return message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos;
            }));
    }

    // The whole line the renderer's record logs for `message` in the native `native`.
    std::string Logged(const std::string& native, const std::string& message)
    {
        return "[renderer scripting] " + native + ": " + message + " (warned once)";
    }

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

    // The natives as the registration calls them, each single output zeroed first (ADR-0045): indices into the full
    // parameter list, context and state counted.
    using ScreenPointToRay = ZeroingOutputs<&RendererNatives::ScreenPointToRay, 4>;
    using WorldToScreenPoint = ZeroingOutputs<&RendererNatives::WorldToScreenPoint, 4>;
    using GetWorldBounds = ZeroingOutputs<&RendererNatives::GetWorldBounds, 3>;
    using Stats = ZeroingOutputs<&RendererNatives::Stats, 2>;
    using CameraAtCall = ZeroingOutputs<&RendererNatives::CameraAt, 4>;
    using SpawnModelCall = ZeroingOutputs<&RendererNatives::SpawnModel, 4>;

    const Math::Vec2 centre(640.0f, 360.0f);
    const Math::Vec3 origin(0.0f);
} // namespace

TEST_CASE("With no renderer, picking, bounds and stats do nothing, leave their outputs zeroed and say so once",
          "[renderer][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    RendererScripting renderer;
    Entity camera = Entities(harness).CreateEntity();
    camera.AddComponent<Camera>();

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": each native should return false, zero or no entity and write nothing");
        Math::Ray ray;
        ray.origin = Math::Vec3(3.0f);
        REQUIRE_FALSE(ScreenPointToRay::Call(harness.Context("RendererNative.ScreenPointToRay"), renderer, &camera,
                                             &centre, &ray));
        REQUIRE(ray.origin == Math::Ray{}.origin);
        Math::Vec2 screenPoint(3.0f);
        REQUIRE_FALSE(WorldToScreenPoint::Call(harness.Context("RendererNative.WorldToScreenPoint"), renderer, &camera,
                                               &origin, &screenPoint));
        REQUIRE(screenPoint == Math::Vec2(0.0f));
        Entity found;
        REQUIRE_FALSE(
            CameraAtCall::Call(harness.Context("RendererNative.CameraAt"), renderer, WindowId{1}, &centre, &found));
        REQUIRE(found == noEntity);
        Math::AABB bounds{};
        bounds.max = Math::Vec3(3.0f);
        REQUIRE_FALSE(
            GetWorldBounds::Call(harness.Context("RendererNative.GetWorldBounds"), renderer, &camera, &bounds));
        REQUIRE(bounds.max == Math::AABB{}.max);
        RendererStats stats;
        stats.draws = 3;
        REQUIRE_FALSE(Stats::Call(harness.Context("RendererNative.Stats"), renderer, &stats));
        REQUIRE(stats.draws == 0);
    }
    INFO("Each native should warn once in a debug build, and never in a release build, that there is no renderer");
    for (const char* native : {"RendererNative.ScreenPointToRay", "RendererNative.WorldToScreenPoint",
                               "RendererNative.CameraAt", "RendererNative.GetWorldBounds", "RendererNative.Stats"})
    {
        INFO(native);
        REQUIRE(Warnings(harness, Logged(native, NoRenderer)) == once);
    }
    {
        INFO("A camera entity should not be checked when there is no renderer to pick with");
        REQUIRE(Warnings(harness, NotAlive) == 0);
        REQUIRE(Warnings(harness, NoCamera) == 0);
    }
}

TEST_CASE("SpawnModel needs no renderer", "[renderer][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    const Assets installed = InstallAssets(harness);
    RendererScripting renderer;
    const Entity parent = Entities(harness).CreateEntity();
    const AssetHandle<Model> model = LoadModel(installed.Store());

    Entity root;
    const bool spawned = RendererNatives::SpawnModel(harness.Context("RendererNative.SpawnModel"), renderer,
                                                     AssetHandleBits(model), &parent, &root);
    (void)harness.ApplyChanges();
    INFO("The model should spawn under the parent with no renderer installed");
    REQUIRE(spawned);
    REQUIRE(root != noEntity);
    REQUIRE(Entities(harness).HasEntity(root.GetId()));
    REQUIRE(Entities(harness).GetEntity(root.GetId()).GetComponent<Relationship>().parent == parent);
    REQUIRE(Warnings(harness, "RendererNative.SpawnModel") == 0);
}

TEST_CASE("SpawnModel with no asset store spawns nothing and says so once, in Assets' words under its own name",
          "[renderer][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    RendererScripting renderer;
    const CallContext context = harness.Context("RendererNative.SpawnModel");
    // The handle that would name the first model.
    const std::uint64_t first = std::uint64_t{1} << 32u;

    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": with no store there is no model to spawn");
        Entity root;
        REQUIRE_FALSE(SpawnModelCall::Call(context, renderer, first, &noEntity, &root));
        REQUIRE(root == noEntity);
    }
    {
        INFO("Nothing should have been recorded to spawn");
        REQUIRE(harness.PendingChanges() == 0);
    }
    INFO("Two spawns with no store should warn once in a debug build, prefixed by the renderer's extension and native");
    REQUIRE(Warnings(harness, Logged("RendererNative.SpawnModel", NoStore)) == once);
}

TEST_CASE("A dead entity, no Camera, a stale or empty model or a dead parent do nothing and say so once",
          "[renderer][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    const Assets installed = InstallAssets(harness);
    const Renderer withRenderer = InstallRenderer(harness);
    RendererScripting renderer;
    const std::uint32_t dead = Entities(harness).CreateEntity().GetId();
    Entities(harness).DestroyEntity(dead);
    const Entity deadEntity = Entities(harness).HandleOf(dead);
    const Entity plain = Entities(harness).CreateEntity();
    // Loading a path again gives the same handle, so the stale one is made first: loaded, unloaded, and the live one
    // loaded after it under a new generation.
    const AssetHandle<Model> unloaded = LoadModel(installed.Store());
    REQUIRE(installed.Store().Unload(unloaded));
    const AssetHandle<Model> model = LoadModel(installed.Store());
    {
        INFO("Reloading the model should leave the unloaded handle stale");
        REQUIRE_FALSE(installed.Store().IsValid(unloaded));
    }

    const CallContext ray = harness.Context("RendererNative.ScreenPointToRay");
    const CallContext bounds = harness.Context("RendererNative.GetWorldBounds");
    const CallContext spawn = harness.Context("RendererNative.SpawnModel");
    for (int call = 0; call < 2; ++call)
    {
        INFO("Call " << call << ": each native should return false or no entity");
        Math::Ray out;
        REQUIRE_FALSE(RendererNatives::ScreenPointToRay(ray, renderer, &deadEntity, &centre, &out));
        REQUIRE_FALSE(RendererNatives::ScreenPointToRay(ray, renderer, &plain, &centre, &out));
        Math::AABB box{};
        REQUIRE_FALSE(RendererNatives::GetWorldBounds(bounds, renderer, &deadEntity, &box));
        Entity root;
        REQUIRE_FALSE(SpawnModelCall::Call(spawn, renderer, AssetHandleBits(unloaded), &noEntity, &root));
        REQUIRE_FALSE(SpawnModelCall::Call(spawn, renderer, 0, &noEntity, &root));
        REQUIRE_FALSE(SpawnModelCall::Call(spawn, renderer, AssetHandleBits(model), &deadEntity, &root));
        REQUIRE(root == noEntity);
    }
    {
        INFO("Nothing should have been recorded to spawn");
        REQUIRE(harness.PendingChanges() == 0);
    }
    INFO("Each reason should warn once in a debug build, and never in a release build, under the native's name");
    REQUIRE(Warnings(harness, Logged("RendererNative.ScreenPointToRay", NotAlive)) == once);
    REQUIRE(Warnings(harness, Logged("RendererNative.ScreenPointToRay", NoCamera)) == once);
    REQUIRE(Warnings(harness, Logged("RendererNative.GetWorldBounds", NotAlive)) == once);
    {
        INFO("A stale and the empty model handle should warn once from the one place SpawnModel resolves them, in "
             "Assets' words");
        REQUIRE(Warnings(harness, "[renderer scripting] RendererNative.SpawnModel: " + std::string(StaleModel)) ==
                once);
    }
    REQUIRE(Warnings(harness, Logged("RendererNative.SpawnModel", NotAlive)) == once);
    REQUIRE(Warnings(harness, NoStore) == 0);
}
