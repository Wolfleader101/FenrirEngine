namespace Microsoft.AspNetCore.Components.CompilerServices;

/// <summary>The helpers the Razor generator's code calls around component parameters and bindings.</summary>
public static class RuntimeHelpers
{
    /// <summary>Wraps each typed component parameter, so a value of the wrong type is a compile error at its place.</summary>
    public static T TypeCheck<T>(T value) => value;

    /// <summary><c>@bind-Value</c> on a Razor component: the <c>ValueChanged</c> callback that sets the field.</summary>
    public static EventCallback<T> CreateInferredEventCallback<T>(object receiver, Action<T> callback, T value)
    {
        _ = value;
        return EventCallback.Factory.Create(receiver, callback);
    }

    /// <summary><c>@bind-Value</c> with <c>@bind-Value:after</c>: the setter runs the field's set, then the after-code.</summary>
    public static EventCallback<T> CreateInferredEventCallback<T>(object receiver, Func<T, Task> callback, T value)
    {
        _ = value;
        return EventCallback.Factory.Create<T>(receiver, (Action<T>)(argument => callback(argument)));
    }

    /// <summary><c>@bind:after</c>'s setter, typed from the field.</summary>
    public static Action<T> CreateInferredBindSetter<T>(Action<T> callback, T value)
    {
        _ = value;
        return callback;
    }

    /// <summary><c>@bind:after</c>'s setter, typed from the field.</summary>
    public static Func<T, Task> CreateInferredBindSetter<T>(Func<T, Task> callback, T value)
    {
        _ = value;
        return callback;
    }

    /// <summary>Runs <c>@bind:after</c>'s code, an <c>Action</c>.</summary>
    public static Task InvokeSynchronousDelegate(Action callback)
    {
        callback();
        return Task.CompletedTask;
    }

    /// <summary>Runs <c>@bind:after</c>'s code when the generator cannot tell it returns nothing: an <c>Action</c>.</summary>
    public static Task InvokeAsynchronousDelegate(Action callback)
    {
        callback();
        return Task.CompletedTask;
    }

    /// <summary>
    /// Runs <c>@bind:after</c>'s code returning a <c>Task</c>, to its first <c>await</c>: a stage has no
    /// synchronisation context to finish the rest on (ADR-0069).
    /// </summary>
    public static Task InvokeAsynchronousDelegate(Func<Task> callback) => callback();
}
