using System.Globalization;
using System.Text;
using Microsoft.AspNetCore.Components;
using Microsoft.AspNetCore.Components.Rendering;

namespace Fenrir.UI.Razor;

/// <summary>
/// Builds a Razor component's elements in place as its generated <c>BuildRenderTree</c> runs, through the element seam
/// (spec: "The builder", ADR-0067). There is no virtual DOM: each element is an entry of the Razor component's
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
/// <item>An uppercase tag places a child Razor component in a region of its own, keyed where it is placed, its elements
/// going under the enclosing element. It builds when made, when a parameter changed by <c>Equals</c> (a
/// <c>RenderFragment</c> always counts), or when it asked; otherwise its elements are kept where they are with no call.
/// A fragment runs in a region of its own too.</item>
/// <item>Whatever is removed is forgotten, its subtree with it: an <c>@ref</c> is handed none, and a child Razor
/// component's <c>OnUnmount</c> runs.</item>
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

    private struct PendingHandler
    {
        public int Sequence;
        public string Name;
        public int Kind;
        public MulticastDelegate? Delegate;
        public IHandleEvent? Receiver;
    }

    private enum FrameKind
    {
        /// <summary>An element: its attributes, then its content.</summary>
        Element,

        /// <summary>A child Razor component's tag: its parameters only, applied at its close.</summary>
        Component,

        /// <summary>
        /// A key space building under the frame below's element: the host at the bottom of the stack, a child Razor
        /// component's build, or a fragment's.
        /// </summary>
        Region,
    }

    /// <summary>An element, a child Razor component's tag or a region open in this build.</summary>
    private sealed class Frame
    {
        public FrameKind Kind;
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
        public readonly List<PendingHandler> Handlers = [];
        public bool RunOpen;
        public EntryKey RunKey;
        public readonly StringBuilder Run = new();

        /// <summary>For a region, the label, button or style below it whose text its text joins; null when none.</summary>
        public Frame? TextTarget;

        public Type? ComponentType;
        public readonly List<KeyValuePair<string, object?>> Parameters = [];
        public Action<object>? ComponentReference;
        public Action<ElementReference>? ElementReference;

        public void Reset()
        {
            Kind = FrameKind.Element;
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
            Handlers.Clear();
            RunOpen = false;
            RunKey = default;
            Run.Clear();
            TextTarget = null;
            ComponentType = null;
            Parameters.Clear();
            ComponentReference = null;
            ElementReference = null;
        }
    }

    private static readonly Dictionary<string, MarkupFragment> markup = new(ReferenceEqualityComparer.Instance);
    private static readonly HashSet<(Type Type, int Sequence)> warnedDirectives = [];

    private readonly List<Frame> open = [];
    private readonly List<Frame> pool = [];
    private readonly List<EntryKey> unvisited = [];
    private readonly List<Entry> kept = [];
    private readonly HashSet<string> tokens = [];
    private readonly List<KeyValuePair<string, string>> declarations = [];
    private ElementId[] children = new ElementId[16];
    private IElementSeam seam = null!;
    private RetainedTree tree = null!;
    private UIComponent component = null!;
    private ChildComponent? building;
    private int build;
    /// <summary>The <c>&lt;style&gt;</c> blocks of the folded markup string being built so far, which tells them apart.</summary>
    private int foldedStyles;

    /// <summary>Forgets the parsed markup and the directives warned about, which name a script generation's types.</summary>
    internal static void ReleaseCaches()
    {
        markup.Clear();
        warnedDirectives.Clear();
    }

    /// <summary>Runs the Razor component's markup, building into its retained tree under its host.</summary>
    public void Build(UIComponent target, RetainedTree retained, IElementSeam elementSeam)
    {
        seam = elementSeam;
        tree = retained;
        component = target;
        building = null;
        build = ++tree.Build;
        try
        {
            Frame host = OpenRegion(tree.Root, tree.Host, null, false);
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
            kept.Clear();
            // A build that throws midway leaves these filled, and a @key in them can be a script generation's object.
            unvisited.Clear();
            tokens.Clear();
            declarations.Clear();
            seam = null!;
            tree = null!;
            component = null!;
            building = null;
        }
    }

    /// <summary>
    /// Forgets a mounted Razor component's whole tree, whose host is going: every <c>@ref</c> is handed none and every
    /// child Razor component forgotten (its <c>OnUnmount</c> run), children first. It makes no native call.
    /// </summary>
    internal static void ForgetTree(RetainedTree retained, UIComponent owner)
    {
        ForgetRegion(retained.Root, null, new Forgetting(retained, null, logErrors: true), owner);
        retained.Children.Clear();
    }

    public override void OpenElement(int sequence, string elementName) => Open(sequence, elementName, null);

    public override void CloseElement()
    {
        Frame frame = open[^1];
        if (frame.Kind != FrameKind.Element)
            throw new InvalidOperationException("CloseElement with no element open");
        if (frame.IsStyle)
        {
            AddSheet(frame.Sequence, 0, frame.Text.ToString());
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
        foldedStyles = 0;
        for (int part = 0; part < fragment.Nodes.Count; ++part)
            Replay(fragment.Nodes[part], frame, sequence, new EntryKey(sequence, occurrence, null, part));
    }

    public override void AddContent(int sequence, string? textContent) => AddText(sequence, textContent);

    public override void AddContent(int sequence, object? textContent) => AddText(sequence, textContent?.ToString());

    public override void AddContent(int sequence, RenderFragment? fragment)
    {
        Frame frame = open[^1];
        Resolve(frame);
        FlushRun(frame);
        if (fragment == null)
            return;
        // Its own key space, keyed here: the numbers inside it are the file's that wrote it, not this one's.
        Region parent = frame.ChildRegion!;
        var key = new EntryKey(sequence, parent.NextOccurrence(sequence, build), null, -1);
        parent.Fragments ??= [];
        if (!parent.Fragments.TryGetValue(key, out Region? region))
        {
            region = new Region();
            parent.Fragments.Add(key, region);
        }
        region.Visited = build;

        Frame inner = OpenRegion(region, frame.ChildPlacement!, TextTargetOf(frame), frame.IsStyle);
        UIComponent outer = component;
        // Its directives are the writer's to warn about, when the writer is a Razor component (not a loop's closure).
        if (fragment.Target is UIComponent writer)
            component = writer;
        fragment(this);
        component = outer;
        CloseRegion(inner, region);
    }

    public override void AddAttribute(int sequence, string name)
    {
        if (!TakeParameter(name, true))
            BufferAttribute(sequence, name, "");
    }

    public override void AddAttribute(int sequence, string name, bool value)
    {
        if (TakeParameter(name, value))
            return;
        // As in HTML, a false boolean attribute is left out.
        if (value)
            BufferAttribute(sequence, name, "true");
    }

    public override void AddAttribute(int sequence, string name, string? value)
    {
        if (!TakeParameter(name, value) && value != null)
            BufferAttribute(sequence, name, value);
    }

    public override void AddAttribute(int sequence, string name, object? value)
    {
        if (TakeParameter(name, value))
            return;
        if (value is bool flag)
            AddAttribute(sequence, name, flag);
        else if (value is IFormattable formattable)
            BufferAttribute(sequence, name, formattable.ToString(null, CultureInfo.InvariantCulture));
        else if (value?.ToString() is { } text)
            BufferAttribute(sequence, name, text);
    }

    public override void AddAttribute(int sequence, string name, MulticastDelegate? value)
    {
        if (TakeParameter(name, value))
            return;
        if (open[^1].Kind != FrameKind.Element)
            throw new InvalidOperationException($"The attribute {name} came with no element open");
        // An element has no delegate attributes of its own: one spelled as a directive was not recognised. Handlers are
        // ticket 06's.
        if (IsUnrecognisedDirective(name))
            WarnDirective(sequence, name);
    }

    public override void AddAttribute(int sequence, string name, EventCallback value)
    {
        if (!TakeParameter(name, value))
            AddHandler(sequence, name, value.Delegate, value.Receiver);
    }

    public override void AddAttribute<TArgument>(int sequence, string name, EventCallback<TArgument> value)
    {
        if (!TakeParameter(name, value))
            AddHandler(sequence, name, value.Delegate, value.Receiver);
    }

    /// <summary>
    /// After <c>@bind</c>'s change callback: nothing to do, as the dispatcher reads a bound control's value on its
    /// change (ADR-0069).
    /// </summary>
    public override void SetUpdatesAttributeName(string updatesAttributeName)
    {
    }

    public override void AddComponentParameter(int sequence, string name, object? value)
    {
        if (!TakeParameter(name, value))
            throw new InvalidOperationException($"The parameter {name} came with no Razor component open");
    }

    public override void SetKey(object? value)
    {
        Frame frame = open[^1];
        if (frame.Kind == FrameKind.Region)
            throw new InvalidOperationException("SetKey with no element or Razor component open");
        if (frame.Resolved)
            throw new InvalidOperationException($"SetKey on <{frame.Tag}> after its content");
        frame.Key = value;
    }

    public override void AddElementReferenceCapture(int sequence, Action<ElementReference> elementReferenceCaptureAction)
    {
        Frame frame = open[^1];
        if (frame.Kind != FrameKind.Element)
            throw new InvalidOperationException("An element's @ref came with no element open");
        if (frame.IsStyle)
            return;
        if (frame.Resolved)
            SetReference(frame.Entry!, elementReferenceCaptureAction);
        else
            frame.ElementReference = elementReferenceCaptureAction;
    }

    public override void AddComponentReferenceCapture(int sequence, Action<object> componentReferenceCaptureAction)
    {
        Frame frame = open[^1];
        if (frame.Kind != FrameKind.Component)
            throw new InvalidOperationException("A Razor component's @ref came with no Razor component open");
        frame.ComponentReference = componentReferenceCaptureAction;
    }

    // ---- Child Razor components ----

    public override void OpenComponent<TComponent>(int sequence)
    {
        if (!typeof(UIComponent).IsAssignableFrom(typeof(TComponent)))
        {
            throw new InvalidOperationException(
                $"The Razor component {typeof(TComponent).FullName} does not derive from {typeof(UIComponent).FullName}");
        }
        Frame parent = open[^1];
        Resolve(parent);
        FlushRun(parent);
        Frame frame = Rent();
        frame.Kind = FrameKind.Component;
        frame.Sequence = sequence;
        frame.Tag = typeof(TComponent).Name;
        frame.ComponentType = typeof(TComponent);
        frame.Region = parent.ChildRegion!;
        frame.Placement = parent.ChildPlacement!;
        frame.TextTarget = TextTargetOf(parent);
        frame.IsStyle = parent.IsStyle;
        open.Add(frame);
    }

    /// <summary>
    /// Finds or makes the child Razor component at its key, sets its parameters when one changed, and builds it when
    /// it is new, a parameter changed, or it asked to; otherwise keeps its elements where they are.
    /// </summary>
    public override void CloseComponent()
    {
        Frame frame = open[^1];
        if (frame.Kind != FrameKind.Component)
            throw new InvalidOperationException("CloseComponent with no Razor component open");
        Region region = frame.Region;
        Placement placement = frame.Placement;
        EntryKey key = frame.Key != null ? new EntryKey(frame.Sequence, 0, frame.Key, -1)
                                         : new EntryKey(frame.Sequence, region.NextOccurrence(frame.Sequence, build), null, -1);
        placement.Begin(build);
        region.Components ??= [];
        ChildComponent? child = null;
        if (region.Components.TryGetValue(key, out ChildComponent? found))
        {
            if (found.Visited == build)
            {
                throw new InvalidOperationException(
                    $"Two {frame.Tag} siblings at sequence {key.Sequence} have the same @key, {key.Key}");
            }
            if (found.Component.GetType() == frame.ComponentType)
            {
                child = found;
            }
            else
            {
                region.Components.Remove(key);
                ForgetChild(found, placement, DuringBuild());
            }
        }
        bool made = child == null;
        if (child == null)
        {
            var instance = (UIComponent)Activator.CreateInstance(frame.ComponentType!)!;
            instance.Root = component.Root;
            instance.Element = placement.Element;
            instance.Mounted = component.Mounted;
            child = new ChildComponent(instance, building);
            instance.ChildRecord = child;
            region.Components.Add(key, child);
            tree.Children.Add(child);
            tree.Sheets?.Load(frame.ComponentType!, seam);
        }
        child.Visited = build;
        bool changed = made || ParametersChanged(child.Parameters, frame.Parameters);
        if (changed)
        {
            child.Parameters.Clear();
            child.Parameters.AddRange(frame.Parameters);
            ComponentParameters.Set(child.Component, child.Parameters);
        }
        Action<object>? reference = frame.ComponentReference;
        Frame? textTarget = frame.TextTarget;
        bool isStyle = frame.IsStyle;
        open.RemoveAt(open.Count - 1);
        Return(frame);

        // One whose text joins an enclosing label's, button's or style's builds every time: kept, its text would be lost.
        if (made || changed || child.Dirty || textTarget != null)
            BuildChild(child, placement, textTarget, isStyle, made, changed);
        else
            KeepPlaced(child.Region, placement);

        if (reference != null && (child.Reference == null || !child.Reference.Equals(reference)))
        {
            child.Reference = reference;
            reference(child.Component);
        }
    }

    /// <summary>
    /// Whether the parameters given this build differ from those last set: by name and order, and by <c>Equals</c>, a
    /// fragment always counting as changed (it is a new delegate each build, and what it builds may differ).
    /// </summary>
    private static bool ParametersChanged(List<KeyValuePair<string, object?>> last, List<KeyValuePair<string, object?>> next)
    {
        if (last.Count != next.Count)
            return true;
        for (int i = 0; i < next.Count; ++i)
        {
            if (last[i].Key != next[i].Key || IsFragment(next[i].Value) || !Equals(last[i].Value, next[i].Value))
                return true;
        }
        return false;
    }

    private static bool IsFragment(object? value) =>
        value is RenderFragment ||
        (value is Delegate && value.GetType().IsGenericType &&
         value.GetType().GetGenericTypeDefinition() == typeof(RenderFragment<>));

    private void BuildChild(ChildComponent child, Placement placement, Frame? textTarget, bool isStyle, bool made,
                            bool changed)
    {
        Frame region = OpenRegion(child.Region, placement, textTarget, isStyle);

        UIComponent outer = component;
        ChildComponent? outerChild = building;
        UIComponent built = child.Component;
        component = built;
        building = child;
        built.Context = outer.Context;
        // Cleared before the build, so StateHasChanged from the build itself or OnBuilt asks for next frame's.
        child.Dirty = false;
        if (made)
        {
            child.MountHookRan = true;
            built.CallOnMount();
        }
        if (changed)
            built.CallOnParametersSet();
        built.BuildInto(this);
        CloseRegion(region, child.Region);
        // After every build, so a hash its new parameters changed does not ask for another next frame.
        child.LastHash = built.CallBuildHash();
        built.CallOnBuilt(made);
        component = outer;
        building = outerChild;
    }

    /// <summary>
    /// Keeps a child Razor component that did not build where it is: its elements, and those of the components and
    /// fragments in it, are visited in their places, so the enclosing element's close neither removes nor moves them.
    /// </summary>
    private void KeepPlaced(Region region, Placement placement)
    {
        kept.Clear();
        CollectPlaced(region, kept);
        kept.Sort(static (a, b) => a.OrderIndex.CompareTo(b.OrderIndex));
        foreach (Entry entry in kept)
        {
            entry.Visited = build;
            placement.Visited.Add(entry);
        }
        kept.Clear();
    }

    private static void CollectPlaced(Region region, List<Entry> into)
    {
        into.AddRange(region.Entries.Values);
        if (region.Components != null)
        {
            foreach (ChildComponent child in region.Components.Values)
                CollectPlaced(child.Region, into);
        }
        if (region.Fragments != null)
        {
            foreach (Region fragment in region.Fragments.Values)
                CollectPlaced(fragment, into);
        }
    }

    /// <summary>Records a parameter of the open child Razor component; false when the open frame is not one.</summary>
    private bool TakeParameter(string name, object? value)
    {
        Frame frame = open[^1];
        if (frame.Kind != FrameKind.Component)
            return false;
        frame.Parameters.Add(new(name, value));
        return true;
    }

    // ---- Documents ----

    /// <summary>
    /// <c>&lt;Document&gt;</c>'s build (ADR-0067): a host <c>panel</c> at <paramref name="sequence"/>, with the class
    /// <c>document</c>, whose children are the document's, unowned: no build visits, updates or removes them. When
    /// <paramref name="src"/> is not what was last loaded, the host's children are removed at once, with no outro, and
    /// <paramref name="src"/> loaded after them (nothing for null or empty). A failed load is not retried until
    /// <paramref name="src"/> changes; the native logs its errors.
    /// </summary>
    internal void PlaceDocument(int sequence, string? src)
    {
        // In a <style>, an element is nothing: the sheet takes only text.
        if (open[^1].IsStyle)
            return;
        OpenElement(sequence, "panel");
        AddAttribute(sequence, "class", "document");
        Entry host = Resolve(open[^1]);
        host.Placement!.HoldsUnowned = true;
        string? load = string.IsNullOrEmpty(src) ? null : src;
        if (host.LoadedDocumentPath != load)
        {
            if (host.LoadedDocumentPath != null)
                RemoveAllChildren(host.Id);
            host.LoadedDocumentPath = load;
            if (load != null)
                seam.LoadDocument(host.Id, load);
        }
        CloseElement();
    }

    /// <summary>Removes every child of the element at once: what a document loaded, which no entry names.</summary>
    private void RemoveAllChildren(ElementId element)
    {
        // Copied out first: the scratch array is the builder's, and each removal changes the native list.
        ElementId[] gone = ReadChildren(element).ToArray();
        foreach (ElementId child in gone)
            seam.Remove(child, RemoveMode.Immediate);
    }

    // ---- Regions ----

    /// <summary>
    /// Opens a key space whose elements go into <paramref name="placement"/>, its text joining
    /// <paramref name="textTarget"/>'s when there is one.
    /// </summary>
    private Frame OpenRegion(Region region, Placement placement, Frame? textTarget, bool isStyle)
    {
        Frame frame = Rent();
        frame.Kind = FrameKind.Region;
        frame.Resolved = true;
        frame.ChildRegion = region;
        frame.ChildPlacement = placement;
        frame.TextTarget = textTarget;
        frame.IsStyle = isStyle;
        open.Add(frame);
        return frame;
    }

    /// <summary>Ends a region: its loose text, then the removal of what it no longer built.</summary>
    private void CloseRegion(Frame frame, Region region)
    {
        if (open[^1] != frame)
            throw new InvalidOperationException("An element opened in a fragment or a Razor component was never closed");
        FlushRun(frame);
        RemoveUnvisited(region, frame.ChildPlacement!);
        open.RemoveAt(open.Count - 1);
        Return(frame);
    }

    /// <summary>The label, button or style a region opened in <paramref name="frame"/> joins its text to.</summary>
    private static Frame? TextTargetOf(Frame frame) =>
        frame.TextTarget ?? (frame.Kind == FrameKind.Element && (frame.TakesText || frame.IsStyle) ? frame : null);

    // ---- Opening, resolving and closing ----

    private void Open(int sequence, string tag, EntryKey? fixedKey)
    {
        Frame parent = open[^1];
        Resolve(parent);
        FlushRun(parent);
        Frame frame = Rent();
        frame.Kind = FrameKind.Element;
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
        if (frame.Kind == FrameKind.Component)
        {
            throw new InvalidOperationException(
                $"Content came inside the Razor component <{frame.Tag}>'s tag, outside a fragment");
        }
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
        foreach (PendingHandler handler in frame.Handlers)
            ApplyHandler(entry, handler);
        frame.Handlers.Clear();
        if (frame.ElementReference != null)
        {
            SetReference(entry, frame.ElementReference);
            frame.ElementReference = null;
        }
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
                Forgotten(entry, DuringBuild(), component);
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
            // The scope of the Razor component whose markup this is: a fragment's writer's, a child's own (ADR-0070).
            seam.AddScope(made, ComponentScope.ClassOf(component.GetType()));
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
        {
            // Gone natively (removed by someone else): append rather than guess.
            int at = IndexOf(native, previous.Id);
            return at >= 0 ? at + 1 : -1;
        }
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
        RemoveUnvisited(region, placement);
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
    /// Removes and forgets what the region did not build this build: its elements, with their outros, and the child
    /// Razor components and fragments it no longer placed, with all their elements.
    /// </summary>
    private void RemoveUnvisited(Region region, Placement placement)
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
            Forgotten(gone, DuringBuild(), component);
            seam.Remove(gone.Id, RemoveMode.Outro);
            placement.MayHoldOutgoing = true;
        }
        unvisited.Clear();

        if (region.Components != null)
        {
            foreach ((EntryKey key, ChildComponent child) in region.Components)
            {
                if (child.Visited != build)
                    unvisited.Add(key);
            }
            foreach (EntryKey key in unvisited)
            {
                ChildComponent gone = region.Components[key];
                region.Components.Remove(key);
                ForgetChild(gone, placement, DuringBuild());
            }
            unvisited.Clear();
        }

        if (region.Fragments != null)
        {
            foreach ((EntryKey key, Region fragment) in region.Fragments)
            {
                if (fragment.Visited != build)
                    unvisited.Add(key);
            }
            foreach (EntryKey key in unvisited)
            {
                Region gone = region.Fragments[key];
                region.Fragments.Remove(key);
                ForgetRegion(gone, placement, DuringBuild(), component);
            }
            unvisited.Clear();
        }
    }

    // ---- Forgetting ----

    /// <summary>
    /// What a forget walk needs besides the region: the tree whose child list it updates, the seam that removes a
    /// placement's elements, and whether a throwing hook is logged and the walk goes on (a mount's whole tree going)
    /// or the throw fails the build (a removal during one).
    /// </summary>
    private readonly struct Forgetting(RetainedTree tree, IElementSeam? seam, bool logErrors)
    {
        public RetainedTree Tree { get; } = tree;

        public IElementSeam? Seam { get; } = seam;

        /// <summary>Calls a Razor component's hook or a <c>@ref</c> action, as <see cref="Forgetting"/> says.</summary>
        public void Call<T>(Action<T> callback, T argument, UIComponent? owner)
        {
            if (!logErrors)
            {
                callback(argument);
                return;
            }
            try
            {
                callback(argument);
            }
            catch (Exception exception)
            {
                RazorDiagnostics.Error($"Forgetting the Razor component {owner?.GetType().FullName}'s elements threw: " +
                                       $"{exception}");
            }
        }
    }

    /// <summary>A removal during this build: a throwing hook fails it.</summary>
    private Forgetting DuringBuild() => new(tree, seam, logErrors: false);

    /// <summary>
    /// Forgets an element that is going, natively with everything under it: its <c>@ref</c> is handed none, and what
    /// its children region holds is forgotten with no native call. Every removal of an entry comes through here, so
    /// its handlers leave the root's handler table here too.
    /// </summary>
    private static void Forgotten(Entry entry, in Forgetting forgetting, UIComponent? owner)
    {
        forgetting.Tree.Handlers?.Remove(entry);
        entry.Handlers = null;
        if (entry.Reference is { } reference)
        {
            entry.Reference = null;
            forgetting.Call(reference, default, owner);
        }
        if (entry.Children != null)
            ForgetRegion(entry.Children, null, forgetting, owner);
    }

    /// <summary>
    /// Forgets everything in a region. With a placement, its elements are the placement's children, each removed with
    /// its outro; without, they are going with an element above them.
    /// </summary>
    private static void ForgetRegion(Region region, Placement? placement, in Forgetting forgetting, UIComponent? owner)
    {
        foreach (Entry entry in region.Entries.Values)
        {
            Forgotten(entry, forgetting, owner);
            if (placement != null)
            {
                forgetting.Seam!.Remove(entry.Id, RemoveMode.Outro);
                placement.MayHoldOutgoing = true;
            }
        }
        region.Entries.Clear();
        if (region.Components != null)
        {
            foreach (ChildComponent child in region.Components.Values)
                ForgetChild(child, placement, forgetting);
            region.Components.Clear();
        }
        if (region.Fragments != null)
        {
            foreach (Region fragment in region.Fragments.Values)
                ForgetRegion(fragment, placement, forgetting, owner);
            region.Fragments.Clear();
        }
    }

    /// <summary>
    /// Forgets a child Razor component: its elements as <see cref="ForgetRegion"/> does, then its <c>OnUnmount</c>
    /// when owed, after those of the Razor components under it, and its <c>@ref</c> is handed null.
    /// </summary>
    private static void ForgetChild(ChildComponent child, Placement? placement, in Forgetting forgetting)
    {
        UIComponent gone = child.Component;
        ForgetRegion(child.Region, placement, forgetting, gone);
        forgetting.Tree.Children.Remove(child);
        gone.Mounted = null;
        gone.ChildRecord = null;
        if (child.MountHookRan)
        {
            child.MountHookRan = false;
            forgetting.Call(static component => component.CallOnUnmount(), gone, gone);
        }
        if (child.Reference is { } reference)
        {
            child.Reference = null;
            forgetting.Call(reference, null!, gone);
        }
    }

    /// <summary>Hands an element's <c>@ref</c> the element, when it is new or the action is not the one it had.</summary>
    private static void SetReference(Entry entry, Action<ElementReference> reference)
    {
        if (entry.Reference != null && entry.Reference.Equals(reference))
            return;
        entry.Reference = reference;
        reference(UIComponent.ReferenceTo(entry.Id));
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
            int at = IndexOf(native, entry.Id);
            if (at < 0)
                continue; // Removed by someone else: nothing to move.
            native.RemoveAt(at);
            int target = anchor != null ? IndexOf(native, anchor.Id) : IndexOf(native, visited[lastKept].Id) + 1;
            if (target < 0)
                target = native.Count;
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
        ReadOnlySpan<ElementId> read = ReadChildren(placement.Element);
        var native = new List<ElementId>(read.Length);
        foreach (ElementId child in read)
            native.Add(child);
        placement.Native = native;
        return native;
    }

    /// <summary>The element's native children, in order, in the builder's scratch array (grown when they do not fit).</summary>
    private ReadOnlySpan<ElementId> ReadChildren(ElementId element)
    {
        int count = seam.GetChildren(element, children);
        if (count > children.Length)
        {
            children = new ElementId[System.Math.Max(count, children.Length * 2)];
            count = seam.GetChildren(element, children);
        }
        return children.AsSpan(0, System.Math.Min(count, children.Length));
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
        Frame frame = open[^1];
        Resolve(frame);
        // An empty run still starts the run, so a generated label keeps its key when its first part is sometimes empty.
        AppendText(frame, text ?? "", sequence, null);
    }

    /// <summary>
    /// Adds text to the frame: to its own text in a label, a button or a style, else to the run that becomes a generated
    /// label, which starts here keyed by <paramref name="runKey"/> or, when null, by this sequence's occurrence.
    /// </summary>
    private void AppendText(Frame frame, string text, int sequence, EntryKey? runKey)
    {
        Frame target = frame.TextTarget ?? frame;
        if (target.IsStyle || target.TakesText)
        {
            target.Text.Append(text);
            target.HasText = true;
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
                AddSheet(sequence, foldedStyles++, node.Text);
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
    /// A <c>&lt;style&gt;</c>'s sheet, folded or not: added with the scope of the Razor component that wrote it, once per
    /// root, type and block, a block being its sequence and, in a folded string, its place among the string's
    /// <c>&lt;style&gt;</c>s (ADR-0070). It makes no element.
    /// </summary>
    private void AddSheet(int sequence, int ordinal, string css)
    {
        tree.Sheets?.AddBlock(component.GetType(), sequence, ordinal, css, seam);
    }

    // ---- Attributes ----

    private void BufferAttribute(int sequence, string name, string value)
    {
        Frame frame = open[^1];
        if (frame.Kind != FrameKind.Element)
            throw new InvalidOperationException($"The attribute {name} came with no element open");
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
        if (entry.Handlers != null)
            FinishHandlers(entry);
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

    // ---- Handlers ----

    /// <summary>A handler attribute (ADR-0069): recorded on the open element's entry by kind, buffered until it is found.</summary>
    private void AddHandler(int sequence, string name, MulticastDelegate? handler, IHandleEvent? receiver)
    {
        Frame frame = open[^1];
        if (frame.Kind != FrameKind.Element)
            throw new InvalidOperationException($"The handler {name} came with no element open");
        if (frame.IsStyle)
            return;
        if (!HandlerKinds.TryKind(name, out int kind))
        {
            WarnDirective(sequence, name);
            return;
        }
        var pending = new PendingHandler
        {
            Sequence = sequence, Name = name, Kind = kind, Delegate = handler, Receiver = receiver,
        };
        if (frame.Resolved)
            ApplyHandler(frame.Entry!, pending);
        else
            frame.Handlers.Add(pending);
    }

    /// <summary>
    /// Gives the entry this build's handler of the kind, replacing the last (a lambda is a new delegate each build, and
    /// replacing it costs nothing native). A null delegate gives none. An element's first pointer handler queues its
    /// one pointer check for after its first layout.
    /// </summary>
    private void ApplyHandler(Entry entry, in PendingHandler pending)
    {
        if (pending.Delegate == null || tree.Handlers == null)
            return;
        entry.Handlers ??= new Handler?[HandlerKinds.Count];
        Handler handler = entry.Handlers[pending.Kind] ??= new Handler();
        handler.Delegate = pending.Delegate;
        handler.Owner = pending.Receiver as UIComponent ?? component;
        // A kind has one name, so the directive is spelled once per handler, not each build.
        if (handler.Directive.Length == 0)
            handler.Directive = "@" + pending.Name;
        handler.Seen = build;
        tree.Handlers.Add(entry);
        if (HandlerKinds.IsPointer(pending.Kind) && !entry.PointerChecked)
        {
            entry.PointerChecked = true;
            tree.Handlers.QueuePointerCheck(entry, PointerWarning(handler, pending.Sequence, entry.Tag));
        }
    }

    /// <summary>Drops the handlers this build did not give; an entry left with none leaves the handler table.</summary>
    private void FinishHandlers(Entry entry)
    {
        Handler?[] handlers = entry.Handlers!;
        bool any = false;
        for (int kind = 0; kind < handlers.Length; ++kind)
        {
            if (handlers[kind] is { } handler && handler.Seen != build)
                handlers[kind] = null;
            any |= handlers[kind] != null;
        }
        if (any)
            return;
        entry.Handlers = null;
        tree.Handlers?.Remove(entry);
    }

    private static string PointerWarning(Handler handler, int sequence, string tag) =>
        $"The Razor component {handler.Owner.GetType().FullName} has {handler.Directive} at sequence {sequence} on a " +
        $"<{tag}> whose style takes no pointer events, so the pointer never reaches it: give the element " +
        "pointer-events: all in a stylesheet (ADR-0065).";

    private static float ParseValue(string value)
    {
        if (value == "true")
            return 1.0f;
        if (value == "false")
            return 0.0f;
        return float.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out float parsed) ? parsed : 0.0f;
    }

    private static bool IsUnrecognisedDirective(string name) =>
        name.StartsWith('@') || HandlerKinds.IsNameInAnyCase(name);

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
