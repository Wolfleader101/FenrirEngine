// The no-op rules (spec: "Scripting: the extensions"), with no .NET: through NativeCallHarness, which runs the assets
// extension as shipped (Assets::ScriptingExtension()) and calls each native by its registered name through the
// registration's own thunk, every assets native function called with no asset store, a stale handle or the empty
// handle does nothing, returns false, zero or the empty handle, leaves its output untouched (the registration zeroes
// it, ADR-0045), and warns once per native call site under its own name, in the words AssetResolve names. And the
// public "no store, then stale handle" rule (AssetResolve.hpp) warns in those words under whichever native reaches it,
// as another package's native (FenrirRenderer's SpawnModel) does.
//
// A native call site warns once per extension state (its record), so each case runs its own extension and counts its
// own warnings. They still run in a process of their own (CTest's FenrirAssetsScripting-NoOps), in declaration order
// (--order decl).

#include <catch2/catch_test_macros.hpp>

#include "NativeCallHarness.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "FenrirAssets/AssetHandle.hpp"
#include "FenrirAssets/Assets.hpp"
#include "FenrirAssets/ShaderContainer.hpp"
#include "FenrirAssetsScripting/AssetResolve.hpp"
#include "FenrirAssetsScripting/AssetsScripting.hpp"
#include "FenrirAssetsScripting/ScriptAssets.hpp"
#include "FenrirMath/Colour.hpp"
#include "FenrirMath/Math.hpp"

using namespace Fenrir;

namespace
{
    using namespace std::string_view_literals;

    // The handles every native that takes one is called with, as the natives take them.
    struct Handles
    {
        AssetHandle<Mesh> mesh;
        AssetHandle<Image> image;
        AssetHandle<Material> material;
        AssetHandle<Shader> shader;
    };

    // `handle` as the kind-generic natives take it.
    template <typename T>
    ScriptAssetHandle Generic(AssetHandle<T> handle)
    {
        return {handle.index, handle.generation};
    }

    // One native call: its registered name, the word log messages use for the kind of the first handle it resolves
    // (Detail::AssetTraits<T>::name; empty for a native that needs only the store), and the call, which says whether
    // the native did nothing (returned false, zero or the empty handle, and wrote no output).
    struct NativeCall
    {
        const char* native;
        std::string_view handleKind;
        std::function<bool(Testing::NativeCallHarness&)> didNothing;
    };

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
    bool Untouched(const ScriptAssetHandle& out)
    {
        return out.index == 0 && out.generation == 0;
    }

    // One output read: the native must return false and leave `out` at its default.
    template <typename Out, typename Native>
    bool ReadsNothing(Native&& native)
    {
        Out out{};
        return !native(&out) && Untouched(out);
    }

    // The empty vertex streams a mesh native is given, typed as the natives take them (Call deduces each argument's
    // type, and a bare nullptr is none of the pointer types).
    constexpr const Math::Vec3* noNormals = nullptr;
    constexpr const Math::Vec2* noUvs = nullptr;

    // Every native that needs only the store: those that make an asset, and IsValid.
    std::vector<NativeCall> StoreCalls(const Handles& handles)
    {
        static const Math::Vec3 positions[] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        static const std::uint32_t indices[] = {0, 1, 2};
        static const std::uint8_t pixel[] = {255, 255, 255, 255};
        return {
            {"AssetsNative.Load",
             {},
             [](Testing::NativeCallHarness& harness) {
                 return ReadsNothing<ScriptAssetHandle>([&](ScriptAssetHandle* out) {
                     return harness.Call<bool>("AssetsNative.Load", Detail::AssetKind::Mesh,
                                               "models/scene.gltf#Mesh0"sv, ScriptAssetOwner::App, out);
                 });
             }},
            {"AssetsNative.IsValid",
             {},
             [=](Testing::NativeCallHarness& harness) {
                 const ScriptAssetHandle mesh = Generic(handles.mesh);
                 return !harness.Call<bool>("AssetsNative.IsValid", Detail::AssetKind::Mesh, &mesh);
             }},
            {"AssetsNative.AddMesh",
             {},
             [](Testing::NativeCallHarness& harness) {
                 return ReadsNothing<AssetHandle<Mesh>>([&](AssetHandle<Mesh>* out) {
                     return harness.Call<bool>("AssetsNative.AddMesh", positions, 3, noNormals, 0, noUvs, 0, indices, 3,
                                               ScriptAssetOwner::App, out);
                 });
             }},
            {"AssetsNative.AddRenderTarget",
             {},
             [](Testing::NativeCallHarness& harness) {
                 return ReadsNothing<AssetHandle<Image>>([&](AssetHandle<Image>* out) {
                     return harness.Call<bool>("AssetsNative.AddRenderTarget", 4, 4, ScriptAssetOwner::App, out);
                 });
             }},
            {"AssetsNative.AddImage",
             {},
             [](Testing::NativeCallHarness& harness) {
                 return ReadsNothing<AssetHandle<Image>>([&](AssetHandle<Image>* out) {
                     return harness.Call<bool>("AssetsNative.AddImage", 1, 1, pixel, 4, ScriptAssetOwner::App, out);
                 });
             }},
        };
    }

