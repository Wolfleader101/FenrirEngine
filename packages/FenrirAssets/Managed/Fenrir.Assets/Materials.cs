using Fenrir.Math;

namespace Fenrir.Assets;

// SamplerDescription's and MaterialTexture's fields are generated (Generated/, from FenrirAssets's MaterialData.hpp), as
// C++'s defaults: new SamplerDescription() is the default sampler (repeat, trilinear, anisotropy 8), and default is all
// zeroes (repeat, nearest, anisotropy 0). These parts add equality, as C++'s operator== does.
public partial struct SamplerDescription : IEquatable<SamplerDescription>
{
    public readonly bool Equals(SamplerDescription other) =>
        WrapU == other.WrapU && WrapV == other.WrapV && Filter == other.Filter && Anisotropy == other.Anisotropy;

    public override readonly bool Equals(object? obj) => obj is SamplerDescription other && Equals(other);

    public override readonly int GetHashCode() => HashCode.Combine(WrapU, WrapV, Filter, Anisotropy);

    public static bool operator ==(SamplerDescription a, SamplerDescription b) => a.Equals(b);

    public static bool operator !=(SamplerDescription a, SamplerDescription b) => !a.Equals(b);
}

// new MaterialTexture() is no image (the shader's default texture, see ShaderParameter.Texture), read with the default
// sampler.
public partial struct MaterialTexture : IEquatable<MaterialTexture>
{
    /// <summary><paramref name="image"/>, read with <paramref name="sampler"/>.</summary>
    public MaterialTexture(AssetHandle<Image> image, SamplerDescription sampler)
    {
        Image = image;
        Sampler = sampler;
    }

    public readonly bool Equals(MaterialTexture other) => Image == other.Image && Sampler == other.Sampler;

    public override readonly bool Equals(object? obj) => obj is MaterialTexture other && Equals(other);

    public override readonly int GetHashCode() => HashCode.Combine(Image, Sampler);

    public static bool operator ==(MaterialTexture a, MaterialTexture b) => a.Equals(b);

    public static bool operator !=(MaterialTexture a, MaterialTexture b) => !a.Equals(b);
}

/// <summary>
/// Makes and edits materials by name, exactly as FenrirAssets' material edits do in C++ (ADR-0024; the C++ documentation
/// says what each does), so C++ and C# agree on names, types and revisions. A material stores only its parameter overrides;
/// every other parameter reads its shader's parameter default.
/// </summary>
/// <remarks>
/// Each set bumps the material's revision; a <see cref="Reset"/> bumps it only when it removed an override, and reads never
/// do. A parameter set checks the name and type against the shader: an unknown name or a wrong type is logged once per
/// material and ignored, and the set returns <c>false</c>. A colour is a <c>vec4</c> parameter, in linear values.
/// <para>
/// Nothing here throws. With no asset store in the app, or a stale or empty handle, a function does nothing and returns
/// <c>false</c> or the empty handle, with the value of an <c>out</c> parameter zeroed. In a debug build of the engine a
/// warning is logged once per scripting install for each place in the engine's native code that noticed.
/// </para>
/// </remarks>
public static class Materials
{
    /// <summary>A new material drawing with <paramref name="shader"/>: opaque, single-sided, with no overrides.</summary>
    /// <returns>The material, or the empty handle if the shader handle does not resolve.</returns>
    public static AssetHandle<Material> Create(AssetHandle<Shader> shader, AssetOwner owner = AssetOwner.Scene) =>
        AssetHandle<Material>.FromBits(AssetsNative.CreateMaterial(shader.Bits, owner));

    /// <summary>
    /// A new material copying <paramref name="material"/>'s shader, blend mode, alpha cutoff, double-sidedness and parameter
    /// overrides. Only overrides are copied, so the clone's unset parameters still follow the shader's defaults.
    /// </summary>
    /// <returns>The clone, or the empty handle if <paramref name="material"/> does not resolve.</returns>
    public static AssetHandle<Material> Clone(AssetHandle<Material> material, AssetOwner owner = AssetOwner.Scene) =>
        AssetHandle<Material>.FromBits(AssetsNative.CloneMaterial(material.Bits, owner));

    /// <summary>Sets a <c>float</c> parameter.</summary>
    public static bool SetFloat(AssetHandle<Material> material, ReadOnlySpan<char> name, float value) =>
        AssetsNative.SetFloat(material.Bits, name, value);

    /// <summary>Sets a <c>vec2</c> parameter.</summary>
    public static bool SetVector2(AssetHandle<Material> material, ReadOnlySpan<char> name, Vector2 value) =>
        AssetsNative.SetVector2(material.Bits, name, in value);

    /// <summary>Sets a <c>vec3</c> parameter.</summary>
    public static bool SetVector3(AssetHandle<Material> material, ReadOnlySpan<char> name, Vector3 value) =>
        AssetsNative.SetVector3(material.Bits, name, in value);

