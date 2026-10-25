using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Iris.Bridge;

// Internal calls: component code reaching native functions through bound function pointer fields.
static unsafe class InternalCallChecks
{
    private const string Natives = "Iris.Examples.Component.Natives.";
    private const int Void = (int)ValueKind.Void, Bool = (int)ValueKind.Bool, Char = (int)ValueKind.Char, Int32 = (int)ValueKind.Int32, UInt32 = (int)ValueKind.UInt32, Int64 = (int)ValueKind.Int64, UInt8 = (int)ValueKind.UInt8, Double = (int)ValueKind.Double, String = (int)ValueKind.String, Pointer = (int)ValueKind.Pointer;
    private static int recorded;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Add(int left, int right) => left + right;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static double Half(double value) => value / 2;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static long Combine(long left, byte right) => left * 1000 + right;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Measure(byte* text, int length) => length;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static long StringBetweenNumbers(int count, byte* label, int length, int factor) => (long)count * factor + length;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void Normalize(float* vector) { vector[0] = 1; vector[1] = 0; vector[2] = 0; }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Throws() => -1;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void Record(int value) => recorded = value;
    // bool is one byte in a signature that has marshalling disabled, so the check's own function uses a byte.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static byte Negate(byte value) => (byte)(value == 0 ? 1 : 0);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static ushort NextChar(ushort value) => (ushort)(value + 1);
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static byte LeakStopped() => 1;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Triple(int value) => value * 3;
    // A native enum and a native wrapper cross as integers, so the check's own functions take those.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int NextGear(int gear) => gear;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static byte Invert(byte shade) => shade;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static uint Advance(uint ticket, int by) => ticket + (uint)by;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static void Ignore(byte value) { }

    private static void Register(string name, int returnKind, int[] kinds, void* function) =>
        InternalCalls.Register(Natives + name, returnKind, kinds, (IntPtr)function);

    internal static void RegisterAll()
    {
        Register("Add", Int32, new[] { Int32, Int32 }, (delegate* unmanaged[Cdecl]<int, int, int>)&Add);
        Register("Half", Double, new[] { Double }, (delegate* unmanaged[Cdecl]<double, double>)&Half);
        Register("Combine", Int64, new[] { Int64, UInt8 }, (delegate* unmanaged[Cdecl]<long, byte, long>)&Combine);
        Register("Measure", Int32, new[] { Pointer, Int32 }, (delegate* unmanaged[Cdecl]<byte*, int, int>)&Measure);
        Register("RecordText", Int32, new[] { String }, (delegate* unmanaged[Cdecl]<byte*, int, int>)&Measure);
        Register("StringBetweenNumbers", Int64, new[] { Int32, String, Int32 }, (delegate* unmanaged[Cdecl]<int, byte*, int, int, long>)&StringBetweenNumbers);
        Register("Normalize", Void, new[] { Pointer }, (delegate* unmanaged[Cdecl]<float*, void>)&Normalize);
        Register("Throws", Int32, Array.Empty<int>(), (delegate* unmanaged[Cdecl]<int>)&Throws);
        Register("Record", Void, new[] { Int32 }, (delegate* unmanaged[Cdecl]<int, void>)&Record);
        Register("Negate", Bool, new[] { Bool }, (delegate* unmanaged[Cdecl]<byte, byte>)&Negate);
        Register("NextChar", Char, new[] { Char }, (delegate* unmanaged[Cdecl]<ushort, ushort>)&NextChar);
        Register("LeakStopped", Bool, Array.Empty<int>(), (delegate* unmanaged[Cdecl]<byte>)&LeakStopped);
        Register("NextGear", Int32, new[] { Int32 }, (delegate* unmanaged[Cdecl]<int, int>)&NextGear);
        Register("Invert", UInt8, new[] { UInt8 }, (delegate* unmanaged[Cdecl]<byte, byte>)&Invert);
        Register("Advance", UInt32, new[] { UInt32, Int32 }, (delegate* unmanaged[Cdecl]<uint, int, uint>)&Advance);
        InternalCalls.Register("Iris.Examples.Contracts.SharedNatives.Triple", Int32, new[] { Int32 }, (IntPtr)(delegate* unmanaged[Cdecl]<int, int>)&Triple);
    }

    private static ulong Method(ulong token, ulong type, string name, int returnKind, params int[] parameters)
    {
        Check(ComponentRegistry.TryFindMethod(token, type, name, returnKind, parameters, true, out ulong id) == ComponentRegistry.FindResult.Found, "Method lookup failed: " + name);
        return id;
    }

