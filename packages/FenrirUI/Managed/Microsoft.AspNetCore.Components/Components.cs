using Microsoft.AspNetCore.Components.Rendering;

namespace Microsoft.AspNetCore.Components;

/// <summary>
/// What makes a class a Razor component to the Razor generator, which matches this interface by its full name. A
/// <c>.razor</c> file's class implements it through <see cref="ComponentBase"/>.
/// </summary>
public interface IComponent
{
}

/// <summary>
/// The base the Razor generator gives a <c>.razor</c> file's class when it names none. Fenrir's script projects name
/// <c>Fenrir.UI.Razor.UIComponent</c> through the default imports, which derives from this.
/// </summary>
public abstract class ComponentBase : IComponent
{
    /// <summary>
    /// The Razor component's markup, which the generated class overrides: one call on <paramref name="__builder"/> per
    /// element, attribute and text run, each with its compile-time sequence number.
    /// </summary>
    protected virtual void BuildRenderTree(RenderTreeBuilder __builder)
    {
    }
}

/// <summary>
/// Marks a Razor component's property as a parameter: a parent's markup sets it by name as an attribute of the
/// Razor component's tag (<c>&lt;Badge Text="@title" /&gt;</c>). A property of type <see cref="RenderFragment"/> named
/// <c>ChildContent</c> takes the markup between the tag's start and end.
/// </summary>
[AttributeUsage(AttributeTargets.Property, AllowMultiple = false, Inherited = true)]
public sealed class ParameterAttribute : Attribute
{
}

/// <summary>A piece of markup a Razor component was handed, built where that Razor component places it.</summary>
public delegate void RenderFragment(RenderTreeBuilder builder);

/// <summary>A piece of markup made from a value, as a templated parameter takes it.</summary>
public delegate RenderFragment RenderFragment<TValue>(TValue value);

/// <summary>
/// What <c>@ref</c> on an element captures: the engine's element, opaque here. <c>Fenrir.UI.Razor</c>'s
/// <c>UIComponent.ElementOf</c> turns it into an <c>ElementId</c>. The default names no element, as the capture is
/// handed when the element is removed.
/// </summary>
public readonly struct ElementReference
{
    /// <summary>A reference to the engine's element <paramref name="value"/>.</summary>
    public ElementReference(ulong value)
    {
        Value = value;
        HasValue = true;
    }

    /// <summary>The engine's element, as the engine wrote it; meaningless when <see cref="HasValue"/> is false.</summary>
    public ulong Value { get; }

    /// <summary>Whether it names an element.</summary>
    public bool HasValue { get; }
}
