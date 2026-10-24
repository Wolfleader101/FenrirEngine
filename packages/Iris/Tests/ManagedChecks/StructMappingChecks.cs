using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Iris.Bridge;

// The struct mapping on its own: a description and a managed type go in, a record or the explanation of the
// mismatch comes out. No component is loaded; the nested types registered so far are a list built here.
static unsafe class StructMappingChecks
{
    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Pt
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Holder
    {
        public Pt P;
        public bool Flag;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Bad
    {
        public string S;
    }

    private struct Named
    {
        public fixed byte Name[16];
    }

    private class NotAStruct
    {
        public int X = 1;
    }

    private enum Small : short { A }

    // The managed types an opaque pointer field can be, and one it cannot (four bytes where the pointer is).
    [StructLayout(LayoutKind.Sequential)]
    private struct WithNint { public int Id; public nint Owner; }
    [StructLayout(LayoutKind.Sequential)]
    private struct WithNuint { public int Id; public nuint Owner; }
    [StructLayout(LayoutKind.Sequential)]
    private struct WithLong { public int Id; public long Owner; }
    [StructLayout(LayoutKind.Sequential)]
    private struct WithUlong { public int Id; public ulong Owner; }
    [StructLayout(LayoutKind.Explicit, Size = 16)]
    private struct WithUint { [FieldOffset(0)] public int Id; [FieldOffset(8)] public uint Owner; }

    private struct Empty { }

    private sealed class Registered : StructMapping.IRegisteredStructs
    {
        private readonly Dictionary<Type, StructRecord> records = new();
        public void Add(Type type, StructRecord record) => records.Add(type, record);
        public bool Contains(Type type) => records.ContainsKey(type);
        public StructRecord RecordOf(Type type) => records[type];
    }

    // The explanation of a mapping the module refuses, or null if it accepts it.
    private static string? Refused(Type type, string description, Registered? registered = null)
    {
        try
        {
            StructMapping.Describe(type, StructMapping.Parse(description), registered ?? new Registered());
            return null;
        }
        catch (BridgeArgumentException exception)
        {
            return exception.Message;
        }
    }

    private static void Expect(Type type, string description, string explanation, string what, Registered? registered = null)
    {
        string? actual = Refused(type, description, registered);
        Check(actual == explanation, $"{what}: expected \"{explanation}\" but got {(actual == null ? "no error" : "\"" + actual + "\"")}");
    }

