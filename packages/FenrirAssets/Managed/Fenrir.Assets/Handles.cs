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

    // How a handle crosses to a native function: one number, the generation in the high half and the index in the low.
    internal readonly ulong Bits => ((ulong)Generation << 32) | Index;

    internal static AssetHandle<T> FromBits(ulong bits) => new((uint)bits, (uint)(bits >> 32));
}

/// <summary>
/// Who an asset is loaded or added for. It lives until its last owner ends, or until it is unloaded; naming an owner
/// twice is the same as naming it once.
/// </summary>
public enum AssetOwner : byte
{
    /// <summary>The active scene: the asset is released when that scene is destroyed.</summary>
    Scene,
    /// <summary>The app: the asset lives as long as the asset store.</summary>
    App,
}

// The number each asset kind crosses to a native function as: FenrirAssets' Detail::AssetKind.
internal static class AssetKind<T> where T : class
{
    internal const byte Unknown = 255;

    internal static readonly byte Value =
        typeof(T) == typeof(Mesh) ? (byte)0 :
        typeof(T) == typeof(Image) ? (byte)1 :
        typeof(T) == typeof(Material) ? (byte)2 :
        typeof(T) == typeof(Shader) ? (byte)3 :
        typeof(T) == typeof(Model) ? (byte)4 :
        Unknown;
}
