using System.Text;
using Microsoft.AspNetCore.Components.Rendering;

namespace Fenrir.UI.Razor;

/// <summary>
/// Makes a Razor component's elements as its generated <c>BuildRenderTree</c> runs, through the element seam
/// (ADR-0067). An element's tag and attributes are buffered until its first call that is not an attribute (content, a
/// child, or its close), and then it is made at the end of its parent and given its attributes: <c>class</c> as one
/// class per token, <c>style</c> as one declaration per <c>name: value</c>, <c>id</c> as its id string, and any other
/// through <c>SetAttribute</c>. The text runs inside a <c>label</c> or <c>button</c> are joined and set once, as it
/// closes.
/// </summary>
internal sealed class ElementBuilder(IElementSeam seam) : RenderTreeBuilder
{
    private sealed class OpenFrame(string tag, ElementId parent)
    {
        public string Tag { get; } = tag;
        public ElementId Parent { get; } = parent;
        public ElementId Element { get; set; } = new();
        public bool Made { get; set; }
        public List<KeyValuePair<string, string>> Attributes { get; } = [];
        public StringBuilder? Text { get; set; }
    }

    private readonly List<OpenFrame> open = [];
    private ElementId host = new();

    /// <summary>Runs the Razor component's markup, making its elements under <paramref name="hostElement"/>.</summary>
    public void Build(UIComponent component, ElementId hostElement)
    {
        host = hostElement;
        open.Clear();
        component.BuildInto(this);
        if (open.Count != 0)
            throw new InvalidOperationException($"{open.Count} element(s) were opened and never closed");
    }

    public override void OpenElement(int sequence, string elementName)
    {
        ElementId parent = open.Count == 0 ? host : Made(open[^1]);
        open.Add(new OpenFrame(elementName, parent));
    }

    public override void CloseElement()
    {
        if (open.Count == 0)
            throw new InvalidOperationException("CloseElement with no element open");
        OpenFrame frame = open[^1];
        ElementId element = Made(frame);
        if (frame.Text is { Length: > 0 } text)
            seam.SetText(element, text.ToString());
        open.RemoveAt(open.Count - 1);
    }

    public override void AddMarkupContent(int sequence, string? markupContent)
    {
        // Whitespace between elements is layout's business, never an element. Folded markup holding elements is not
        // built yet.
    }

    public override void AddContent(int sequence, string? textContent) => AddText(textContent);

    public override void AddContent(int sequence, object? textContent) => AddText(textContent?.ToString());

    public override void AddAttribute(int sequence, string name) => Attribute(name, "");

    public override void AddAttribute(int sequence, string name, bool value)
    {
        // As in HTML, a false boolean attribute is left out.
        if (value)
            Attribute(name, "true");
    }

    public override void AddAttribute(int sequence, string name, string? value)
    {
        if (value != null)
            Attribute(name, value);
    }

    public override void AddAttribute(int sequence, string name, object? value)
    {
        if (value is bool flag)
            AddAttribute(sequence, name, flag);
        else if (value?.ToString() is { } text)
            Attribute(name, text);
    }

    private void AddText(string? text)
    {
        if (open.Count == 0 || string.IsNullOrEmpty(text))
            return;
        OpenFrame frame = open[^1];
        Made(frame);
        if (TakesText(frame.Tag))
            (frame.Text ??= new StringBuilder()).Append(text);
    }

    private void Attribute(string name, string value)
    {
        if (open.Count == 0)
            throw new InvalidOperationException($"The attribute {name} came with no element open");
        OpenFrame frame = open[^1];
        if (frame.Made)
            Apply(frame.Element, name, value);
        else
            frame.Attributes.Add(new(name, value));
    }

    private ElementId Made(OpenFrame frame)
    {
        if (frame.Made)
            return frame.Element;
        frame.Element = seam.CreateElement(frame.Parent, frame.Tag, -1);
        frame.Made = true;
        foreach ((string name, string value) in frame.Attributes)
            Apply(frame.Element, name, value);
        frame.Attributes.Clear();
        return frame.Element;
    }

    private void Apply(ElementId element, string name, string value)
    {
        switch (name)
        {
            case "class":
                foreach (string token in value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries))
                    seam.AddClass(element, token);
                break;
            case "style":
                foreach (string declaration in value.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
                {
                    int colon = declaration.IndexOf(':');
                    if (colon <= 0)
                        continue;
                    string property = declaration[..colon].Trim();
                    string propertyValue = declaration[(colon + 1)..].Trim();
                    if (property.Length != 0)
                        seam.SetStyle(element, property, propertyValue);
                }
                break;
            case "id":
                seam.SetIdString(element, value);
                break;
            default:
                seam.SetAttribute(element, name, value);
                break;
        }
    }

    private static bool TakesText(string tag) => tag is "label" or "button";
}
