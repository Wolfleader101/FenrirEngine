using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Iris.Bridge;

// How a value crosses the boundary: between the 64-bit wire slot and a managed object, for every kind. A kind
// and a slot go in and an object comes out, or the other way; nothing here knows about components or handles.
// A registered struct or enum is given as its StructRecord, which the caller looks up (see StructRecord).
// The kinds themselves, and which are valid where, are in ValueKinds.
internal static unsafe class ValueCodec
{
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
            ValueKind.Char => unchecked((char)(ushort)slot),
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
            ValueKind.Char => (char)value!,
            ValueKind.String => EncodeString((string?)value),
            _ => throw new BridgeArgumentException($"Unsupported return kind {kind}.")
        };
    }

    // ---- Registered structs and enums

    // Copies a struct out of the caller's bytes: a plain memory copy, then every bool byte made 0 or 1, so a
    // stray nonzero byte reads as true and managed code never sees an invalid bool. An opaque pointer field is
    // part of the copy, its eight bytes unchanged in both directions.
    internal static object StructFromSlot(StructRecord record, ulong slot)
    {
        if (slot == 0) throw new BridgeArgumentException("A struct argument has no value.");
        object box = ManagedLayout.Box(record.Type, (byte*)slot, record.Size);
        if (record.BoolOffsets.Length > 0)
        {
            fixed (byte* data = &ManagedLayout.Data(box)) NormalizeBools(data, record.BoolOffsets);
        }
        return box;
    }

    // Makes the bool byte at each offset 0 or 1.
    internal static void NormalizeBools(byte* origin, int[] offsets)
    {
        foreach (int offset in offsets)
            if (origin[offset] > 1) origin[offset] = 1;
    }

    // Copies a struct into a new block the caller gives back with FreeString.
    internal static ulong StructToBlock(StructRecord record, object value)
    {
        byte* block = (byte*)NativeMemory.Alloc((nuint)record.Size);
        ManagedLayout.Unbox(value, block, record.Size);
        return (ulong)block;
    }

    // An enum travels as its underlying integer.
    internal static object EnumFromSlot(StructRecord record, ulong slot) =>
        Enum.ToObject(record.Type, ToObject(record.UnderlyingKind, slot)!);

    internal static ulong EnumToSlot(StructRecord record, object value) => ToSlot(record.UnderlyingKind, value);
}
