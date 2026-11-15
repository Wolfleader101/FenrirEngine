namespace Microsoft.AspNetCore.Components;

/// <summary>
/// What a Razor component that takes events implements (Fenrir.UI.Razor's <c>UIComponent</c> does): an
/// <see cref="EventCallback"/> whose receiver it is runs through it, so it can rebuild after the delegate ran.
/// </summary>
public interface IHandleEvent
{
    /// <summary>Runs <paramref name="item"/> with <paramref name="arg"/>, then whatever the receiver does after an event.</summary>
    Task HandleEventAsync(EventCallbackWorkItem item, object? arg);
}

/// <summary>An <see cref="EventCallback"/>'s delegate, as <see cref="IHandleEvent"/> is handed it.</summary>
public readonly struct EventCallbackWorkItem
{
    private readonly MulticastDelegate? callback;

    /// <summary>A work item that runs <paramref name="delegate"/>.</summary>
    public EventCallbackWorkItem(MulticastDelegate? @delegate)
    {
        callback = @delegate;
    }

    /// <summary>
    /// Runs the delegate: an <c>Action</c>, or one taking <paramref name="arg"/>; a <c>Func</c> returning a
    /// <c>Task</c> runs to its first <c>await</c>, since a stage has no synchronisation context to finish it on.
    /// </summary>
    public Task InvokeAsync(object? arg) => EventCallbackInvoker.Invoke(callback, arg);
}

/// <summary>
/// A handler as Razor passes it: an attribute such as <c>@onclick</c>, or a component's event parameter. It holds the
/// Razor component that wrote it, the receiver, and the delegate.
/// </summary>
public readonly struct EventCallback
{
    /// <summary>What generated code makes callbacks with.</summary>
    public static readonly EventCallbackFactory Factory = new();

    /// <summary>A callback that does nothing.</summary>
    public static readonly EventCallback Empty = default;

    /// <summary>A callback running <paramref name="delegate"/>, written by <paramref name="receiver"/>.</summary>
    public EventCallback(IHandleEvent? receiver, MulticastDelegate? @delegate)
    {
        Receiver = receiver;
        Delegate = @delegate;
    }

    /// <summary>The Razor component that wrote it, which rebuilds after it runs.</summary>
    public IHandleEvent? Receiver { get; }

    /// <summary>The handler.</summary>
    public MulticastDelegate? Delegate { get; }

    /// <summary>Whether it has a handler.</summary>
    public bool HasDelegate => Delegate != null;

    /// <summary>Runs the handler with <paramref name="arg"/>, through the receiver when there is one.</summary>
    public Task InvokeAsync(object? arg) => EventCallbackInvoker.Run(Receiver, Delegate, arg);

    /// <summary>Runs the handler with no argument.</summary>
    public Task InvokeAsync() => InvokeAsync(null);
}

/// <summary>An <see cref="EventCallback"/> whose argument is a <typeparamref name="TValue"/>.</summary>
public readonly struct EventCallback<TValue>
{
    /// <summary>A callback that does nothing.</summary>
    public static readonly EventCallback<TValue> Empty = default;

    /// <summary>A callback running <paramref name="delegate"/>, written by <paramref name="receiver"/>.</summary>
    public EventCallback(IHandleEvent? receiver, MulticastDelegate? @delegate)
    {
        Receiver = receiver;
        Delegate = @delegate;
    }

    /// <summary>The Razor component that wrote it, which rebuilds after it runs.</summary>
    public IHandleEvent? Receiver { get; }

    /// <summary>The handler.</summary>
    public MulticastDelegate? Delegate { get; }

    /// <summary>Whether it has a handler.</summary>
    public bool HasDelegate => Delegate != null;

    /// <summary>Runs the handler with <paramref name="arg"/>, through the receiver when there is one.</summary>
    public Task InvokeAsync(TValue arg) => EventCallbackInvoker.Run(Receiver, Delegate, arg);

    /// <summary>Runs the handler with the default argument.</summary>
    public Task InvokeAsync() => InvokeAsync(default!);
}

internal static class EventCallbackInvoker
{
    public static Task Run(IHandleEvent? receiver, MulticastDelegate? callback, object? arg)
    {
        if (receiver != null)
            return receiver.HandleEventAsync(new EventCallbackWorkItem(callback), arg);
        return Invoke(callback, arg);
    }

    public static Task Invoke(MulticastDelegate? callback, object? arg)
    {
        switch (callback)
        {
            case null:
                return Task.CompletedTask;
            case Action action:
                action();
                return Task.CompletedTask;
            case Func<Task> run:
                return run();
            default:
                return callback.DynamicInvoke(arg) as Task ?? Task.CompletedTask;
        }
    }
}

/// <summary>
/// Makes <see cref="EventCallback"/>s from the handlers generated code passes: an <c>Action</c> or an
/// <c>Action&lt;T&gt;</c> (ADR-0069 has no async handlers, as a stage has no synchronisation context).
/// </summary>
public sealed class EventCallbackFactory
{
    public EventCallback Create(object receiver, Action callback) => new(receiver as IHandleEvent, callback);

    public EventCallback Create(object receiver, Action<object> callback) => new(receiver as IHandleEvent, callback);

    public EventCallback Create(object receiver, EventCallback callback) => callback;

    public EventCallback<TValue> Create<TValue>(object receiver, Action callback) => new(receiver as IHandleEvent, callback);

    public EventCallback<TValue> Create<TValue>(object receiver, Action<TValue> callback) =>
        new(receiver as IHandleEvent, callback);

    public EventCallback<TValue> Create<TValue>(object receiver, EventCallback callback) =>
        new(callback.Receiver, callback.Delegate);

    public EventCallback<TValue> Create<TValue>(object receiver, EventCallback<TValue> callback) => callback;
}

/// <summary>What <c>@bind</c>'s change callback is handed: the control's new value, read by the dispatcher.</summary>
public sealed class ChangeEventArgs : EventArgs
{
    /// <summary>
    /// The control's value: a <c>float</c> for a slider or a progress bar, a <c>bool</c> for a checkbox, a
    /// <c>string</c> for a text entry.
    /// </summary>
    public object? Value { get; set; }
}

/// <summary>
/// <c>@bind</c>'s change callbacks: <c>Factory.CreateBinder(this, __value => Field = __value, Field)</c>, a callback
/// that converts the control's value to the field's type and hands it to the setter.
/// </summary>
public static class EventCallbackFactoryBinderExtensions
{
    public static EventCallback<ChangeEventArgs> CreateBinder<T>(this EventCallbackFactory factory, object receiver,
                                                                 Action<T> setter, T existingValue,
                                                                 System.Globalization.CultureInfo? culture = null)
    {
        _ = factory;
        _ = existingValue;
        _ = culture;
        return new(receiver as IHandleEvent, (Action<ChangeEventArgs>)(e => setter(BindConverter.ConvertTo<T>(e.Value))));
    }

    /// <summary>The setter <c>@bind:after</c> makes: it sets the field and runs the after-code.</summary>
    public static EventCallback<ChangeEventArgs> CreateBinder<T>(this EventCallbackFactory factory, object receiver,
                                                                 Func<T, Task> setter, T existingValue,
                                                                 System.Globalization.CultureInfo? culture = null)
    {
        _ = factory;
        _ = existingValue;
        _ = culture;
        return new(receiver as IHandleEvent,
                   (Action<ChangeEventArgs>)(e => setter(BindConverter.ConvertTo<T>(e.Value))));
    }
}
