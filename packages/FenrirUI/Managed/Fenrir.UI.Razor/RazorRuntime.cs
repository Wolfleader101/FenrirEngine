using System.Text;
using Fenrir.Scripting;

namespace Fenrir.UI.Razor;

/// <summary>One mount of a Razor component: the object, the seam to its root and its host element.</summary>
internal sealed class MountedRazorComponent(UIComponent component, IElementSeam seam, ElementId host)
{
    public UIComponent Component { get; } = component;

    public IElementSeam Seam { get; } = seam;

    public ElementId Host { get; } = host;

    public bool IsMounted { get; set; } = true;
}

/// <summary>
/// The registry of mounted Razor components, which the Razor dispatcher drives (ADR-0068). It lives in a shared
/// contract that never reloads, so it holds the current script generation's Razor components only until
/// <see cref="Release"/>, which the dispatcher calls when the generation is released.
/// </summary>
public static class RazorRuntime
{
    private static readonly List<MountedRazorComponent> mounted = [];
    private static List<MountedRazorComponent> queued = [];
    private static List<MountedRazorComponent> building = [];

    /// <summary>
    /// Builds the Razor components waiting for their first build, in the order they were mounted. The Razor dispatcher
    /// calls it once a frame from <c>Fenrir.UI.Build</c>, with its context. A Razor component mounted while this runs
    /// builds next frame. One whose build throws is logged with its type, and the others still build.
    /// </summary>
    public static void Build(in SystemContext context)
    {
        (queued, building) = (building, queued);
        try
        {
            foreach (MountedRazorComponent entry in building)
            {
                if (!entry.IsMounted)
                    continue;
                entry.Component.Context = context;
                try
                {
                    new ElementBuilder(entry.Seam).Build(entry.Component, entry.Host);
                }
                catch (Exception exception)
                {
                    Log.Error($"The Razor component {entry.Component.GetType().FullName} failed to build: {exception}");
                }
            }
        }
        finally
        {
            building.Clear();
        }
    }

    /// <summary>
    /// Lets go of the script generation (ADR-0068): removes every mounted Razor component's host at once, with no
    /// outro, and forgets every Razor component, so nothing here refers to the generation's objects or types. The
    /// Razor dispatcher calls it from its <c>OnDestroy</c>, before the generation is released.
    /// </summary>
    public static void Release()
    {
        // The latest first, so a Razor component mounted into another's elements goes before the element it is under.
        for (int i = mounted.Count - 1; i >= 0; --i)
        {
            MountedRazorComponent entry = mounted[i];
            entry.IsMounted = false;
            entry.Seam.Remove(entry.Host, RemoveMode.Immediate);
        }
        mounted.Clear();
        queued.Clear();
        building.Clear();
    }

    internal static MountedRazorComponent Mount<T>(UIRoot root, IElementSeam seam, ElementId parent, int index,
                                              Action<T>? init)
        where T : UIComponent, new()
    {
        ElementId host = seam.CreateElement(parent, "panel", index);
        seam.AddClass(host, ReadableClass(typeof(T)));
        var component = new T { Root = root, Element = host };
        try
        {
            init?.Invoke(component);
        }
        catch
        {
            seam.Remove(host, RemoveMode.Immediate);
            throw;
        }
        var entry = new MountedRazorComponent(component, seam, host);
        mounted.Add(entry);
        queued.Add(entry);
        return entry;
    }

    internal static bool Unmount(MountedRazorComponent entry, RemoveMode mode)
    {
        if (!entry.IsMounted)
            return false;
        entry.IsMounted = false;
        mounted.Remove(entry);
        queued.Remove(entry);
        entry.Seam.Remove(entry.Host, mode);
        return true;
    }

    /// <summary>
    /// The class a Razor component's host carries: its type name, without generic arity, in kebab case
    /// (<c>HealthBar</c> is <c>health-bar</c>, <c>HUDPanel</c> is <c>hud-panel</c>).
    /// </summary>
    internal static string ReadableClass(Type type)
    {
        string name = type.Name;
        int arity = name.IndexOf('`');
        if (arity >= 0)
            name = name[..arity];

        var kebab = new StringBuilder(name.Length + 4);
        for (int i = 0; i < name.Length; ++i)
        {
            char c = name[i];
            if (char.IsUpper(c) && i > 0)
            {
                char previous = name[i - 1];
                bool wordStarts = char.IsLower(previous) || char.IsDigit(previous) ||
                                  (char.IsUpper(previous) && i + 1 < name.Length && char.IsLower(name[i + 1]));
                if (wordStarts)
                    kebab.Append('-');
            }
            kebab.Append(char.ToLowerInvariant(c));
        }
        return kebab.ToString();
    }
}
