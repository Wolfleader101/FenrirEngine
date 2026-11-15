using System.Globalization;
using System.Text;
using Microsoft.AspNetCore.Components.Rendering;

namespace Fenrir.UI.Razor;

/// <summary>
/// Builds a Razor component's elements in place as its generated <c>BuildRenderTree</c> runs, through the element seam
/// (spec: "The builder", ADR-0067). There is no virtual DOM: each element is an entry of the component's
/// <see cref="RetainedTree"/>, keyed by its sequence number and occurrence (or <c>@key</c>) within its parent, and a
/// rebuild finds it again and calls a native only for a value that changed, so a rebuild that changes nothing makes no
/// native call.
/// <list type="bullet">
/// <item>An element's tag and attributes are buffered until its first other call (<c>SetKey</c>, content, a child, or
/// its close), where it is found by key, or made after the last child visited before it.</item>
/// <item><c>class</c> tokens and <c>style</c> declarations are diffed; a dropped declaration is reset with
/// <c>SetStyle(name, "")</c>. <c>id</c> is the id string, <c>value</c> the value (a text entry's text), and any other
/// attribute goes through <c>SetAttribute</c>.</item>
/// <item>Text in a <c>label</c> or <c>button</c> is joined and set once; elsewhere each run of text becomes a generated
/// <c>label</c>.</item>
/// <item>Folded markup is parsed once per string (<see cref="MarkupFragment"/>) and built through the same rules.</item>
/// <item>At an element's close, children not visited are removed with their outro, and the visited ones are moved into
/// order, those outside the longest increasing subsequence of their old places only.</item>
/// </list>
/// One builder serves every Razor component, one build at a time; between builds it holds nothing.
/// </summary>
internal sealed class ElementBuilder : RenderTreeBuilder
{
    private struct PendingAttribute
    {
        public int Sequence;
        public string Name;
        public string Value;
    }

    /// <summary>An element open in this build, or the host at the bottom of the stack.</summary>
    private sealed class Frame
    {
        public int Sequence;
        public string Tag = "";
        public object? Key;
        public EntryKey? FixedKey;
        public Region Region = null!;
        public Placement Placement = null!;
        public bool Resolved;
        public Entry? Entry;
        public Region? ChildRegion;
        public Placement? ChildPlacement;
        public bool TakesText;
        public bool IsStyle;
        public bool HasText;
        public readonly StringBuilder Text = new();
        public readonly List<PendingAttribute> Attributes = [];
        public bool RunOpen;
        public EntryKey RunKey;
        public readonly StringBuilder Run = new();

        public void Reset()
        {
            Sequence = 0;
            Tag = "";
            Key = null;
            FixedKey = null;
            Region = null!;
            Placement = null!;
            Resolved = false;
            Entry = null;
            ChildRegion = null;
            ChildPlacement = null;
            TakesText = false;
            IsStyle = false;
            HasText = false;
            Text.Clear();
            Attributes.Clear();
            RunOpen = false;
            RunKey = default;
            Run.Clear();
        }
    }

    private static readonly Dictionary<string, MarkupFragment> markup = new(ReferenceEqualityComparer.Instance);
    private static readonly HashSet<(Type Type, int Sequence)> warnedDirectives = [];

    private static readonly HashSet<string> KnownEvents = new(StringComparer.OrdinalIgnoreCase)
    {
        "onclick", "ondblclick", "onpointerdown", "onpointerup", "onpointerenter", "onpointerleave", "onpointermove",
        "onwheel", "onkeydown", "onkeyup", "ontextinput", "onfocus", "onblur", "onchange", "onsubmit", "oncancel",
    };

    private readonly List<Frame> open = [];
    private readonly List<Frame> pool = [];
    private readonly List<EntryKey> unvisited = [];
    private readonly HashSet<string> tokens = [];
    private readonly List<KeyValuePair<string, string>> declarations = [];
    private ElementId[] children = new ElementId[16];
    private IElementSeam seam = null!;
    private UIComponent component = null!;
    private int build;

