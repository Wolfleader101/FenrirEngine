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

// Every edit and read of a material is generated (Generated/Materials.cs, from the natives' @facade marks in
// Scripting/src/AssetsScripting.cpp): the sets, the reads with their bool and out, IsSet and Reset, and Create and Clone
// taking the owner. This part holds what a description cannot say: the class doc and the overloads that default the owner
// to the active scene and the sampler to the default one.

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
/// <c>false</c> or the empty handle, with an <c>out</c> parameter left at its C++ default (zero, the default sampler, white
/// for a colour). In a debug build of the engine a warning is logged once per scripting install for each place in the
/// engine's native code that noticed.
/// </para>
/// </remarks>
public static partial class Materials
{
    /// <summary><see cref="Create(AssetHandle{Shader}, AssetOwner)"/> for the active scene.</summary>
    public static AssetHandle<Material> Create(AssetHandle<Shader> shader) => Create(shader, AssetOwner.Scene);

    /// <summary><see cref="Clone(AssetHandle{Material}, AssetOwner)"/> for the active scene.</summary>
    public static AssetHandle<Material> Clone(AssetHandle<Material> material) => Clone(material, AssetOwner.Scene);

    /// <summary>Sets a texture parameter to <paramref name="image"/>, read with the default sampler.</summary>
    public static bool SetTexture(AssetHandle<Material> material, ReadOnlySpan<char> name, AssetHandle<Image> image) =>
        SetTexture(material, name, image, new SamplerDescription());
}