    // Every native that takes a handle, called with `handles`.
    std::vector<NativeCall> HandleCalls(const Handles& handles)
    {
        static const Math::Vec3 positions[] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        static const std::uint32_t indices[] = {0, 1, 2};
        static const std::uint8_t pixel[] = {255, 255, 255, 255};
        // Each lambda copies its handles, so the pointers it passes are to its own.
        const AssetHandle<Mesh> mesh = handles.mesh;
        const AssetHandle<Image> image = handles.image;
        const AssetHandle<Material> material = handles.material;
        const AssetHandle<Shader> shader = handles.shader;
        return {
            {"AssetsNative.Unload", "mesh",
             [=](Testing::NativeCallHarness& h) {
                 const ScriptAssetHandle generic = Generic(mesh);
                 return !h.Call<bool>("AssetsNative.Unload", Detail::AssetKind::Mesh, &generic);
             }},
            {"AssetsNative.ReplaceMesh", "mesh",
             [=](Testing::NativeCallHarness& h) {
                 return !h.Call<bool>("AssetsNative.ReplaceMesh", &mesh, positions, 3, noNormals, 0, noUvs, 0, indices,
                                      3);
             }},
            {"AssetsNative.ResizeRenderTarget", "image",
             [=](Testing::NativeCallHarness& h) {
                 return !h.Call<bool>("AssetsNative.ResizeRenderTarget", &image, 8, 8);
             }},
            {"AssetsNative.ReplaceImage", "image",
             [=](Testing::NativeCallHarness& h) {
                 return !h.Call<bool>("AssetsNative.ReplaceImage", &image, 1, 1, pixel, 4);
             }},
            {"AssetsNative.CreateMaterial", "shader",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<AssetHandle<Material>>([&](AssetHandle<Material>* out) {
                     return h.Call<bool>("AssetsNative.CreateMaterial", &shader, ScriptAssetOwner::App, out);
                 });
             }},
            {"AssetsNative.CloneMaterial", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<AssetHandle<Material>>([&](AssetHandle<Material>* out) {
                     return h.Call<bool>("AssetsNative.CloneMaterial", &material, ScriptAssetOwner::App, out);
                 });
             }},
            {"AssetsNative.SetFloat", "material",
             [=](Testing::NativeCallHarness& h) {
                 return !h.Call<bool>("AssetsNative.SetFloat", &material, "roughness"sv, 1.0f);
             }},
            {"AssetsNative.SetVector2", "material",
             [=](Testing::NativeCallHarness& h) {
                 const Math::Vec2 value(1, 1);
                 return !h.Call<bool>("AssetsNative.SetVector2", &material, "offset"sv, &value);
             }},
            {"AssetsNative.SetVector3", "material",
             [=](Testing::NativeCallHarness& h) {
                 const Math::Vec3 value(1, 1, 1);
                 return !h.Call<bool>("AssetsNative.SetVector3", &material, "emissive"sv, &value);
             }},
            {"AssetsNative.SetVector4", "material",
             [=](Testing::NativeCallHarness& h) {
                 const Math::Vec4 value(1, 1, 1, 1);
                 return !h.Call<bool>("AssetsNative.SetVector4", &material, "tint"sv, &value);
             }},
            {"AssetsNative.SetColour", "material",
             [=](Testing::NativeCallHarness& h) {
                 const Colour value = Colour::White;
                 return !h.Call<bool>("AssetsNative.SetColour", &material, "tint"sv, &value);
             }},
            {"AssetsNative.SetTexture", "material",
             [=](Testing::NativeCallHarness& h) {
                 const SamplerDescription sampler;
                 return !h.Call<bool>("AssetsNative.SetTexture", &material, "baseColourTexture"sv, &image, &sampler);
             }},
            {"AssetsNative.SetBlendMode", "material",
             [=](Testing::NativeCallHarness& h) {
                 return !h.Call<bool>("AssetsNative.SetBlendMode", &material, BlendMode::Additive);
             }},
            {"AssetsNative.SetAlphaCutoff", "material",
             [=](Testing::NativeCallHarness& h) {
                 return !h.Call<bool>("AssetsNative.SetAlphaCutoff", &material, 0.1f);
             }},
            {"AssetsNative.SetDoubleSided", "material",
             [=](Testing::NativeCallHarness& h) {
                 return !h.Call<bool>("AssetsNative.SetDoubleSided", &material, true);
             }},
            {"AssetsNative.TryGetFloat", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<float>([&](float* out) {
                     return h.Call<bool>("AssetsNative.TryGetFloat", &material, "roughness"sv, out);
                 });
             }},
            {"AssetsNative.TryGetVector2", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<Math::Vec2>([&](Math::Vec2* out) {
                     return h.Call<bool>("AssetsNative.TryGetVector2", &material, "offset"sv, out);
                 });
             }},
            {"AssetsNative.TryGetVector3", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<Math::Vec3>([&](Math::Vec3* out) {
                     return h.Call<bool>("AssetsNative.TryGetVector3", &material, "emissive"sv, out);
                 });
             }},
            {"AssetsNative.TryGetVector4", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<Math::Vec4>([&](Math::Vec4* out) {
                     return h.Call<bool>("AssetsNative.TryGetVector4", &material, "tint"sv, out);
                 });
             }},
            {"AssetsNative.TryGetColour", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<Colour>(
                     [&](Colour* out) { return h.Call<bool>("AssetsNative.TryGetColour", &material, "tint"sv, out); });
             }},
            {"AssetsNative.TryGetTexture", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<MaterialTexture>([&](MaterialTexture* out) {
                     return h.Call<bool>("AssetsNative.TryGetTexture", &material, "baseColourTexture"sv, out);
                 });
             }},
            {"AssetsNative.IsSet", "material",
             [=](Testing::NativeCallHarness& h) { return !h.Call<bool>("AssetsNative.IsSet", &material, "tint"sv); }},
            {"AssetsNative.TryGetOverrideFloat", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<float>([&](float* out) {
                     return h.Call<bool>("AssetsNative.TryGetOverrideFloat", &material, "roughness"sv, out);
                 });
             }},
            {"AssetsNative.TryGetOverrideVector2", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<Math::Vec2>([&](Math::Vec2* out) {
                     return h.Call<bool>("AssetsNative.TryGetOverrideVector2", &material, "offset"sv, out);
                 });
             }},
            {"AssetsNative.TryGetOverrideVector3", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<Math::Vec3>([&](Math::Vec3* out) {
                     return h.Call<bool>("AssetsNative.TryGetOverrideVector3", &material, "emissive"sv, out);
                 });
             }},
            {"AssetsNative.TryGetOverrideVector4", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<Math::Vec4>([&](Math::Vec4* out) {
                     return h.Call<bool>("AssetsNative.TryGetOverrideVector4", &material, "tint"sv, out);
                 });
             }},
            {"AssetsNative.TryGetOverrideColour", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<Colour>([&](Colour* out) {
                     return h.Call<bool>("AssetsNative.TryGetOverrideColour", &material, "tint"sv, out);
                 });
             }},
            {"AssetsNative.TryGetOverrideTexture", "material",
             [=](Testing::NativeCallHarness& h) {
                 return ReadsNothing<MaterialTexture>([&](MaterialTexture* out) {
                     return h.Call<bool>("AssetsNative.TryGetOverrideTexture", &material, "baseColourTexture"sv, out);
                 });
             }},
            {"AssetsNative.Reset", "material",
             [=](Testing::NativeCallHarness& h) { return !h.Call<bool>("AssetsNative.Reset", &material, "tint"sv); }},
            {"AssetsNative.GetShaderParameterCount", "shader",
             [=](Testing::NativeCallHarness& h) {
                 return h.Call<std::int32_t>("AssetsNative.GetShaderParameterCount", &shader) == 0;
             }},
            {"AssetsNative.GetShaderParameter", "shader",
             [=](Testing::NativeCallHarness& h) {
                 char16_t name[16] = {};
                 const bool wroteNothing = ReadsNothing<ScriptShaderParameter>([&](ScriptShaderParameter* out) {
                     return h.Call<std::int32_t>("AssetsNative.GetShaderParameter", &shader, 0, out, name, 16) != 0;
                 });
                 return wroteNothing && name[0] == u'\0';
             }},
        };
    }

    // Calls each native twice from one place and checks it did nothing both times and warned `message(call)` once.
    void EachDoesNothing(Testing::NativeCallHarness& harness, const std::vector<NativeCall>& calls,
                         const std::function<std::string(const NativeCall&)>& message)
    {
        for (const NativeCall& call : calls)
        {
            for (int time = 0; time < 2; ++time)
            {
                INFO(call.native << ", call " << time
                                 << ": did something, returned other than false, zero or the empty handle, or "
                                    "wrote its output");
                REQUIRE(call.didNothing(harness));
            }
            INFO(call.native << " did not warn once in a debug build (never in a release build), though called twice");
            REQUIRE(harness.WarnedOnce(call.native, message(call)));
        }
    }

    // What every native warns with no store, whatever handle it takes.
    std::string NoStore(const NativeCall&)
    {
        return std::string(AssetResolve::NoStore);
    }

    // What a native warns when the first handle it resolves is empty or stale.
    std::string StaleHandle(const NativeCall& call)
    {
        return AssetResolve::StaleHandle(call.handleKind);
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
        return {mesh, image, material, shader};
    }
} // namespace