    /// <summary>How many folded markup strings are parsed and held, for the tests.</summary>
    internal static int ParsedMarkupCount => markup.Count;

    /// <summary>Forgets the parsed markup and the directives warned about, which name a script generation's types.</summary>
    internal static void ReleaseCaches()
    {
        markup.Clear();
        warnedDirectives.Clear();
    }

    /// <summary>Runs the Razor component's markup, building into its retained tree under its host.</summary>
    public void Build(UIComponent target, RetainedTree tree, IElementSeam elementSeam)
    {
        seam = elementSeam;
        component = target;
        build = ++tree.Build;
        try
        {
            Frame host = Rent();
            host.Resolved = true;
            host.ChildRegion = tree.Root;
            host.ChildPlacement = tree.Host;
            open.Add(host);
            component.BuildInto(this);
            if (open.Count != 1)
                throw new InvalidOperationException($"{open.Count - 1} element(s) were opened and never closed");
            FlushRun(host);
            FinishChildren(tree.Root, tree.Host);
        }
        finally
        {
            foreach (Frame frame in open)
                Return(frame);
            open.Clear();
            seam = null!;
            component = null!;
        }
    }

    public override void OpenElement(int sequence, string elementName) => Open(sequence, elementName, null);

    public override void CloseElement()
    {
        if (open.Count < 2)
            throw new InvalidOperationException("CloseElement with no element open");
        Frame frame = open[^1];
        if (frame.IsStyle)
        {
            AddSheet(frame.Text.ToString());
        }
        else
        {
            Entry entry = Resolve(frame);
            FlushRun(frame);
            if (frame.TakesText)
                ApplyText(entry, frame.HasText ? frame.Text.ToString() : "");
            FinishAttributes(entry);
            if (entry.Children != null && entry.Placement != null)
                FinishChildren(entry.Children, entry.Placement);
        }
        open.RemoveAt(open.Count - 1);
        Return(frame);
    }

    public override void AddMarkupContent(int sequence, string? markupContent)
    {
        // Whitespace between elements is layout's business, never an element.
        if (string.IsNullOrWhiteSpace(markupContent))
            return;
        if (!markup.TryGetValue(markupContent, out MarkupFragment? fragment))
        {
            fragment = MarkupFragment.Parse(markupContent);
            markup.Add(markupContent, fragment);
        }
        Frame frame = open[^1];
        Resolve(frame);
        if (frame.IsStyle)
        {
            foreach (MarkupFragment.Node node in fragment.Nodes)
            {
                if (node.Kind == MarkupFragment.NodeKind.Text)
                    AppendText(frame, node.Text, sequence, null);
            }
            return;
        }
        // In a label or a button, folded text joins the text around it (AppendText); elsewhere it is a generated label.
        int occurrence = frame.ChildRegion!.NextOccurrence(sequence, build);
        for (int part = 0; part < fragment.Nodes.Count; ++part)
            Replay(fragment.Nodes[part], frame, sequence, new EntryKey(sequence, occurrence, null, part));
    }

    public override void AddContent(int sequence, string? textContent) => AddText(sequence, textContent);

    public override void AddContent(int sequence, object? textContent) => AddText(sequence, textContent?.ToString());

    public override void AddAttribute(int sequence, string name) => BufferAttribute(sequence, name, "");

    public override void AddAttribute(int sequence, string name, bool value)
    {
        // As in HTML, a false boolean attribute is left out.
        if (value)
            BufferAttribute(sequence, name, "true");
    }

    public override void AddAttribute(int sequence, string name, string? value)
    {
        if (value != null)
            BufferAttribute(sequence, name, value);
    }

    public override void AddAttribute(int sequence, string name, object? value)
    {
        if (value is bool flag)
            AddAttribute(sequence, name, flag);
        else if (value is IFormattable formattable)
            BufferAttribute(sequence, name, formattable.ToString(null, CultureInfo.InvariantCulture));
        else if (value?.ToString() is { } text)
            BufferAttribute(sequence, name, text);
    }

