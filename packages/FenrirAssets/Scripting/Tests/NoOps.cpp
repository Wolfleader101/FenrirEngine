// The no-op rules (spec: "Scripting: the extensions"), with no .NET: through NativeCallHarness, with an AssetsScripting
// state the test makes, every assets native function called with no asset store, a stale handle or the empty handle
// does nothing, returns false, zero or the empty handle, leaves its output untouched (the registration zeroes it,
// ADR-0045), and warns once per native call site under its own name. And the public "no store, then stale handle"
// rule (AssetResolve.hpp) warns in the words another package's native (FenrirRenderer's SpawnModel) will reuse.
//
// A native call site warns once per extension state (its record), so each case counts its own warnings. They still
// run in a process of their own (CTest's FenrirAssetsScripting-NoOps), in declaration order (--order decl).

#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "AssetsScriptingState.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssetsScripting/AssetHandleBits.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"
#include "FenrirScripting/NativeGuard.hpp"
#include "Generated/FenrirAssetsNatives.hpp"

using namespace Fenrir;

namespace
{
    // A release build warns about nothing (WarnOnceRecord, ADR-0043); a debug build once per native call site.
#ifdef NDEBUG
    constexpr std::size_t once = 0;
#else
    constexpr std::size_t once = 1;
#endif

    constexpr const char* NoStore = "the app has no asset store (FenrirAssets is not installed), so it does nothing";

    // How many warnings the app logged that read `text` (and `alsoText`, when given).
    std::size_t Warnings(const Testing::NativeCallHarness& harness, const std::string& text,
                         const std::string& alsoText = {})
    {
        const auto messages = harness.Logger().Messages();
        return static_cast<std::size_t>(
            std::count_if(messages.begin(), messages.end(), [&](const Testing::LoggedMessage& message) {
                return message.level == Testing::LogLevel::Warn && message.text.find(text) != std::string::npos &&
                       message.text.find(alsoText) != std::string::npos;
            }));
    }

    // How the assets record logs `message` in the native `native` (the record then says it warned once).
    std::string Logged(std::string_view native, std::string_view message)
    {
        return "[assets scripting] " + std::string(native) + ": " + std::string(message);
    }

    // The handles every native that takes one is called with.
    struct Handles
    {
        std::uint64_t mesh;
        std::uint64_t image;
        std::uint64_t material;
        std::uint64_t shader;
    };

    // One native call: its name as its context carries it, and the call, which says whether the native did nothing
    // (returned false, zero or the empty handle, and wrote no output).
    struct NativeCall
    {
        const char* native;
        std::function<bool(const CallContext&, AssetsScripting&)> didNothing;
    };

    // Every native that needs only the store: those that make an asset, and IsValid.
    std::vector<NativeCall> StoreCalls(const Handles& handles)
    {
        static const Math::Vec3 positions[] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        static const std::uint32_t indices[] = {0, 1, 2};
        static const std::uint8_t pixel[] = {255, 255, 255, 255};
        return {
            {"AssetsNative.Load",
             [](const CallContext& context, AssetsScripting& assets) {
                 return AssetsNatives::Load(context, assets, Detail::AssetKind::Mesh, "models/scene.gltf#Mesh0",
                                            ScriptAssetOwner::App) == 0;
             }},
            {"AssetsNative.IsValid",
             [=](const CallContext& context, AssetsScripting& assets) {
                 return !AssetsNatives::IsValid(context, assets, Detail::AssetKind::Mesh, handles.mesh);
             }},
            {"AssetsNative.AddMesh",
             [](const CallContext& context, AssetsScripting& assets) {
                 return AssetsNatives::AddMesh(context, assets, positions, 3, nullptr, 0, nullptr, 0, indices, 3,
                                               ScriptAssetOwner::App) == 0;
             }},
            {"AssetsNative.AddRenderTarget",
             [](const CallContext& context, AssetsScripting& assets) {
                 return AssetsNatives::AddRenderTarget(context, assets, 4, 4, ScriptAssetOwner::App) == 0;
             }},
            {"AssetsNative.AddImage",
             [](const CallContext& context, AssetsScripting& assets) {
                 return AssetsNatives::AddImage(context, assets, 1, 1, pixel, 4, ScriptAssetOwner::App) == 0;
             }},
        };
    }