    /// <summary>Sets a <c>vec4</c> parameter.</summary>
    public static bool SetVector4(AssetHandle<Material> material, ReadOnlySpan<char> name, Vector4 value) =>
        AssetsNative.SetVector4(material.Bits, name, in value);

    /// <summary>Sets a <c>vec4</c> parameter to a colour's linear red, green, blue and alpha.</summary>
    public static bool SetColour(AssetHandle<Material> material, ReadOnlySpan<char> name, Colour value) =>
        AssetsNative.SetColour(material.Bits, name, in value);

    /// <summary>Sets a texture parameter to <paramref name="image"/>, read with the default sampler.</summary>
    public static bool SetTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, AssetHandle<Image> image) =>
        SetTexture(material, name, image, new SamplerDescription());

    /// <summary>Sets a texture parameter to <paramref name="image"/>, read with <paramref name="sampler"/>.</summary>
    public static bool SetTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, AssetHandle<Image> image,
                                  SamplerDescription sampler) =>
        AssetsNative.SetTexture(material.Bits, name, image.Bits, in sampler);

    /// <summary>Sets how the material's pixels combine with what is behind them.</summary>
    public static bool SetBlendMode(AssetHandle<Material> material, BlendMode blendMode) =>
        AssetsNative.SetBlendMode(material.Bits, blendMode);

    /// <summary>Sets the alpha below which a <see cref="BlendMode.Masked"/> material discards a pixel.</summary>
    public static bool SetAlphaCutoff(AssetHandle<Material> material, float alphaCutoff) =>
        AssetsNative.SetAlphaCutoff(material.Bits, alphaCutoff);

    /// <summary>Sets whether both faces of the material are drawn.</summary>
    public static bool SetDoubleSided(AssetHandle<Material> material, bool doubleSided) =>
        AssetsNative.SetDoubleSided(material.Bits, doubleSided);

    /// <summary>
    /// Reads the parameter's effective value: the material's override, else the shader's parameter default.
    /// </summary>
    /// <returns>False only if the material does not resolve, its shader lacks the name, or the parameter is another type.</returns>
    public static bool TryGetFloat(AssetHandle<Material> material, ReadOnlySpan<char> name, out float value) =>
        Read(AssetsNative.TryGetFloat(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetFloat"/>
    public static bool TryGetVector2(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector2 value) =>
        Read(AssetsNative.TryGetVector2(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetFloat"/>
    public static bool TryGetVector3(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector3 value) =>
        Read(AssetsNative.TryGetVector3(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetFloat"/>
    public static bool TryGetVector4(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector4 value) =>
        Read(AssetsNative.TryGetVector4(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetFloat"/>
    public static bool TryGetColour(AssetHandle<Material> material, ReadOnlySpan<char> name, out Colour value) =>
        Read(AssetsNative.TryGetColour(material.Bits, name, out value), ref value);

    /// <summary>
    /// Reads the texture parameter's effective value. Left unset, it reads the empty image (the shader's default texture)
    /// with the default sampler.
    /// </summary>
    /// <returns>False only if the material does not resolve, its shader lacks the name, or the parameter is another type.</returns>
    public static bool TryGetTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, out MaterialTexture value) =>
        Read(AssetsNative.TryGetTexture(material.Bits, name, out value), ref value);

    /// <summary>
    /// Reads the material's own override of the parameter, if it sets one of this type. It does not consult the shader, so
    /// it also reads an override the shader no longer has.
    /// </summary>
    public static bool TryGetOverrideFloat(AssetHandle<Material> material, ReadOnlySpan<char> name, out float value) =>
        Read(AssetsNative.TryGetOverrideFloat(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideVector2(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector2 value) =>
        Read(AssetsNative.TryGetOverrideVector2(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideVector3(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector3 value) =>
        Read(AssetsNative.TryGetOverrideVector3(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideVector4(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector4 value) =>
        Read(AssetsNative.TryGetOverrideVector4(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideColour(AssetHandle<Material> material, ReadOnlySpan<char> name, out Colour value) =>
        Read(AssetsNative.TryGetOverrideColour(material.Bits, name, out value), ref value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, out MaterialTexture value) =>
        Read(AssetsNative.TryGetOverrideTexture(material.Bits, name, out value), ref value);

    /// <summary>Whether the material has an override of the parameter.</summary>
    public static bool IsSet(AssetHandle<Material> material, ReadOnlySpan<char> name) =>
        AssetsNative.IsSet(material.Bits, name);

    /// <summary>
    /// Removes the material's override of the parameter, so it reads the shader's default again.
    /// </summary>
    /// <returns>True, and the revision bumped, only if there was an override to remove.</returns>
    public static bool Reset(AssetHandle<Material> material, ReadOnlySpan<char> name) =>
        AssetsNative.Reset(material.Bits, name);

    // A read's result, with `value` zeroed when it read nothing: the native writes C++'s default there (a colour's white),
    // and a TryGet that fails gives default.
    private static bool Read<T>(bool found, ref T value) where T : struct
    {
        if (!found)
        {
            value = default;
        }
        return found;
    }
}
