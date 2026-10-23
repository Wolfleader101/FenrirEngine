using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Assets;

/// <summary>
/// How a material's pixels combine with what is behind them: it picks the render stage and whether depth is written
/// (ADR-0024). FenrirAssets' <c>BlendMode</c>.
/// </summary>
public enum BlendMode : byte
{
    /// <summary>Drawn over what is behind, writing depth.</summary>
    Opaque,
    /// <summary>Opaque, but a pixel whose alpha is below the material's alpha cutoff is discarded.</summary>
    Masked,
    /// <summary>Mixed with what is behind by its alpha, without writing depth.</summary>
    AlphaBlend,
    /// <summary>Added to what is behind, without writing depth.</summary>
    Additive,
}

/// <summary>What a texture does with coordinates outside [0, 1]. FenrirAssets' <c>SamplerWrap</c>.</summary>
public enum SamplerWrap : byte
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
}

/// <summary>How a texture is filtered between its pixels and its mips. FenrirAssets' <c>SamplerFilter</c>.</summary>
public enum SamplerFilter : byte
{
    /// <summary>The nearest pixel of the nearest mip.</summary>
    Nearest,
    /// <summary>Linear between pixels, from the nearest mip.</summary>
    Bilinear,
    /// <summary>Linear between pixels and between mips.</summary>
    Trilinear,
}

/// <summary>
/// How a texture parameter is sampled (ADR-0024), laid out exactly like FenrirAssets' <c>SamplerDescription</c>. As in C++,
/// <c>new SamplerDescription()</c> is the default sampler: repeat, trilinear, anisotropy 8. <c>default</c> is all zeroes:
/// repeat, nearest, anisotropy 0.
/// </summary>
public struct SamplerDescription : IEquatable<SamplerDescription>
{
    /// <summary>The wrap across the texture's width, the U coordinate.</summary>
    public SamplerWrap WrapU;
    /// <summary>The wrap down the texture's height, the V coordinate.</summary>
    public SamplerWrap WrapV;
    public SamplerFilter Filter;
    /// <summary>The most anisotropic filtering taken, 1 for none. The renderer clamps it to what the device has.</summary>
    public uint Anisotropy;

    /// <summary>The default sampler: repeat, trilinear, anisotropy 8.</summary>
    public SamplerDescription()
    {
        WrapU = SamplerWrap.Repeat;
        WrapV = SamplerWrap.Repeat;
        Filter = SamplerFilter.Trilinear;
        Anisotropy = 8;
    }

    public readonly bool Equals(SamplerDescription other) =>
        WrapU == other.WrapU && WrapV == other.WrapV && Filter == other.Filter && Anisotropy == other.Anisotropy;

    public override readonly bool Equals(object? obj) => obj is SamplerDescription other && Equals(other);

    public override readonly int GetHashCode() => HashCode.Combine(WrapU, WrapV, Filter, Anisotropy);

    public static bool operator ==(SamplerDescription a, SamplerDescription b) => a.Equals(b);

    public static bool operator !=(SamplerDescription a, SamplerDescription b) => !a.Equals(b);
}

/// <summary>
/// A texture parameter's value, laid out exactly like FenrirAssets' <c>MaterialTexture</c>: an image and the sampler it is
/// read with. An empty image stands for the shader's default texture (see <see cref="ShaderParameter.Texture"/>).
/// </summary>
public struct MaterialTexture : IEquatable<MaterialTexture>
{
    public AssetHandle<Image> Image;
    public SamplerDescription Sampler;

    /// <summary>No image (the shader's default texture), read with the default sampler.</summary>
    public MaterialTexture()
    {
        Image = default;
        Sampler = new SamplerDescription();
    }

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
/// warning is logged once per process for each place in the engine's native code that noticed.
/// </para>
/// </remarks>
public static class Materials
{
    /// <summary>A new material drawing with <paramref name="shader"/>: opaque, single-sided, with no overrides.</summary>
    /// <returns>The material, or the empty handle if the shader handle does not resolve.</returns>
    public static AssetHandle<Material> Create(AssetHandle<Shader> shader, AssetOwner owner = AssetOwner.Scene) =>
        AssetHandle<Material>.FromBits(MaterialsNative.Create(shader.Bits, (byte)owner));

    /// <summary>
    /// A new material copying <paramref name="material"/>'s shader, blend mode, alpha cutoff, double-sidedness and parameter
    /// overrides. Only overrides are copied, so the clone's unset parameters still follow the shader's defaults.
    /// </summary>
    /// <returns>The clone, or the empty handle if <paramref name="material"/> does not resolve.</returns>
    public static AssetHandle<Material> Clone(AssetHandle<Material> material, AssetOwner owner = AssetOwner.Scene) =>
        AssetHandle<Material>.FromBits(MaterialsNative.Clone(material.Bits, (byte)owner));

