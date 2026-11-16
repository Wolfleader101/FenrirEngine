namespace Fenrir.UI.Razor;

/// <summary>
/// An entry's key within its region (spec: "The builder", ADR-0067): the compile-time sequence number with the n-th
/// time it ran in this region this build, or with the element's <c>@key</c> in place of the count. A folded markup
/// string's elements add their place in the string, <see cref="Part"/>; everything else has -1.
/// </summary>
internal readonly record struct EntryKey(int Sequence, int Occurrence, object? Key, int Part);

/// <summary>
/// A key space: the entries made at one level of one Razor component's markup, found again by key on each build. Each
/// element has one for its children, and a mounted Razor component has one under its host. (Ticket 05 adds one per child
/// Razor component and per fragment, placed under the element they run in.)
/// </summary>
internal sealed class Region
{
    private readonly Dictionary<int, int> occurrences = [];
    private int stamp = -1;

    public Dictionary<EntryKey, Entry> Entries { get; } = [];

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
}

/// <summary>What the builder keeps for one mounted Razor component: its root region and its host's placement.</summary>
internal sealed class RetainedTree(ElementId host)
{
    public Region Root { get; } = new();

    public Placement Host { get; } = new(host);

    /// <summary>The number of the build running or last run; the first is 1.</summary>
    public int Build { get; set; }
}
