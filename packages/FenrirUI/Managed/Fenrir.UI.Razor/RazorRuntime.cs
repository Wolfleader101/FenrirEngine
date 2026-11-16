using System.Reflection;
using System.Text;
using Fenrir.Painter;
using Fenrir.Scripting;

namespace Fenrir.UI.Razor;

/// <summary>One mount of a Razor component: the object, the seam to its root, its host element and what it built.</summary>
internal sealed class MountedRazorComponent(UIComponent component, IElementSeam seam, ElementId host)
{
    public UIComponent Component { get; } = component;

    public IElementSeam Seam { get; } = seam;

    public ElementId Host { get; } = host;

    public RetainedTree Tree { get; } = new(host);

    public bool IsMounted { get; set; } = true;

    /// <summary>Whether its first build ran.</summary>
    public bool Built { get; set; }

    /// <summary>Whether <c>StateHasChanged</c> asked for a build since the last one began.</summary>
    public bool Dirty { get; set; }

    /// <summary>Whether a throw turned it off until the next reload.</summary>
    public bool Disabled { get; set; }

    /// <summary>Whether <c>OnMount</c> ran, so <c>OnUnmount</c> is owed.</summary>
    public bool MountHookRan { get; set; }

    /// <summary>What <c>BuildHash</c> returned last frame.</summary>
    public int LastHash { get; set; }
}

/// <summary>
/// The registry of mounted Razor components, which the Razor dispatcher drives (ADR-0068). It lives in a shared
/// contract that never reloads, so it holds the current script generation's Razor components only until
/// <see cref="Release"/>, which the dispatcher calls when the generation is released.
/// </summary>
public static class RazorRuntime
{
    /// <summary>The Razor components mounted on one UI root, in mount order, with the root's one seam.</summary>
    private sealed class RootGroup(UIRoot root, IElementSeam seam)
    {
        public UIRoot Root { get; } = root;

        public IElementSeam Seam { get; } = seam;

        public List<MountedRazorComponent> Mounted { get; } = [];

        /// <summary>The root's elements with handlers, of all its Razor components.</summary>
        public HandlerTable Handlers { get; } = new();
    }

    private static readonly List<RootGroup> groups = [];
    private static readonly List<MountedRazorComponent> building = [];
    private static readonly ElementBuilder builder = new();

    /// <summary>Whether each Razor component type seen in the paint stage overrides <c>OnPaint</c>, found once per type.</summary>
    private static readonly Dictionary<Type, bool> overridesOnPaint = [];

    /// <summary>This frame's paint calls: the mount, the Razor component, and the element it paints into.</summary>
    private static readonly List<(MountedRazorComponent Mount, UIComponent Component, ElementId Element)> painting = [];

    /// <summary>
    /// Runs one frame of the Razor components (ADR-0068, ADR-0069), from <c>Fenrir.UI.Build</c>: the Razor dispatcher
    /// calls it with its context and the UI events it read this frame. First the Razor components of a destroyed root
    /// are dropped (one liveness check per root a frame), and one whose host someone else removed is dropped when it
    /// would next build. Then each event runs the handler of its kind nearest its target, walking up through the
    /// parents, and that handler's Razor component is marked to build (a <c>Change</c> on a bound control first writes
    /// the control's value into the bound field). Then each Razor component builds, in mount order, when it was just
    /// mounted, when <c>StateHasChanged</c> or a handler asked, or when its <c>BuildHash</c> changed; at most once. One
    /// mounted while this runs builds next frame. One whose build, hook or handler throws is logged with its type and
    /// turned off until the next reload.
    /// </summary>
    public static void Build(in SystemContext context, EventRange<UIEvent> events)
    {
        BeginFrame();
        for (int i = 0; i < events.Count; ++i)
            Dispatch(events[i], context);
        BuildAll(context);
    }

    /// <summary>
    /// Runs one frame with no UI events, as <see cref="Build(in SystemContext, EventRange{UIEvent})"/> does with some.
    /// </summary>
    public static void Build(in SystemContext context) => Build(context, ReadOnlySpan<UIEvent>.Empty);

    /// <summary>
    /// Runs one frame with these UI events, as the tests give them: the public overload's steps over a span, since an
    /// <c>EventRange</c> is the engine's queue and a test has none.
    /// </summary>
    internal static void Build(in SystemContext context, ReadOnlySpan<UIEvent> events)
    {
        BeginFrame();
        foreach (ref readonly UIEvent e in events)
            Dispatch(e, context);
        BuildAll(context);
    }

    /// <summary>Drops dead roots, then runs the pointer checks earlier frames queued, now their elements are laid out.</summary>
    private static void BeginFrame()
    {
        DropDeadRoots();
        foreach (RootGroup group in groups)
            group.Handlers.RunPointerChecks(group.Seam);
    }

