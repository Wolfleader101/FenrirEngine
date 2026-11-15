using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Fenrir.UI.Razor.Tests;

/// <summary>
/// The element seam's recording fake: it hands out element ids #1, #2 and so on, in the order they are made, and
/// records every call as text, ids written as #index. It keeps the tree as a root would: children in order, an element
/// removed with its outro staying among its parent's children until <see cref="FinishOutros"/>. <c>IsAlive</c>, the
/// registry's liveness check, is counted in <see cref="AliveChecks"/> rather than recorded.
/// </summary>
internal sealed class RecordingSeam : IElementSeam
{
    [StructLayout(LayoutKind.Sequential)]
    private struct RawId
    {
        public uint Index;
        public uint Version;
    }

    private sealed class Node
    {
        public uint Parent;
        public List<uint> Children { get; } = [];
        public bool Outgoing;
    }

    private readonly Dictionary<uint, Node> nodes = [];
    private uint next = 1;
    private bool destroyed;

    public List<string> Calls { get; } = [];

    public int AliveChecks { get; private set; }

    /// <summary>An element id with this index, as the fake writes it.</summary>
    public static ElementId Id(uint index)
    {
        var raw = new RawId { Index = index, Version = 1 };
        return Unsafe.As<RawId, ElementId>(ref raw);
    }

    /// <summary>The element's children in order, as #index names, outgoing ones among them.</summary>
    public List<string> ChildrenOf(ElementId element) =>
        NodeOf(element.Index).Children.Select(child => $"#{child}").ToList();

    /// <summary>Ends every outro: the elements removed with one go from their parents.</summary>
    public void FinishOutros()
    {
        foreach (uint outgoing in nodes.Where(pair => pair.Value.Outgoing).Select(pair => pair.Key).ToList())
            Detach(outgoing);
    }

    /// <summary>Destroys the root: every element, made or not, is dead from now on.</summary>
    public void DestroyRoot() => destroyed = true;

    public bool IsAlive(ElementId element)
    {
        ++AliveChecks;
        return !destroyed && nodes.ContainsKey(element.Index);
    }

    public ElementId CreateElement(ElementId parent, string tag, int index)
    {
        ElementId made = Id(next++);
        nodes[made.Index] = new Node { Parent = parent.Index };
        Insert(NodeOf(parent.Index).Children, made.Index, index);
        Calls.Add($"CreateElement({Name(parent)}, {tag}, {index}) = {Name(made)}");
        return made;
    }

    public void Remove(ElementId element, RemoveMode mode)
    {
        Calls.Add($"Remove({Name(element)}, {mode})");
        if (!nodes.TryGetValue(element.Index, out Node? node))
            return;
        if (mode == RemoveMode.Outro)
            node.Outgoing = true;
        else
            Detach(element.Index);
    }

    public void Reorder(ElementId element, int index)
    {
        Calls.Add($"Reorder({Name(element)}, {index})");
        List<uint> siblings = NodeOf(NodeOf(element.Index).Parent).Children;
        siblings.Remove(element.Index);
        Insert(siblings, element.Index, index);
    }

    public int GetChildren(ElementId element, Span<ElementId> children)
    {
        Calls.Add($"GetChildren({Name(element)})");
        List<uint> list = NodeOf(element.Index).Children;
        for (int i = 0; i < list.Count && i < children.Length; ++i)
            children[i] = Id(list[i]);
        return list.Count;
    }

    public void AddClass(ElementId element, string name) => Calls.Add($"AddClass({Name(element)}, {name})");

    public void RemoveClass(ElementId element, string name) => Calls.Add($"RemoveClass({Name(element)}, {name})");

    public void SetStyle(ElementId element, string name, string value) =>
        Calls.Add($"SetStyle({Name(element)}, {name}, {value})");

    public void SetIdString(ElementId element, string id) => Calls.Add($"SetIdString({Name(element)}, {id})");

    public void SetText(ElementId element, string text) => Calls.Add($"SetText({Name(element)}, {text})");

    public void SetAttribute(ElementId element, string name, string value) =>
        Calls.Add($"SetAttribute({Name(element)}, {name}, {value})");

    public void SetValue(ElementId element, float value) =>
        Calls.Add($"SetValue({Name(element)}, {value.ToString(CultureInfo.InvariantCulture)})");

    public void SetEntryText(ElementId element, string text) => Calls.Add($"SetEntryText({Name(element)}, {text})");

    private Node NodeOf(uint index)
    {
        // A parent the fake did not make (the mount's parent) is a root-level node of its own.
        if (!nodes.TryGetValue(index, out Node? node))
            nodes[index] = node = new Node();
        return node;
    }

    private void Detach(uint index)
    {
        Node node = nodes[index];
        if (nodes.TryGetValue(node.Parent, out Node? parent))
            parent.Children.Remove(index);
        foreach (uint child in node.Children.ToList())
            Detach(child);
        nodes.Remove(index);
    }

    private static void Insert(List<uint> list, uint item, int index)
    {
        if (index < 0 || index >= list.Count)
            list.Add(item);
        else
            list.Insert(index, item);
    }

    private static string Name(ElementId element) => $"#{element.Index}";
}
