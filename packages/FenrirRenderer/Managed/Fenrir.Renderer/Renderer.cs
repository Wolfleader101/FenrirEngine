using Fenrir.Assets;
using Fenrir.Input;
using Fenrir.Math;
using Fenrir.Scripting;

namespace Fenrir.Renderer;

// The renderer's components (MeshRenderer, Camera and its unions, DirectionalLight), its scene resources (AmbientLight, Sky),
// RendererStats and RendererNative are generated from FenrirRenderer's headers and Scripting/src/RendererScripting.cpp
// (Generated/). This file holds the facade over the natives.

/// <summary>
/// The renderer from C#: picking, bounds, stats and spawning models, as FenrirRenderer's <c>Renderer</c> and
/// <c>SpawnModel</c> offer them in C++ (whose documentation says what each does).
/// </summary>
/// <remarks>
/// Inside a <c>Fenrir.*</c> namespace, <c>Renderer</c> names this namespace: write <c>Fenrir.Renderer.Renderer</c> there, or
/// use an alias. Picking is computed on call from the camera's world matrix and its target's size now, so it works in
/// <c>Update</c>; points are in a window's logical coordinates from its top-left corner, or in an image's pixels.
/// <para>
/// Nothing here throws. With no renderer in the app, an entity that is not alive or has no <see cref="Camera"/>, a camera that
/// draws nothing, or a point outside its viewport or behind it, a function does nothing and returns <c>false</c>, zero or
/// <see cref="Entity.None"/>. In a debug build of the engine a warning is logged once per process for each place in the
/// engine's native code that noticed and each reason (not once per C# call site).
/// </para>
/// </remarks>
public static class Renderer
{
    /// <summary>The ray from <paramref name="camera"/> through <paramref name="point"/> of its target.</summary>
    /// <returns>Whether there is one; false for a point outside the camera's viewport.</returns>
    public static bool ScreenPointToRay(Entity camera, Vector2 point, out Ray ray) =>
        RendererNative.ScreenPointToRay(camera, in point, out ray);

    /// <summary>Where <paramref name="point"/> appears in <paramref name="camera"/>'s target.</summary>
    /// <returns>Whether it appears; false for a point behind the camera.</returns>
    public static bool WorldToScreenPoint(Entity camera, Vector3 point, out Vector2 screenPoint) =>
        RendererNative.WorldToScreenPoint(camera, in point, out screenPoint);

    /// <summary>
    /// The camera drawn last at <paramref name="point"/> of <paramref name="window"/>: the highest-order enabled camera in the
    /// active scene whose viewport holds it. <see cref="Entity.None"/> if there is none.
    /// </summary>
    public static Entity CameraAt(WindowId window, Vector2 point) => new(RendererNative.CameraAt(window, in point));

    /// <summary>The box around <paramref name="entity"/>'s drawn mesh in world space, now.</summary>
    /// <returns>Whether there is one: false if it has no <see cref="MeshRenderer"/> or its mesh does not resolve.</returns>
    public static bool GetWorldBounds(Entity entity, out Bounds bounds) =>
        RendererNative.GetWorldBounds(entity, out bounds);

    /// <summary>What the last drawn frame did; zeros before the first.</summary>
    /// <returns>Whether the app has a renderer.</returns>
    public static bool Stats(out RendererStats stats) => RendererNative.Stats(out stats);

    /// <summary>
    /// Spawns <paramref name="model"/> into the active scene as drawn entities, as C++'s <c>SpawnModel</c> does: a root named
    /// after the file holding one entity per node, each mesh a <see cref="MeshRenderer"/>. The root is usable at once (alive,
    /// so it can be given components or parented in the same call); the tree is made, as every structural change is, when
    /// the system returns. It needs no renderer in the app.
    /// </summary>
    /// <returns>The root, or <see cref="Entity.None"/> for an empty or stale model handle, spawning nothing.</returns>
    public static Entity SpawnModel(AssetHandle<Model> model) => SpawnModel(model, Entity.None);

    /// <summary>
    /// <see cref="SpawnModel(AssetHandle{Model})"/> with the root a child of <paramref name="parent"/>, unless that is
    /// <see cref="Entity.None"/>. A parent that is not alive spawns nothing.
    /// </summary>
    public static Entity SpawnModel(AssetHandle<Model> model, Entity parent) =>
        new(RendererNative.SpawnModel(((ulong)model.Generation << 32) | model.Index, parent));
}