    // Whether an output still holds its default. Colour and ScriptShaderParameter have no operator==.
    template <typename Out>
    bool Untouched(const Out& out)
    {
        return out == Out{};
    }
    bool Untouched(const Colour& out)
    {
        return out.r == 1.0f && out.g == 1.0f && out.b == 1.0f && out.a == 1.0f;
    }
    bool Untouched(const ScriptShaderParameter& out)
    {
        return out.type == ShaderParameterType::Float && out.value == Math::Vec4(0.0f) &&
               out.texture == DefaultTexture::White;
    }

    // One output read: the native must return false and leave `out` at its default.
    template <typename Out, typename Native>
    bool ReadsNothing(Native&& native)
    {
        Out out{};
        return !native(&out) && Untouched(out);
    }

    // Every native that takes a handle, called with `handles`.
    std::vector<NativeCall> HandleCalls(const Handles& handles)
    {
        static const Math::Vec3 positions[] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        static const std::uint32_t indices[] = {0, 1, 2};
        static const std::uint8_t pixel[] = {255, 255, 255, 255};
        const std::uint64_t mesh = handles.mesh;
        const std::uint64_t image = handles.image;
        const std::uint64_t material = handles.material;
        const std::uint64_t shader = handles.shader;
        return {
            {"AssetsNative.Unload",
             [=](const CallContext& c, AssetsScripting& a) {
                 return !AssetsNatives::Unload(c, a, Detail::AssetKind::Mesh, mesh);
             }},
            {"AssetsNative.ReplaceMesh",
             [=](const CallContext& c, AssetsScripting& a) {
                 return !AssetsNatives::ReplaceMesh(c, a, mesh, positions, 3, nullptr, 0, nullptr, 0, indices, 3);
             }},
            {"AssetsNative.ResizeRenderTarget",
             [=](const CallContext& c, AssetsScripting& a) {
                 return !AssetsNatives::ResizeRenderTarget(c, a, image, 8, 8);
             }},
            {"AssetsNative.ReplaceImage",
             [=](const CallContext& c, AssetsScripting& a) {
                 return !AssetsNatives::ReplaceImage(c, a, image, 1, 1, pixel, 4);
             }},
            {"AssetsNative.CreateMaterial",
             [=](const CallContext& c, AssetsScripting& a) {
                 return AssetsNatives::CreateMaterial(c, a, shader, ScriptAssetOwner::App) == 0;
             }},
            {"AssetsNative.CloneMaterial",
             [=](const CallContext& c, AssetsScripting& a) {
                 return AssetsNatives::CloneMaterial(c, a, material, ScriptAssetOwner::App) == 0;
             }},
            {"AssetsNative.SetFloat",
             [=](const CallContext& c, AssetsScripting& a) {
                 return !AssetsNatives::SetFloat(c, a, material, "roughness", 1.0f);
             }},
            {"AssetsNative.SetVector2",
             [=](const CallContext& c, AssetsScripting& a) {
                 const Math::Vec2 value(1, 1);
                 return !AssetsNatives::SetVector2(c, a, material, "offset", &value);
             }},
            {"AssetsNative.SetVector3",
             [=](const CallContext& c, AssetsScripting& a) {
                 const Math::Vec3 value(1, 1, 1);
                 return !AssetsNatives::SetVector3(c, a, material, "emissive", &value);
             }},
            {"AssetsNative.SetVector4",
             [=](const CallContext& c, AssetsScripting& a) {
                 const Math::Vec4 value(1, 1, 1, 1);
                 return !AssetsNatives::SetVector4(c, a, material, "tint", &value);
             }},
            {"AssetsNative.SetColour",
             [=](const CallContext& c, AssetsScripting& a) {
                 const Colour value = Colour::White;
                 return !AssetsNatives::SetColour(c, a, material, "tint", &value);
             }},
            {"AssetsNative.SetTexture",
             [=](const CallContext& c, AssetsScripting& a) {
                 const SamplerDescription sampler;
                 return !AssetsNatives::SetTexture(c, a, material, "baseColourTexture", image, &sampler);
             }},
            {"AssetsNative.SetBlendMode",
             [=](const CallContext& c, AssetsScripting& a) {
                 return !AssetsNatives::SetBlendMode(c, a, material, BlendMode::Additive);
             }},
            {"AssetsNative.SetAlphaCutoff",
             [=](const CallContext& c, AssetsScripting& a) {
                 return !AssetsNatives::SetAlphaCutoff(c, a, material, 0.1f);
             }},
            {"AssetsNative.SetDoubleSided",
             [=](const CallContext& c, AssetsScripting& a) {
                 return !AssetsNatives::SetDoubleSided(c, a, material, true);
             }},
            {"AssetsNative.TryGetFloat",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<float>(
                     [&](float* out) { return AssetsNatives::TryGetFloat(c, a, material, "roughness", out); });
             }},
            {"AssetsNative.TryGetVector2",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<Math::Vec2>(
                     [&](Math::Vec2* out) { return AssetsNatives::TryGetVector2(c, a, material, "offset", out); });
             }},
            {"AssetsNative.TryGetVector3",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<Math::Vec3>(
                     [&](Math::Vec3* out) { return AssetsNatives::TryGetVector3(c, a, material, "emissive", out); });
             }},
            {"AssetsNative.TryGetVector4",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<Math::Vec4>(
                     [&](Math::Vec4* out) { return AssetsNatives::TryGetVector4(c, a, material, "tint", out); });
             }},
            {"AssetsNative.TryGetColour",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<Colour>(
                     [&](Colour* out) { return AssetsNatives::TryGetColour(c, a, material, "tint", out); });
             }},
            {"AssetsNative.TryGetTexture",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<MaterialTexture>([&](MaterialTexture* out) {
                     return AssetsNatives::TryGetTexture(c, a, material, "baseColourTexture", out);
                 });
             }},
            {"AssetsNative.IsSet",
             [=](const CallContext& c, AssetsScripting& a) { return !AssetsNatives::IsSet(c, a, material, "tint"); }},
            {"AssetsNative.TryGetOverrideFloat",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<float>(
                     [&](float* out) { return AssetsNatives::TryGetOverrideFloat(c, a, material, "roughness", out); });
             }},
            {"AssetsNative.TryGetOverrideVector2",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<Math::Vec2>([&](Math::Vec2* out) {
                     return AssetsNatives::TryGetOverrideVector2(c, a, material, "offset", out);
                 });
             }},
            {"AssetsNative.TryGetOverrideVector3",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<Math::Vec3>([&](Math::Vec3* out) {
                     return AssetsNatives::TryGetOverrideVector3(c, a, material, "emissive", out);
                 });
             }},
            {"AssetsNative.TryGetOverrideVector4",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<Math::Vec4>([&](Math::Vec4* out) {
                     return AssetsNatives::TryGetOverrideVector4(c, a, material, "tint", out);
                 });
             }},
            {"AssetsNative.TryGetOverrideColour",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<Colour>(
                     [&](Colour* out) { return AssetsNatives::TryGetOverrideColour(c, a, material, "tint", out); });
             }},
            {"AssetsNative.TryGetOverrideTexture",
             [=](const CallContext& c, AssetsScripting& a) {
                 return ReadsNothing<MaterialTexture>([&](MaterialTexture* out) {
                     return AssetsNatives::TryGetOverrideTexture(c, a, material, "baseColourTexture", out);
                 });
             }},
            {"AssetsNative.Reset",
             [=](const CallContext& c, AssetsScripting& a) { return !AssetsNatives::Reset(c, a, material, "tint"); }},
            {"AssetsNative.GetShaderParameterCount",
             [=](const CallContext& c, AssetsScripting& a) {
                 return AssetsNatives::GetShaderParameterCount(c, a, shader) == 0;
             }},
            {"AssetsNative.GetShaderParameter",
             [=](const CallContext& c, AssetsScripting& a) {
                 char16_t name[16] = {};
                 const bool wroteNothing = ReadsNothing<ScriptShaderParameter>([&](ScriptShaderParameter* out) {
                     return AssetsNatives::GetShaderParameter(c, a, shader, 0, out, name, 16) != 0;
                 });
                 return wroteNothing && name[0] == u'\0';
             }},
        };
    }

    // Calls each native twice from one place and checks it did nothing both times and warned `message` once.
    void EachDoesNothing(Testing::NativeCallHarness& harness, AssetsScripting& assets,
                         const std::vector<NativeCall>& calls, std::string_view message)
    {
        for (const NativeCall& call : calls)
        {
            const CallContext context = harness.Context(call.native);
            for (int time = 0; time < 2; ++time)
            {
                INFO(call.native << ", call " << time
                                 << ": did something, returned other than false, zero or the empty handle, or "
                                    "wrote its output");
                REQUIRE(call.didNothing(context, assets));
            }
            INFO(call.native << " did not warn once in a debug build (never in a release build), though called twice");
            REQUIRE(Warnings(harness, Logged(call.native, message)) == once);
        }
    }

    // A shader container written by hand: one variant, two stages, no parameters, enough for the store to accept.
    ShaderData MinimalShader()
    {
        ShaderContainer container;
        container.name = "NoOps";
        ShaderVariant variant;
        variant.stages.push_back({ShaderStage::Vertex, {0x07230203u}, {0x07230203u}});
        variant.stages.push_back({ShaderStage::Fragment, {0x07230203u}, {0x07230203u}});
        container.variants.push_back(std::move(variant));
        ShaderData data;
        data.bytes = WriteShaderContainer(container);
        return data;
    }

    // Assets installed in the harness's app, its asset root FenrirAssets's test fixtures.
    Assets InstallAssets(Testing::NativeCallHarness& harness)
    {
        return Assets::Install(harness.GetApp(), {std::filesystem::absolute(FENRIR_ASSETS_FIXTURES)});
    }

    // One asset of each kind, unloaded, so each handle is stale.
    Handles StaleHandles(AssetStore& store)
    {
        MeshData meshData;
        meshData.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        meshData.indices = {0, 1, 2};
        meshData.submeshes = {{0, 3, 0}};
        const AssetHandle<Mesh> mesh = store.Add<Mesh>(std::move(meshData), AssetOwner::App());
        ImageData imageData;
        imageData.width = 4;
        imageData.height = 4;
        const AssetHandle<Image> image = store.Add<Image>(std::move(imageData), AssetOwner::App());
        const AssetHandle<Shader> shader = store.Add<Shader>(MinimalShader(), AssetOwner::App());
        const AssetHandle<Material> material = store.CreateMaterial(shader, AssetOwner::App());
        {
            INFO("An asset was refused, so there is nothing to make stale");
            REQUIRE(store.IsValid(mesh));
            REQUIRE(store.IsValid(image));
            REQUIRE(store.IsValid(shader));
            REQUIRE(store.IsValid(material));
        }
        REQUIRE(store.Unload(mesh));
        REQUIRE(store.Unload(image));
        REQUIRE(store.Unload(material));
        REQUIRE(store.Unload(shader));
        return {AssetHandleBits(mesh), AssetHandleBits(image), AssetHandleBits(material), AssetHandleBits(shader)};
    }
} // namespace