TEST_CASE("With no asset store, every assets native does nothing and warns once per native call site",
          "[assets][scripting][noops]")
{
    Testing::NativeCallHarness harness({Assets::ScriptingExtension()});
    // Handles that would name the first asset of each kind: slot 0, generation 1.
    const Handles handles{{0, 1}, {0, 1}, {0, 1}, {0, 1}};
    EachDoesNothing(harness, StoreCalls(handles), NoStore);
    EachDoesNothing(harness, HandleCalls(handles), NoStore);
}

TEST_CASE("A stale handle makes every assets native do nothing, warning once per native call site",
          "[assets][scripting][noops]")
{
    Testing::NativeCallHarness harness({Assets::ScriptingExtension()});
    Assets installed = InstallAssets(harness);
    AssetStore& store = installed.Store();
    const Handles handles = StaleHandles(store);

    for (const NativeCall& call : HandleCalls(handles))
    {
        for (int time = 0; time < 2; ++time)
        {
            INFO(call.native << ", call " << time
                             << ": did something with a stale handle, returned other than false, zero or the empty "
                                "handle, or wrote its output");
            REQUIRE(call.didNothing(harness));
        }
        INFO(call.native << " did not warn once in a debug build (never in a release build), though called twice");
        REQUIRE(harness.WarnedOnce(call.native, AssetResolve::StaleHandle(call.handleKind)));
        REQUIRE(harness.Warnings(call.native, AssetResolve::NoStore) == 0);
    }
    {
        INFO("Nothing was made from the stale handles");
        REQUIRE_FALSE(store.IsValid(handles.mesh));
        REQUIRE_FALSE(store.IsValid(handles.image));
    }
}

