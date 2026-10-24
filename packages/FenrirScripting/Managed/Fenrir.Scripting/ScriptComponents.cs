using System.Runtime.CompilerServices;

namespace Fenrir.Scripting;

/// <summary>
/// How a script assembly tells the engine which components it defines. The Fenrir.Scripting source generator writes a
/// table that calls <see cref="Report{T}"/> once for every <see cref="IComponent"/> struct in the script; scripts do not
/// call it themselves.
/// </summary>
public static unsafe class ScriptComponents
{
    // A struct with a byte before a T: the T sits at the first offset T allows, which is its alignment.
    // Only ever laid out by the runtime to measure an offset; never assigned.
#pragma warning disable CS0649
    private struct AlignmentProbe<T> where T : unmanaged
    {
        public byte Marker;
        public T Value;
    }
#pragma warning restore CS0649

    /// <summary>The alignment, in bytes, that C# lays <typeparamref name="T"/> out with.</summary>
    internal static int AlignmentOf<T>() where T : unmanaged
    {
        AlignmentProbe<T> probe = default;
        return (int)Unsafe.ByteOffset(ref probe.Marker, ref Unsafe.As<T, byte>(ref probe.Value));
    }

    /// <summary>
    /// Reports <typeparamref name="T"/> to the engine under its full name, with its size and alignment, so the engine can
    /// keep an EnTT storage for it. Only meaningful while the script assembly is loading.
    /// </summary>
    public static void Report<T>() where T : unmanaged, IComponent
    {
        string name = typeof(T).FullName ?? typeof(T).Name;
        if (!Native.RegisterScriptComponent(name, Unsafe.SizeOf<T>(), AlignmentOf<T>()))
            throw new InvalidOperationException($"The engine did not accept the component {name}: components are reported only while a script assembly loads, and must have a plausible size.");
    }
}