TEST_CASE("With no asset store, every assets native does nothing and warns once per native call site",
          "[assets][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    AssetsScripting assets;
    // Handles that would name the first asset of each kind.
    const std::uint64_t first = std::uint64_t{1} << 32u;
    const Handles handles{first, first, first, first};
    EachDoesNothing(harness, assets, StoreCalls(handles), NoStore);
    EachDoesNothing(harness, assets, HandleCalls(handles), NoStore);
}

TEST_CASE("A stale handle makes every assets native do nothing, warning once per native call site",
          "[assets][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    Assets installed = InstallAssets(harness);
    AssetStore& store = installed.Store();
    AssetsScripting assets;
    const Handles handles = StaleHandles(store);

    for (const NativeCall& call : HandleCalls(handles))
    {
        const CallContext context = harness.Context(call.native);
        for (int time = 0; time < 2; ++time)
        {
            INFO(call.native << ", call " << time
                             << ": did something with a stale handle, returned other than false, zero or the empty "
                                "handle, or wrote its output");
            REQUIRE(call.didNothing(context, assets));
        }
        INFO(call.native << " did not warn once in a debug build (never in a release build), though called twice");
        REQUIRE(Warnings(harness, Logged(call.native, "the "), "handle is empty or stale, so it does nothing") == once);
        REQUIRE(Warnings(harness, NoStore) == 0);
    }
    {
        INFO("Nothing was made from the stale handles");
        REQUIRE_FALSE(store.IsValid(AssetHandleFromBits<Mesh>(handles.mesh)));
        REQUIRE_FALSE(store.IsValid(AssetHandleFromBits<Image>(handles.image)));
    }
}

