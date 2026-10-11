using System;
using System.Runtime.InteropServices;
using System.Text;

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
    Double = 11,
    // An object handle in the slot. It matches any class or interface parameter or return type (but not
    // string or arrays), since the native signature cannot name the managed type; the real type is
    // checked when the call is made. Zero is null.
    Object = 12,
    // A string. An argument slot holds a pointer to a borrowed StringArgument; a return slot holds a block
    // from EncodeString (zero is null) that the caller copies and gives back to FreeString.
    String = 13,
    // A blittable struct. On the wire the kind is Struct in the low byte and a struct handle from
    // RegisterStruct above it (id << 8). An argument slot points to the struct's bytes, borrowed from the
    // caller; a return slot holds a block of the struct's bytes that the caller copies and gives back to
    // FreeString.
    Struct = 14
}

// What a string argument's slot points to. The native side owns it and keeps it alive for the call.
[StructLayout(LayoutKind.Sequential)]
internal unsafe struct StringArgument
{
    internal byte* Data;
    internal int Length;
    internal int IsNull; // Nonzero for null; Data and Length are then ignored.
}

internal static class ValueKinds
{
    internal static bool IsStruct(int kind) => kind > 0 && (kind & 0xFF) == (int)ValueKind.Struct && (kind >> 8) > 0;

    internal static ulong StructId(int kind) => (ulong)(kind >> 8);

    // The wire kind of a registered struct. Ids are small positive counters.
    internal static int StructKind(ulong id) => checked((int)(id << 8) | (int)ValueKind.Struct);

    internal static bool IsValid(int kind) =>
        IsStruct(kind) || (kind >= (int)ValueKind.Void && kind <= (int)ValueKind.String);

    // Throws BridgeArgumentException for a kind that is unknown, or that names no single type: Object
    // matches many, and Void is a return type only.
    internal static void ValidateParameter(int kind)
    {
        if (!IsValid(kind) || (ValueKind)kind == ValueKind.Void)
            throw new BridgeArgumentException($"Unsupported parameter kind {kind}.");
    }

    // Strict on both sides: bytes that are not UTF-8 and text with an unpaired surrogate are errors, never
    // replacement characters.
    private static readonly UTF8Encoding StrictUtf8 = new(encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true);

    // Reads a borrowed argument. Malformed UTF-8 is an argument error, found before managed code runs.
    private static unsafe string? DecodeString(ulong slot)
    {
        if (slot == 0) throw new BridgeArgumentException("A string argument has no value.");
        StringArgument* argument = (StringArgument*)slot;
        if (argument->IsNull != 0) return null;
        if (argument->Length < 0 || (argument->Length > 0 && argument->Data == null))
            throw new BridgeArgumentException("A string argument has an invalid length.");
        if (argument->Length == 0) return string.Empty;
        try
        {
            return StrictUtf8.GetString(argument->Data, argument->Length);
        }
        catch (DecoderFallbackException)
        {
            throw new BridgeArgumentException("A string argument is not valid UTF-8.");
        }
    }

    // A returned string: an eight-byte length followed by that many UTF-8 bytes, in memory the caller
    // must give back with FreeString. Null is zero. Nothing about the size is capped or negotiated.
    private static unsafe ulong EncodeString(string? text)
    {
        if (text == null) return 0;
        int count;
        try
        {
            count = StrictUtf8.GetByteCount(text);
        }
        catch (EncoderFallbackException)
        {
            throw new BridgeArgumentException("A returned string has no UTF-8 form (it has an unpaired surrogate).");
        }
        catch (Exception exception) when (exception is ArgumentException or OverflowException)
        {
            throw new BridgeArgumentException("A returned string is too long to cross the boundary (2 GiB of UTF-8).");
        }
        byte* block = (byte*)NativeMemory.Alloc((nuint)count + 8);
        *(long*)block = count;
        StrictUtf8.GetBytes(text, new Span<byte>(block + 8, count));
        return (ulong)block;
    }

    internal static unsafe void FreeString(ulong block) => NativeMemory.Free((void*)block);

    // The one managed type of a by-value kind. Object has none; callers match it with IsObjectType.
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
            ValueKind.String => typeof(string),
            _ => throw new BridgeArgumentException($"Unsupported value kind {kind}.")
        };
    }

    internal static object? ToObject(int kind, ulong slot)
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
            ValueKind.String => DecodeString(slot),
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
            ValueKind.String => EncodeString((string?)value),
            _ => throw new BridgeArgumentException($"Unsupported return kind {kind}.")
        };
    }
}