TEST_CASE("The empty handle makes every assets native do nothing", "[assets][scripting][noops]")
{
    Testing::NativeCallHarness harness({Assets::ScriptingExtension()});
    Assets installed = InstallAssets(harness);
    EachDoesNothing(harness, HandleCalls(Handles{}), StaleHandle);
}

TEST_CASE("The public Resolve<Model> is null with no store, then with a stale handle, warning once each in Assets' "
          "words under the caller's native",
          "[assets][scripting][noops]")
{
    // Resolve<Model> is reached as another package's native reaches it (FenrirRenderer's SpawnModel): through a
    // registered native that resolves the kind C# names, here Unload, so the warning names the caller, in Assets'
    // words. Unloading a live model is the resolve that finds the store, and leaves the handle stale.
    constexpr const char* Native = "AssetsNative.Unload";
    Testing::NativeCallHarness harness({Assets::ScriptingExtension()});
    const auto unload = [&](const ScriptAssetHandle& handle) {
        return harness.Call<bool>(Native, Detail::AssetKind::Model, &handle);
    };
    // The handle that would name the first model: slot 0, generation 1.
    const ScriptAssetHandle first{0, 1};

    for (int time = 0; time < 2; ++time)
    {
        INFO("Resolve<Model> found a store in an app with none");
        REQUIRE_FALSE(unload(first));
    }
    {
        INFO("Two resolves with no store from one place did not warn once, under the caller's native, in Assets' "
             "words");
        REQUIRE(harness.WarnedOnce(Native, AssetResolve::NoStore));
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
        REQUIRE(unload(Generic(model)));
    }
    {
        INFO("Unloading the model did not leave its handle stale");
        REQUIRE_FALSE(store.IsValid(model));
    }
    for (int time = 0; time < 2; ++time)
    {
        INFO("Resolve<Model> resolved a stale model");
        REQUIRE_FALSE(unload(Generic(model)));
    }
    {
        INFO("Two resolves of a stale model from one place did not warn once, under the caller's native, in Assets' "
             "words");
        REQUIRE(harness.WarnedOnce(Native, AssetResolve::StaleHandle("model")));
    }
    {
        INFO("The empty model handle did not resolve as stale");
        REQUIRE_FALSE(unload(ScriptAssetHandle{}));
    }
}
