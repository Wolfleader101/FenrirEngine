namespace Fenrir.UI.Razor;

/// <summary>
/// The element seam: the few calls on one UI root's elements the builder and mounting make, and nothing else of the
/// root. The production implementation is <see cref="RootSeam"/>, over the <see cref="UIRoot"/> natives; tests use a
/// recording fake, so what a build does is a list of these calls.
/// </summary>
internal interface IElementSeam
{
    /// <summary>Makes an element of <paramref name="tag"/> under <paramref name="parent"/>; a negative index is after the last child.</summary>
    ElementId CreateElement(ElementId parent, string tag, int index);

    /// <summary>Removes the element and everything under it.</summary>
    void Remove(ElementId element, RemoveMode mode);

    /// <summary>Adds the class <paramref name="name"/> to the element.</summary>
    void AddClass(ElementId element, string name);

    /// <summary>Sets one inline style declaration of the element.</summary>
    void SetStyle(ElementId element, string name, string value);

    /// <summary>Sets the element's id string, the one <c>FindById</c> and <c>#id</c> selectors match.</summary>
    void SetIdString(ElementId element, string id);

    /// <summary>Sets the text of a <c>label</c> or <c>button</c>.</summary>
    void SetText(ElementId element, string text);

    /// <summary>Sets an attribute the element's tag reads, as markup gives it.</summary>
    void SetAttribute(ElementId element, string name, string value);
}

/// <summary>The element seam over one UI root's natives.</summary>
internal sealed class RootSeam(UIRoot root) : IElementSeam
{
    public ElementId CreateElement(ElementId parent, string tag, int index) => root.CreateElement(parent, tag, index);

    public void Remove(ElementId element, RemoveMode mode) => root.Remove(element, mode);

    public void AddClass(ElementId element, string name) => root.AddClass(element, name);

    public void SetStyle(ElementId element, string name, string value) => root.SetStyle(element, name, value);

    public void SetIdString(ElementId element, string id) => root.SetIdString(element, id);

    public void SetText(ElementId element, string text) => root.SetText(element, text);

    public void SetAttribute(ElementId element, string name, string value) => root.SetAttribute(element, name, value);
}