    /// <summary>Runs the event's nearest handler, on its root's Razor components.</summary>
    private static void Dispatch(in UIEvent e, in SystemContext context)
    {
        foreach (RootGroup group in groups)
        {
            if (group.Root.Value == e.Root.Value && group.Handlers.Dispatch(e, group.Seam, context))
                return;
        }
    }

    private static void BuildAll(in SystemContext context)
    {
        foreach (RootGroup group in groups)
            building.AddRange(group.Mounted);
        try
        {
            foreach (MountedRazorComponent entry in building)
            {
                if (entry.IsMounted && !entry.Disabled)
                    BuildOne(entry, context);
            }
        }
        finally
        {
            building.Clear();
        }
    }

    /// <summary>
    /// Paints the mounted Razor components (ADR-0068), from <c>Fenrir.UI.Paint</c>: the Razor dispatcher's paint system
    /// calls it with its context. Each mounted Razor component whose type overrides <c>OnPaint</c>, from its first build
    /// on, is handed a painter for its host and the host's boxes; each child Razor component whose type overrides it,
    /// after its mount's, one for the first element its markup places. One whose element has no painter this frame is
    /// skipped. One that throws is logged and turns its mount off until the next reload.
    /// </summary>
    public static void Paint(in SystemContext context)
    {
        // Gathered first, so an OnPaint that unmounts or mounts changes no list being walked.
        foreach (RootGroup group in groups)
        {
            foreach (MountedRazorComponent mount in group.Mounted)
            {
                if (!mount.IsMounted || mount.Disabled || !mount.Built)
                    continue;
                if (OverridesOnPaint(mount.Component.GetType()))
                    painting.Add((mount, mount.Component, mount.Host));
                foreach (ChildComponent child in mount.Tree.Children)
                {
                    if (OverridesOnPaint(child.Component.GetType()) && child.Region.FirstPlaced() is { } first)
                        painting.Add((mount, child.Component, first.Id));
                }
            }
        }
        try
        {
            foreach ((MountedRazorComponent mount, UIComponent component, ElementId element) in painting)
            {
                if (mount.IsMounted && !mount.Disabled)
                    PaintOne(mount, component, element, context);
            }
        }
        finally
        {
            painting.Clear();
        }
    }

    private static void PaintOne(MountedRazorComponent mount, UIComponent component, ElementId element,
                                 in SystemContext context)
    {
        PainterId painter = mount.Seam.PaintInto(element);
        if (painter.Value == 0)
            return;
        component.Context = context;
        try
        {
            component.CallOnPaint(painter, mount.Seam.GetBoxes(element));
        }
        catch (Exception exception)
        {
            mount.Disabled = true;
            RazorDiagnostics.Error($"The Razor component {component.GetType().FullName} failed in OnPaint, and " +
                                   $"{mount.Component.GetType().FullName} is off until the next script reload: {exception}");
        }
    }

    /// <summary>Whether the type overrides <c>OnPaint</c>: looked up once per type, until <see cref="Release"/>.</summary>
    private static bool OverridesOnPaint(Type type)
    {
        if (!overridesOnPaint.TryGetValue(type, out bool overrides))
        {
            MethodInfo? method = type.GetMethod("OnPaint", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
                                                [typeof(PainterId), typeof(ElementBoxes)]);
            overrides = method != null && method.DeclaringType != typeof(UIComponent);
            overridesOnPaint.Add(type, overrides);
        }
        return overrides;
    }

    /// <summary>
    /// Lets go of the script generation (ADR-0068): runs each Razor component's <c>OnUnmount</c>, removes every mounted
    /// host still alive at once, with no outro, and forgets every Razor component and every cache naming the
    /// generation's types, so nothing here refers to its objects: the handler tables go with their roots'
    /// groups, and the paint overrides found are forgotten. The Razor dispatcher calls it from its
    /// <c>OnDestroy</c>, before the generation is released.
    /// </summary>
    public static void Release()
    {
        // The latest first, so a Razor component mounted into another's elements goes before the element it is under.
        for (int g = groups.Count - 1; g >= 0; --g)
        {
            List<MountedRazorComponent> entries = groups[g].Mounted;
            for (int i = entries.Count - 1; i >= 0; --i)
            {
                MountedRazorComponent entry = entries[i];
                Forget(entry);
                if (entry.Seam.IsAlive(entry.Host))
                    entry.Seam.Remove(entry.Host, RemoveMode.Immediate);
            }
        }
        groups.Clear();
        building.Clear();
        painting.Clear();
        overridesOnPaint.Clear();
        ElementBuilder.ReleaseCaches();
        ComponentParameters.Release();
    }

