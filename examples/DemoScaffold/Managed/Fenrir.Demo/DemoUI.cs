using Fenrir.Input;
using Fenrir.UI;

namespace Fenrir.Demo;

/// <summary>
/// The demo UI kit (the scaffold's <c>FenrirDemo::DemoUI</c>, ADR-0071, ADR-0072) as C# reaches it: through FenrirUI's
/// own contract, with no natives of its own. The kit's root is on the main window's overlay at <see cref="Layer"/>, and
/// a scene that asks for a demo slot (the C++ <c>DemoScene::razorPanel</c>) has an element of that id in the demo panel,
/// which a script mounts its Razor panel into.
/// </summary>
public static class DemoUI
{
    /// <summary>The overlay layer the kit's root draws on (the C++ <c>DemoUILayer</c>).</summary>
    public const int Layer = 900;

    // The main window, as the input and window contracts name it.
    private static readonly WindowId MainWindow = new() { Value = 1 };

    /// <summary>The kit's root, found on the main window at <see cref="Layer"/>; false when there is none.</summary>
    public static bool TryGetRoot(out UIRoot root)
    {
        root = UIRoot.Find(MainWindow, Layer);
        return root.Value != 0;
    }

    /// <summary>
    /// The active scene's demo slot <paramref name="id"/> in the kit's root: false when there is no root or no such
    /// slot, as before the scene is entered or after it is left. A reloaded <c>demo.ui</c> makes the slot again under
    /// a new element, so find it again rather than keeping it.
    /// </summary>
    public static bool TryGetSceneSlot(string id, out UIRoot root, out ElementId slot)
    {
        slot = default;
        if (!TryGetRoot(out root))
            return false;
        slot = root.FindById(id);
        return root.IsAlive(slot);
    }
}