    public override void SetKey(object? value)
    {
        if (open.Count < 2)
            throw new InvalidOperationException("SetKey with no element open");
        Frame frame = open[^1];
        if (frame.Resolved)
            throw new InvalidOperationException($"SetKey on <{frame.Tag}> after its content");
        frame.Key = value;
    }

    // ---- Opening, resolving and closing ----

    private void Open(int sequence, string tag, EntryKey? fixedKey)
    {
        Frame parent = open[^1];
        Resolve(parent);
        FlushRun(parent);
        Frame frame = Rent();
        frame.Sequence = sequence;
        frame.Tag = tag;
        frame.FixedKey = fixedKey;
        frame.Region = parent.ChildRegion!;
        frame.Placement = parent.ChildPlacement!;
        frame.TakesText = TakesText(tag);
        // A <style> makes no element: its text is a sheet (ticket 07).
        frame.IsStyle = tag == "style" || parent.IsStyle;
        if (frame.IsStyle)
            frame.Resolved = true;
        open.Add(frame);
    }

    /// <summary>Finds or makes the frame's element, at its first call that is not an attribute, and applies its attributes.</summary>
    private Entry Resolve(Frame frame)
    {
        if (frame.Resolved)
            return frame.Entry!;
        frame.Resolved = true;
        EntryKey key = frame.FixedKey ??
                       (frame.Key != null ? new EntryKey(frame.Sequence, 0, frame.Key, -1)
                                          : new EntryKey(frame.Sequence, frame.Region.NextOccurrence(frame.Sequence, build), null, -1));
        Entry entry = Place(frame.Region, frame.Placement, key, frame.Tag);
        frame.Entry = entry;
        entry.Children ??= new Region();
        entry.Placement ??= new Placement(entry.Id);
        frame.ChildRegion = entry.Children;
        frame.ChildPlacement = entry.Placement;
        foreach (PendingAttribute attribute in frame.Attributes)
            Apply(entry, attribute.Sequence, attribute.Name, attribute.Value);
        frame.Attributes.Clear();
        return entry;
    }

    /// <summary>
    /// Visits the entry at <paramref name="key"/>: the one kept, or a new element made after the last child visited
    /// before it. A different tag at the same key replaces the element.
    /// </summary>
    private Entry Place(Region region, Placement placement, EntryKey key, string tag)
    {
        placement.Begin(build);
        if (region.Entries.TryGetValue(key, out Entry? entry))
        {
            if (entry.Visited == build)
            {
                throw new InvalidOperationException(
                    $"Two siblings of <{tag}> at sequence {key.Sequence} have the same @key, {key.Key}");
            }
            if (entry.Tag != tag)
            {
                seam.Remove(entry.Id, RemoveMode.Outro);
                region.Entries.Remove(key);
                placement.MayHoldOutgoing = true;
                if (placement.Tail == entry)
                    placement.Tail = PreviousOwned(placement, entry);
                entry = null;
            }
        }
        if (entry == null)
        {
            Entry? previous = placement.Visited.Count > 0 ? placement.Visited[^1] : null;
            int index = InsertIndex(placement, previous);
            ElementId made = seam.CreateElement(placement.Element, tag, index);
            entry = new Entry(made, tag);
            region.Entries.Add(key, entry);
            if (placement.Native != null)
            {
                if (index < 0)
                    placement.Native.Add(made);
                else
                    placement.Native.Insert(index, made);
            }
            if (index < 0)
                placement.Tail = entry;
        }
        entry.Visited = build;
        placement.Visited.Add(entry);
        return entry;
    }

