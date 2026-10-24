using System;
using System.Runtime.InteropServices;
using System.Text;
using Iris.Bridge;

// How a value crosses the boundary, checked on the codec alone: a kind and a slot go in and an object comes out, or the
// other way. No component is loaded and no bridge export is called; the registered structs and enums are built here
// from the managed layout, the way the registry builds them.
static class ValueCodecChecks
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void Throws<T>(Action action, string message) where T : Exception
    {
        try { action(); }
        catch (T) { return; }
        throw new Exception(message);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Flagged
    {
        public int Number;
        public bool Flag;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Outer
    {
        public byte Lead;
        public Flagged Inner;
        public long Tail;
    }

    private enum Small : short { Low = -2, High = 1 }

    private static StructRecord FlaggedRecord() =>
        new(typeof(Flagged), ManagedLayout.SizeOf(typeof(Flagged)),
            new[] { ManagedLayout.OffsetOf(typeof(Flagged).GetField(nameof(Flagged.Flag))!) });

    private static StructRecord OuterRecord() =>
        new(typeof(Outer), ManagedLayout.SizeOf(typeof(Outer)),
            new[] { ManagedLayout.OffsetOf(typeof(Outer).GetField(nameof(Outer.Inner))!) +
                    ManagedLayout.OffsetOf(typeof(Flagged).GetField(nameof(Flagged.Flag))!) });

    internal static unsafe void Run()
    {
        Primitives();
        Booleans();
        Enums();
        Structs();
        Strings();
        RulesOutsideTheCodec();
        Console.WriteLine("Value codec: primitives, stray bool bytes, enums, nested structs and strings convert as documented with no component.");
    }

    private static void Primitives()
    {
        // Integers are sign- or zero-extended by the sender; the codec narrows, whatever the upper bits hold.
        Check(Equals(ValueCodec.ToObject((int)ValueKind.Int8, ulong.MaxValue), (sbyte)-1), "A sign-extended int8 did not read as -1");
        Check(Equals(ValueCodec.ToObject((int)ValueKind.UInt8, 0x1FF), (byte)0xFF), "A uint8 did not drop the bits above it");
        Check(Equals(ValueCodec.ToObject((int)ValueKind.Int32, 0xFFFFFFFFUL), (int)-1), "An int32 slot of 0xFFFFFFFF did not read as -1");
        Check(ValueCodec.ToSlot((int)ValueKind.Int8, (sbyte)-1) == ulong.MaxValue, "A negative int8 was not sign-extended");
        Check(ValueCodec.ToSlot((int)ValueKind.UInt8, (byte)0xFF) == 0xFF, "A uint8 was not zero-extended");
        Check(ValueCodec.ToSlot((int)ValueKind.Int32, -2) == 0xFFFFFFFFFFFFFFFEUL, "A negative int32 was not sign-extended");
        Check(ValueCodec.ToSlot((int)ValueKind.Float, 1.5f) == 0x3FC00000UL, "A float is not its raw bits in the low 32");
        Check(ValueCodec.ToSlot((int)ValueKind.Double, 1.5) == 0x3FF8000000000000UL, "A double is not its raw bits");
        Check(Equals(ValueCodec.ToObject((int)ValueKind.Float, 0x3FC00000UL), (float)1.5f), "A float slot did not read back");
        Check(Equals(ValueCodec.ToObject((int)ValueKind.Char, 0x20AC), '€'), "A char did not read as its code unit");
        Check(ValueCodec.ToSlot((int)ValueKind.Char, '€') == 0x20AC, "A char was not stored as its code unit");
        Check(ValueCodec.ToSlot((int)ValueKind.Void, null) == 0, "A void return is not slot zero");
    }

    private static void Booleans()
    {
        // Any non-zero slot reads as true and is stored back as exactly one.
        Check(Equals(ValueCodec.ToObject((int)ValueKind.Bool, 0), (bool)false), "Zero did not read as false");
        Check(Equals(ValueCodec.ToObject((int)ValueKind.Bool, 1), (bool)true), "One did not read as true");
        Check(Equals(ValueCodec.ToObject((int)ValueKind.Bool, 0x80), (bool)true), "A stray non-zero value did not read as true");
        Check(ValueCodec.ToSlot((int)ValueKind.Bool, true) == 1, "True was not stored as one");
    }

    private static unsafe void Enums()
    {
        var record = new StructRecord(typeof(Small), sizeof(short), Array.Empty<int>(), (int)ValueKind.Int16);
        // An enum travels as its underlying integer, sign-extended.
        Check(ValueCodec.EnumToSlot(record, Small.Low) == 0xFFFFFFFFFFFFFFFEUL, "A negative enum was not sign-extended");
        Check(ValueCodec.EnumToSlot(record, Small.High) == 1, "An enum did not travel as its integer");
        Check(ValueCodec.EnumFromSlot(record, 0xFFFFFFFFFFFFFFFEUL) is Small.Low, "A sign-extended enum slot did not read back");
        // A value the enum does not name is kept as the integer it is; the upper bits beyond the underlying width are dropped.
        Check(Convert.ToInt16(ValueCodec.EnumFromSlot(record, 7)) == 7, "An undefined value was not kept as its integer");
        Check(ValueCodec.EnumFromSlot(record, 0x1FFFE) is Small.Low, "The bits beyond the underlying integer were not dropped");
    }

    private static unsafe void Structs()
    {
        // A stray non-zero bool byte reads as true, and the managed copy holds exactly one in that byte.
        StructRecord flagged = FlaggedRecord();
        int flagOffset = flagged.BoolOffsets[0];
        byte* source = stackalloc byte[flagged.Size];
        new Span<byte>(source, flagged.Size).Clear();
        *(int*)source = 42;
        source[flagOffset] = 0x7F;
        object box = ValueCodec.StructFromSlot(flagged, (ulong)source);
        Check(box is Flagged { Number: 42, Flag: true }, "A struct with a stray bool byte did not read as true");
        byte* copy = stackalloc byte[flagged.Size];
        ManagedLayout.Unbox(box, copy, flagged.Size);
        Check(copy[flagOffset] == 1, "A stray bool byte was not made one in the managed struct");

        // Out: a block holding the struct's bytes, which the caller gives back.
        ulong block = ValueCodec.StructToBlock(flagged, new Flagged { Number = 7, Flag = true });
        try
        {
            Check(*(int*)block == 7 && ((byte*)block)[flagOffset] == 1, "A struct was not copied into its block");
        }
        finally { ValueCodec.FreeString(block); }

        // A nested struct round-trips.
        StructRecord outer = OuterRecord();
        var value = new Outer { Lead = 9, Inner = new Flagged { Number = -3, Flag = true }, Tail = long.MinValue };
        ulong outerBlock = ValueCodec.StructToBlock(outer, value);
        try
        {
            Check(ValueCodec.StructFromSlot(outer, outerBlock) is Outer { Lead: 9, Inner: { Number: -3, Flag: true }, Tail: long.MinValue },
                "A nested struct did not round-trip");
        }
        finally { ValueCodec.FreeString(outerBlock); }

        // A stray bool byte inside a nested struct is made one at its offset in the outer struct.
        byte* nested = stackalloc byte[outer.Size];
        ManagedLayout.Unbox(value, nested, outer.Size);
        nested[outer.BoolOffsets[0]] = 0xFF;
        byte* normalized = stackalloc byte[outer.Size];
        ManagedLayout.Unbox(ValueCodec.StructFromSlot(outer, (ulong)nested), normalized, outer.Size);
        Check(normalized[outer.BoolOffsets[0]] == 1, "The nested bool byte was not made one");

        Throws<BridgeArgumentException>(() => ValueCodec.StructFromSlot(flagged, 0), "A struct argument slot of zero was accepted");
    }

    private static unsafe void Strings()
    {
        static ulong Argument(byte* data, int length, int isNull, StringArgument* storage)
        {
            *storage = new StringArgument { Data = data, Length = length, IsNull = isNull };
            return (ulong)storage;
        }
        StringArgument holder;
        Check(ValueCodec.ToObject((int)ValueKind.String, Argument(null, 0, 1, &holder)) is null, "A null string argument did not read as null");
        Check(ValueCodec.ToObject((int)ValueKind.String, Argument(null, 0, 0, &holder)) is "", "An empty string argument did not read as empty");
        byte[] withNul = { (byte)'a', 0, (byte)'b' };
        fixed (byte* data = withNul)
            Check(ValueCodec.ToObject((int)ValueKind.String, Argument(data, 3, 0, &holder)) is "a\0b", "A string with an embedded null did not read whole");
        byte[] invalid = { 0xFF, 0xFE };
        fixed (byte* data = invalid)
        {
            ulong slot = Argument(data, 2, 0, &holder);
            Throws<BridgeArgumentException>(() => ValueCodec.ToObject((int)ValueKind.String, slot), "A string argument that is not UTF-8 was accepted");
        }
        Throws<BridgeArgumentException>(() => ValueCodec.ToObject((int)ValueKind.String, 0), "A string slot of zero was accepted");
        fixed (byte* data = withNul)
        {
            ulong slot = Argument(data, -1, 0, &holder);
            Throws<BridgeArgumentException>(() => ValueCodec.ToObject((int)ValueKind.String, slot), "A negative string length was accepted");
        }

        // A returned string is a block: an eight-byte length, then the UTF-8 bytes. Null is zero.
        Check(ValueCodec.ToSlot((int)ValueKind.String, null) == 0, "A null string was not slot zero");
        ulong block = ValueCodec.ToSlot((int)ValueKind.String, "a\0€");
        try
        {
            Check(*(long*)block == 5, "The block's length was not the UTF-8 byte count");
            Check(new ReadOnlySpan<byte>((byte*)block + 8, 5).SequenceEqual(new byte[] { (byte)'a', 0, 0xE2, 0x82, 0xAC }), "The block's bytes were not the UTF-8 text");
        }
        finally { ValueCodec.FreeString(block); }
        Throws<BridgeArgumentException>(() => ValueCodec.ToSlot((int)ValueKind.String, "\uD800"), "A lone surrogate was encoded");
    }

    // Objects and arrays travel as handles the registry owns; the codec has no rule for them, so asking it is an error
    // and not a quiet zero.
    private static void RulesOutsideTheCodec()
    {
        Throws<BridgeArgumentException>(() => ValueCodec.ToObject((int)ValueKind.Object, 1), "The codec read an object handle");
        Throws<BridgeArgumentException>(() => ValueCodec.ToObject((int)ValueKind.Array, 1), "The codec read an array handle");
        Throws<BridgeArgumentException>(() => ValueCodec.ToSlot((int)ValueKind.Object, new object()), "The codec stored an object");
    }
}
