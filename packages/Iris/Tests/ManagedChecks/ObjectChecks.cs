using System;
using System.Runtime.CompilerServices;
using System.Text;
using Iris.Bridge;

// Managed objects through the real CreateObject / ReleaseObject / FindInstanceMethod /
// InvokeInstanceMethod / ReleaseComponent / ForceReleaseComponent exports.
static unsafe class ObjectChecks
{
    private const string Namespace = "Iris.Examples.Component.";
    private const int Void = 0, Bool = 1, Int32 = 6, Double = 11;
    private const int InUse = 7;

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static ulong TypeId(ulong token, string name)
    {
        Check(ComponentRegistry.TryFindType(token, Namespace + name, out ulong id) == ComponentRegistry.FindResult.Found, "Type lookup failed: " + name);
        return id;
    }

    private static int Create(ulong token, ulong type, int[] kinds, ulong[] args, out ulong id, out string diagnostic)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, int*, ulong*, int, ulong*, byte*, int, int*, int*, int> create = &Bootstrap.CreateObject;
        byte[] storage = new byte[4096];
        fixed (int* kindPointer = kinds)
        fixed (ulong* argumentPointer = args)
        fixed (byte* diagnosticPointer = storage)
        {
            ulong value = 99;
            int written = 99, truncated = 99;
            int status = create(token, type, kindPointer, argumentPointer, args.Length, &value, diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 || value == 0, "A failed construction published a handle");
            id = value;
            diagnostic = new UTF8Encoding(false, true).GetString(storage, 0, written);
            return status;
        }
    }

    private static ulong New(ulong token, ulong type, int[] kinds, params ulong[] args)
    {
        Check(Create(token, type, kinds, args, out ulong id, out string diagnostic) == 0 && id != 0, "Construction failed: " + diagnostic);
        return id;
    }

    private static int ReleaseObject(ulong token, ulong id)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int*, int*, int> release = &Bootstrap.ReleaseObject;
        int written = 99, truncated = 99;
        return release(token, id, null, 0, &written, &truncated);
    }

    private static ulong InstanceMethod(ulong token, ulong type, string name, int returnKind, params int[] parameters)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, byte*, int, int, int*, int, ulong*, byte*, int, int*, int*, int> find = &Bootstrap.FindInstanceMethod;
        byte[] bytes = Encoding.UTF8.GetBytes(name);
        ulong id = 0;
        int written = 0, truncated = 0;
        fixed (byte* namePointer = bytes)
        fixed (int* kinds = parameters)
            Check(find(token, type, namePointer, bytes.Length, returnKind, kinds, parameters.Length, &id, null, 0, &written, &truncated) == 0 && id != 0,
                  "Instance method lookup failed: " + name);
        return id;
    }

    private static int Invoke(ulong token, ulong method, ulong obj, ulong[] args, out ulong result, out string diagnostic)
    {
        delegate* unmanaged[Cdecl]<ulong, ulong, ulong, ulong*, int, ulong*, byte*, int, int*, int*, int> invoke = &Bootstrap.InvokeInstanceMethod;
        byte[] storage = new byte[4096];
        fixed (ulong* argumentPointer = args)
        fixed (byte* diagnosticPointer = storage)
        {
            ulong value = 99;
            int written = 99, truncated = 99;
            int status = invoke(token, method, obj, argumentPointer, args.Length, &value, diagnosticPointer, storage.Length, &written, &truncated);
            Check(status == 0 || value == 0, "A failed call published a result");
            result = value;
            diagnostic = new UTF8Encoding(false, true).GetString(storage, 0, written);
            return status;
        }
    }

    private static ulong Call(ulong token, ulong method, ulong obj, params ulong[] args)
    {
        Check(Invoke(token, method, obj, args, out ulong result, out string diagnostic) == 0, "Instance call failed: " + diagnostic);
        return result;
    }

    private static int ReleaseComponent(ulong token)
    {
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> release = &Bootstrap.ReleaseComponent;
        int written = 99, truncated = 99;
        return release(token, null, 0, &written, &truncated);
    }

    private static int ForceReleaseComponent(ulong token)
    {
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> release = &Bootstrap.ForceReleaseComponent;
        int written = 99, truncated = 99;
        return release(token, null, 0, &written, &truncated);
    }

    // Keeps the object reference out of the caller's frame so the GC check is not fooled by a local root.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static WeakReference WatchObject(ulong token, ulong id)
    {
        object? target = ComponentRegistry.PeekObject(token, id);
        Check(target != null, "A live object was not in the table");
        return new WeakReference(target);
    }

    private static void CollectCycle()
    {
        // Verification only: normal library operations never force garbage collection.
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    internal static void Run(string componentPath)
    {
        string path = System.IO.Path.GetFullPath(componentPath);
        ulong token = ComponentRegistry.Load(path), other = ComponentRegistry.Load(path);
        bool released = false;
        try
        {
            ulong counter = TypeId(token, "Counter"), step = TypeId(token, "StepCounter");
            ulong value = InstanceMethod(token, counter, "Value", Int32);
            ulong add = InstanceMethod(token, counter, "Add", Void, Int32);
            ulong next = InstanceMethod(token, counter, "Next", Int32);

            // Constructors are chosen by exact parameter types; state lives in the object between calls.
            ulong first = New(token, counter, Array.Empty<int>());
            ulong started = New(token, counter, new[] { Int32 }, 5);
            ulong scaled = New(token, counter, new[] { Int32, Double }, 4, (ulong)BitConverter.DoubleToInt64Bits(2.5));
            Check(Call(token, value, first) == 0 && Call(token, value, started) == 5 && Call(token, value, scaled) == 10, "A constructor overload was not selected");
            Check(first != started && started != scaled, "Two objects shared a handle");
            Call(token, add, started, 3);
            Check(Call(token, value, started) == 8 && Call(token, value, first) == 0, "Instance state was shared or lost");

            // A virtual method dispatches to the override of the object's real type.
            ulong stepper = New(token, step, new[] { Int32 }, 1);
            Check(Call(token, next, started) == 9 && Call(token, next, stepper) == 11, "A virtual call did not dispatch to the override");

            // Construction failures are contained, publish nothing and leave no root.
            int live = ComponentRegistry.LiveObjectCount(token);
            Check(Create(token, counter, new[] { Bool }, new ulong[] { 1 }, out _, out string failed) == 3 && failed.Contains("InvalidOperationException") &&
                  failed.Contains("π \U0001F43A"), "A constructor exception was not contained with its details");
            Check(ComponentRegistry.LiveObjectCount(token) == live, "A failed construction left a root");
            Check(Create(token, TypeId(token, "Shape"), Array.Empty<int>(), Array.Empty<ulong>(), out _, out _) == 6, "An abstract type was constructed");
            Check(Create(token, TypeId(token, "Calculations"), Array.Empty<int>(), Array.Empty<ulong>(), out _, out _) == 6, "A static type was constructed");
            Check(Create(token, counter, new[] { 8 }, new ulong[] { 1 }, out _, out _) == 6, "A constructor matched by widening");
            Check(Create(token, counter, new int[300], new ulong[300], out _, out _) == 1, "More than 255 constructor parameters were accepted");
            Check(Create(token, counter, new[] { 99 }, new ulong[] { 1 }, out _, out _) == 1 && Create(token, 0, Array.Empty<int>(), Array.Empty<ulong>(), out _, out _) == 5,
                  "Invalid kinds or type handles were accepted");

            // Instance methods and objects must agree.
            ulong ping = InstanceMethod(token, TypeId(token, "Other"), "Ping", Int32);
            Check(Invoke(token, ping, started, Array.Empty<ulong>(), out _, out string wrongType) == 1 && wrongType.Contains("instance"), "An object of the wrong type was accepted");
            Check(Invoke(token, next, 0, Array.Empty<ulong>(), out _, out _) == 5 && Invoke(token, next, started + 1000, Array.Empty<ulong>(), out _, out _) == 5,
                  "An unknown object handle was accepted");
            Check(Invoke(token, InstanceMethod(token, counter, "Fail", Int32), started, Array.Empty<ulong>(), out _, out string instanceFailure) == 3 &&
                  instanceFailure.Contains("InvalidOperationException"), "An instance method exception was not contained");
            Check(Call(token, value, started) == 9, "An object was unusable after a failing call");

            // Handle validation across components.
            ulong otherCounter = TypeId(other, "Counter");
            ulong foreign = New(other, otherCounter, Array.Empty<int>());
            Check(Invoke(token, next, foreign, Array.Empty<ulong>(), out _, out _) == 5, "An object from another component was accepted");
            Check(ReleaseObject(token, foreign) == 5 && ReleaseObject(other, foreign) == 0, "A release crossed components");

            // The object is rooted while owned and unrooted when released.
            WeakReference watched = WatchObject(token, started);
            for (int cycle = 0; cycle < 3; ++cycle) CollectCycle();
            Check(watched.IsAlive && Call(token, value, started) == 9, "An owned object was collected");
            Check(ReleaseObject(token, started) == 0, "Releasing an object failed");
            for (int cycle = 0; cycle < 3 && watched.IsAlive; ++cycle) CollectCycle();
            Check(!watched.IsAlive, "A released object was still rooted");
            Check(ReleaseObject(token, started) == 5 && Invoke(token, value, started, Array.Empty<ulong>(), out _, out _) == 5, "A released object stayed valid");

            // The component cannot be released while objects are live; forcing drops them.
            Check(ReleaseComponent(token) == InUse, "A component with live objects was released");
            Check(Call(token, value, first) == 0, "A rejected release damaged the component");
            Check(ReleaseObject(token, first) == 0 && ReleaseObject(token, scaled) == 0 && ReleaseObject(token, stepper) == 0, "Releasing objects failed");
            Check(ComponentRegistry.LiveObjectCount(token) == 0, "Objects remained after releasing them all");
            ulong kept = New(token, counter, Array.Empty<int>());
            Check(ForceReleaseComponent(token) == 0, "A forced release failed");
            released = true;
            Check(Invoke(token, next, kept, Array.Empty<ulong>(), out _, out _) == 5 && ReleaseObject(token, kept) == 5,
                  "An object stayed valid after its component was force-released");
            Check(ForceReleaseComponent(token) == 5 && ReleaseComponent(token) == 5, "A released component accepted another release");
        }
        finally
        {
            if (!released) ForceReleaseComponent(token);
            ForceReleaseComponent(other);
            ComponentRegistry.ForgetUnload(token);
            ComponentRegistry.ForgetUnload(other);
        }
        Console.WriteLine("Objects: constructors, state, virtual dispatch, contained failures, ownership across components, GC rooting and the live-object release policy passed.");
    }
}