    /// <summary>
    /// Where a new element goes so it follows <paramref name="previous"/> (or leads, when null): after the last child
    /// (-1) when that is where it lands, else an index into the native children, which count outgoing and unowned ones.
    /// </summary>
    private int InsertIndex(Placement placement, Entry? previous)
    {
        if (placement.Tail == previous && !placement.HoldsUnowned)
            return -1;
        List<ElementId> native = ReadNative(placement);
        if (previous != null)
            return IndexOf(native, previous.Id) + 1;
        foreach (Entry owned in placement.Order)
        {
            int at = IndexOf(native, owned.Id);
            if (at >= 0)
                return at;
        }
        return placement.HoldsUnowned ? -1 : 0;
    }

    /// <summary>Whether the entry was among the placement's children at the end of the last build.</summary>
    private static bool IsInOrder(Placement placement, Entry entry) =>
        entry.OrderIndex >= 0 && entry.OrderIndex < placement.Order.Count && placement.Order[entry.OrderIndex] == entry;

    private static Entry? PreviousOwned(Placement placement, Entry entry)
    {
        int at = placement.Order.IndexOf(entry);
        return at > 0 ? placement.Order[at - 1] : null;
    }

    /// <summary>Removes the children not visited this build, and moves the visited ones into the order visited.</summary>
    private void FinishChildren(Region region, Placement placement)
    {
        placement.Begin(build);
        unvisited.Clear();
        foreach ((EntryKey key, Entry entry) in region.Entries)
        {
            if (entry.Visited != build)
                unvisited.Add(key);
        }
        foreach (EntryKey key in unvisited)
        {
            Entry gone = region.Entries[key];
            region.Entries.Remove(key);
            seam.Remove(gone.Id, RemoveMode.Outro);
            placement.MayHoldOutgoing = true;
        }
        unvisited.Clear();

        MoveIntoOrder(placement);

        (placement.Order, placement.Visited) = (placement.Visited, placement.Order);
        placement.Visited.Clear();
        for (int i = 0; i < placement.Order.Count; ++i)
            placement.Order[i].OrderIndex = i;
        placement.Tail = placement.Order.Count > 0 ? placement.Order[^1] : null;
        if (placement.Native != null && placement.Native.Count == placement.Order.Count)
            placement.MayHoldOutgoing = false;
        placement.Native = null;
    }

    /// <summary>
    /// Reorders the native children to the visited order, moving only those outside the longest increasing subsequence
    /// of their current places, so one insert or removal before a sibling costs that sibling nothing.
    /// </summary>
    private void MoveIntoOrder(Placement placement)
    {
        List<Entry> visited = placement.Visited;
        int count = visited.Count;
        if (count < 2)
            return;
        Span<int> places = count <= 128 ? stackalloc int[count] : new int[count];
        if (!CurrentPlaces(placement, places))
            return;

        Span<bool> keep = count <= 128 ? stackalloc bool[count] : new bool[count];
        LongestIncreasing(places, keep);
        List<ElementId> native = ReadNative(placement);
        int lastKept = keep.LastIndexOf(true);
        Entry? anchor = null;
        for (int i = count - 1; i >= 0; --i)
        {
            Entry entry = visited[i];
            if (keep[i])
            {
                anchor = entry;
                continue;
            }
            native.RemoveAt(IndexOf(native, entry.Id));
            int target = anchor != null ? IndexOf(native, anchor.Id) : IndexOf(native, visited[lastKept].Id) + 1;
            seam.Reorder(entry.Id, target);
            native.Insert(target, entry.Id);
            anchor = entry;
        }
    }

    /// <summary>
    /// Writes each visited child's current native place into <paramref name="places"/>, and returns whether they are out
    /// of order. Without a native read this build, the places come from the last order, new children appended after it.
    /// </summary>
    private static bool CurrentPlaces(Placement placement, Span<int> places)
    {
        List<Entry> visited = placement.Visited;
        int appended = placement.Order.Count;
        bool outOfOrder = false;
        for (int i = 0; i < visited.Count; ++i)
        {
            Entry entry = visited[i];
            if (placement.Native != null)
                places[i] = IndexOf(placement.Native, entry.Id);
            else
                places[i] = IsInOrder(placement, entry) ? entry.OrderIndex : appended++;
            if (i > 0 && places[i] < places[i - 1])
                outOfOrder = true;
        }
        return outOfOrder;
    }