    internal static void Run()
    {
        const string Int32 = "6", UInt32 = "7", UInt8 = "3", Struct = "14", InlineArray = "20";

        // A matching description is accepted, and the record says how big the struct is and where its bool bytes are.
        var registered = new Registered();
        StructRecord pt = StructMapping.Describe(typeof(Pt), StructMapping.Parse($"StructMappingChecks+Pt|8\nX|0|{Int32}|\nY|4|{Int32}|"), registered);
        Check(pt.Type == typeof(Pt) && pt.Size == 8 && pt.BoolOffsets.Length == 0, "A matching struct was not accepted as described");
        registered.Add(typeof(Pt), pt);
        StructRecord holder = StructMapping.Describe(typeof(Holder),
            StructMapping.Parse($"StructMappingChecks+Holder|12\nP|0|{Struct}|StructMappingChecks+Pt\nFlag|8|1|"), registered);
        Check(holder.BoolOffsets.Length == 1 && holder.BoolOffsets[0] == 8, "The bool byte of a struct with a nested struct was not found at offset 8");

        // The descriptions it cannot read.
        foreach (string malformed in new[] { "Pt", "Pt|x", "Pt|8|1|2", "" })
            Check(ThrowsMalformed(malformed), $"The description \"{malformed}\" was not refused as malformed");

        // Each mismatch is named.
        Expect(typeof(Pt), $"StructMappingChecks+Pt|12\nX|0|{Int32}|\nY|4|{Int32}|",
            "StructMappingChecks+Pt is 8 bytes but the native struct is 12.", "A wrong size");
        Expect(typeof(Pt), $"StructMappingChecks+Pt|8\nX|0|{Int32}|",
            "StructMappingChecks+Pt has 2 fields but the native struct declares 1.", "A missing field");
        Expect(typeof(Pt), $"StructMappingChecks+Pt|8\nZ|0|{Int32}|\nY|4|{Int32}|",
            "Field 0 of StructMappingChecks+Pt is 'X' but the native struct has 'Z' there (field order must match).", "A wrong field name");
        Expect(typeof(Pt), $"StructMappingChecks+Pt|8\nX|0|{Int32}|\nY|8|{Int32}|",
            "Field 'Y' of StructMappingChecks+Pt is at offset 4 but the native field is at 8.", "A wrong offset");
        Expect(typeof(Pt), $"StructMappingChecks+Pt|8\nX|0|{UInt32}|\nY|4|{Int32}|",
            "Field 'X' of StructMappingChecks+Pt is System.Int32, which does not match the native field's type.", "A wrong field kind");
        Expect(typeof(Holder), $"StructMappingChecks+Holder|12\nP|0|{Struct}|StructMappingChecks+Pt\nFlag|8|1|",
            "Field 'P' of StructMappingChecks+Holder is StructMappingChecks+Pt, which the native struct does not map to the same type.",
            "A nested type that is not registered first");
        Expect(typeof(Holder), $"StructMappingChecks+Holder|12\nP|0|{Struct}|Other.Name\nFlag|8|1|",
            "Field 'P' of StructMappingChecks+Holder is StructMappingChecks+Pt, which the native struct does not map to the same type.",
            "A registered nested type under another native name", registered);
        Expect(typeof(Bad), $"StructMappingChecks+Bad|8\nS|0|{Int32}|",
            "StructMappingChecks+Bad is not blittable: field 'S' has type System.String.", "A struct with a string field");
        Expect(typeof(NotAStruct), $"StructMappingChecks+NotAStruct|4\nX|0|{Int32}|",
            "StructMappingChecks+NotAStruct is not a struct.", "A class");

        // A fixed-size array field.
        Expect(typeof(Named), $"StructMappingChecks+Named|16\nName|0|{InlineArray}||{UInt8}|8",
            "Field 'Name' of StructMappingChecks+Named holds 16 elements but the native array holds 8.", "An inline array of the wrong length");
        Expect(typeof(Named), $"StructMappingChecks+Named|16\nName|0|{InlineArray}||{Int32}|16",
            "The elements of field 'Name' of StructMappingChecks+Named are Byte, which does not match the native array's element type.",
            "An inline array of the wrong element");
        Expect(typeof(Named), $"StructMappingChecks+Named|16\nName|0|{Int32}|",
            "Field 'Name' of StructMappingChecks+Named is StructMappingChecks+Named+<Name>e__FixedBuffer, which does not match the native field's type.",
            "A fixed buffer described as a number");
        Check(Refused(typeof(Named), $"StructMappingChecks+Named|16\nName|0|{InlineArray}||{UInt8}|16") == null,
            "A matching fixed buffer was refused");

        // Enums.
        StructRecord small = StructMapping.Describe(typeof(Small), StructMapping.Parse("StructMappingChecks+Small|2|4"), new Registered());
        Check(small.UnderlyingKind == 4 && small.Size == 2, "A matching enum was not accepted as described");
        Expect(typeof(Small), "StructMappingChecks+Small|2|6",
            "StructMappingChecks+Small is an enum of Int16, which does not match the native enum's underlying type.", "A wrong underlying type");
        Expect(typeof(Small), "StructMappingChecks+Small|4|4",
            "StructMappingChecks+Small is 2 bytes but the native enum is 4.", "A wrong enum size");
        Expect(typeof(Small), "StructMappingChecks+Small|2|x", "The enum description is malformed.", "A malformed enum");
        Expect(typeof(Pt), "StructMappingChecks+Pt|2|4", "StructMappingChecks+Pt is not an enum.", "A struct described as an enum");

        // An opaque pointer field: eight bytes held by nint, nuint, long or ulong.
        const string Pointer = "17";
        foreach (Type pointerHolder in new[] { typeof(WithNint), typeof(WithNuint), typeof(WithLong), typeof(WithUlong) })
        {
            string? refused = Refused(pointerHolder, $"{pointerHolder.FullName}|16\nId|0|{Int32}|\nOwner|8|{Pointer}|");
            Check(refused == null, $"A pointer field held as {pointerHolder.Name} was refused: {refused}");
        }
        Expect(typeof(WithUint), $"StructMappingChecks+WithUint|16\nId|0|{Int32}|\nOwner|8|{Pointer}|",
            "Field 'Owner' of StructMappingChecks+WithUint is System.UInt32, which does not match the native field's type (a pointer, held as nint, nuint, long or ulong).",
            "A pointer field held as uint");
        Expect(typeof(WithNint), $"StructMappingChecks+WithNint|16\nId|0|{Int32}|\nOwner|8|{UInt32}|",
            "Field 'Owner' of StructMappingChecks+WithNint is System.IntPtr, which does not match the native field's type.",
            "A nint field described as a number");

        // An empty struct: no field lines, one byte.
        StructRecord empty = StructMapping.Describe(typeof(Empty), StructMapping.Parse("StructMappingChecks+Empty|1"), new Registered());
        Check(empty.Size == 1 && empty.BoolOffsets.Length == 0, "An empty struct was not accepted as one byte");
        Expect(typeof(Pt), "StructMappingChecks+Pt|1",
            "StructMappingChecks+Pt has field 'X' but the native struct declares no fields.", "No fields for a struct with fields");
        Expect(typeof(Empty), "StructMappingChecks+Empty|4",
            "The native struct StructMappingChecks+Empty declares no fields but is 4 bytes; an empty struct is 1.", "No fields but four bytes");
        Expect(typeof(Empty), $"StructMappingChecks+Empty|1\nX|0|{UInt8}|",
            "StructMappingChecks+Empty has 0 fields but the native struct declares 1.", "A field for an empty struct");

        Console.WriteLine("Struct mapping: each mismatch of a description with its managed type is refused and named, with no component.");
    }

    private static bool ThrowsMalformed(string description)
    {
        try { StructMapping.Parse(description); }
        catch (BridgeArgumentException exception) { return exception.Message == "The struct description is malformed."; }
        return false;
    }
}
