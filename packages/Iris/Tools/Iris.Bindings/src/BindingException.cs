namespace Iris.Bindings;

/// <summary>Something the generator cannot bind; the message is the diagnostic it prints.</summary>
internal sealed class BindingException(string message) : Exception(message);