    /// <summary>Marks in <paramref name="keep"/> one longest strictly increasing subsequence of <paramref name="values"/>.</summary>
    internal static void LongestIncreasing(ReadOnlySpan<int> values, Span<bool> keep)
    {
        int count = values.Length;
        var tails = new int[count];
        var previous = new int[count];
        int length = 0;
        for (int i = 0; i < count; ++i)
        {
            int low = 0, high = length;
            while (low < high)
            {
                int middle = (low + high) / 2;
                if (values[tails[middle]] < values[i])
                    low = middle + 1;
                else
                    high = middle;
            }
            previous[i] = low > 0 ? tails[low - 1] : -1;
            tails[low] = i;
            if (low == length)
                ++length;
        }
        keep.Clear();
        for (int i = length > 0 ? tails[length - 1] : -1; i >= 0; i = previous[i])
            keep[i] = true;
    }

    private List<ElementId> ReadNative(Placement placement)
    {
        if (placement.Native != null)
            return placement.Native;
        if (!placement.MayHoldOutgoing && !placement.HoldsUnowned)
        {
            // Only owned children are there: the last order, then those made since, each appended in turn (a child made
            // anywhere else would have needed this list first).
            var owned = new List<ElementId>(placement.Order.Count + placement.Visited.Count);
            foreach (Entry entry in placement.Order)
                owned.Add(entry.Id);
            foreach (Entry entry in placement.Visited)
            {
                if (!IsInOrder(placement, entry))
                    owned.Add(entry.Id);
            }
            placement.Native = owned;
            return owned;
        }
        int count = seam.GetChildren(placement.Element, children);
        if (count > children.Length)
        {
            children = new ElementId[System.Math.Max(count, children.Length * 2)];
            count = seam.GetChildren(placement.Element, children);
        }
        var native = new List<ElementId>(count);
        for (int i = 0; i < count && i < children.Length; ++i)
            native.Add(children[i]);
        placement.Native = native;
        return native;
    }

    private static int IndexOf(List<ElementId> list, ElementId element)
    {
        for (int i = 0; i < list.Count; ++i)
        {
            if (list[i].Index == element.Index && list[i].Version == element.Version)
                return i;
        }
        return -1;
    }

    // ---- Text and folded markup ----

    private void AddText(int sequence, string? text)
    {
        if (open.Count == 0 || string.IsNullOrEmpty(text))
            return;
        Frame frame = open[^1];
        Resolve(frame);
        AppendText(frame, text, sequence, null);
    }

    /// <summary>
    /// Adds text to the frame: to its own text in a label, a button or a style, else to the run that becomes a generated
    /// label, which starts here keyed by <paramref name="runKey"/> or, when null, by this sequence's occurrence.
    /// </summary>
    private void AppendText(Frame frame, string text, int sequence, EntryKey? runKey)
    {
        if (frame.IsStyle || frame.TakesText)
        {
            frame.Text.Append(text);
            frame.HasText = true;
            return;
        }
        if (!frame.RunOpen)
        {
            frame.RunOpen = true;
            frame.RunKey = runKey ?? new EntryKey(sequence, frame.ChildRegion!.NextOccurrence(sequence, build), null, -1);
        }
        frame.Run.Append(text);
    }

    /// <summary>Ends the frame's run of loose text: a generated label holds it, unless it is whitespace alone.</summary>
    private void FlushRun(Frame frame)
    {
        if (!frame.RunOpen)
            return;
        frame.RunOpen = false;
        string text = frame.Run.ToString();
        frame.Run.Clear();
        if (string.IsNullOrWhiteSpace(text))
            return;
        Entry label = Place(frame.ChildRegion!, frame.ChildPlacement!, frame.RunKey, "label");
        ApplyText(label, text);
    }

