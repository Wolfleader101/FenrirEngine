using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Assets;

/// <summary>
/// The asset store from C#: loading, adding, replacing and unloading assets, as FenrirAssets' <c>AssetStore</c> does in C++
/// (whose documentation says what each does). Materials are edited through <see cref="Materials"/>, and shaders read
/// through <see cref="Shaders"/>.
/// </summary>
/// <remarks>
/// Inside a <c>Fenrir.*</c> namespace, <c>Assets</c> names this namespace: write <c>Fenrir.Assets.Assets</c> there, or use
/// an alias.
/// <para>
/// Nothing here throws. With no asset store in the app (FenrirAssets not installed), a stale or empty handle, or an
/// argument C++ refuses, a function does nothing and returns <c>false</c> or the empty handle. A load that fails returns the
/// empty handle, and the store logs why. In a debug build of the engine a warning is logged once per process for each place
/// in the engine's native code that noticed (not once per C# call site).
/// </para>
/// </remarks>
public static class Assets
{
    /// <summary>
    /// Loads the asset at <paramref name="path"/> for <paramref name="owner"/>: a path relative to the asset root (or
    /// absolute), <c>file#Label</c> for a model's mesh, material or image, or a <c>builtin:</c> name. A path already loaded
    /// gives back the same handle, with <paramref name="owner"/> added.
    /// </summary>
    /// <returns>The asset, or the empty handle if the load failed (logged with its reason).</returns>
    public static AssetHandle<T> Load<T>(ReadOnlySpan<char> path, AssetOwner owner = AssetOwner.Scene) where T : class =>
        AssetHandle<T>.FromBits(AssetsNative.Load(AssetKind<T>.Value, path, (byte)owner));

    /// <summary>Frees the asset now, whoever owns it: every handle to it becomes stale.</summary>
    /// <returns>Whether the handle named a live asset.</returns>
    public static bool Unload<T>(AssetHandle<T> handle) where T : class =>
        AssetsNative.Unload(AssetKind<T>.Value, handle.Bits);

    /// <summary>Whether <paramref name="handle"/> names a live asset: false for an empty or stale handle.</summary>
    public static bool IsValid<T>(AssetHandle<T> handle) where T : class =>
        AssetsNative.IsValid(AssetKind<T>.Value, handle.Bits);

    /// <summary>
    /// Adds a mesh made of these vertex streams and triangles (three indices each, counter-clockwise seen from the front),
    /// drawn with one material. <paramref name="normals"/> and <paramref name="uvs"/> may be empty; otherwise each holds one
    /// value per position. The data is copied, and the mesh's bounds are computed from the positions.
    /// </summary>
    /// <returns>The mesh, or the empty handle (and an error logged) if the streams or indices break those rules.</returns>
    public static unsafe AssetHandle<Mesh> AddMesh(ReadOnlySpan<Vector3> positions, ReadOnlySpan<Vector3> normals,
                                                   ReadOnlySpan<Vector2> uvs, ReadOnlySpan<uint> indices,
                                                   AssetOwner owner = AssetOwner.Scene)
    {
        fixed (Vector3* firstPosition = positions)
        fixed (Vector3* firstNormal = normals)
        fixed (Vector2* firstUv = uvs)
        fixed (uint* firstIndex = indices)
            return AssetHandle<Mesh>.FromBits(AssetsNative.AddMesh(firstPosition, positions.Length, firstNormal, normals.Length,
                                                                   firstUv, uvs.Length, firstIndex, indices.Length, (byte)owner));
    }

    /// <summary>
    /// Swaps <paramref name="mesh"/>'s data for these streams and triangles, as <see cref="AddMesh"/> takes them, and bumps
    /// its revision so every user sees the change. Its bounds are computed again.
    /// </summary>
    /// <returns>Whether it was replaced: false for a stale handle or data that breaks the rules, which leave it unchanged.</returns>
    public static unsafe bool ReplaceMesh(AssetHandle<Mesh> mesh, ReadOnlySpan<Vector3> positions, ReadOnlySpan<Vector3> normals,
                                          ReadOnlySpan<Vector2> uvs, ReadOnlySpan<uint> indices)
    {
        fixed (Vector3* firstPosition = positions)
        fixed (Vector3* firstNormal = normals)
        fixed (Vector2* firstUv = uvs)
        fixed (uint* firstIndex = indices)
            return AssetsNative.ReplaceMesh(mesh.Bits, firstPosition, positions.Length, firstNormal, normals.Length, firstUv,
                                            uvs.Length, firstIndex, indices.Length);
    }

