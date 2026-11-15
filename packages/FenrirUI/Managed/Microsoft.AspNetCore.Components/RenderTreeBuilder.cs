namespace Microsoft.AspNetCore.Components.Rendering;

/// <summary>
/// The calls a Razor component's generated <c>BuildRenderTree</c> makes, with Blazor's signatures. Every sequence number is
/// an integer literal the Razor generator numbers per file, from 0. The engine supplies the implementation, which builds
/// FenrirUI elements in place.
/// </summary>
public abstract class RenderTreeBuilder
{
    /// <summary>Opens an element of tag <paramref name="elementName"/>; its attributes follow, then its content.</summary>
    public abstract void OpenElement(int sequence, string elementName);

    /// <summary>Closes the element the matching <see cref="OpenElement"/> opened.</summary>
    public abstract void CloseElement();

    /// <summary>
    /// Static markup the generator folded into one string: whitespace between elements, or a run of elements with no
    /// C# in them.
    /// </summary>
    public abstract void AddMarkupContent(int sequence, string? markupContent);

    /// <summary>A run of text, written in the markup or from a <c>string</c> expression.</summary>
    public abstract void AddContent(int sequence, string? textContent);

    /// <summary>A run of text from an expression of any other type, shown as its <c>ToString()</c>.</summary>
    public abstract void AddContent(int sequence, object? textContent);

    /// <summary>A piece of markup built here, such as a Razor component's <c>@ChildContent</c>.</summary>
    public abstract void AddContent(int sequence, RenderFragment? fragment);

    /// <summary>An attribute with no value.</summary>
    public abstract void AddAttribute(int sequence, string name);

    /// <summary>An attribute from a <c>bool</c> expression.</summary>
    public abstract void AddAttribute(int sequence, string name, bool value);

    /// <summary>An attribute from text, or text with expressions in it (<c>class="a @b"</c>).</summary>
    public abstract void AddAttribute(int sequence, string name, string? value);

    /// <summary>An attribute from an expression of any other type.</summary>
    public abstract void AddAttribute(int sequence, string name, object? value);

    /// <summary>An attribute from a delegate: a Razor component's <c>ChildContent</c> or another fragment parameter.</summary>
    public abstract void AddAttribute(int sequence, string name, MulticastDelegate? value);

    /// <summary>A handler (<c>@onclick</c> and the others) or a Razor component's event parameter.</summary>
    public abstract void AddAttribute(int sequence, string name, EventCallback value);

    /// <summary>
    /// A handler taking an argument: <c>@onclick</c> and the others with a <c>UIEvent</c>, <c>@bind</c>'s change callback
    /// with a <c>ChangeEventArgs</c>, or a Razor component's event parameter.
    /// </summary>
    public abstract void AddAttribute<TArgument>(int sequence, string name, EventCallback<TArgument> value);

    /// <summary>
    /// After <c>@bind</c>'s attributes: the open element's change callback updates the attribute named
    /// <paramref name="updatesAttributeName"/>.
    /// </summary>
    public abstract void SetUpdatesAttributeName(string updatesAttributeName);

    /// <summary>Opens a child Razor component of type <typeparamref name="TComponent"/>; its parameters follow.</summary>
    public abstract void OpenComponent<TComponent>(int sequence) where TComponent : IComponent;

    /// <summary>Closes the Razor component the matching <see cref="OpenComponent{TComponent}"/> opened.</summary>
    public abstract void CloseComponent();

    /// <summary>A parameter of the open Razor component, set by its property's name.</summary>
    public abstract void AddComponentParameter(int sequence, string name, object? value);

    /// <summary>The open element's <c>@ref</c>: the action is handed the element, and none once it is removed.</summary>
    public abstract void AddElementReferenceCapture(int sequence, Action<ElementReference> elementReferenceCaptureAction);

    /// <summary>The open Razor component's <c>@ref</c>: the action is handed the Razor component.</summary>
    public abstract void AddComponentReferenceCapture(int sequence, Action<object> componentReferenceCaptureAction);

    /// <summary>
    /// The open element's or Razor component's <c>@key</c>, written after its attributes: it keeps the element when siblings come, go or move.
    /// </summary>
    public abstract void SetKey(object? value);
}
