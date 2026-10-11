using System;
using System.Text;
using Iris.Bridge;

// Arrays through the registry behind the CreateArray / GetArrayElement / SetArrayElement / CopyArray exports.
static unsafe class ArrayChecks
{
    private const string Component = "Iris.Examples.Component.";
    private const int Int32 = 6, Double = 11, Float = 10, Bool = 1, Str = 13, ArrayOf = 15, StructArray = 16;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static int KindOf(int element) => (element << 8) | ArrayOf;

    private static ulong TypeId(ulong token, string name)
    {
        Check(ComponentRegistry.TryFindType(token, name, out ulong id) == ComponentRegistry.FindResult.Found, "Type lookup failed: " + name);
        return id;
    }

    private static bool Throws<T>(Action action) where T : Exception
    {
        try { action(); return false; }
        catch (T) { return true; }
    }

    private static ulong Create(ulong token, int kind, long length)
    {
        Check(ComponentRegistry.TryCreateArray(token, kind, length, out ulong id) && id != 0, "Array creation failed");
        return id;
    }

    private static long Length(ulong token, ulong array)
    {
        Check(ComponentRegistry.TryGetArrayLength(token, array, out long length), "Length failed");
        return length;
    }

    private static ulong Element(ulong token, ulong array, int kind, long index)
    {
        Check(ComponentRegistry.TryGetArrayElement(token, array, kind, index, out ulong slot), "Get failed");
        return slot;
    }

    private static string? Take(ulong block)
    {
        if (block == 0) return null;
        string text = new UTF8Encoding(false, true).GetString((byte*)block + 8, checked((int)*(long*)block));
        ValueKinds.FreeString(block);
        return text;
    }

    private static ulong Method(ulong token, ulong type, string name, int returnKind, params int[] parameters)
    {
        Check(ComponentRegistry.TryFindMethod(token, type, name, returnKind, parameters, true, out ulong id) == ComponentRegistry.FindResult.Found, "Method lookup failed: " + name);
        return id;
    }

