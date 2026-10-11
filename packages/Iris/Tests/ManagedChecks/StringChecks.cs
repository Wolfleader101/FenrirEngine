using System;
using System.Runtime.InteropServices;
using System.Text;
using Iris.Bridge;

// Strings through the real FindMethod / InvokeMethod / member exports: borrowed UTF-8 arguments decoded
// strictly, owned UTF-8 returns, and null kept apart from empty.
static unsafe class StringChecks
{
    private const string Strings = "Iris.Examples.Component.Strings";
    private const string Component = "Iris.Examples.Component.";
    private const int Int32 = 6, Bool = 1, Obj = 12, Str = 13;
    private const int Field = 0, Property = 1;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static string Text(byte[] storage, int written) => new UTF8Encoding(false, true).GetString(storage, 0, written);

    private static byte[] Utf8(string text) => Encoding.UTF8.GetBytes(text);

    private static ulong TypeId(ulong token, string name)
    {
        Check(ComponentRegistry.TryFindType(token, name, out ulong id) == ComponentRegistry.FindResult.Found, "Type lookup failed: " + name);
        return id;
    }

    private static ulong Method(ulong token, ulong type, string name, int returnKind, params int[] parameters)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int, int*, int, ulong*, byte*, int, int*, int*, int> find = &Bootstrap.FindMethod;
        byte[] bytes = Encoding.UTF8.GetBytes(name), storage = new byte[4096];
        fixed (int* kinds = parameters)
        fixed (byte* namePointer = bytes, diagnosticPointer = storage)
        {
            ulong value = 0;
            int written = 0, truncated = 0;
            int status = find(token, type, namePointer, bytes.Length, returnKind, kinds, parameters.Length, &value,
                              diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 && value != 0, $"Method lookup failed for {name}: {status} {Text(storage, written)}");
            return value;
        }
    }

    // An argument: a string (null for a null string), or a plain slot value.
    private readonly record struct Arg(bool IsString, byte[]? Bytes, ulong Slot)
    {
        internal static Arg Of(string? text) => new(true, text == null ? null : Utf8(text), 0);
        internal static Arg Raw(byte[] bytes) => new(true, bytes, 0);
        internal static Arg Number(ulong value) => new(false, null, value);
    }

    // Calls the export with each string pinned and described in place, as the native side does.
    private static int Invoke(ulong token, ulong method, out ulong result, out string diagnostic, params Arg[] arguments)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, ulong*, int, ulong*, byte*, int, int*, int*, int> invoke = &Bootstrap.InvokeMethod;
        var descriptors = new StringArgument[arguments.Length];
        var slots = new ulong[arguments.Length];
        var pins = new GCHandle[arguments.Length];
        byte[] storage = new byte[4096];
        try
        {
            for (int index = 0; index < arguments.Length; ++index)
            {
                Arg argument = arguments[index];
                if (!argument.IsString) continue;
                if (argument.Bytes is byte[] bytes)
                {
                    pins[index] = GCHandle.Alloc(bytes, GCHandleType.Pinned);
                    descriptors[index] = new StringArgument { Data = (byte*)pins[index].AddrOfPinnedObject(), Length = bytes.Length, IsNull = 0 };
                }
                else
                {
                    descriptors[index] = new StringArgument { IsNull = 1 };
                }
            }
            fixed (StringArgument* descriptorBase = descriptors)
            fixed (ulong* slotPointer = slots)
            fixed (byte* diagnosticPointer = storage)
            {
                for (int index = 0; index < arguments.Length; ++index)
                    slots[index] = arguments[index].IsString ? (ulong)(descriptorBase + index) : arguments[index].Slot;
                ulong value = 99;
                int written = 99, truncated = 99;
                int status = invoke(token, method, slotPointer, slots.Length, &value, diagnosticPointer, storage.Length, &written, &truncated);
                Check(status == 0 || value == 0, "A failed invocation published a result");
                result = value;
                diagnostic = Text(storage, written);
                return status;
            }
        }
        finally
        {
            foreach (GCHandle pin in pins) if (pin.IsAllocated) pin.Free();
        }
    }

    // Takes a returned block: its text, or null for the zero slot. The block is given back.
    private static string? Take(ulong block)
    {
        if (block == 0) return null;
        long length = *(long*)block;
        string text = new UTF8Encoding(false, true).GetString((byte*)block + 8, checked((int)length));
        delegate* unmanaged[Cdecl]<ulong, void> free = &Bootstrap.FreeString;
        free(block);
        return text;
    }

    private static string? Call(ulong token, ulong method, params Arg[] arguments)
    {
        Check(Invoke(token, method, out ulong result, out string diagnostic, arguments) == 0, "Invocation failed: " + diagnostic);
        return Take(result);
    }

    private static ulong Member(ulong token, ulong type, string name, int memberKind)
    {
        Check(ComponentRegistry.TryFindMember(token, type, name, memberKind, Str, out ulong id) == ComponentRegistry.FindResult.Found, "String member lookup failed: " + name);
        return id;
    }

    // Stores a string in a member through the registry, the way SetMember does.
    private static void Store(ulong token, ulong member, ulong target, byte[]? bytes)
    {
        fixed (byte* data = bytes)
        {
            var value = new StringArgument { Data = data, Length = bytes?.Length ?? 0, IsNull = bytes == null ? 1 : 0 };
            Check(ComponentRegistry.TrySetMemberValue(token, member, target, (ulong)&value), "Set failed");
        }
    }

    private static string? Read(ulong token, ulong member, ulong target)
    {
        Check(ComponentRegistry.TryGetMemberValue(token, member, target, out ulong block), "Get failed");
        return Take(block);
    }

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        ulong token = ComponentRegistry.Load(path);
        try
        {
            ulong strings = TypeId(token, Strings);

            // Arguments and returns round-trip, including multi-byte scalars and embedded nulls.
            ulong echo = Method(token, strings, "Echo", Str, Str);
            foreach (string sample in new[] { "plain", "héllo π \U0001F43A — ok", "A\0B", "\0", "tail\0", "" })
                Check(Call(token, echo, Arg.Of(sample)) == sample, "A string did not round-trip: " + sample.Replace("\0", "\\0"));

            // Null and empty are different in both directions.
            ulong echoOrNull = Method(token, strings, "EchoOrNull", Str, Str);
            Check(Call(token, echoOrNull, Arg.Of(null)) is null, "A null argument did not come back null");
            Check(Call(token, echoOrNull, Arg.Of("")) == "", "An empty argument came back null");
            ulong isNull = Method(token, strings, "IsNull", Bool, Str);
            Check(Invoke(token, isNull, out ulong sawNull, out _, Arg.Of(null)) == 0 && sawNull == 1, "Managed code did not see a null string");
            Check(Invoke(token, isNull, out ulong sawEmpty, out _, Arg.Of("")) == 0 && sawEmpty == 0, "Managed code saw an empty string as null");
            Check(Call(token, Method(token, strings, "Null", Str)) is null, "A null return was not the zero slot");
            Check(Call(token, Method(token, strings, "Empty", Str)) == "", "An empty return was null");

            // Returns against the real bridge: non-ASCII, embedded null, and one far larger than any fixed buffer.
            Check(Call(token, Method(token, strings, "Greeting", Str)) == "héllo π \U0001F43A — ok", "A non-ASCII return was altered");
            Check(Call(token, Method(token, strings, "WithNull", Str)) == "A\0B", "An embedded null in a return was lost");
            ulong repeat = Method(token, strings, "Repeat", Str, Str, Int32);
            Check(Invoke(token, repeat, out ulong big, out string bigDiagnostic, Arg.Of("π\U0001F43A"), Arg.Number(200_000)) == 0, "A large return failed: " + bigDiagnostic);
            string? large = Take(big);
            Check(large != null && large.Length == 600_000 && large.StartsWith("π\U0001F43Aπ\U0001F43A") && large.EndsWith("π\U0001F43A"), "A large return was truncated or changed");

            // Malformed UTF-8 is rejected before managed code runs.
            ulong join = Method(token, strings, "Join", Str, Str, Str);
            foreach (byte[] bad in new[] { new byte[] { 0xFF }, new byte[] { 0xC3 }, new byte[] { 0xC0, 0x80 }, new byte[] { 0xED, 0xA0, 0x80 }, new byte[] { 0xF4, 0x90, 0x80, 0x80 } })
            {
                Check(Invoke(token, join, out _, out string malformed, Arg.Of("x"), Arg.Raw(bad)) == 1 && malformed.Contains("UTF-8"),
                      "Malformed UTF-8 was not an argument error: " + BitConverter.ToString(bad));
            }
            ulong throws = Method(token, strings, "Throws", Str, Str);
            Check(Invoke(token, throws, out _, out string thrown, Arg.Of("π")) == 3 && thrown.Contains("Rejected: π"), "A managed exception with a string argument was not contained");

            // A returned string with no UTF-8 form is diagnosed, not mangled.
            ulong lone = Method(token, strings, "UnpairedSurrogate", Str);
            Check(Invoke(token, lone, out ulong none, out string surrogate) == 1 && none == 0 && surrogate.Contains("surrogate"), "An unpaired surrogate return was not diagnosed");

            // Strings are their own kind: never an object, and not matched by other kinds.
            ulong kindOfString = Method(token, strings, "Kind", Int32, Str);
            Check(Invoke(token, kindOfString, out ulong chosen, out _, Arg.Of("x")) == 0 && chosen == 1, "A string kind did not pick the string overload");
            ComponentRegistry.TryCreateObject(token, TypeId(token, Component + "Counter"), Array.Empty<int>(), ReadOnlySpan<ulong>.Empty, out ulong counter);
            ulong kindOfObject = Method(token, strings, "Kind", Int32, Obj);
            Check(Invoke(token, kindOfObject, out ulong chosenObject, out _, Arg.Number(counter)) == 0 && chosenObject == 2, "An object kind did not pick the object overload");
            Check(ComponentRegistry.TryFindMethod(token, strings, "Length", Int32, new[] { Obj }, true, out _) == ComponentRegistry.FindResult.NotFound, "An object kind matched a string parameter");
            Check(ComponentRegistry.TryFindMethod(token, strings, "Length", Int32, new[] { Int32 }, true, out _) == ComponentRegistry.FindResult.NotFound, "An integer kind matched a string parameter");

            // Constructors and instance methods take strings too.
            ulong greeterType = TypeId(token, Component + "Greeter");
            byte[] name = Utf8("Wölfe"), mark = Utf8("!");
            fixed (byte* namePointer = name, markPointer = mark)
            {
                var nameArgument = new StringArgument { Data = namePointer, Length = name.Length };
                var markArgument = new StringArgument { Data = markPointer, Length = mark.Length };
                Check(ComponentRegistry.TryCreateObject(token, greeterType, new[] { Str }, new[] { (ulong)&nameArgument }, out ulong greeter) == ComponentRegistry.CreateResult.Created,
                      "A constructor with a string failed");
                Check(ComponentRegistry.TryFindMethod(token, greeterType, "Greet", Str, new[] { Str }, false, out ulong greet) == ComponentRegistry.FindResult.Found, "Instance method lookup failed");
                Check(ComponentRegistry.TryInvokeInstanceMethod(token, greet, greeter, new[] { (ulong)&markArgument }, out ulong greeting) && Take(greeting) == "Hello, Wölfe!",
                      "An instance method with strings failed");
            }

            // Fields and properties.
            ulong settingsType = TypeId(token, Component + "Settings");
            ComponentRegistry.TryCreateObject(token, settingsType, Array.Empty<int>(), ReadOnlySpan<ulong>.Empty, out ulong settings);
            foreach ((string member, int kind, bool isStatic) in new[] { ("Label", Field, false), ("StaticLabel", Field, true), ("Caption", Property, false) })
            {
                ulong id = Member(token, settingsType, member, kind), target = isStatic ? 0 : settings;
                Check(Read(token, id, target) is null, "An unset string member was not null: " + member);
                foreach (string sample in new[] { "π\U0001F43A", "", "A\0B" })
                {
                    Store(token, id, target, Utf8(sample));
                    Check(Read(token, id, target) == sample, "A string member did not round-trip: " + member);
                }
                Store(token, id, target, null);
                Check(Read(token, id, target) is null, "Storing null did not clear the member: " + member);
            }
            Check(ComponentRegistry.TryFindMember(token, settingsType, "Label", Field, Obj, out _) == ComponentRegistry.FindResult.NotFound, "A string field matched an object kind");
            Check(ComponentRegistry.TryFindMember(token, settingsType, "Count", Field, Str, out _) == ComponentRegistry.FindResult.NotFound, "An int field matched a string kind");

            ulong readOnly = Member(token, settingsType, "ReadOnlyCaption", Property);
            bool wrote = true;
            try { Store(token, readOnly, settings, null); } catch (BridgeArgumentException) { wrote = false; }
            Check(!wrote, "A read-only string property was writable");
            ulong label = Member(token, settingsType, "Label", Field);
            Store(token, label, settings, Utf8("kept"));
            bool stored = true;
            try { Store(token, label, settings, new byte[] { 0xFF }); } catch (BridgeArgumentException) { stored = false; }
            Check(!stored && Read(token, label, settings) == "kept", "Malformed UTF-8 was stored in a member");
        }
        finally
        {
            ComponentRegistry.TryRelease(token, force: true);
            ComponentRegistry.ForgetUnload(token);
        }
        Console.WriteLine("Strings: UTF-8 round-trips with embedded nulls and large sizes, null stays distinct from empty, and malformed input is rejected before managed code runs.");
    }
}