    private void ApplyText(Entry entry, string text)
    {
        if (entry.Text == text || (entry.Text == null && text.Length == 0))
            return;
        entry.Text = text;
        seam.SetText(entry.Id, text);
    }

    /// <summary>Builds one node of a folded markup string, as the generated calls for it would.</summary>
    private void Replay(MarkupFragment.Node node, Frame frame, int sequence, EntryKey key)
    {
        switch (node.Kind)
        {
            case MarkupFragment.NodeKind.Text:
                AppendText(frame, node.Text, sequence, key);
                break;
            case MarkupFragment.NodeKind.Style:
                AddSheet(node.Text);
                break;
            case MarkupFragment.NodeKind.Element:
                Open(sequence, node.Text, key);
                Frame element = open[^1];
                foreach ((string name, string value) in node.Attributes)
                    BufferAttribute(sequence, name, value);
                if (!element.IsStyle)
                    Resolve(element);
                for (int part = 0; part < node.Children.Count; ++part)
                    Replay(node.Children[part], element, sequence, new EntryKey(sequence, 0, null, part));
                CloseElement();
                break;
        }
    }

    /// <summary>
    /// A <c>&lt;style&gt;</c>'s sheet. Ticket 07 adds it as a scoped sheet once per root and Razor component type; until
    /// then it is dropped, and makes no element either way.
    /// </summary>
    private void AddSheet(string css)
    {
        _ = css;
    }

    // ---- Attributes ----

    private void BufferAttribute(int sequence, string name, string value)
    {
        if (open.Count < 2)
            throw new InvalidOperationException($"The attribute {name} came with no element open");
        Frame frame = open[^1];
        if (frame.IsStyle)
            return;
        if (frame.Resolved)
            Apply(frame.Entry!, sequence, name, value);
        else
            frame.Attributes.Add(new PendingAttribute { Sequence = sequence, Name = name, Value = value });
    }

    private void Apply(Entry entry, int sequence, string name, string value)
    {
        switch (name)
        {
            case "class":
                ApplyClasses(entry, value);
                break;
            case "style":
                ApplyStyle(entry, value);
                break;
            case "id":
                entry.IdSeen = build;
                if (entry.IdString != value)
                {
                    entry.IdString = value;
                    seam.SetIdString(entry.Id, value);
                }
                break;
            case "value" when entry.Tag is "textentry" or "slider" or "progress" or "checkbox":
                entry.ValueSeen = build;
                if (entry.Value != value)
                {
                    entry.Value = value;
                    if (entry.Tag == "textentry")
                        seam.SetEntryText(entry.Id, value);
                    else
                        seam.SetValue(entry.Id, ParseValue(value));
                }
                break;
            default:
                if (IsUnrecognisedDirective(name))
                {
                    WarnDirective(sequence, name);
                    break;
                }
                ApplyAttribute(entry, name, value);
                break;
        }
    }

    private void ApplyClasses(Entry entry, string value)
    {
        entry.ClassSeen = build;
        if (entry.ClassText == value)
            return;
        entry.ClassText = value;
        string[] next = value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        tokens.Clear();
        foreach (string token in next)
            tokens.Add(token);
        foreach (string token in entry.Classes)
        {
            if (!tokens.Contains(token))
                seam.RemoveClass(entry.Id, token);
        }
        tokens.Clear();
        foreach (string token in entry.Classes)
            tokens.Add(token);
        foreach (string token in next)
        {
            if (tokens.Add(token))
                seam.AddClass(entry.Id, token);
        }
        tokens.Clear();
        entry.Classes = next;
    }

