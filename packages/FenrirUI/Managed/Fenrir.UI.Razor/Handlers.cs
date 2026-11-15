using Fenrir.Scripting;
using Microsoft.AspNetCore.Components;

namespace Fenrir.UI.Razor;

/// <summary>One handler an element was given, of one kind: the delegate and the Razor component that wrote it.</summary>
internal sealed class Handler
{
    public MulticastDelegate Delegate = null!;

    /// <summary>The Razor component whose markup wrote it, which rebuilds after it runs.</summary>
    public UIComponent Owner = null!;

    /// <summary>The directive as written, such as <c>@onclick</c>.</summary>
    public string Directive = "";

    /// <summary>The build that last gave it.</summary>
    public int Seen;
}

/// <summary>The handler directives (ADR-0069) and the <see cref="UIEventKind"/> each takes.</summary>
internal static class HandlerKinds
{
    /// <summary>How many kinds there are: an entry's handler array is indexed by kind.</summary>
    public const int Count = (int)UIEventKind.Cancel + 1;

    private static readonly Dictionary<string, UIEventKind> byName = new(StringComparer.Ordinal)
    {
        ["onclick"] = UIEventKind.Click,
        ["ondblclick"] = UIEventKind.DoubleClick,
        ["onpointerdown"] = UIEventKind.PointerPress,
        ["onpointerup"] = UIEventKind.PointerRelease,
        ["onpointerenter"] = UIEventKind.PointerEnter,
        ["onpointerleave"] = UIEventKind.PointerLeave,
        ["onpointermove"] = UIEventKind.PointerMove,
        ["onwheel"] = UIEventKind.Scroll,
        ["onkeydown"] = UIEventKind.KeyPress,
        ["onkeyup"] = UIEventKind.KeyRelease,
        ["ontextinput"] = UIEventKind.TextInput,
        ["onfocus"] = UIEventKind.Focus,
        ["onblur"] = UIEventKind.Blur,
        ["onchange"] = UIEventKind.Change,
        ["onsubmit"] = UIEventKind.Submit,
        ["oncancel"] = UIEventKind.Cancel,
    };

    /// <summary>The kind the attribute <paramref name="name"/> handles; false for a name that is no handler directive.</summary>
    public static bool TryKind(string name, out int kind)
    {
        bool found = byName.TryGetValue(name, out UIEventKind value);
        kind = (int)value;
        return found;
    }

    /// <summary>Whether the kind is the pointer's, which an element takes only with <c>pointer-events: all</c>.</summary>
    public static bool IsPointer(int kind) => kind <= (int)UIEventKind.Scroll;
}

/// <summary>
/// One UI root's elements that have handlers, of every Razor component mounted on it, and the pointer checks queued for
/// next frame (ADR-0069). The builder adds an entry when it first gives it a handler and removes it when the entry has
/// none left or is forgotten; <see cref="RazorRuntime"/> dispatches the frame's UI events through it.
/// </summary>
internal sealed class HandlerTable
{
    // A chain of parents longer than this is a cycle no live tree has: the walk stops rather than spin.
    private const int MaxDepth = 1024;

    private readonly Dictionary<ulong, Entry> entries = [];
    private readonly List<(Entry Entry, string Message)> checks = [];

    public int Count => entries.Count;

    public void Add(Entry entry)
    {
        if (entry.InHandlerTable)
            return;
        entry.InHandlerTable = true;
        entries[Key(entry.Id)] = entry;
    }

    public void Remove(Entry entry)
    {
        if (!entry.InHandlerTable)
            return;
        entry.InHandlerTable = false;
        entries.Remove(Key(entry.Id));
    }

    /// <summary>Queues the entry's pointer check, which runs next frame, after its first layout.</summary>
    public void QueuePointerCheck(Entry entry, string message) => checks.Add((entry, message));

    /// <summary>Runs the pointer checks queued by earlier frames' builds, for the elements still handled.</summary>
    public void RunPointerChecks(IElementSeam seam)
    {
        foreach ((Entry entry, string message) in checks)
        {
            if (entry.InHandlerTable)
                seam.WarnIfTakesNoPointer(entry.Id, message);
        }
        checks.Clear();
    }

    /// <summary>
    /// Runs the handler of the event's kind nearest its target, walking up through the parents; returns whether one was
    /// found. Its Razor component then rebuilds; a throw turns the component's mount off, as a throwing build does.
    /// </summary>
    public bool Dispatch(in UIEvent e, IElementSeam seam, in SystemContext context)
    {
        int kind = (int)e.Kind;
        if ((uint)kind >= HandlerKinds.Count || entries.Count == 0)
            return false;
        ElementId at = e.Target;
        for (int depth = 0; depth < MaxDepth && at.Index != uint.MaxValue; ++depth)
        {
            if (entries.TryGetValue(Key(at), out Entry? entry) && entry.Handlers?[kind] is { } handler)
            {
                Run(entry, handler, e, seam, context);
                return true;
            }
            at = seam.GetParent(at);
        }
        return false;
    }

    private static void Run(Entry entry, Handler handler, in UIEvent e, IElementSeam seam, in SystemContext context)
    {
        UIComponent owner = handler.Owner;
        MountedRazorComponent? mount = owner.Mounted;
        if (mount == null || !mount.IsMounted || mount.Disabled)
            return;
        owner.Context = context;
        try
        {
            object? value = null;
            bool moved = true;
            if (e.Kind == UIEventKind.Change)
                value = ReadControl(entry, seam, e, out moved);
            switch (handler.Delegate)
            {
                case Action action:
                    action();
                    break;
                case Action<UIEvent> action:
                    action(e);
                    break;
                case Action<ChangeEventArgs> binder:
                    // @bind moves the field only when the control moved: a drag's closing Change repeats its value.
                    if (!moved)
                        return;
                    binder(new ChangeEventArgs { Value = value ?? e.Value });
                    break;
                default:
                    new EventCallbackWorkItem(handler.Delegate).InvokeAsync(e);
                    break;
            }
            owner.StateHasChanged();
        }
        catch (Exception exception)
        {
            mount.Disabled = true;
            RazorDiagnostics.Error($"The Razor component {owner.GetType().FullName}'s {handler.Directive} handler threw, " +
                                   "so its mount is off until the next script reload, its elements left as they were: " +
                                   $"{exception}");
        }
    }

    /// <summary>
    /// A changed control's value, read from the control, which the entry then records as what the control holds: the
    /// next build pushes the value only if the field it binds came out different. <paramref name="moved"/> is whether
    /// it differs from what the entry last recorded; any other element gives the event's value, as moved.
    /// </summary>
    private static object? ReadControl(Entry entry, IElementSeam seam, in UIEvent e, out bool moved)
    {
        moved = true;
        object? value;
        string? text;
        switch (entry.Tag)
        {
            case "slider" or "progress":
                if (!seam.GetValue(entry.Id, out float number))
                    return null;
                value = number;
                text = BindConverter.FormatValue(number);
                break;
            case "checkbox":
                if (!seam.GetValue(entry.Id, out float state))
                    return null;
                bool isChecked = state != 0;
                value = isChecked;
                text = BindConverter.FormatValue(isChecked);
                break;
            case "textentry":
                text = seam.GetEntryText(entry.Id);
                value = text;
                break;
            default:
                return e.Value;
        }
        moved = entry.Value != text;
        entry.Value = text;
        return value;
    }

    private static ulong Key(ElementId element) => ((ulong)element.Index << 32) | element.Version;
}
