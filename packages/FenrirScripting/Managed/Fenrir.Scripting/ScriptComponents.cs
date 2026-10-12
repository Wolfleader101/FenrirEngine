using System.Runtime.CompilerServices;
using System.Text;

namespace Fenrir.Scripting;

/// <summary>
/// How a script assembly tells the engine which components it defines. The Fenrir.Scripting source generator writes a
/// table that calls <see cref="Report{T}"/> once for every <see cref="IComponent"/> struct in the script; scripts do not
/// call it themselves.
/// </summary>
public static unsafe class ScriptComponents
{
    // A struct with a byte before a T: the T sits at the first offset T allows, which is its alignment.
    private struct AlignmentProbe<T> where T : unmanaged
    {
        public byte Marker;
        public T Value;
    }

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
        Span<byte> utf8 = stackalloc byte[Encoding.UTF8.GetMaxByteCount(name.Length)];
        int length = Encoding.UTF8.GetBytes(name, utf8);
        bool accepted;
        fixed (byte* bytes = utf8)
            accepted = Native.RegisterScriptComponent(bytes, length, Unsafe.SizeOf<T>(), AlignmentOf<T>());
        if (!accepted)
            throw new InvalidOperationException($"The engine did not accept the component {name}: components are reported only while a script assembly loads, and must have a plausible size.");
    }
}
