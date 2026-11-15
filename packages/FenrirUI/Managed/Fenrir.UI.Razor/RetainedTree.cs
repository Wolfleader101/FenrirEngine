namespace Fenrir.UI.Razor;

/// <summary>
/// An entry's key within its region (spec: "The builder", ADR-0067): the compile-time sequence number with the n-th
/// time it ran in this region this build, or with the element's <c>@key</c> in place of the count. A folded markup
/// string's elements add their place in the string, <see cref="Part"/>; everything else has -1.
/// </summary>
internal readonly record struct EntryKey(int Sequence, int Occurrence, object? Key, int Part);

/// <summary>
/// A key space: the entries made at one level of one Razor component's markup, found again by key on each build. Each
/// element has one for its children, and a mounted Razor component has one under its host. Each child Razor component
/// and each fragment (<c>ChildContent</c> or another <c>RenderFragment</c>) has one of its own, keyed in the region it
/// was placed in, whose elements go under the element it runs in: their sequence numbers are unrelated to those around
/// them.
/// </summary>
internal sealed class Region
{
    private readonly Dictionary<int, int> occurrences = [];
    private int stamp = -1;

    public Dictionary<EntryKey, Entry> Entries { get; } = [];

    /// <summary>The child Razor components placed here, by key; null until the first.</summary>
    public Dictionary<EntryKey, ChildComponent>? Components { get; set; }

    /// <summary>The fragments run here, each a region of its own, by key; null until the first.</summary>
    public Dictionary<EntryKey, Region>? Fragments { get; set; }

    /// <summary>For a fragment's region, the build that last ran it.</summary>
    public int Visited { get; set; } = -1;

    /// <summary>How many times <paramref name="sequence"/> has run here this build before now.</summary>
    public int NextOccurrence(int sequence, int build)
    {
        if (stamp != build)
        {
            occurrences.Clear();
            stamp = build;
        }
        occurrences.TryGetValue(sequence, out int count);
        occurrences[sequence] = count + 1;
        return count;
    }

    /// <summary>
    /// The first element placed here as the last build left it, its child Razor components' and fragments' among them
    /// (all go under the same element); null when it placed none.
    /// </summary>
    public Entry? FirstPlaced()
    {
        Entry? first = null;
        foreach (Entry entry in Entries.Values)
        {
            if (first == null || entry.OrderIndex < first.OrderIndex)
                first = entry;
        }
        if (Components != null)
        {
            foreach (ChildComponent child in Components.Values)
                first = Earlier(first, child.Region.FirstPlaced());
        }
        if (Fragments != null)
        {
            foreach (Region fragment in Fragments.Values)
                first = Earlier(first, fragment.FirstPlaced());
        }
        return first;
    }

    private static Entry? Earlier(Entry? a, Entry? b) => a == null || (b != null && b.OrderIndex < a.OrderIndex) ? b : a;
}

/// <summary>
/// The children the builder places under one native element, in native order. The visited list is this build's order,
/// which <see cref="ElementBuilder"/> makes the native order at the element's close.
/// </summary>
internal sealed class Placement(ElementId element)
{
    private int stamp = -1;

    public ElementId Element { get; } = element;

    /// <summary>The owned children in native order, as the last build left them.</summary>
    public List<Entry> Order { get; set; } = [];

    /// <summary>The owned children visited this build, in the order visited.</summary>
    public List<Entry> Visited { get; set; } = [];

    /// <summary>The owned child natively last of all owned children; null when there are none.</summary>
    public Entry? Tail { get; set; }

    /// <summary>
    /// The native children, read once this build when an index was needed and kept in step with the builder's own calls
    /// since; null when not read. Outgoing and unowned children are among them.
    /// </summary>
    public List<ElementId>? Native { get; set; }

    /// <summary>
    /// Whether the element also holds children the builder does not own (ticket 08's placed documents), so that even an
    /// append must read the native children first.
    /// </summary>
    public bool HoldsUnowned { get; set; }

    /// <summary>
    /// Whether children the builder removed may still be there, outgoing: then indices come from a native read. Cleared
    /// when a read finds the owned children alone.
    /// </summary>
    public bool MayHoldOutgoing { get; set; }

    /// <summary>Starts this build's visit: once per build, before the first child is placed.</summary>
    public void Begin(int build)
    {
        if (stamp == build)
            return;
        stamp = build;
        Visited.Clear();
        Native = null;
    }
}

