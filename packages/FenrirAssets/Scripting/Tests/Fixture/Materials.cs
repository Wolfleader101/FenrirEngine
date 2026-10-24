using System.Globalization;
using System.Text;
using Fenrir.Assets;
using Fenrir.Math;

namespace AssetsScriptFixture;

// Materials as C# makes, edits and reads them, called from the tests outside any system. The tests make the same edits from
// C++ and compare.
public static class MaterialCalls
{
    public static ulong Create(ulong shader) => Bits.Of(Materials.Create(Bits.To<Shader>(shader)));

    public static ulong Clone(ulong material) => Bits.Of(Materials.Clone(Bits.To<Material>(material)));

    // The tests' edit sequence, on the fixture shader's parameters. Returns each call's result, "1" or "0", in order.
    public static string Edit(ulong materialBits, ulong imageBits)
    {
        AssetHandle<Material> material = Bits.To<Material>(materialBits);
        var sampler = new SamplerDescription
        {
            WrapU = SamplerWrap.ClampToEdge,
            WrapV = SamplerWrap.MirroredRepeat,
            Filter = SamplerFilter.Nearest,
            Anisotropy = 1,
        };
        bool[] results =
        [
            Materials.SetFloat(material, "roughness", 0.25f),
            Materials.SetColour(material, "tint", new Colour(0.5f, 0.25f, 1, 1)),
            Materials.SetVector2(material, "offset", new Vector2(4, 5)),
            Materials.SetVector3(material, "emissive", new Vector3(1, 2, 3)),
            Materials.SetTexture(material, "baseColourTexture", Bits.To<Image>(imageBits), sampler),
            Materials.SetFloat(material, "tint", 1), // the wrong type
            Materials.SetFloat(material, "missing", 1), // a name the shader lacks
            Materials.SetVector4(material, "tint", new Vector4(0.5f, 0.25f, 1, 0.75f)),
            Materials.SetBlendMode(material, BlendMode.Masked),
            Materials.SetAlphaCutoff(material, 0.25f),
            Materials.SetDoubleSided(material, true),
            Materials.Reset(material, "emissive"),
            Materials.Reset(material, "emissive"), // nothing left to remove
        ];
        return string.Join(",", results.Select(result => result ? "1" : "0"));
    }

    public static bool Reset(ulong material, string name) => Materials.Reset(Bits.To<Material>(material), name);

    // Every read of every name the tests ask about, as the tests write the same reads from C++: one line per name, its IsSet
    // then each type's effective value and override ("-" when there is none).
    public static string Describe(ulong materialBits, string names)
    {
        AssetHandle<Material> material = Bits.To<Material>(materialBits);
        var text = new StringBuilder();
        foreach (string name in names.Split(','))
        {
            text.Append(name).Append(": set=").Append(Materials.IsSet(material, name) ? 1 : 0);
            text.Append(" float=")
                .Append(Materials.TryGetFloat(material, name, out float f) ? Number(f) : "-").Append('/')
                .Append(Materials.TryGetOverrideFloat(material, name, out float of) ? Number(of) : "-");
            text.Append(" vec2=")
                .Append(Materials.TryGetVector2(material, name, out Vector2 v2) ? Numbers(v2.X, v2.Y) : "-").Append('/')
                .Append(Materials.TryGetOverrideVector2(material, name, out Vector2 ov2) ? Numbers(ov2.X, ov2.Y) : "-");
            text.Append(" vec3=")
                .Append(Materials.TryGetVector3(material, name, out Vector3 v3) ? Numbers(v3.X, v3.Y, v3.Z) : "-").Append('/')
                .Append(Materials.TryGetOverrideVector3(material, name, out Vector3 ov3) ? Numbers(ov3.X, ov3.Y, ov3.Z) : "-");
            text.Append(" vec4=")
                .Append(Materials.TryGetVector4(material, name, out Vector4 v4) ? Numbers(v4.X, v4.Y, v4.Z, v4.W) : "-")
                .Append('/')
                .Append(Materials.TryGetOverrideVector4(material, name, out Vector4 ov4) ? Numbers(ov4.X, ov4.Y, ov4.Z, ov4.W) : "-");
            text.Append(" colour=")
                .Append(Materials.TryGetColour(material, name, out Colour c) ? Numbers(c.R, c.G, c.B, c.A) : "-").Append('/')
                .Append(Materials.TryGetOverrideColour(material, name, out Colour oc) ? Numbers(oc.R, oc.G, oc.B, oc.A) : "-");
            text.Append(" texture=")
                .Append(Materials.TryGetTexture(material, name, out MaterialTexture t) ? Texture(t) : "-").Append('/')
                .Append(Materials.TryGetOverrideTexture(material, name, out MaterialTexture ot) ? Texture(ot) : "-");
            text.Append('\n');
        }
        return text.ToString();
    }

    // What a default made SamplerDescription and MaterialTexture hold, as "wrapU,wrapV,filter,anisotropy" and
    // "index:generation".
    public static string DefaultSampler()
    {
        var sampler = new SamplerDescription();
        return $"{(int)sampler.WrapU},{(int)sampler.WrapV},{(int)sampler.Filter},{sampler.Anisotropy}";
    }

    public static bool DefaultTextureUsesDefaultSampler() => new MaterialTexture().Sampler == new SamplerDescription() &&
                                                              new MaterialTexture().Image.IsEmpty;

    private static string Number(float value) => value.ToString(CultureInfo.InvariantCulture);

    private static string Numbers(params float[] values) => string.Join(",", values.Select(Number));

    private static string Texture(MaterialTexture texture) =>
        $"{texture.Image.Index}:{texture.Image.Generation}|{(int)texture.Sampler.WrapU}|{(int)texture.Sampler.WrapV}|" +
        $"{(int)texture.Sampler.Filter}|{texture.Sampler.Anisotropy}";
}
