namespace Fenrir.UI.Razor;

/// <summary>
/// A mounted Razor component, as <see cref="UIRootRazor.Mount{T}"/> returns it: the instance, its host element, and
/// the way to unmount it. Default constructed, it names nothing and is not mounted.
/// </summary>
public readonly struct ComponentRef<T> where T : UIComponent
{
    private readonly MountedComponent? mounted;

    internal ComponentRef(MountedComponent mounted)
    {
        this.mounted = mounted;
    }

    /// <summary>The Razor component's object, or null for a default reference.</summary>
    public T? Instance => (T?)mounted?.Component;

    /// <summary>The host element the mount made, which the Razor component builds under; none for a default reference.</summary>
    public ElementId Host => mounted?.Host ?? new ElementId();

    /// <summary>
    /// Whether it is still mounted: false after <see cref="Unmount"/>, and after a script reload released the runtime.
    /// </summary>
    public bool IsMounted => mounted is { IsMounted: true };

    /// <summary>
    /// Removes the host element with everything under it (after its <c>:outro</c> transitions by default) and forgets
    /// the Razor component. Returns false when it was not mounted.
    /// </summary>
    public bool Unmount(RemoveMode mode = RemoveMode.Outro) => mounted != null && RazorRuntime.Unmount(mounted, mode);
}