/// <summary>
/// One element the builder made and keeps: its id and tag, and the values it last gave the element, so a native is
/// called only for a value that changed.
/// </summary>
internal sealed class Entry(ElementId id, string tag)
{
    internal struct Attribute
    {
        public string Name;
        public string Value;
        public int Seen;
    }

    public ElementId Id { get; } = id;

    public string Tag { get; } = tag;

    /// <summary>The build that last visited it.</summary>
    public int Visited { get; set; } = -1;

    /// <summary>Its place in its placement's <see cref="Placement.Order"/>.</summary>
    public int OrderIndex { get; set; } = -1;

    /// <summary>Its children's key space and placement, made when it first has a child.</summary>
    public Region? Children { get; set; }

    public Placement? Placement { get; set; }

    public string? ClassText;
    public string[] Classes = [];
    public int ClassSeen = -1;

    public string? StyleText;
    public List<KeyValuePair<string, string>> Styles = [];
    public int StyleSeen = -1;

    public string? IdString;
    public int IdSeen = -1;

    public string? Value;
    public int ValueSeen = -1;

    public List<Attribute>? Attributes;

    /// <summary>The text last set, for a <c>label</c>, a <c>button</c> or a generated label; null when never set.</summary>
    public string? Text;

    /// <summary>Its <c>@ref</c>, handed the element when made and none when the element is forgotten.</summary>
    public Action<Microsoft.AspNetCore.Components.ElementReference>? Reference;

    /// <summary>Its handlers by <see cref="UIEventKind"/>; null when it has none.</summary>
    public Handler?[]? Handlers;

    /// <summary>Whether its root's <see cref="HandlerTable"/> holds it, so events reach its handlers.</summary>
    public bool InHandlerTable;

    /// <summary>Whether its pointer check is queued or done: each element is checked once.</summary>
    public bool PointerChecked;

    /// <summary>For a <c>&lt;Document&gt;</c>'s host panel, the path last loaded into it; null when none was.</summary>
    public string? LoadedDocumentPath;
}

/// <summary>
/// A child Razor component a parent's markup placed (spec: "The builder" child components, ADR-0067): the object, its
/// region, the parameters it was last given, and its own state for the rebuild rules. It has no element of its own:
/// its elements go under the element it was placed in.
/// </summary>
internal sealed class ChildComponent(UIComponent component, ChildComponent? parent)
{
    public UIComponent Component { get; } = component;

    /// <summary>The child Razor component whose build places it; null when its mount's own markup does.</summary>
    public ChildComponent? Parent { get; } = parent;

    public Region Region { get; } = new();

    /// <summary>The parameters as last set, in the order given.</summary>
    public List<KeyValuePair<string, object?>> Parameters { get; } = [];

    /// <summary>The build that last placed it.</summary>
    public int Visited { get; set; } = -1;

    /// <summary>
    /// Whether it must build when next placed although its parameters are the same: it, or a child Razor component
    /// under it, asked with <c>StateHasChanged</c> or a changed <c>BuildHash</c>.
    /// </summary>
    public bool Dirty { get; set; }

    /// <summary>Whether <c>OnMount</c> ran, so <c>OnUnmount</c> is owed.</summary>
    public bool MountHookRan { get; set; }

    /// <summary>What <c>BuildHash</c> returned last frame.</summary>
    public int LastHash { get; set; }

    /// <summary>Its <c>@ref</c> in the parent's markup, handed the object, and null when it is forgotten.</summary>
    public Action<object>? Reference { get; set; }

    /// <summary>Marks it and the child Razor components above it to build, so the build reaches it.</summary>
    public void MarkDirty()
    {
        for (ChildComponent? child = this; child != null; child = child.Parent)
            child.Dirty = true;
    }
}

/// <summary>What the builder keeps for one mounted Razor component: its root region and its host's placement.</summary>
internal sealed class RetainedTree(ElementId host)
{
    public Region Root { get; } = new();

    public Placement Host { get; } = new(host);

    /// <summary>The number of the build running or last run; the first is 1.</summary>
    public int Build { get; set; }

    /// <summary>Every child Razor component in the tree, at any depth, for the frame's <c>BuildHash</c> checks.</summary>
    public List<ChildComponent> Children { get; } = [];

    /// <summary>Its root's handler table, which its entries with handlers are in; null until mounted.</summary>
    public HandlerTable? Handlers { get; set; }
}