    /// <summary>Sets a <c>float</c> parameter.</summary>
    public static bool SetFloat(AssetHandle<Material> material, ReadOnlySpan<char> name, float value) =>
        MaterialsNative.SetFloat(material.Bits, name, value);

    /// <summary>Sets a <c>vec2</c> parameter.</summary>
    public static bool SetVector2(AssetHandle<Material> material, ReadOnlySpan<char> name, Vector2 value) =>
        MaterialsNative.SetVector2(material.Bits, name, in value);

    /// <summary>Sets a <c>vec3</c> parameter.</summary>
    public static bool SetVector3(AssetHandle<Material> material, ReadOnlySpan<char> name, Vector3 value) =>
        MaterialsNative.SetVector3(material.Bits, name, in value);

    /// <summary>Sets a <c>vec4</c> parameter.</summary>
    public static bool SetVector4(AssetHandle<Material> material, ReadOnlySpan<char> name, Vector4 value) =>
        MaterialsNative.SetVector4(material.Bits, name, in value);

    /// <summary>Sets a <c>vec4</c> parameter to a colour's linear red, green, blue and alpha.</summary>
    public static bool SetColour(AssetHandle<Material> material, ReadOnlySpan<char> name, Colour value) =>
        MaterialsNative.SetColour(material.Bits, name, in value);

    /// <summary>Sets a texture parameter to <paramref name="image"/>, read with the default sampler.</summary>
    public static bool SetTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, AssetHandle<Image> image) =>
        SetTexture(material, name, image, new SamplerDescription());