    private static string LoadFailure(string path)
    {
        try
        {
            ulong token = ComponentRegistry.Load(path);
            ComponentRegistry.TryRelease(token, force: true);
            ComponentRegistry.ForgetUnload(token);
            return "";
        }
        catch (BridgeArgumentException exception)
        {
            return exception.Message;
        }
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        int before = ComponentRegistry.TrackingCount;

        // With nothing registered the load fails, names every field, and registers nothing.
        InternalCalls.Clear();
        try
        {
            string unbound = LoadFailure(path);
            Check(unbound.Contains("15 internal call(s)") && unbound.Contains(Natives + "Add") && unbound.Contains(Natives + "Record") && unbound.Contains("no internal call is registered"),
                  "Unbound fields were not diagnosed: " + unbound);
            Check(ComponentRegistry.TrackingCount == before, "A failed load left a registration");

            // A signature mismatch is diagnosed with both signatures.
            RegisterAll();
            InternalCalls.Clear();
            Register("Add", Int64, new[] { Int32, Int32 }, (delegate* unmanaged[Cdecl]<int, int, int>)&Add);
            string mismatch = LoadFailure(path);
            Check(mismatch.Contains(Natives + "Add") && mismatch.Contains("does not match") && mismatch.Contains("Int64") && mismatch.Contains("Int32"), "A signature mismatch was not diagnosed: " + mismatch);

            // Registrations are idempotent, and a conflicting one is rejected.
            InternalCalls.Clear();
            RegisterAll();
            RegisterAll();
            bool conflict = false;
            try { Register("Add", Int32, new[] { Int32, Int32 }, (delegate* unmanaged[Cdecl]<int>)&Throws); }
            catch (BridgeArgumentException) { conflict = true; }
            Check(conflict, "A conflicting registration was accepted");
            bool invalid = false;
            try { Register("Bad", 12, Array.Empty<int>(), (delegate* unmanaged[Cdecl]<int>)&Throws); }
            catch (BridgeArgumentException) { invalid = true; }
            Check(invalid, "An object return kind was accepted");
        }
        finally
        {
            InternalCalls.Clear();
            RegisterAll();
        }

        // bool and char in a signature need an assembly with marshalling disabled, since otherwise the runtime
        // gives them a size of its own choosing. This assembly (the checks) has not disabled it.
        try
        {
            InternalCalls.Clear();
            InternalCalls.Register(typeof(UnmarshalledNatives).FullName + ".Flag", Void, new[] { Bool }, (IntPtr)(delegate* unmanaged[Cdecl]<byte, void>)&Ignore);
            string marshalled = "";
            try { InternalCalls.Bind(typeof(UnmarshalledNatives).Assembly); }
            catch (BridgeArgumentException exception) { marshalled = exception.Message; }
            Check(marshalled.Contains("UnmarshalledNatives.Flag") && marshalled.Contains("DisableRuntimeMarshalling"),
                  "A bool internal call in an assembly with marshalling enabled was bound: " + marshalled);
        }
        finally
        {
            InternalCalls.Clear();
            RegisterAll();
        }

        // A string parameter binds to a pointer followed by an Int32 length, and to nothing else; a string is never a
        // result.
        try
        {
            InternalCalls.Clear();
            string strings = typeof(StringNatives).FullName + ".";
            foreach (string field in new[] { "Paired", "Bare", "Swapped" })
                InternalCalls.Register(strings + field, Int32, new[] { String }, (IntPtr)(delegate* unmanaged[Cdecl]<byte*, int, int>)&Measure);
            string bound = "";
            try { InternalCalls.Bind(typeof(StringNatives).Assembly); }
            catch (BridgeArgumentException exception) { bound = exception.Message; }
            Check(bound.Contains(strings + "Bare") && bound.Contains(strings + "Swapped") && !bound.Contains(strings + "Paired"),
                  "A string parameter bound to something other than a pointer and a length, or not to them: " + bound);
            Check(bound.Contains("does not match") && bound.Contains("a string (a pointer, then an Int32 length)") && bound.Contains("Int32(Int32, a pointer)"),
                  "A mismatched string field's diagnostic did not show both signatures: " + bound);
            bool returned = false;
            try { InternalCalls.Register(strings + "Returned", String, Array.Empty<int>(), (IntPtr)(delegate* unmanaged[Cdecl]<int>)&Throws); }
            catch (BridgeArgumentException) { returned = true; }
            Check(returned, "A string result was accepted");
        }
        finally
        {
            InternalCalls.Clear();
            RegisterAll();
        }

        // With everything registered the component loads and its fields call native code.
        ulong token = ComponentRegistry.Load(path);
        try
        {
            Check(ComponentRegistry.TryFindType(token, "Iris.Examples.Component.Natives", out ulong type) == ComponentRegistry.FindResult.Found, "Type lookup failed");
            Check(ComponentRegistry.TryInvokeMethod(token, Method(token, type, "CallAdd", Int32, Int32, Int32), new ulong[] { 20, 22 }, out ulong sum) && sum == 42, "A bound function returned the wrong value");
            Check(ComponentRegistry.TryInvokeMethod(token, Method(token, type, "CallHalf", Double, Double), new[] { (ulong)BitConverter.DoubleToInt64Bits(5) }, out ulong half) &&
                  BitConverter.Int64BitsToDouble((long)half) == 2.5, "A double function was wrong");
            Check(ComponentRegistry.TryInvokeMethod(token, Method(token, type, "CallCombine", Int64, Int64, UInt8), new ulong[] { 7, 255 }, out ulong combined) && combined == 7255, "Mixed argument widths were wrong");
            ComponentRegistry.TryInvokeMethod(token, Method(token, type, "CallRecord", Void, Int32), new ulong[] { 9 }, out _);
            Check(recorded == 9, "A void function was not called");
            Check(ComponentRegistry.TryInvokeMethod(token, Method(token, type, "CallThrows", Int32), Array.Empty<ulong>(), out ulong thrown) && (int)thrown == -1, "A no-argument function was wrong");
        }
        finally
        {
            ComponentRegistry.TryRelease(token, force: true);
            ComponentRegistry.ForgetUnload(token);
        }
        Console.WriteLine("Internal calls: unbound and mismatched fields fail the load with diagnostics, registrations are idempotent, and bound fields reach native code.");
    }
}

// A public static function pointer field with a bool, in an assembly that has not disabled runtime marshalling.
public static unsafe class UnmarshalledNatives
{
    public static delegate* unmanaged[Cdecl]<bool, void> Flag;
}

// String parameters: one field per way of declaring a registered string parameter.
public static unsafe class StringNatives
{
    public static delegate* unmanaged[Cdecl]<byte*, int, int> Paired;
    public static delegate* unmanaged[Cdecl]<byte*, int> Bare;
    public static delegate* unmanaged[Cdecl]<int, byte*, int> Swapped;
}