    /// <summary>The seam for <paramref name="root"/>: the one its mounted Razor components share, or a new one.</summary>
    internal static IElementSeam SeamFor(UIRoot root)
    {
        foreach (RootGroup group in groups)
        {
            // Only a production seam over this root: a test's fake for the default root is never handed out.
            if (group.Root.Value == root.Value && group.Seam is RootSeam)
                return group.Seam;
        }
        return new RootSeam(root);
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
        component.Mounted = entry;
        RootGroup group = GroupOf(root, seam);
        entry.Tree.Handlers = group.Handlers;
        group.Mounted.Add(entry);
        return entry;
    }

    internal static bool Unmount(MountedRazorComponent entry, RemoveMode mode)
    {
        if (!entry.IsMounted)
            return false;
        Unregister(entry);
        Forget(entry);
        if (entry.Seam.IsAlive(entry.Host))
            entry.Seam.Remove(entry.Host, mode);
        return true;
    }

    private static void BuildOne(MountedRazorComponent entry, in SystemContext context)
    {
        UIComponent component = entry.Component;
        component.Context = context;
        try
        {
            bool first = !entry.Built;
            if (!first)
            {
                int hash = component.CallBuildHash();
                bool changed = hash != entry.LastHash;
                entry.LastHash = hash;
                if (ChildHashesChanged(entry.Tree, context))
                    changed = true;
                if (!changed && !entry.Dirty)
                    return;
            }
            // A host someone else removed while its root lives is found here, when it would build.
            if (!entry.Seam.IsAlive(entry.Host))
            {
                Unregister(entry);
                Forget(entry);
                return;
            }
            if (first)
            {
                entry.MountHookRan = true;
                component.CallOnMount();
                if (!entry.IsMounted)
                    return;
            }
            // Cleared before the build, so StateHasChanged from the build itself or OnBuilt asks for next frame's.
            entry.Dirty = false;
            builder.Build(component, entry.Tree, entry.Seam);
            entry.Built = true;
            if (first)
                entry.LastHash = component.CallBuildHash();
            component.CallOnBuilt(first);
        }
        catch (Exception exception)
        {
            entry.Disabled = true;
            RazorDiagnostics.Error($"The Razor component {component.GetType().FullName} failed and is off until the " +
                                   $"next script reload, its elements left as they were: {exception}");
        }
    }

    /// <summary>
    /// Checks every child Razor component's <c>BuildHash</c> against last frame's, marking each that changed (and the
    /// ones above it) to build; returns whether any did. They build inside their mount's build, which this then asks for.
    /// </summary>
    private static bool ChildHashesChanged(RetainedTree tree, in SystemContext context)
    {
        bool any = false;
        foreach (ChildComponent child in tree.Children)
        {
            child.Component.Context = context;
            int hash = child.Component.CallBuildHash();
            if (hash == child.LastHash)
                continue;
            child.LastHash = hash;
            child.MarkDirty();
            any = true;
        }
        return any;
    }

    /// <summary>
    /// Drops the Razor components of destroyed roots: one liveness check of each root's oldest host a frame, and of every
    /// host in that root only when that one is gone (its root destroyed, or that host removed by someone else).
    /// </summary>
    private static void DropDeadRoots()
    {
        for (int g = groups.Count - 1; g >= 0; --g)
        {
            RootGroup group = groups[g];
            if (group.Mounted.Count != 0 && group.Seam.IsAlive(group.Mounted[0].Host))
                continue;
            for (int i = group.Mounted.Count - 1; i >= 0; --i)
            {
                MountedRazorComponent mount = group.Mounted[i];
                if (i != 0 && mount.Seam.IsAlive(mount.Host))
                    continue;
                group.Mounted.RemoveAt(i);
                Forget(mount);
            }
            if (group.Mounted.Count == 0)
                groups.RemoveAt(g);
        }
    }

    private static void Unregister(MountedRazorComponent mount)
    {
        foreach (RootGroup group in groups)
        {
            if (group.Mounted.Remove(mount))
                return;
        }
    }

    /// <summary>
    /// Marks it unmounted, forgets its tree (every <c>@ref</c> handed none, every child Razor component's <c>OnUnmount</c> run) and
    /// runs its own <c>OnUnmount</c> when owed; the host is the caller's.
    /// </summary>
    private static void Forget(MountedRazorComponent entry)
    {
        entry.IsMounted = false;
        entry.Component.Mounted = null;
        // Each hook that throws is logged and the walk goes on.
        ElementBuilder.ForgetTree(entry.Tree, entry.Component);
        if (!entry.MountHookRan)
            return;
        entry.MountHookRan = false;
        try
        {
            entry.Component.CallOnUnmount();
        }
        catch (Exception exception)
        {
            RazorDiagnostics.Error($"The Razor component {entry.Component.GetType().FullName} threw from OnUnmount: {exception}");
        }
    }

    private static RootGroup GroupOf(UIRoot root, IElementSeam seam)
    {
        foreach (RootGroup group in groups)
        {
            if (group.Seam == seam)
                return group;
        }
        var made = new RootGroup(root, seam);
        groups.Add(made);
        return made;
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
