using System;

namespace Iris.Bridge;

// The value kinds that cross the boundary by value: each travels in one 64-bit slot. Integers are
// sign- or zero-extended by the sender, bool is zero or nonzero, and float and double carry their raw
// IEEE bits (float in the low 32 bits). Wire values are mirrored by the native Detail::ValueKind.
internal enum ValueKind : int
{
    Void = 0, // Return type only.
    Bool = 1,
    Int8 = 2,
    UInt8 = 3,
    Int16 = 4,
    UInt16 = 5,
    Int32 = 6,
    UInt32 = 7,
    Int64 = 8,
    UInt64 = 9,
    Float = 10,
    Double = 11
}

internal static class ValueKinds
{
    internal static bool IsValid(int kind) => kind >= (int)ValueKind.Void && kind <= (int)ValueKind.Double;

    // Throws BridgeArgumentException for an unknown kind, or Void where a value is required.
    internal static Type ToType(int kind, bool allowVoid)
    {
        return (ValueKind)kind switch
        {
            ValueKind.Void when allowVoid => typeof(void),
            ValueKind.Bool => typeof(bool),
            ValueKind.Int8 => typeof(sbyte),
            ValueKind.UInt8 => typeof(byte),
            ValueKind.Int16 => typeof(short),
            ValueKind.UInt16 => typeof(ushort),
            ValueKind.Int32 => typeof(int),
            ValueKind.UInt32 => typeof(uint),
            ValueKind.Int64 => typeof(long),
            ValueKind.UInt64 => typeof(ulong),
            ValueKind.Float => typeof(float),
            ValueKind.Double => typeof(double),
            _ => throw new BridgeArgumentException($"Unsupported value kind {kind}.")
        };
    }

    internal static object ToObject(int kind, ulong slot)
    {
        return (ValueKind)kind switch
        {
            ValueKind.Bool => slot != 0,
            ValueKind.Int8 => unchecked((sbyte)(long)slot),
            ValueKind.UInt8 => unchecked((byte)slot),
            ValueKind.Int16 => unchecked((short)(long)slot),
            ValueKind.UInt16 => unchecked((ushort)slot),
            ValueKind.Int32 => unchecked((int)(long)slot),
            ValueKind.UInt32 => unchecked((uint)slot),
            ValueKind.Int64 => unchecked((long)slot),
            ValueKind.UInt64 => slot,
            ValueKind.Float => BitConverter.UInt32BitsToSingle(unchecked((uint)slot)),
            ValueKind.Double => BitConverter.UInt64BitsToDouble(slot),
            _ => throw new BridgeArgumentException($"Unsupported argument kind {kind}.")
        };
    }

    // A void return is slot zero.
    internal static ulong ToSlot(int kind, object? value)
    {
        return (ValueKind)kind switch
        {
            ValueKind.Void => 0,
            ValueKind.Bool => (bool)value! ? 1UL : 0UL,
            ValueKind.Int8 => unchecked((ulong)(long)(sbyte)value!),
            ValueKind.UInt8 => (byte)value!,
            ValueKind.Int16 => unchecked((ulong)(long)(short)value!),
            ValueKind.UInt16 => (ushort)value!,
            ValueKind.Int32 => unchecked((ulong)(long)(int)value!),
            ValueKind.UInt32 => (uint)value!,
            ValueKind.Int64 => unchecked((ulong)(long)value!),
            ValueKind.UInt64 => (ulong)value!,
            ValueKind.Float => BitConverter.SingleToUInt32Bits((float)value!),
            ValueKind.Double => unchecked((ulong)BitConverter.DoubleToInt64Bits((double)value!)),
            _ => throw new BridgeArgumentException($"Unsupported return kind {kind}.")
        };
    }
}
