namespace Fenrir.UI.Razor;

/// <summary>
/// The element seam: the few calls on one UI root's elements the builder and mounting make, and nothing else of the
/// root. The production implementation is <see cref="RootSeam"/>, over the <see cref="UIRoot"/> natives; tests use a
/// recording fake, so what a build does is a list of these calls.
/// </summary>
internal interface IElementSeam
{
    /// <summary>Whether the element is alive: made, not removed, and its root not destroyed. Outgoing is alive.</summary>
    bool IsAlive(ElementId element);

    /// <summary>Makes an element of <paramref name="tag"/> under <paramref name="parent"/>; a negative index is after the last child.</summary>
    ElementId CreateElement(ElementId parent, string tag, int index);

    /// <summary>Removes the element and everything under it.</summary>
    void Remove(ElementId element, RemoveMode mode);

    /// <summary>Moves the element among its siblings: to <paramref name="index"/> of the children without it.</summary>
    void Reorder(ElementId element, int index);

    /// <summary>
    /// Writes the element's children, in order, into <paramref name="children"/>, as many as fit, and returns how many it
    /// has. Outgoing children (removed with their outro, not yet gone) are among them.
    /// </summary>
    int GetChildren(ElementId element, Span<ElementId> children);

    /// <summary>Adds the class <paramref name="name"/> to the element.</summary>
    void AddClass(ElementId element, string name);

    /// <summary>Removes the class <paramref name="name"/> from the element.</summary>
    void RemoveClass(ElementId element, string name);

    /// <summary>Sets one inline style declaration of the element; an empty value resets it to the cascade's.</summary>
    void SetStyle(ElementId element, string name, string value);

    /// <summary>Sets the element's id string, the one <c>FindById</c> and <c>#id</c> selectors match.</summary>
    void SetIdString(ElementId element, string id);

    /// <summary>Sets the text of a <c>label</c> or <c>button</c>.</summary>
    void SetText(ElementId element, string text);

    /// <summary>Sets an attribute the element's tag reads, as markup gives it.</summary>
    void SetAttribute(ElementId element, string name, string value);

    /// <summary>Sets a slider's, progress bar's or checkbox's value, sending no Change.</summary>
    void SetValue(ElementId element, float value);

    /// <summary>Sets a text entry's value, sending no Change.</summary>
    void SetEntryText(ElementId element, string text);
}

/// <summary>The element seam over one UI root's natives.</summary>
internal sealed class RootSeam(UIRoot root) : IElementSeam
{
    public bool IsAlive(ElementId element) => root.IsAlive(element);

    public ElementId CreateElement(ElementId parent, string tag, int index) => root.CreateElement(parent, tag, index);

    public void Remove(ElementId element, RemoveMode mode) => root.Remove(element, mode);

    public void Reorder(ElementId element, int index) => root.Reorder(element, index);

    public int GetChildren(ElementId element, Span<ElementId> children) => root.GetChildren(element, children);

    public void AddClass(ElementId element, string name) => root.AddClass(element, name);

    public void RemoveClass(ElementId element, string name) => root.RemoveClass(element, name);

    public void SetStyle(ElementId element, string name, string value) => root.SetStyle(element, name, value);

    public void SetIdString(ElementId element, string id) => root.SetIdString(element, id);

    public void SetText(ElementId element, string text) => root.SetText(element, text);

    public void SetAttribute(ElementId element, string name, string value) => root.SetAttribute(element, name, value);

    public void SetValue(ElementId element, float value) => root.SetValue(element, value);

    public void SetEntryText(ElementId element, string text) => root.SetEntryText(element, text);
}
