using Fenrir.Assets;
using Fenrir.Math;

namespace AssetsScriptFixture;

// Every Fenrir.Assets function at once, for the no-op rule: with no asset store, or with stale handles, each should do
// nothing and return false, zero or the empty handle, with its out values zeroed.
public static class NoOpCalls
{
    // Every call's result as one line of "Name=value", in a fixed order: those that make an asset, then EveryHandleCall's.
    // A handle reads as its bits.
    public static string EveryCall(ulong meshBits, ulong imageBits, ulong materialBits, ulong shaderBits)
    {
        ReadOnlySpan<Vector3> positions = [new(0, 0, 0), new(1, 0, 0), new(0, 1, 0)];
        ReadOnlySpan<uint> indices = [0, 1, 2];
        byte[] pixel = [255, 255, 255, 255];
        var results = new List<string>
        {
            "Load=" + Bits.Of(Assets.Load<Mesh>("models/scene.gltf#Mesh0")),
            "AddMesh=" + Bits.Of(Assets.AddMesh(positions, [], [], indices)),
            "AddRenderTarget=" + Bits.Of(Assets.AddRenderTarget(4, 4)),
            "AddImage=" + Bits.Of(Assets.AddImage(1, 1, pixel)),
        };
        return string.Join("\n", results) + "\n" + EveryHandleCall(meshBits, imageBits, materialBits, shaderBits);
    }

    // The result of every call that takes a handle, one line of "Name=value" each, in a fixed order.
    public static string EveryHandleCall(ulong meshBits, ulong imageBits, ulong materialBits, ulong shaderBits)
    {
        AssetHandle<Mesh> mesh = Bits.To<Mesh>(meshBits);
        AssetHandle<Image> image = Bits.To<Image>(imageBits);
        AssetHandle<Material> material = Bits.To<Material>(materialBits);
        AssetHandle<Shader> shader = Bits.To<Shader>(shaderBits);
        ReadOnlySpan<Vector3> positions = [new(0, 0, 0), new(1, 0, 0), new(0, 1, 0)];
        ReadOnlySpan<uint> indices = [0, 1, 2];
        byte[] pixel = [255, 255, 255, 255];
        Span<char> name = stackalloc char[16];

        var results = new List<string>
        {
            "IsValid=" + Assets.IsValid(mesh),
            "Unload=" + Assets.Unload(mesh),
            "ReplaceMesh=" + Assets.ReplaceMesh(mesh, positions, [], [], indices),
            "ResizeRenderTarget=" + Assets.ResizeRenderTarget(image, 8, 8),
            "ReplaceImage=" + Assets.ReplaceImage(image, 1, 1, pixel),
            "Create=" + Bits.Of(Materials.Create(shader)),
            "Clone=" + Bits.Of(Materials.Clone(material)),
            "SetFloat=" + Materials.SetFloat(material, "roughness", 1),
            "SetVector2=" + Materials.SetVector2(material, "offset", new Vector2(1, 1)),
            "SetVector3=" + Materials.SetVector3(material, "emissive", new Vector3(1, 1, 1)),
            "SetVector4=" + Materials.SetVector4(material, "tint", new Vector4(1, 1, 1, 1)),
            "SetColour=" + Materials.SetColour(material, "tint", Colour.White),
            "SetTexture=" + Materials.SetTexture(material, "baseColourTexture", image),
            "SetBlendMode=" + Materials.SetBlendMode(material, BlendMode.Additive),
            "SetAlphaCutoff=" + Materials.SetAlphaCutoff(material, 0.1f),
            "SetDoubleSided=" + Materials.SetDoubleSided(material, true),
            "TryGetFloat=" + Materials.TryGetFloat(material, "roughness", out float f) + ":" + f,
            "TryGetVector2=" + Materials.TryGetVector2(material, "offset", out Vector2 v2) + ":" + (v2 == default),
            "TryGetVector3=" + Materials.TryGetVector3(material, "emissive", out Vector3 v3) + ":" + (v3 == default),
            "TryGetVector4=" + Materials.TryGetVector4(material, "tint", out Vector4 v4) + ":" + (v4 == default),
            "TryGetColour=" + Materials.TryGetColour(material, "tint", out Colour c) + ":" + (c == default),
            "TryGetTexture=" + Materials.TryGetTexture(material, "baseColourTexture", out MaterialTexture t) + ":" +
                (t == default),
            "TryGetOverrideFloat=" + Materials.TryGetOverrideFloat(material, "roughness", out float of) + ":" + of,
            "TryGetOverrideVector2=" + Materials.TryGetOverrideVector2(material, "offset", out Vector2 ov2) + ":" +
                (ov2 == default),
            "TryGetOverrideVector3=" + Materials.TryGetOverrideVector3(material, "emissive", out Vector3 ov3) + ":" +
                (ov3 == default),
            "TryGetOverrideVector4=" + Materials.TryGetOverrideVector4(material, "tint", out Vector4 ov4) + ":" +
                (ov4 == default),
            "TryGetOverrideColour=" + Materials.TryGetOverrideColour(material, "tint", out Colour oc) + ":" + (oc == default),
            "TryGetOverrideTexture=" + Materials.TryGetOverrideTexture(material, "baseColourTexture", out MaterialTexture ot) +
                ":" + (ot == default),
            "IsSet=" + Materials.IsSet(material, "roughness"),
            "Reset=" + Materials.Reset(material, "roughness"),
            "GetParameterCount=" + Shaders.GetParameterCount(shader),
            "GetParameter=" + Shaders.GetParameter(shader, 0, out ShaderParameter parameter, name) + ":" +
                (parameter.Type == default && parameter.Value == default && parameter.Texture == default),
        };
        return string.Join("\n", results);
    }
}
