using Fenrir.Math;

namespace Fenrir.Assets;

// AddMesh, ReplaceMesh, the mesh levels of detail (GenerateLods, AddMeshLod, GetMeshLodErrors, GetMeshLodIndices),
// AddRenderTarget, ResizeRenderTarget, AddImage, ReplaceImage and the instance lists' add, replace and reads are
// generated (Generated/Assets.cs, from the natives' @facade marks in Scripting/src/AssetsScripting.cpp), each taking the
// owner the native takes. This part holds what a description cannot say: the class doc, the generic methods over the
// kind-generic natives, and the overloads that default the owner to the active scene, an instance list's colours to
// none and the level-of-detail settings to C++'s.

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
/// empty handle, and the store logs why. In a debug build of the engine a warning is logged once per scripting install for
/// each place in the engine's native code that noticed (not once per C# call site).
/// </para>
/// </remarks>
public static partial class Assets
{
    /// <summary>
    /// Loads the asset at <paramref name="path"/> for <paramref name="owner"/>: a path relative to the asset root (or
    /// absolute), <c>file#Label</c> for a model's mesh, material, image, skeleton or clip (<c>rig.glb#Clip0</c>), or a
    /// <c>builtin:</c> name. A path already loaded gives back the same handle, with <paramref name="owner"/> added.
    /// </summary>
    /// <returns>The asset, or the empty handle if the load failed (logged with its reason).</returns>
    public static AssetHandle<T> Load<T>(ReadOnlySpan<char> path, AssetOwner owner = AssetOwner.Scene) where T : class
    {
        AssetsNative.Load(AssetKind<T>.Value, path, owner, out NativeAssetHandle handle);
        return AssetHandle<T>.FromNative(handle);
    }

    /// <summary>Frees the asset now, whoever owns it: every handle to it becomes stale.</summary>
    /// <returns>Whether the handle named a live asset.</returns>
    public static bool Unload<T>(AssetHandle<T> handle) where T : class
    {
        NativeAssetHandle native = handle.ToNative();
        return AssetsNative.Unload(AssetKind<T>.Value, in native);
    }

    /// <summary>Whether <paramref name="handle"/> names a live asset: false for an empty or stale handle.</summary>
    public static bool IsValid<T>(AssetHandle<T> handle) where T : class
    {
        NativeAssetHandle native = handle.ToNative();
        return AssetsNative.IsValid(AssetKind<T>.Value, in native);
    }

    /// <summary><see cref="AddMesh(ReadOnlySpan{Vector3}, ReadOnlySpan{Vector3}, ReadOnlySpan{Vector2}, ReadOnlySpan{uint}, AssetOwner)"/> for the active scene.</summary>
    public static AssetHandle<Mesh> AddMesh(ReadOnlySpan<Vector3> positions, ReadOnlySpan<Vector3> normals,
                                            ReadOnlySpan<Vector2> uvs, ReadOnlySpan<uint> indices) =>
        AddMesh(positions, normals, uvs, indices, AssetOwner.Scene);

    /// <summary><see cref="GenerateLods(AssetHandle{Mesh}, LodSettings)"/> with C++'s default <see cref="LodSettings"/>.</summary>
    public static bool GenerateLods(AssetHandle<Mesh> mesh) => GenerateLods(mesh, new LodSettings());

    /// <summary><see cref="AddRenderTarget(int, int, AssetOwner)"/> for the active scene.</summary>
    public static AssetHandle<Image> AddRenderTarget(int width, int height) => AddRenderTarget(width, height, AssetOwner.Scene);

    /// <summary><see cref="AddImage(int, int, ReadOnlySpan{byte}, AssetOwner)"/> for the active scene.</summary>
    public static AssetHandle<Image> AddImage(int width, int height, ReadOnlySpan<byte> rgba8) =>
        AddImage(width, height, rgba8, AssetOwner.Scene);

    /// <summary><see cref="AddInstanceList(ReadOnlySpan{Matrix4x4}, ReadOnlySpan{Colour}, AssetOwner)"/> for the active scene.</summary>
    public static AssetHandle<InstanceList> AddInstanceList(ReadOnlySpan<Matrix4x4> transforms, ReadOnlySpan<Colour> colours) =>
        AddInstanceList(transforms, colours, AssetOwner.Scene);

    /// <summary>An instance list of white copies for the active scene: <see cref="AddInstanceList(ReadOnlySpan{Matrix4x4}, ReadOnlySpan{Colour}, AssetOwner)"/> with no colours.</summary>
    public static AssetHandle<InstanceList> AddInstanceList(ReadOnlySpan<Matrix4x4> transforms) =>
        AddInstanceList(transforms, [], AssetOwner.Scene);

    /// <summary><see cref="ReplaceInstanceList(AssetHandle{InstanceList}, ReadOnlySpan{Matrix4x4}, ReadOnlySpan{Colour})"/> with no colours: every copy white.</summary>
    public static bool ReplaceInstanceList(AssetHandle<InstanceList> list, ReadOnlySpan<Matrix4x4> transforms) =>
        ReplaceInstanceList(list, transforms, []);
}
