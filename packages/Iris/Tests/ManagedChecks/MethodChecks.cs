using System;
using System.Text;
using Iris.Bridge;

// Method handles and typed static invocation through the real FindMethod / InvokeMethod exports.
static unsafe class MethodChecks
{
    private const string Calculations = "Iris.Examples.Component.Calculations";
    private const int Void = 0, Bool = 1, Int8 = 2, UInt8 = 3, Int16 = 4, UInt16 = 5, Int32 = 6, UInt32 = 7,
                      Int64 = 8, UInt64 = 9, Float = 10, Double = 11;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static string Text(byte[] storage, int written) => new UTF8Encoding(false, true).GetString(storage, 0, written);

    private static ulong TypeId(ulong token, string name)
    {
        Check(ComponentRegistry.TryFindType(token, name, out ulong id) == ComponentRegistry.FindResult.Found, "Type lookup failed: " + name);
        return id;
    }

    private static int Find(ulong token, ulong type, byte[] name, int returnKind, int[] parameters, out ulong id, out string diagnostic)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int, int*, int, ulong*, byte*, int, int*, int*, int> find = &Bootstrap.FindMethod;
        byte[] storage = new byte[4096];
        fixed (int* kinds = parameters)
        fixed (byte* namePointer = name, diagnosticPointer = storage)
        {
            ulong value = 99;
            int written = 99, truncated = 99;
            int status = find(token, type, namePointer, name.Length, returnKind, kinds, parameters.Length, &value,
                              diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 || value == 0, "A failed lookup published a handle");
            id = value;
            diagnostic = Text(storage, written);
            return status;
        }
    }

    private static int Find(ulong token, ulong type, string name, int returnKind, int[] parameters, out ulong id, out string diagnostic) =>
        Find(token, type, Encoding.UTF8.GetBytes(name), returnKind, parameters, out id, out diagnostic);

    private static int Invoke(ulong token, ulong method, ulong[] arguments, out ulong result, out string diagnostic, int count = -1)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, ulong*, int, ulong*, byte*, int, int*, int*, int> invoke = &Bootstrap.InvokeMethod;
        byte[] storage = new byte[4096];
        fixed (ulong* slots = arguments)
        fixed (byte* diagnosticPointer = storage)
        {
            ulong value = 99;
            int written = 99, truncated = 99;
            int status = invoke(token, method, slots, count < 0 ? arguments.Length : count, &value,
                                diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 || value == 0, "A failed invocation published a result");
            result = value;
            diagnostic = Text(storage, written);
            return status;
        }
    }

    private static ulong Method(ulong token, ulong type, string name, int returnKind, params int[] parameters)
    {
        Check(Find(token, type, name, returnKind, parameters, out ulong id, out string diagnostic) == 0 && id != 0,
              "Method lookup failed for " + name + ": " + diagnostic);
        return id;
    }

    private static ulong Call(ulong token, ulong method, params ulong[] arguments)
    {
        Check(Invoke(token, method, arguments, out ulong result, out string diagnostic) == 0, "Invocation failed: " + diagnostic);
        return result;
    }

    private static ulong Bits(double value) => (ulong)BitConverter.DoubleToInt64Bits(value);
    private static ulong Bits(float value) => BitConverter.SingleToUInt32Bits(value);

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        ulong token = ComponentRegistry.Load(path);
        ulong other = ComponentRegistry.Load(path);
        ulong staleMethod;
        try
        {
            ulong type = TypeId(token, Calculations);

            // Each supported value kind round-trips, and overloads resolve by exact parameter types.
            ulong add = Method(token, type, "Add", Int32, Int32, Int32);
            Check(Call(token, add, 20, 22) == 42, "Add(int, int) returned the wrong value");
            Check(Method(token, type, "Add", Int32, Int32, Int32) == add, "Repeating a lookup created another handle");
            ulong addLong = Method(token, type, "Add", Int64, Int64, Int64);
            Check(addLong != add && Call(token, addLong, 5_000_000_000UL, 7) == 5_000_000_007UL, "Add(long, long) overload not selected");
            Check(Call(token, add, unchecked((ulong)(long)-5), 2) == unchecked((ulong)(long)-3), "A negative int did not round-trip");
            Check(BitConverter.Int64BitsToDouble((long)Call(token, Method(token, type, "Scale", Double, Double, Float), Bits(2.5), Bits(4f))) == 10.0,
                  "double/float did not round-trip");
            Check(Call(token, Method(token, type, "Not", Bool, Bool), 1) == 0 && Call(token, Method(token, type, "Not", Bool, Bool), 0) == 1, "bool did not round-trip");
            Check(Call(token, Method(token, type, "WrapByte", UInt8, UInt8), 255) == 0, "byte did not round-trip");
            Check(Call(token, Method(token, type, "NegateSByte", Int8, Int8), unchecked((ulong)(long)-7)) == unchecked((ulong)(long)7), "sbyte did not round-trip");
            Check(Call(token, Method(token, type, "NegateShort", Int16, Int16), 300) == unchecked((ulong)(long)-300), "short did not round-trip");
            Check(Call(token, Method(token, type, "IncrementUShort", UInt16, UInt16), 65535) == 0, "ushort did not round-trip");
            Check(Call(token, Method(token, type, "IncrementUInt", UInt32, UInt32), uint.MaxValue) == 0, "uint did not round-trip");
            Check(Call(token, Method(token, type, "Sum64", UInt64, UInt64, UInt64), ulong.MaxValue, 2) == 1, "ulong did not round-trip");
            Check(BitConverter.UInt32BitsToSingle((uint)Call(token, Method(token, type, "Halve", Float, Float), Bits(5f))) == 2.5f, "float did not round-trip");

            // A void method really runs, and returns no value.
            ulong touch = Method(token, type, "Touch", Void);
            ulong touches = Method(token, type, "Touches", Int32);
            ulong before = Call(token, touches);
            Check(Call(token, touch) == 0 && Call(token, touches) == before + 1, "A void method did not run exactly once");

            // NotFound: a missing name, a wrong return type, wrong parameter types, a generic method, an
            // unmanaged-only method, a non-public method, and an instance method.
            foreach ((string name, int ret, int[] parameters) in new (string, int, int[])[]
            {
                ("Missing", Int32, new[] { Int32, Int32 }),
                ("WrongReturn", Int32, new[] { Int32, Int32 }),
                ("Add", Int32, new[] { Int32, Int64 }),
                ("Add", Int32, new[] { Int32 }),
                ("Add", Int64, new[] { Int32, Int32 }),
                ("Generic", Int32, new[] { Int32, Int32 }),
                ("UnmanagedOnly", Int32, new[] { Int32, Int32 }),
                ("Hidden", Int32, new[] { Int32, Int32 }),
                ("add", Int32, new[] { Int32, Int32 })
            })
            {
                Check(Find(token, type, name, ret, parameters, out ulong missing, out string notFound) == 6 && missing == 0 &&
                      notFound.Contains(name, StringComparison.Ordinal), "Lookup was not a NotFound naming " + name);
            }
            ulong instanceType = TypeId(token, "Iris.Examples.Component.InstanceCalculation");
            Check(Find(token, instanceType, "Add", Int32, new[] { Int32, Int32 }, out _, out _) == 6, "An instance method was found as static");

            // Argument validation.
            Check(Find(token, type, Array.Empty<byte>(), Int32, new[] { Int32 }, out _, out _) == 1, "An empty name was accepted");
            Check(Find(token, type, Encoding.UTF8.GetBytes("A\0B"), Int32, new[] { Int32 }, out _, out _) == 1, "A name with a null was accepted");
            Check(Find(token, type, new byte[] { 0xFF }, Int32, new[] { Int32 }, out _, out string decoder) == 1 && decoder.Contains("DecoderFallbackException"),
                  "Malformed UTF-8 was accepted or undiagnosed");
            Check(Find(token, type, "Add", 99, new[] { Int32 }, out _, out _) == 1, "An unknown return kind was accepted");
            Check(Find(token, type, "Add", Int32, new[] { Void }, out _, out _) == 1 && Find(token, type, "Add", Int32, new[] { 99 }, out _, out _) == 1,
                  "A void or unknown parameter kind was accepted");
            Check(Invoke(token, add, new ulong[] { 1 }, out _, out _) == 1 && Invoke(token, add, new ulong[] { 1, 2, 3 }, out _, out _) == 1,
                  "A wrong argument count was accepted");

            // Exceptions are contained as ManagedException (never Overflow or InvalidArgument), and the next call works.
            ulong throwMethod = Method(token, type, "Throw", Int32, Int32, Int32);
            Check(Invoke(token, throwMethod, new ulong[] { 0, 0 }, out _, out string thrown) == 3 &&
                  thrown.Contains("InvalidOperationException") && thrown.Contains("π \U0001F43A") && !thrown.Contains("TargetInvocationException"),
                  "A component exception was not reported as itself");
            Check(Invoke(token, Method(token, type, "ThrowArgument", Int32, Int32, Int32), new ulong[] { 0, 0 }, out _, out string argument) == 3 &&
                  argument.Contains("ArgumentException"), "A component ArgumentException was confused with an invalid argument");
            Check(Invoke(token, add, new ulong[] { int.MaxValue, 1 }, out _, out string overflow) == 3 && overflow.Contains("OverflowException"),
                  "A component OverflowException was not a ManagedException");
            Check(Invoke(token, Method(token, type, "LongError", Int32, Int32, Int32), new ulong[] { 0, 0 }, out _, out _) == 3, "A long error changed status");
            Check(Call(token, add, 20, 22) == 42, "A call did not recover after exceptions");

            // Handle validation: unknown, zero, another component's, and a type handle that is not this component's.
            Check(Invoke(token, 0, Array.Empty<ulong>(), out _, out _) == 5 && Invoke(token, add + 1000, new ulong[] { 1, 2 }, out _, out _) == 5,
                  "An unknown method handle was accepted");
            Check(Invoke(other, add, new ulong[] { 20, 22 }, out _, out _) == 5, "A method handle worked on another component");
            Check(Find(other, type, "Add", Int32, new[] { Int32, Int32 }, out _, out _) == 5, "Another component's type handle was accepted");
            Check(Find(0, type, "Add", Int32, new[] { Int32, Int32 }, out _, out _) == 5, "Component token zero was accepted");
            staleMethod = add;
        }
        finally
        {
            Check(ComponentRegistry.Release(other) && ComponentRegistry.ForgetUnload(other), "Release failed");
            Check(ComponentRegistry.Release(token) && ComponentRegistry.ForgetUnload(token), "Release failed");
        }

        // After release, the handles stop working and are never reused by a new load.
        Check(Invoke(token, staleMethod, new ulong[] { 20, 22 }, out _, out _) == 5, "A released component still ran methods");
        ulong reloaded = ComponentRegistry.Load(path);
        try
        {
            ulong type = TypeId(reloaded, Calculations);
            Check(Method(reloaded, type, "Add", Int32, Int32, Int32) > staleMethod, "A method handle was reused after reload");
            Check(Invoke(reloaded, staleMethod, new ulong[] { 20, 22 }, out _, out _) == 5, "A stale handle selected a method in a new component");
        }
        finally { Check(ComponentRegistry.Release(reloaded) && ComponentRegistry.ForgetUnload(reloaded), "Release failed"); }
        Console.WriteLine("Method handles: every value kind, overloads, void, NotFound, validation, contained exceptions and release invalidation passed.");
    }
}
