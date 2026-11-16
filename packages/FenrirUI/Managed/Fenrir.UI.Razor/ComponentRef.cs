using System.Diagnostics;

namespace Fenrir.UI.Razor;

/// <summary>
/// A mounted Razor component, as <see cref="UIRootRazor.Mount{T}"/> returns it: the instance, its host element, and
/// the way to unmount it. Default constructed, it names nothing and is not mounted.
/// </summary>
public readonly struct ComponentRef<T> where T : UIComponent
{
    private readonly MountedRazorComponent? mounted;

    internal ComponentRef(MountedRazorComponent mounted)
    {
        this.mounted = mounted;
    }

    /// <summary>The Razor component's object, or null for a default reference.</summary>
    public T? Instance => (T?)mounted?.Component;

    /// <summary>The host element the mount made, which the Razor component builds under; none for a default reference.</summary>
    public ElementId Host => mounted?.Host ?? new ElementId();

    /// <summary>
    /// Whether it is still mounted: false after <see cref="Unmount"/>, after a script reload released the runtime, and
    /// at once after something else removed its host (a markup reload, the game removing the host's parent, the root
    /// destroyed; once the host's outro ends, if it had one). Asking it then forgets the Razor component, running its
    /// <c>OnUnmount</c>, as its next build would have; so a game remounts with <c>if (!gauge.IsMounted)</c> alone. Hidden
    /// from the debugger's variable views, so inspecting a reference runs no hook.
    /// </summary>
    [DebuggerBrowsable(DebuggerBrowsableState.Never)]
    public bool IsMounted => mounted != null && RazorRuntime.StillMounted(mounted);

    /// <summary>
    /// Removes the host element with everything under it (after its <c>:outro</c> transitions by default) and forgets
    /// the Razor component. Returns false when it was not mounted.
    /// </summary>
    public bool Unmount(RemoveMode mode = RemoveMode.Outro) => mounted != null && RazorRuntime.Unmount(mounted, mode);
}
