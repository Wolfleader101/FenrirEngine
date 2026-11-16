using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Fenrir.UI.Razor.Tests;

/// <summary>
/// The element seam's recording fake: it hands out element ids #1, #2 and so on, in the order they are made, and
/// records every call as text, ids written as #index.
/// </summary>
internal sealed class RecordingSeam : IElementSeam
{
    [StructLayout(LayoutKind.Sequential)]
    private struct RawId
    {
        public uint Index;
        public uint Version;
    }

    private uint next = 1;

    public List<string> Calls { get; } = [];

    /// <summary>An element id with this index, as the fake writes it.</summary>
    public static ElementId Id(uint index)
    {
        var raw = new RawId { Index = index, Version = 1 };
        return Unsafe.As<RawId, ElementId>(ref raw);
    }

    public ElementId CreateElement(ElementId parent, string tag, int index)
    {
        ElementId made = Id(next++);
        Calls.Add($"CreateElement({Name(parent)}, {tag}, {index}) = {Name(made)}");
        return made;
    }

    public void Remove(ElementId element, RemoveMode mode) => Calls.Add($"Remove({Name(element)}, {mode})");

    public void AddClass(ElementId element, string name) => Calls.Add($"AddClass({Name(element)}, {name})");

    public void SetStyle(ElementId element, string name, string value) =>
        Calls.Add($"SetStyle({Name(element)}, {name}, {value})");

    public void SetIdString(ElementId element, string id) => Calls.Add($"SetIdString({Name(element)}, {id})");

    public void SetText(ElementId element, string text) => Calls.Add($"SetText({Name(element)}, {text})");

    public void SetAttribute(ElementId element, string name, string value) =>
        Calls.Add($"SetAttribute({Name(element)}, {name}, {value})");

    private static string Name(ElementId element) => $"#{element.Index}";
}