    /// <summary>
    /// Adds a render target: an RGBA8 sRGB image of this size with no pixels, which a camera draws into and materials sample
    /// (ADR-0026).
    /// </summary>
    /// <returns>The image, or the empty handle if either side is under one pixel.</returns>
    public static AssetHandle<Image> AddRenderTarget(int width, int height, AssetOwner owner = AssetOwner.Scene) =>
        AssetHandle<Image>.FromBits(AssetsNative.AddRenderTarget(width, height, (byte)owner));

    /// <summary>Gives the render target <paramref name="image"/> a new size, bumping its revision.</summary>
    /// <returns>
    /// Whether it was resized: false for a stale handle, an image with pixels (which is not a render target), or a side under
    /// one pixel.
    /// </returns>
    public static bool ResizeRenderTarget(AssetHandle<Image> image, int width, int height) =>
        AssetsNative.ResizeRenderTarget(image.Bits, width, height);

    /// <summary>
    /// Adds an RGBA8 sRGB image of this size from <paramref name="rgba8"/>: four bytes per pixel (red, green, blue, alpha),
    /// rows top to bottom. The pixels are copied.
    /// </summary>
    /// <returns>The image, or the empty handle (and an error logged) if the pixels are not <c>width * height * 4</c> bytes.</returns>
    public static unsafe AssetHandle<Image> AddImage(int width, int height, ReadOnlySpan<byte> rgba8,
                                                     AssetOwner owner = AssetOwner.Scene)
    {
        fixed (byte* first = rgba8)
            return AssetHandle<Image>.FromBits(AssetsNative.AddImage(width, height, first, rgba8.Length, (byte)owner));
    }

    /// <summary>
    /// Swaps <paramref name="image"/>'s data for an RGBA8 sRGB image of this size from <paramref name="rgba8"/>, as
    /// <see cref="AddImage"/> takes it, and bumps its revision.
    /// </summary>
    /// <returns>Whether it was replaced: false for a stale handle or the wrong number of bytes, which leave it unchanged.</returns>
    public static unsafe bool ReplaceImage(AssetHandle<Image> image, int width, int height, ReadOnlySpan<byte> rgba8)
    {
        fixed (byte* first = rgba8)
            return AssetsNative.ReplaceImage(image.Bits, width, height, first, rgba8.Length);
    }
}

// The native functions behind Assets, each named "Fenrir.Assets.AssetsNative.<Method>" in FenrirAssetsScripting's extension.
// A handle crosses as its bits (AssetHandle<T>.Bits) and its kind as FenrirAssets' Detail::AssetKind; an owner as its
// AssetOwner value; spans as a pointer and a count.
internal static unsafe partial class AssetsNative
{
    [NativeFunction]
    public static partial ulong Load(byte kind, ReadOnlySpan<char> path, byte owner);

    [NativeFunction]
    public static partial bool Unload(byte kind, ulong handle);

    [NativeFunction]
    public static partial bool IsValid(byte kind, ulong handle);

    [NativeFunction]
    public static partial ulong AddMesh(Vector3* positions, int positionCount, Vector3* normals, int normalCount, Vector2* uvs,
                                        int uvCount, uint* indices, int indexCount, byte owner);

    [NativeFunction]
    public static partial bool ReplaceMesh(ulong mesh, Vector3* positions, int positionCount, Vector3* normals, int normalCount,
                                           Vector2* uvs, int uvCount, uint* indices, int indexCount);

    [NativeFunction]
    public static partial ulong AddRenderTarget(int width, int height, byte owner);

    [NativeFunction]
    public static partial bool ResizeRenderTarget(ulong image, int width, int height);

    [NativeFunction]
    public static partial ulong AddImage(int width, int height, byte* rgba8, int byteCount, byte owner);

    [NativeFunction]
    public static partial bool ReplaceImage(ulong image, int width, int height, byte* rgba8, int byteCount);
}
