using System.Runtime.CompilerServices;
using Fenrir.Scripting;
using Microsoft.AspNetCore.Components;
using Microsoft.AspNetCore.Components.Rendering;

namespace Fenrir.UI.Razor;

/// <summary>
/// The base of a Razor component (ADR-0068): one managed object per mount, whose fields hold UI-local state and whose
/// markup the builder makes into FenrirUI elements under <see cref="Element"/>. A <c>.razor</c> file in a script project
/// that imports <c>Fenrir.UI.Razor.props</c> derives from it through the default imports' <c>@inherits</c>. Mount one
/// with <see cref="UIRootRazor.Mount{T}"/>.
/// <para>
/// It builds in <c>Fenrir.UI.Build</c> when it is mounted, after <see cref="StateHasChanged"/>, and when
/// <see cref="BuildHash"/> returns a value it did not return the frame before; at most once a frame. A rebuild changes
/// only what changed. If its build or one of its hooks throws, the error is logged with its type and it is not built
/// again until the next script reload; its elements stay as they were.
/// </para>
/// </summary>
public abstract class UIComponent : ComponentBase
{
    /// <summary>The UI root it is mounted on.</summary>
    public UIRoot Root { get; internal set; }

    /// <summary>
    /// Its root element: the host panel its mount made, which carries its readable class and holds the elements its
    /// markup builds. A child Razor component has no host: it is the element the parent placed it in.
    /// </summary>
    public ElementId Element { get; internal set; }

    /// <summary>The frame's system context while it builds, for reading game state from the ECS.</summary>
    public SystemContext Context { get; internal set; }

    /// <summary>The mount it builds in: its own, or for a child Razor component its parent's.</summary>
    internal MountedRazorComponent? Mounted { get; set; }

    /// <summary>What the builder keeps for it as a child Razor component; null for a mounted one.</summary>
    internal ChildComponent? ChildRecord { get; set; }

    /// <summary>
    /// Asks for a rebuild in this frame's <c>Fenrir.UI.Build</c>, or the next one's when it is called from that stage,
    /// its own build among it. On a child Razor component, the Razor components above it build too, to reach it (a
    /// rebuild that changes nothing makes no native call); its siblings with unchanged parameters do not.
    /// </summary>
    public void StateHasChanged()
    {
        if (Mounted == null)
            return;
        Mounted.Dirty = true;
        ChildRecord?.MarkDirty();
    }

    /// <summary>
    /// The element an <c>@ref</c> on an element captured (<c>&lt;panel @ref="box"&gt;</c> with an
    /// <c>ElementReference box</c> field); none before the element is made and after it is removed.
    /// </summary>
    protected static ElementId ElementOf(ElementReference reference)
    {
        if (!reference.HasValue)
            return new ElementId();
        ulong value = reference.Value;
        return Unsafe.As<ulong, ElementId>(ref value);
    }

    internal static ElementReference ReferenceTo(ElementId element) => new(Unsafe.As<ElementId, ulong>(ref element));

    /// <summary>
    /// A hash of what it shows, checked every frame before the builds: when it differs from the frame before's, it
    /// rebuilds. A HUD returns <c>HashCode.Combine(health, ammo)</c> from what it reads through <see cref="Context"/>.
    /// The default never changes.
    /// </summary>
    protected virtual int BuildHash() => 0;

    /// <summary>Runs once, in the build stage, before the first build.</summary>
    protected virtual void OnMount()
    {
    }

    /// <summary>
    /// On a child Razor component, runs after its parent set its parameters and before the build they cause: before the
    /// first build (after <see cref="OnMount"/>), and before each rebuild where a parameter changed by <c>Equals</c> (a
    /// <c>RenderFragment</c> always counts as changed).
    /// </summary>
    protected virtual void OnParametersSet()
    {
    }

    /// <summary>Runs after each build; <paramref name="first"/> is true after the first.</summary>
    protected virtual void OnBuilt(bool first)
    {
    }

    /// <summary>
    /// Runs once when it is forgotten after <see cref="OnMount"/> ran: on <c>Unmount</c>, in the frame after its root is
    /// destroyed, when it would build after someone else removed its host, and when a script reload releases it. A
    /// child Razor component is also forgotten when its parent's markup no longer places it, and before its parent.
    /// </summary>
    protected virtual void OnUnmount()
    {
    }

    // The runtime's ways into the protected members, which the builder and RazorRuntime call.
    internal void BuildInto(RenderTreeBuilder builder) => BuildRenderTree(builder);

    internal int CallBuildHash() => BuildHash();

    internal void CallOnMount() => OnMount();

    internal void CallOnParametersSet() => OnParametersSet();

    internal void CallOnBuilt(bool first) => OnBuilt(first);

    internal void CallOnUnmount() => OnUnmount();
}
