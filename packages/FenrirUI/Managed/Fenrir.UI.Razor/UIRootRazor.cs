namespace Fenrir.UI.Razor;

/// <summary>Mounting Razor components on a UI root.</summary>
public static class UIRootRazor
{
    /// <summary>
    /// Mounts a new <typeparamref name="T"/> under <paramref name="parent"/> (ADR-0068): makes its host <c>panel</c>
    /// there at once, carrying the Razor component's readable class (its type name in kebab case: <c>HealthBar</c> is
    /// <c>health-bar</c>), constructs it, runs <paramref name="init"/> on it, and queues its first build for the next
    /// <c>Fenrir.UI.Build</c> stage, where the Razor dispatcher builds it. Mounting from outside the stage is allowed.
    /// </summary>
    /// <param name="parent">The element it goes under, of this root.</param>
    /// <param name="index">The host's place among the parent's children; negative for after the last.</param>
    /// <param name="init">Runs once, before the first build: the place to set its fields.</param>
    public static ComponentRef<T> Mount<T>(this UIRoot root, ElementId parent, int index = -1, Action<T>? init = null)
        where T : UIComponent, new()
    {
        return new ComponentRef<T>(RazorRuntime.Mount(root, new RootSeam(root), parent, index, init));
    }
}
