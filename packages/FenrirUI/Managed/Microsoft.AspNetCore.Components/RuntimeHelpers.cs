namespace Microsoft.AspNetCore.Components.CompilerServices;

/// <summary>The helpers the Razor generator's code calls around component parameters.</summary>
public static class RuntimeHelpers
{
    /// <summary>Wraps each typed component parameter, so a value of the wrong type is a compile error at its place.</summary>
    public static T TypeCheck<T>(T value) => value;
}