    private void ApplyStyle(Entry entry, string value)
    {
        entry.StyleSeen = build;
        if (entry.StyleText == value)
            return;
        entry.StyleText = value;
        declarations.Clear();
        foreach (string declaration in value.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            int colon = declaration.IndexOf(':');
            if (colon <= 0)
                continue;
            string property = declaration[..colon].Trim();
            string propertyValue = declaration[(colon + 1)..].Trim();
            if (property.Length == 0)
                continue;
            int same = FindDeclaration(declarations, property);
            if (same >= 0)
                declarations[same] = new(property, propertyValue);
            else
                declarations.Add(new(property, propertyValue));
        }
        // Resets first, so a dropped shorthand never undoes a longhand set in its place.
        foreach ((string property, _) in entry.Styles)
        {
            if (FindDeclaration(declarations, property) < 0)
                seam.SetStyle(entry.Id, property, "");
        }
        foreach ((string property, string propertyValue) in declarations)
        {
            int old = FindDeclaration(entry.Styles, property);
            if (old < 0 || entry.Styles[old].Value != propertyValue)
                seam.SetStyle(entry.Id, property, propertyValue);
        }
        entry.Styles = [.. declarations];
        declarations.Clear();
    }

    private static int FindDeclaration(List<KeyValuePair<string, string>> list, string property)
    {
        for (int i = 0; i < list.Count; ++i)
        {
            if (list[i].Key == property)
                return i;
        }
        return -1;
    }

    private void ApplyAttribute(Entry entry, string name, string value)
    {
        entry.Attributes ??= [];
        List<Entry.Attribute> attributes = entry.Attributes;
        for (int i = 0; i < attributes.Count; ++i)
        {
            if (attributes[i].Name != name)
                continue;
            Entry.Attribute held = attributes[i];
            held.Seen = build;
            if (held.Value != value)
            {
                held.Value = value;
                seam.SetAttribute(entry.Id, name, value);
            }
            attributes[i] = held;
            return;
        }
        attributes.Add(new Entry.Attribute { Name = name, Value = value, Seen = build });
        seam.SetAttribute(entry.Id, name, value);
    }

    /// <summary>
    /// Undoes what this build's attributes left out: the classes and declarations of a dropped <c>class</c> or
    /// <c>style</c>, a dropped id, and dropped attributes (a boolean one set <c>"false"</c>, any other <c>""</c>).
    /// </summary>
    private void FinishAttributes(Entry entry)
    {
        if (entry.ClassSeen != build && entry.ClassText != null)
        {
            ApplyClasses(entry, "");
            entry.ClassText = null;
        }
        if (entry.StyleSeen != build && entry.StyleText != null)
        {
            ApplyStyle(entry, "");
            entry.StyleText = null;
        }
        if (entry.IdSeen != build && entry.IdString != null)
        {
            entry.IdString = null;
            seam.SetIdString(entry.Id, "");
        }
        if (entry.ValueSeen != build)
            entry.Value = null;
        if (entry.Attributes == null)
            return;
        for (int i = entry.Attributes.Count - 1; i >= 0; --i)
        {
            Entry.Attribute held = entry.Attributes[i];
            if (held.Seen == build)
                continue;
            entry.Attributes.RemoveAt(i);
            seam.SetAttribute(entry.Id, held.Name, held.Value == "true" ? "false" : "");
        }
    }

    private static float ParseValue(string value)
    {
        if (value == "true")
            return 1.0f;
        if (value == "false")
            return 0.0f;
        return float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed) ? parsed : 0.0f;
    }

    private static bool IsUnrecognisedDirective(string name) =>
        name.StartsWith('@') || KnownEvents.Contains(name);

    private void WarnDirective(int sequence, string name)
    {
        Type type = component.GetType();
        if (!warnedDirectives.Add((type, sequence)))
            return;
        RazorDiagnostics.Warn(
            $"The Razor component {type.FullName} has the attribute \"{name}\" at sequence {sequence}, which no directive " +
            "recognised, so it was left out: is \"@using Microsoft.AspNetCore.Components.Web\" missing from its imports?");
    }

    private static bool TakesText(string tag) => tag is "label" or "button";

    private Frame Rent()
    {
        if (pool.Count == 0)
            return new Frame();
        Frame frame = pool[^1];
        pool.RemoveAt(pool.Count - 1);
        return frame;
    }

    private void Return(Frame frame)
    {
        frame.Reset();
        pool.Add(frame);
    }
}
