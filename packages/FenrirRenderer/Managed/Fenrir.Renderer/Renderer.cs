using Fenrir.Assets;
using Fenrir.Scripting;

namespace Fenrir.Renderer;

// The renderer's components (MeshRenderer, Camera and its unions, DirectionalLight), its scene resources (AmbientLight, Sky),
// RendererStats and RendererNative are generated from FenrirRenderer's headers and Scripting/src/RendererScripting.cpp
// (Generated/), and so are Renderer's pass-throughs (Generated/Renderer.cs, from the natives' @facade marks: picking,
// bounds and stats). This part of the facade holds what a description cannot say: the class doc and SpawnModel, which
// defaults the parent and packs the model handle.

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
/// <see cref="Entity.None"/>. In a debug build of the engine a warning is logged once per scripting install for each place in the
/// engine's native code that noticed and each reason (not once per C# call site).
/// </para>
/// </remarks>
public static partial class Renderer
{
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
        RendererNative.SpawnModel(model.Bits, parent);
}
