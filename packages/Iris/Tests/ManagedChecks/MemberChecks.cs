using System;
using System.Text;
using Iris.Bridge;

// Fields and properties through the real FindMember / GetMember / SetMember exports.
static unsafe class MemberChecks
{
    private const string Namespace = "Iris.Examples.Component.";
    private const int Field = 0, Property = 1;
    private const int Bool = (int)ValueKind.Bool, Int32 = (int)ValueKind.Int32, Int64 = (int)ValueKind.Int64, Double = (int)ValueKind.Double, Obj = (int)ValueKind.Object;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static ulong TypeId(ulong token, string name)
    {
        Check(ComponentRegistry.TryFindType(token, Namespace + name, out ulong id) == ComponentRegistry.FindResult.Found, "Type lookup failed: " + name);
        return id;
    }

    private static int Find(ulong token, ulong type, string name, int memberKind, int valueKind, out ulong id, out string diagnostic)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int, int, ulong*, byte*, int, int*, int*, int> find = &Bootstrap.FindMember;
        byte[] bytes = Encoding.UTF8.GetBytes(name), storage = new byte[1024];
        fixed (byte* namePointer = bytes, diagnosticPointer = storage)
        {
            ulong value = 99;
            int written = 99, truncated = 99;
            int status = find(token, type, namePointer, bytes.Length, memberKind, valueKind, &value, diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 || value == 0, "A failed lookup published a handle");
            id = value;
            diagnostic = new UTF8Encoding(false, true).GetString(storage, 0, written);
            return status;
        }
    }

    private static ulong Member(ulong token, ulong type, string name, int memberKind, int valueKind)
    {
        Check(Find(token, type, name, memberKind, valueKind, out ulong id, out string diagnostic) == 0 && id != 0, "Member lookup failed for " + name + ": " + diagnostic);
        return id;
    }

    private static int Get(ulong token, ulong member, ulong obj, out ulong value, out string diagnostic)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, ulong, ulong*, byte*, int, int*, int*, int> get = &Bootstrap.GetMember;
        byte[] storage = new byte[1024];
        fixed (byte* diagnosticPointer = storage)
        {
            ulong result = 99;
            int written = 99, truncated = 99;
            int status = get(token, member, obj, &result, diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 || result == 0, "A failed get published a value");
            value = result;
            diagnostic = new UTF8Encoding(false, true).GetString(storage, 0, written);
            return status;
        }
    }

    private static int Set(ulong token, ulong member, ulong obj, ulong value, out string diagnostic)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, ulong, ulong, byte*, int, int*, int*, int> set = &Bootstrap.SetMember;
        byte[] storage = new byte[1024];
        fixed (byte* diagnosticPointer = storage)
        {
            int written = 99, truncated = 99;
            int status = set(token, member, obj, value, diagnosticPointer, storage.Length, &written, &truncated);
            diagnostic = new UTF8Encoding(false, true).GetString(storage, 0, written);
            return status;
        }
    }

    private static ulong Value(ulong token, ulong member, ulong obj = 0)
    {
        Check(Get(token, member, obj, out ulong value, out string diagnostic) == 0, "Get failed: " + diagnostic);
        return value;
    }

    private static void Assign(ulong token, ulong member, ulong obj, ulong value)
    {
        Check(Set(token, member, obj, value, out string diagnostic) == 0, "Set failed: " + diagnostic);
    }

    private static ulong New(ulong token, string typeName)
    {
        ulong type = TypeId(token, typeName), id = 0;
        Check(ComponentRegistry.TryCreateObject(token, type, Array.Empty<int>(), ReadOnlySpan<ulong>.Empty, out id) == ComponentRegistry.CreateResult.Created, "Construction failed");
        return id;
    }

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        ulong token = ComponentRegistry.Load(path), other = ComponentRegistry.Load(path);
        ulong stale = 0;
        try
        {
            ulong settings = TypeId(token, "Settings");
            ulong obj = New(token, "Settings");

            // Static and instance fields round-trip.
            ulong staticCount = Member(token, settings, "StaticCount", Field, Int32);
            Assign(token, staticCount, 0, 41);
            Check(Value(token, staticCount) == 41, "A static field did not round-trip");
            ulong count = Member(token, settings, "Count", Field, Int32);
            Assign(token, count, obj, unchecked((ulong)(long)-5));
            Check(Value(token, count, obj) == unchecked((ulong)(long)-5), "An instance field did not round-trip");
            ulong ratio = Member(token, settings, "Ratio", Field, Double);
            Assign(token, ratio, obj, (ulong)BitConverter.DoubleToInt64Bits(2.5));
            Check(BitConverter.Int64BitsToDouble((long)Value(token, ratio, obj)) == 2.5, "A double field did not round-trip");
            ulong flag = Member(token, settings, "Flag", Field, Bool);
            Assign(token, flag, obj, 1);
            Check(Value(token, flag, obj) == 1, "A bool field did not round-trip");
            Check(Member(token, settings, "StaticCount", Field, Int32) == staticCount, "Repeating a lookup created another handle");

            // Properties round-trip through their accessors, static and instance.
            ulong enabled = Member(token, settings, "Enabled", Property, Bool);
            Assign(token, enabled, obj, 1);
            Check(Value(token, enabled, obj) == 1, "A property did not round-trip");
            ulong staticProperty = Member(token, settings, "StaticProperty", Property, Int32);
            Assign(token, staticProperty, 0, 12);
            Check(Value(token, staticProperty) == 12, "A static property did not round-trip");

            // Read-only and write-only targets are diagnosed, and nothing changed.
            ulong id = Member(token, settings, "Id", Field, Int32);
            Check(Value(token, id, obj) == 7 && Set(token, id, obj, 9, out string readonlyField) == 1 && readonlyField.Contains("read-only") && Value(token, id, obj) == 7,
                  "A readonly field was writable");
            Check(Set(token, Member(token, settings, "Limit", Field, Int32), 0, 1, out _) == 1, "A static readonly field was writable");
            Check(Set(token, Member(token, settings, "Constant", Field, Int32), 0, 1, out _) == 1 && Value(token, Member(token, settings, "Constant", Field, Int32)) == 5,
                  "A constant was writable or unreadable");
            ulong readOnly = Member(token, settings, "ReadOnly", Property, Int32);
            Check(Value(token, readOnly, obj) == 3 && Set(token, readOnly, obj, 1, out string noSetter) == 1 && noSetter.Contains("read-only"), "A property without a setter was writable");
            ulong writeOnly = Member(token, settings, "WriteOnly", Property, Int32);
            Check(Get(token, writeOnly, obj, out _, out string noGetter) == 1 && noGetter.Contains("write-only") && Set(token, writeOnly, obj, 4, out _) == 0, "A write-only property was readable");

            // Wrong static/instance usage.
            Check(Get(token, count, 0, out _, out string needsObject) == 1 && needsObject.Contains("object"), "An instance field was read without an object");
            Check(Get(token, staticCount, obj, out _, out string noObject) == 1 && noObject.Contains("static"), "A static field was read with an object");
            Check(Set(token, count, 0, 1, out _) == 1 && Set(token, staticCount, obj, 1, out _) == 1, "Wrong static/instance writes were accepted");
            ulong otherObject = New(token, "Other");
            Check(Get(token, count, otherObject, out _, out string wrongType) == 1 && wrongType.Contains("instance"), "An object of the wrong type was accepted");

            // Accessor exceptions are contained and the object stays usable.
            ulong checkedProperty = Member(token, settings, "Checked", Property, Int32);
            Check(Set(token, checkedProperty, obj, unchecked((ulong)(long)-1), out string thrown) == 3 && thrown.Contains("ArgumentOutOfRangeException") && thrown.Contains("π"),
                  "A setter exception was not contained with its details");
            Assign(token, checkedProperty, obj, 8);
            Check(Value(token, checkedProperty, obj) == 8, "An object was unusable after a setter failed");
            ulong exploding = Member(token, settings, "Exploding", Property, Int32);
            Check(Get(token, exploding, obj, out _, out string getterFailure) == 3 && getterFailure.Contains("InvalidOperationException") && !getterFailure.Contains("TargetInvocationException"),
                  "A getter exception was not reported as itself");
            Check(Value(token, count, obj) == unchecked((ulong)(long)-5), "An object was unusable after a getter failed");

            // Lookup failures.
            Check(Find(token, settings, "Missing", Field, Int32, out _, out string missing) == 6 && missing.Contains("Missing"), "A missing member was not NotFound");
            Check(Find(token, settings, "Count", Field, Int64, out _, out _) == 6, "A field matched by widening");
            Check(Find(token, settings, "Count", Property, Int32, out _, out _) == 6 && Find(token, settings, "Enabled", Field, Bool, out _, out _) == 6,
                  "A field was found as a property or the reverse");
            Check(Find(token, settings, "secret", Field, Int32, out _, out _) == 6, "A private field was found");
            Check(Find(token, settings, "Item", Property, Int32, out _, out _) == 6, "An indexer was found as a property");
            Check(Find(token, settings, "Count", 7, Int32, out _, out _) == 1 && Find(token, settings, "Count", Field, 99, out _, out _) == 1 &&
                  Find(token, settings, "Count", Field, 0, out _, out _) == 1, "Invalid member or value kinds were accepted");
            Check(Find(token, settings, "", Field, Int32, out _, out _) == 1, "An empty name was accepted");

            // Object-typed members: a get is a new handle, a set takes a handle or null.
            ulong child = Member(token, settings, "Child", Field, Obj);
            ulong childProperty = Member(token, settings, "ChildProperty", Property, Obj);
            Check(Value(token, child, obj) == 0 && Value(token, childProperty, obj) == 0, "An unset object member was not null");
            ulong counterObject = New(token, "Counter");
            Assign(token, child, obj, counterObject);
            Check(Value(token, child, obj) is ulong got && got != 0 && got != counterObject, "An object field did not return a new handle");
            Assign(token, childProperty, obj, counterObject);
            Check(Value(token, childProperty, obj) != 0, "An object property did not return an object");
            Check(Set(token, child, obj, otherObject, out string badObject) == 1 && badObject.Contains("instance of"), "An object of the wrong type was stored");
            Check(Set(token, child, obj, counterObject + 1000, out _) == 5, "An unknown object was stored");
            Assign(token, child, obj, 0);
            Check(Value(token, child, obj) == 0, "Storing null did not clear the field");

            // Handle validation: unknown, zero, another component's.
            Check(Get(token, 0, obj, out _, out _) == 5 && Get(token, count + 1000, obj, out _, out _) == 5, "An unknown member handle was accepted");
            Check(Get(other, count, obj, out _, out _) == 5, "A member handle worked on another component");
            stale = count;
        }
        finally
        {
            ComponentRegistry.Release(token);
            ComponentRegistry.TryRelease(token, force: true);
            ComponentRegistry.ForgetUnload(token);
            ComponentRegistry.TryRelease(other, force: true);
            ComponentRegistry.ForgetUnload(other);
        }
        Check(Get(token, stale, 0, out _, out _) == 5, "A released component's member still worked");
        Console.WriteLine("Members: fields and properties round-trip, read-only and write-only targets are diagnosed, accessor exceptions are contained, and handles are validated.");
    }
}