    /// <summary>Sets a texture parameter to <paramref name="image"/>, read with <paramref name="sampler"/>.</summary>
    public static bool SetTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, AssetHandle<Image> image,
                                  SamplerDescription sampler) =>
        MaterialsNative.SetTexture(material.Bits, name, image.Bits, in sampler);

    /// <summary>Sets how the material's pixels combine with what is behind them.</summary>
    public static bool SetBlendMode(AssetHandle<Material> material, BlendMode blendMode) =>
        MaterialsNative.SetBlendMode(material.Bits, (byte)blendMode);

    /// <summary>Sets the alpha below which a <see cref="BlendMode.Masked"/> material discards a pixel.</summary>
    public static bool SetAlphaCutoff(AssetHandle<Material> material, float alphaCutoff) =>
        MaterialsNative.SetAlphaCutoff(material.Bits, alphaCutoff);

    /// <summary>Sets whether both faces of the material are drawn.</summary>
    public static bool SetDoubleSided(AssetHandle<Material> material, bool doubleSided) =>
        MaterialsNative.SetDoubleSided(material.Bits, doubleSided);

    /// <summary>
    /// Reads the parameter's effective value: the material's override, else the shader's parameter default.
    /// </summary>
    /// <returns>False only if the material does not resolve, its shader lacks the name, or the parameter is another type.</returns>
    public static bool TryGetFloat(AssetHandle<Material> material, ReadOnlySpan<char> name, out float value) =>
        MaterialsNative.TryGetFloat(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetFloat"/>
    public static bool TryGetVector2(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector2 value) =>
        MaterialsNative.TryGetVector2(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetFloat"/>
    public static bool TryGetVector3(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector3 value) =>
        MaterialsNative.TryGetVector3(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetFloat"/>
    public static bool TryGetVector4(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector4 value) =>
        MaterialsNative.TryGetVector4(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetFloat"/>
    public static bool TryGetColour(AssetHandle<Material> material, ReadOnlySpan<char> name, out Colour value) =>
        MaterialsNative.TryGetColour(material.Bits, name, out value);

    /// <summary>
    /// Reads the texture parameter's effective value. Left unset, it reads the empty image (the shader's default texture)
    /// with the default sampler.
    /// </summary>
    /// <returns>False only if the material does not resolve, its shader lacks the name, or the parameter is another type.</returns>
    public static bool TryGetTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, out MaterialTexture value) =>
        MaterialsNative.TryGetTexture(material.Bits, name, out value);

    /// <summary>
    /// Reads the material's own override of the parameter, if it sets one of this type. It does not consult the shader, so
    /// it also reads an override the shader no longer has.
    /// </summary>
    public static bool TryGetOverrideFloat(AssetHandle<Material> material, ReadOnlySpan<char> name, out float value) =>
        MaterialsNative.TryGetOverrideFloat(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideVector2(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector2 value) =>
        MaterialsNative.TryGetOverrideVector2(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideVector3(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector3 value) =>
        MaterialsNative.TryGetOverrideVector3(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideVector4(AssetHandle<Material> material, ReadOnlySpan<char> name, out Vector4 value) =>
        MaterialsNative.TryGetOverrideVector4(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideColour(AssetHandle<Material> material, ReadOnlySpan<char> name, out Colour value) =>
        MaterialsNative.TryGetOverrideColour(material.Bits, name, out value);

    /// <inheritdoc cref="TryGetOverrideFloat"/>
    public static bool TryGetOverrideTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, out MaterialTexture value) =>
        MaterialsNative.TryGetOverrideTexture(material.Bits, name, out value);

    /// <summary>Whether the material has an override of the parameter.</summary>
    public static bool IsSet(AssetHandle<Material> material, ReadOnlySpan<char> name) =>
        MaterialsNative.IsSet(material.Bits, name);

    /// <summary>
    /// Removes the material's override of the parameter, so it reads the shader's default again.
    /// </summary>
    /// <returns>True, and the revision bumped, only if there was an override to remove.</returns>
    public static bool Reset(AssetHandle<Material> material, ReadOnlySpan<char> name) =>
        MaterialsNative.Reset(material.Bits, name);
}

// The native functions behind Materials, each named "Fenrir.Assets.MaterialsNative.<Method>" in FenrirAssetsScripting's
// extension. Handles cross as their bits, names as UTF-8, and values in and out as pointers.
internal static unsafe partial class MaterialsNative
{
    [NativeFunction]
    public static partial ulong Create(ulong shader, byte owner);

    [NativeFunction]
    public static partial ulong Clone(ulong material, byte owner);

    [NativeFunction]
    public static partial bool SetFloat(ulong material, ReadOnlySpan<char> name, float value);

    [NativeFunction]
    public static partial bool SetVector2(ulong material, ReadOnlySpan<char> name, in Vector2 value);

    [NativeFunction]
    public static partial bool SetVector3(ulong material, ReadOnlySpan<char> name, in Vector3 value);

    [NativeFunction]
    public static partial bool SetVector4(ulong material, ReadOnlySpan<char> name, in Vector4 value);

    [NativeFunction]
    public static partial bool SetColour(ulong material, ReadOnlySpan<char> name, in Colour value);

    [NativeFunction]
    public static partial bool SetTexture(ulong material, ReadOnlySpan<char> name, ulong image, in SamplerDescription sampler);

    [NativeFunction]
    public static partial bool SetBlendMode(ulong material, byte blendMode);

    [NativeFunction]
    public static partial bool SetAlphaCutoff(ulong material, float alphaCutoff);

    [NativeFunction]
    public static partial bool SetDoubleSided(ulong material, bool doubleSided);

    [NativeFunction]
    public static partial bool TryGetFloat(ulong material, ReadOnlySpan<char> name, out float value);

    [NativeFunction]
    public static partial bool TryGetVector2(ulong material, ReadOnlySpan<char> name, out Vector2 value);

    [NativeFunction]
    public static partial bool TryGetVector3(ulong material, ReadOnlySpan<char> name, out Vector3 value);

    [NativeFunction]
    public static partial bool TryGetVector4(ulong material, ReadOnlySpan<char> name, out Vector4 value);

    [NativeFunction]
    public static partial bool TryGetColour(ulong material, ReadOnlySpan<char> name, out Colour value);

    [NativeFunction]
    public static partial bool TryGetTexture(ulong material, ReadOnlySpan<char> name, out MaterialTexture value);

    [NativeFunction]
    public static partial bool TryGetOverrideFloat(ulong material, ReadOnlySpan<char> name, out float value);

    [NativeFunction]
    public static partial bool TryGetOverrideVector2(ulong material, ReadOnlySpan<char> name, out Vector2 value);

    [NativeFunction]
    public static partial bool TryGetOverrideVector3(ulong material, ReadOnlySpan<char> name, out Vector3 value);

    [NativeFunction]
    public static partial bool TryGetOverrideVector4(ulong material, ReadOnlySpan<char> name, out Vector4 value);

    [NativeFunction]
    public static partial bool TryGetOverrideColour(ulong material, ReadOnlySpan<char> name, out Colour value);

    [NativeFunction]
    public static partial bool TryGetOverrideTexture(ulong material, ReadOnlySpan<char> name, out MaterialTexture value);

    [NativeFunction]
    public static partial bool IsSet(ulong material, ReadOnlySpan<char> name);

    [NativeFunction]
    public static partial bool Reset(ulong material, ReadOnlySpan<char> name);
}