    private static ulong Call(ulong token, ulong method, params ulong[] arguments)
    {
        Check(ComponentRegistry.TryInvokeMethod(token, method, arguments, out ulong result), "Invocation failed");
        return result;
    }

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        ulong token = ComponentRegistry.Load(path), other = ComponentRegistry.Load(path);
        try
        {
            int ints = KindOf(Int32);

            // Create, length, element access and range errors.
            ulong array = Create(token, ints, 4);
            Check(Length(token, array) == 4, "A new array had the wrong length");
            Check(Element(token, array, ints, 2) == 0, "A new array was not zeroed");
            Check(ComponentRegistry.TrySetArrayElement(token, array, ints, 2, unchecked((ulong)(long)-7)), "Set failed");
            Check(Element(token, array, ints, 2) == unchecked((ulong)(long)-7), "An element did not round-trip");
            Check(Create(token, ints, 0) != 0, "An empty array could not be created");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryGetArrayElement(token, array, ints, 4, out _)), "Index == length was accepted");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryGetArrayElement(token, array, ints, -1, out _)), "A negative index was accepted");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TrySetArrayElement(token, array, ints, 99, 1)), "An out-of-range set was accepted");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryCreateArray(token, ints, -1, out _)), "A negative length was accepted");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryCreateArray(token, 12, 1, out _)), "A non-array kind was accepted");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryGetArrayElement(token, array, KindOf(Double), 0, out _)), "A wrong element type was accepted");

            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryGetArrayElement(token, array, 0, 0, out _)), "Element access with no array kind read a quiet zero");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryFindMember(token, TypeId(token, Component + "Settings"), "Count", 0, ints, out _)), "An array kind was accepted for a member");
            ulong counterObject = 0;
            Check(ComponentRegistry.TryCreateObject(token, TypeId(token, Component + "Counter"), Array.Empty<int>(), ReadOnlySpan<ulong>.Empty, out counterObject) == ComponentRegistry.CreateResult.Created, "setup");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryGetArrayLength(token, counterObject, out _)), "A non-array object was read as an array");

            // Bulk copies.
            int[] source = { 10, 20, 30 }, back = new int[4];
            fixed (int* s = source) Check(ComponentRegistry.TryCopyArray(token, array, ints, 1, 3, s, intoArray: true), "Copy in failed");
            fixed (int* b = back) Check(ComponentRegistry.TryCopyArray(token, array, ints, 0, 4, b, intoArray: false), "Copy out failed");
            Check(back[0] == 0 && back[1] == 10 && back[2] == 20 && back[3] == 30, "A bulk copy was wrong");
            fixed (int* s = source)
            {
                ulong sourceAddress = (ulong)s;
                Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryCopyArray(token, array, ints, 2, 3, (void*)sourceAddress, true)), "A copy past the end was accepted");
                Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryCopyArray(token, array, ints, -1, 1, (void*)sourceAddress, true)), "A negative copy start was accepted");
                Check(ComponentRegistry.TryCopyArray(token, array, ints, 4, 0, null, true), "An empty copy at the end failed");
            }

            // Bool, byte and double arrays use their real element sizes.
            ulong flags = Create(token, KindOf(Bool), 3);
            byte[] flagBytes = { 1, 0, 1 };
            fixed (byte* f = flagBytes) Check(ComponentRegistry.TryCopyArray(token, flags, KindOf(Bool), 0, 3, f, true), "Bool copy failed");
            Check(Element(token, flags, KindOf(Bool), 0) == 1 && Element(token, flags, KindOf(Bool), 1) == 0, "A bool array was copied wrongly");
            ulong doubles = Create(token, KindOf(Double), 2);
            Check(ComponentRegistry.TrySetArrayElement(token, doubles, KindOf(Double), 1, (ulong)BitConverter.DoubleToInt64Bits(2.5)), "Double set failed");
            Check(BitConverter.Int64BitsToDouble((long)Element(token, doubles, KindOf(Double), 1)) == 2.5, "A double element did not round-trip");

            // String arrays: null elements and UTF-8.
            int strings = KindOf(Str);
            ulong words = Create(token, strings, 3);
            Check(Element(token, words, strings, 0) == 0, "A new string element was not null");
            byte[] text = Encoding.UTF8.GetBytes("π\U0001F43A");
            fixed (byte* t = text)
            {
                var argument = new StringArgument { Data = t, Length = text.Length };
                Check(ComponentRegistry.TrySetArrayElement(token, words, strings, 1, (ulong)&argument), "String set failed");
            }
            Check(Take(Element(token, words, strings, 1)) == "π\U0001F43A", "A string element did not round-trip");
            var nullArgument = new StringArgument { IsNull = 1 };
            Check(ComponentRegistry.TrySetArrayElement(token, words, strings, 1, (ulong)&nullArgument) && Element(token, words, strings, 1) == 0, "Storing a null string failed");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryCopyArray(token, words, strings, 0, 1, (void*)1, true)), "A string array was bulk-copied");
            byte[] malformed = { 0xFF };
            fixed (byte* m = malformed)
            {
                var bad = new StringArgument { Data = m, Length = 1 };
                ulong badSlot = (ulong)&bad;
                Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TrySetArrayElement(token, words, strings, 0, badSlot)), "Malformed UTF-8 was stored in an array");
            }

            // Struct arrays: the struct must be registered, and copies use its real size.
            string vec3 = $"{Component}Vec3|12\nX|0|{Float}|\nY|4|{Float}|\nZ|8|{Float}|";
            Check(ComponentRegistry.TryRegisterStruct(token, vec3, out ulong vecId) == ComponentRegistry.FindResult.Found, "Struct registration failed");
            int points = (int)(vecId << 8) | StructArray;
            ulong line = Create(token, points, 3);
            float[] coordinates = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
            fixed (float* c = coordinates) Check(ComponentRegistry.TryCopyArray(token, line, points, 0, 3, c, true), "Struct copy in failed");
            ulong element = Element(token, line, points, 1);
            Check(((float*)element)[0] == 4 && ((float*)element)[2] == 6, "A struct element was wrong");
            ValueKinds.FreeString(element);
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryGetArrayElement(token, array, points, 0, out _)), "An int array was read as structs");
            Check(Throws<BridgeArgumentException>(() => ComponentRegistry.TryCreateArray(other, points, 1, out _)), "A foreign struct handle created an array");

            // Methods: arrays as arguments (zero is null) and returns (new handles, zero is null).
            ulong arrays = TypeId(token, Component + "Arrays");
            ulong range = Method(token, arrays, "Range", ints, Int32);
            ulong made = Call(token, range, 5);
            Check(made != 0 && Length(token, made) == 5 && Element(token, made, ints, 4) == 4, "A returned array was wrong");
            ulong sum = Method(token, arrays, "Sum", Int32, ints);
            Check(Call(token, sum, made) == 10, "An array argument was not passed");
            ulong isNull = Method(token, arrays, "IsNull", Bool, ints);
            Check(Call(token, isNull, 0) == 1 && Call(token, isNull, made) == 0, "Null and non-null arrays were confused");
            Check(Call(token, Method(token, arrays, "Null", ints)) == 0, "A null array return was not zero");
            ulong empty = Call(token, Method(token, arrays, "Empty", ints));
            Check(empty != 0 && Length(token, empty) == 0, "An empty array return was null");
            Check(Throws<BridgeArgumentException>(() => Call(token, sum, Create(token, KindOf(Double), 1))), "An array of another type was passed");
            Check(Throws<BridgeHandleException>(() => Call(token, sum, 99999)), "An unknown array handle was accepted");

            // The same managed array returned twice gives separate handles that share elements.
            ulong same = Method(token, arrays, "SameArray", ints);
            ulong firstHandle = Call(token, same), secondHandle = Call(token, same);
            Check(firstHandle != secondHandle, "Two returns shared a handle");
            Check(ComponentRegistry.TrySetArrayElement(token, firstHandle, ints, 0, 99) && Element(token, secondHandle, ints, 0) == 99, "Handles did not share the array");
            Check(ComponentRegistry.ReleaseObject(token, firstHandle) && Element(token, secondHandle, ints, 0) == 99, "Releasing one handle affected the other");
            Check(!ComponentRegistry.TryGetArrayElement(token, firstHandle, ints, 0, out _), "A released array handle still worked");
            Check(!ComponentRegistry.TryGetArrayElement(other, secondHandle, ints, 0, out _), "An array handle worked on another component");

            // Signature matching: array parameters are exact.
            Check(ComponentRegistry.TryFindMethod(token, arrays, "Sum", Int32, new[] { KindOf(Double) }, true, out _) == ComponentRegistry.FindResult.NotFound, "double[] matched int[]");
            Check(ComponentRegistry.TryFindMethod(token, arrays, "Sum", Int32, new[] { Int32 }, true, out _) == ComponentRegistry.FindResult.NotFound, "int matched int[]");
            Check(ComponentRegistry.TryFindMethod(token, arrays, "Count", Int32, new[] { 12 }, true, out _) == ComponentRegistry.FindResult.NotFound, "An object kind matched object[]");
            Check(ComponentRegistry.TryFindMethod(token, arrays, "Jagged", Int32, new[] { ints }, true, out _) == ComponentRegistry.FindResult.NotFound, "int[] matched int[][]");

            // Live arrays block release until freed.
            Check(ComponentRegistry.TryRelease(token, force: false) == ComponentRegistry.ReleaseResult.InUse, "A component with live arrays was released");
        }
        finally
        {
            ComponentRegistry.TryRelease(token, force: true);
            ComponentRegistry.ForgetUnload(token);
            ComponentRegistry.TryRelease(other, force: true);
            ComponentRegistry.ForgetUnload(other);
        }
        Console.WriteLine("Arrays: element and bulk access, range checks, strings and structs, argument and return handles, and release blocking passed.");
    }
}
