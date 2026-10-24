using System;
using System.Reflection;
using System.Reflection.Emit;
using System.Runtime.CompilerServices;

namespace Iris.Bridge;

// The in-memory layout of managed structs, which is what native code sees. It is read from the runtime
// rather than from the marshaller, because the marshaller gives bool four bytes, char an encoding, and does
// not work at all in an assembly with DisableRuntimeMarshalling.
internal static unsafe class ManagedLayout
{
    private sealed class RawData
    {
        internal byte First;
    }

    private static readonly MethodInfo SizeOfDefinition = typeof(Unsafe).GetMethod(nameof(Unsafe.SizeOf))!;

    internal static int SizeOf(Type type) => (int)SizeOfDefinition.MakeGenericMethod(type).Invoke(null, null)!;

    // The byte offset of an instance field of a struct, as the runtime lays it out. Nothing is cached: a
    // cache would hold the component's types.
    internal static int OffsetOf(FieldInfo field)
    {
        Type owner = field.DeclaringType!;
        var method = new DynamicMethod("FieldOffset", typeof(int), Type.EmptyTypes, typeof(ManagedLayout).Module, skipVisibility: true);
        ILGenerator il = method.GetILGenerator();
        il.DeclareLocal(owner);
        il.Emit(OpCodes.Ldloca_S, (byte)0);
        il.Emit(OpCodes.Ldflda, field);
        il.Emit(OpCodes.Ldloca_S, (byte)0);
        il.Emit(OpCodes.Sub);
        il.Emit(OpCodes.Conv_I4);
        il.Emit(OpCodes.Ret);
        return ((Func<int>)method.CreateDelegate(typeof(Func<int>)))();
    }

    // The first byte of a boxed struct's data.
    internal static ref byte Data(object box) => ref Unsafe.As<RawData>(box).First;

    // A new boxed struct holding a copy of `size` bytes.
    internal static object Box(Type type, byte* source, int size)
    {
        object box = RuntimeHelpers.GetUninitializedObject(type);
        Unsafe.CopyBlockUnaligned(ref Unsafe.As<RawData>(box).First, ref *source, (uint)size);
        return box;
    }

    // Copies a boxed struct's bytes out.
    internal static void Unbox(object box, byte* destination, int size) =>
        Unsafe.CopyBlockUnaligned(ref *destination, ref Unsafe.As<RawData>(box).First, (uint)size);
}