TEST_CASE("The empty handle makes every assets native do nothing", "[assets][scripting][noops]")
{
    Testing::NativeCallHarness harness;
    Assets installed = InstallAssets(harness);
    AssetsScripting assets;
    EachDoesNothing(harness, assets, HandleCalls(Handles{0, 0, 0, 0}), "the ");
}

TEST_CASE("The public Resolve<Model> is null with no store, then with a stale handle, warning once each in Assets' "
          "words under the caller's native",
          "[assets][scripting][noops]")
{
    // The native another package's extension would call it from (FenrirRenderer's SpawnModel), with that
    // extension's record: the warning names the caller, in Assets' words.
    constexpr const char* Native = "RendererNative.SpawnModel";
    Testing::NativeCallHarness harness;
    WarnOnceRecord warnings("renderer scripting");
    const CallContext context = harness.Context(Native);
    const NativeGuard guard(context, warnings);
    const std::string prefix = std::string("[renderer scripting] ") + Native + ": ";
    const std::uint64_t first = std::uint64_t{1} << 32u;

    for (int time = 0; time < 2; ++time)
    {
        INFO("Resolve<Model> found a store in an app with none");
        REQUIRE(Resolve<Model>(guard, first) == nullptr);
    }
    {
        INFO("Two resolves with no store from one place did not warn once, under the caller's native, in Assets' "
             "words");
        REQUIRE(Warnings(harness, prefix + NoStore + " (warned once)") == once);
    }

    Assets installed = InstallAssets(harness);
    AssetStore& store = installed.Store();
    const AssetHandle<Model> model = store.Load<Model>("models/scene.gltf", AssetOwner::App()).handle;
    {
        INFO("The fixture model did not load, so there is no handle to make stale");
        REQUIRE(store.IsValid(model));
    }
    {
        INFO("Resolve<Model> did not give the store for a live model");
        REQUIRE(Resolve<Model>(guard, AssetHandleBits(model)) == &store);
    }
    REQUIRE(store.Unload(model));
    for (int time = 0; time < 2; ++time)
    {
        INFO("Resolve<Model> resolved a stale model");
        REQUIRE(Resolve<Model>(guard, AssetHandleBits(model)) == nullptr);
    }
    {
        INFO("Two resolves of a stale model from one place did not warn once, under the caller's native, in Assets' "
             "words");
        REQUIRE(Warnings(harness, prefix + "the model handle is empty or stale, so it does nothing (warned once)") ==
                once);
    }
    {
        INFO("The empty model handle did not resolve as stale");
        REQUIRE(Resolve<Model>(guard, 0) == nullptr);
    }
}
