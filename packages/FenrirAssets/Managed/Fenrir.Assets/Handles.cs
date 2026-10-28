namespace Fenrir.Assets;

/// <summary>A mesh asset: the kind an <see cref="AssetHandle{T}"/> names. Never made.</summary>
public sealed class Mesh
{
    private Mesh()
    {
    }
}

/// <summary>An image asset: a picture, a cubemap or a render target. Never made.</summary>
public sealed class Image
{
    private Image()
    {
    }
}

/// <summary>A material asset: a shader, a blend mode and parameter overrides. Never made.</summary>
public sealed class Material
{
    private Material()
    {
    }
}

/// <summary>A shader asset: a shader container and its parameter table. Never made.</summary>
public sealed class Shader
{
    private Shader()
    {
    }
}

/// <summary>A model asset: an imported file's node tree, naming its meshes and materials. Never made.</summary>
public sealed class Model
{
    private Model()
    {
    }
}

/// <summary>
/// Names one asset of kind <typeparamref name="T"/> (<see cref="Mesh"/>, <see cref="Image"/>, <see cref="Material"/>,
/// <see cref="Shader"/> or <see cref="Model"/>) in the asset store, laid out exactly like FenrirAssets'
/// <c>AssetHandle&lt;T&gt;</c>: a slot index and the slot's generation when the asset was made (ADR-0021). Plain data that
/// keeps nothing alive. Once the asset is freed the handle is stale: it no longer resolves, and everything that takes it
/// does nothing. <c>default</c> is the empty handle, which names no asset.
/// </summary>
public struct AssetHandle<T> : IEquatable<AssetHandle<T>> where T : class
{
    /// <summary>The slot in the store's table of <typeparamref name="T"/>.</summary>
    public uint Index;
    /// <summary>The slot's generation when the asset was made; zero only in the empty handle.</summary>
    public uint Generation;

    public AssetHandle(uint index, uint generation)
    {
        Index = index;
        Generation = generation;
    }

    /// <summary>The empty handle, which names no asset.</summary>
    public static AssetHandle<T> None => default;

    /// <summary>
    /// Whether this is the empty handle. A handle that is not empty may still be stale: ask
    /// <see cref="Fenrir.Assets.Assets.IsValid{T}"/>.
    /// </summary>
    public readonly bool IsEmpty => Generation == 0;

    public readonly bool Equals(AssetHandle<T> other) => Index == other.Index && Generation == other.Generation;

    public override readonly bool Equals(object? obj) => obj is AssetHandle<T> other && Equals(other);

    public override readonly int GetHashCode() => HashCode.Combine(Index, Generation);

    public static bool operator ==(AssetHandle<T> a, AssetHandle<T> b) => a.Equals(b);

    public static bool operator !=(AssetHandle<T> a, AssetHandle<T> b) => !a.Equals(b);

    public override readonly string ToString() => $"AssetHandle<{typeof(T).Name}>({Index}, {Generation})";

    // How a handle crosses the three kind-generic natives (Load, Unload, IsValid), which take the kind beside it: as
    // the kind-neutral NativeAssetHandle (the mirror of C++'s ScriptAssetHandle), laid out as this is. Every other
    // native takes an AssetHandle<T> itself, by `in` or `out`.
    internal readonly NativeAssetHandle ToNative() => new() { Index = Index, Generation = Generation };

    internal static AssetHandle<T> FromNative(NativeAssetHandle native) => new(native.Index, native.Generation);
}

// The kind each asset name crosses to a native function as: FenrirAssets' Detail::AssetKind, generated as AssetKind.
internal static class AssetKind<T> where T : class
{
    // No kind of asset: the natives warn and do nothing.
    internal const AssetKind Unknown = (AssetKind)255;

    internal static readonly AssetKind Value =
        typeof(T) == typeof(Mesh) ? AssetKind.Mesh :
        typeof(T) == typeof(Image) ? AssetKind.Image :
        typeof(T) == typeof(Material) ? AssetKind.Material :
        typeof(T) == typeof(Shader) ? AssetKind.Shader :
        typeof(T) == typeof(Model) ? AssetKind.Model :
        Unknown;
}
