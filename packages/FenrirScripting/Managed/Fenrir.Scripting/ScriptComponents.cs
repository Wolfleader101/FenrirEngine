using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Fenrir.Scripting;

/// <summary>What a component type says about itself besides its fields: the doc commands its attributes mirror.</summary>
[Flags]
[EditorBrowsable(EditorBrowsableState.Never)]
public enum ScriptTypeFlags
{
    None = 0,
    /// <summary><see cref="TransientAttribute"/> on the type: never written to a file.</summary>
    Transient = 1,
    /// <summary><see cref="EditorOnlyAttribute"/> on the type: authoring data for an editor only.</summary>
    EditorOnly = 2,
}

/// <summary>
/// One field of a component, or of a struct a component holds, as the generated component table reports it: its name,
/// its kind as the API description spells it (<c>i32</c>, <c>struct</c>, <c>entity</c>, <c>reference</c>,
/// <c>fixed-string</c>), what it names, and where it lies, measured at run time.
/// </summary>
[EditorBrowsable(EditorBrowsableState.Never)]
public readonly struct ScriptField
{
    public ScriptField(string name, string kind, string elementKind, string target, int offset, int size, int count, bool transient)
    {
        Name = name;
        Kind = kind;
        ElementKind = elementKind;
        Target = target;
        Offset = offset;
        Size = size;
        Count = count;
        Transient = transient;
    }

    /// <summary>The field's name, which files spell.</summary>
    public string Name { get; }

    /// <summary>Its kind, as the API description spells it.</summary>
    public string Kind { get; }

    /// <summary>For an <c>array</c>: the kind of each element; empty otherwise.</summary>
    public string ElementKind { get; }

    /// <summary>
    /// For a <c>struct</c>: the full name of the struct, or the engine's canonical name of the type it mirrors; for a
    /// <c>reference</c>: the canonical name of the target type. Empty otherwise.
    /// </summary>
    public string Target { get; }

    /// <summary>Bytes from the start of the type that holds it.</summary>
    public int Offset { get; }

    /// <summary>Bytes it takes.</summary>
    public int Size { get; }

    /// <summary>For an <c>array</c>: its element count; for a <c>fixed-string</c>: its length in bytes. Zero otherwise.</summary>
    public int Count { get; }

    /// <summary><see cref="TransientAttribute"/> on the field: kept in the layout, never written to a file.</summary>
    public bool Transient { get; }
}

/// <summary>
/// How a script assembly tells the engine which components it defines. The Fenrir.Scripting source generator writes a
/// table that calls <see cref="Report{T}(in T, ScriptTypeFlags, ReadOnlySpan{ScriptField})"/> once for every
/// <see cref="IComponent"/> struct in the script, with its fields, and <see cref="ReportStruct{T}"/> once for every
/// struct those components hold; scripts do not call them themselves.
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
    /// The offset, in bytes, of <paramref name="field"/> inside <paramref name="instance"/>, which must hold it: the
    /// layout the runtime chose, which the generator cannot compute.
    /// </summary>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static int OffsetOf<T, TField>(ref T instance, ref TField field) =>
        (int)Unsafe.ByteOffset(ref Unsafe.As<T, byte>(ref instance), ref Unsafe.As<TField, byte>(ref field));

    /// <summary>The size, in bytes, of the field <paramref name="field"/> refers to.</summary>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static int SizeOf<TField>(ref TField field) => Unsafe.SizeOf<TField>();

    /// <summary>The full name a struct is reported under: the runtime's, so it never depends on how code spells it.</summary>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static string NameOf<T>(ref T value) where T : unmanaged => typeof(T).FullName ?? typeof(T).Name;

    /// <summary>
    /// Reports <typeparamref name="T"/> to the engine under its full name, with its size and alignment, so the engine can
    /// keep an EnTT storage for it, and no fields. Only meaningful while the script assembly is loading.
    /// </summary>
    public static void Report<T>() where T : unmanaged, IComponent
    {
        T defaults = new();
        Report(in defaults, ScriptTypeFlags.None, ReadOnlySpan<ScriptField>.Empty);
    }

    /// <summary>
    /// Reports <typeparamref name="T"/> to the engine under its full name, with its size and alignment, so the engine can
    /// keep an EnTT storage for it, and with <paramref name="fields"/> and the bytes of <paramref name="defaults"/> (its
    /// <c>new T()</c>), so the engine can describe it and save it in scenes. Only meaningful while the script assembly
    /// is loading.
    /// </summary>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static void Report<T>(in T defaults, ScriptTypeFlags flags, ReadOnlySpan<ScriptField> fields) where T : unmanaged, IComponent
    {
        string name = typeof(T).FullName ?? typeof(T).Name;
        if (!Native.RegisterScriptComponent(name, Unsafe.SizeOf<T>(), AlignmentOf<T>(), BytesOf(in defaults), (int)flags))
            throw new InvalidOperationException($"The engine did not accept the component {name}: components are reported only while a script assembly loads, and must have a plausible size.");
        ReportFields(name, fields);
    }

    /// <summary>
    /// Reports the struct a component holds at <paramref name="value"/> to the engine under its full name, with its size,
    /// alignment, <c>new T()</c> and <paramref name="fields"/>, whose offsets are measured from
    /// <paramref name="value"/>. Only meaningful while the script assembly is loading.
    /// </summary>
    [EditorBrowsable(EditorBrowsableState.Never)]
    public static void ReportStruct<T>(ref T value, ReadOnlySpan<ScriptField> fields) where T : unmanaged
    {
        string name = NameOf(ref value);
        T defaults = new();
        if (!Native.RegisterScriptStruct(name, Unsafe.SizeOf<T>(), AlignmentOf<T>(), BytesOf(in defaults)))
            throw new InvalidOperationException($"The engine did not accept the struct {name}: structs are reported only while a script assembly loads.");
        ReportFields(name, fields);
    }

    private static ReadOnlySpan<byte> BytesOf<T>(in T value) where T : unmanaged =>
        MemoryMarshal.AsBytes(new ReadOnlySpan<T>(in value));

    private static void ReportFields(string owner, ReadOnlySpan<ScriptField> fields)
    {
        foreach (ScriptField field in fields)
        {
            if (!Native.RegisterScriptField(owner, field.Name, field.Kind, field.ElementKind, field.Target, field.Offset,
                                            field.Size, field.Count, field.Transient))
                throw new InvalidOperationException($"The engine did not accept the field {field.Name} of {owner}, of kind {field.Kind}.");
        }
    }
}
