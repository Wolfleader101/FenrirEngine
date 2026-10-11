using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using Iris.Bridge;

static class UnloadChecks
{
    private static Type? retainedType;
    private sealed record Observation(ulong Token, WeakReference Assembly, WeakReference Dependency);

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static unsafe int State(ulong token, int expectedStatus = 0)
    {
        delegate* unmanaged[Cdecl]<ulong, int*, byte*, int, int*, int*, int> query = &Bootstrap.GetComponentUnloadState;
        int state = 99, written = 99, truncated = 99;
        Check(query(token, &state, null, 0, &written, &truncated) == expectedStatus && written == 0 && truncated == 0,
              "Unload query status/diagnostic contract failed");
        Check(expectedStatus == 0 ? state >= 0 && state <= 2 : state == -1, "Unload query published an invalid state");
        return state;
    }

    private static unsafe void Forget(ulong token, int expectedStatus = 0)
    {
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> forget = &Bootstrap.ForgetComponentUnload;
        int written = 99, truncated = 99;
        Check(forget(token, null, 0, &written, &truncated) == expectedStatus && written == 0 && truncated == 0,
              "Observation cleanup status/diagnostic contract failed");
    }

    // Keep all context/assembly/type/delegate/exception locals off the GC-loop stack.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static unsafe Observation LoadInvokeRelease(string path, bool retain, bool leakObject = false)
    {
        delegate* unmanaged[Cdecl]<byte*, int, ulong*, byte*, int, int*, int*, int> load = &Bootstrap.LoadComponent;
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> release = &Bootstrap.ReleaseComponent;
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> forceRelease = &Bootstrap.ForceReleaseComponent;
        byte[] bytes = Encoding.UTF8.GetBytes(path);
        ulong token = 0;
        int written = 0, truncated = 0;
        fixed (byte* pointer = bytes)
            Check(load(pointer, bytes.Length, &token, null, 0, &written, &truncated) == 0 && token != 0,
                  "Unload fixture load failed");
        Check(State(token) == 0, "Live registration did not report NotRequested");
        Forget(token, 5); // A weak observer cannot be forgotten while it owns a live registration.
        Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Add").Value == 42, "Unload fixture invocation failed");
        Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Throw").Status == 3, "Unload fixture exception was not contained");
        Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Add").Value == 42, "Unload fixture recovery failed");
        // A type handle is only an id in the registry's table; it must not keep the released context alive.
        Check(ComponentRegistry.TryFindType(token, InvocationChecks.TypeName, out ulong typeId) == ComponentRegistry.FindResult.Found && typeId != 0,
              "Unload fixture type lookup failed");
        Assembly assembly = AssemblyLoadContext.All.SelectMany(context => context.Assemblies).Single(item => item.Location == path);
        AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(assembly)!;
        Assembly dependency = context.Assemblies.Single(item => item.GetName().Name == "Iris.Examples.Dependency");
        var observation = new Observation(token, new WeakReference(assembly, true), new WeakReference(dependency, true));
        if (retain) retainedType = assembly.GetType("Iris.Examples.Component.ComponentMarker", throwOnError: true);
        // An owned object roots its component's state. Releasing the component is rejected while an object
        // is live; once the last object is released the component unloads and is collected as before. A
        // leaked object only survives a forced release, which drops it with the component.
        ulong counterType = 0, objectId = 0;
        Check(ComponentRegistry.TryFindType(token, "Iris.Examples.Component.Counter", out counterType) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryCreateObject(token, counterType, Array.Empty<int>(), ReadOnlySpan<ulong>.Empty, out objectId) == ComponentRegistry.CreateResult.Created,
              "Unload fixture object construction failed");
        // A member handle is only an id in the registry's table; it must not keep the context alive.
        ulong settingsType = 0, memberId = 0, memberValue = 0;
        Check(ComponentRegistry.TryFindType(token, "Iris.Examples.Component.Settings", out settingsType) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryFindMember(token, settingsType, "StaticCount", 0, 6, out memberId) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TrySetMemberValue(token, memberId, 0, 5) && ComponentRegistry.TryGetMemberValue(token, memberId, 0, out memberValue) && memberValue == 5,
              "Unload fixture member access failed");

        // A returned object is rooted like a constructed one: Factory.Make returns a new Counter.
        ulong factoryType = 0, makeMethod = 0, returnedId = 0;
        Check(ComponentRegistry.TryFindType(token, "Iris.Examples.Component.Factory", out factoryType) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryFindMethod(token, factoryType, "Make", 12, new[] { 6 }, true, out makeMethod) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryInvokeMethod(token, makeMethod, new ulong[] { 3 }, out returnedId) && returnedId != 0,
              "Unload fixture returned-object call failed");
        // An array is rooted like an object: it blocks release until freed, and a leaked one is dropped by a forced release.
        Check(ComponentRegistry.TryCreateArray(token, (6 << 8) | 15, 8, out ulong arrayId) && arrayId != 0, "Unload fixture array creation failed");
        if (!leakObject)
        {
            Check(release(token, null, 0, &written, &truncated) == 7 && State(token) == 0, "A component with a live array was released");
            Check(ComponentRegistry.ReleaseObject(token, arrayId), "Unload fixture array release failed");
        }
        if (!leakObject)
        {
            Check(release(token, null, 0, &written, &truncated) == 7 && State(token) == 0, "A component with live objects was released");
            Check(ComponentRegistry.ReleaseObject(token, objectId), "Unload fixture object release failed");
            Check(release(token, null, 0, &written, &truncated) == 7 && State(token) == 0, "A component with a live returned object was released");
            Check(ComponentRegistry.ReleaseObject(token, returnedId), "Unload fixture returned-object release failed");
        }
        Check((leakObject ? forceRelease(token, null, 0, &written, &truncated) : release(token, null, 0, &written, &truncated)) == 0 && written == 0 && truncated == 0,
              "Unload request failed");
        Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Add").Status == 5, "Release did not invalidate invocation");
        Check(!ComponentRegistry.TryGetTypeName(token, typeId, 0, out _), "Release did not invalidate the type handle");
        Check(!ComponentRegistry.TryGetMemberValue(token, memberId, 0, out _), "Release did not invalidate the member handle");
        Check(State(token) == 1, "A context still rooted in the helper did not report Pending");
        GC.KeepAlive(context);
        return observation;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void DropRetainedType() => retainedType = null;

    private static void CollectCycle()
    {
        // Verification only: normal library operations never force garbage collection.
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    private static void WaitForCollection(Observation observation, bool forgotten = false)
    {
        for (int cycle = 0; cycle < 10; ++cycle)
        {
            CollectCycle();
            bool contextsCollected = forgotten || State(observation.Token) == 2;
            if (contextsCollected && !observation.Assembly.IsAlive && !observation.Dependency.IsAlive) return;
        }
        throw new Exception("Released context/component/private dependency did not collect within 10 verification cycles");
    }

    internal static unsafe void Run(string componentPath)
    {
        string path = Path.GetFullPath(componentPath);
        int baseline = ComponentRegistry.Count, trackingBaseline = ComponentRegistry.TrackingCount;
        for (int cycle = 0; cycle < 8; ++cycle)
        {
            Observation observation = LoadInvokeRelease(path, false);
            WaitForCollection(observation);
            Check(State(observation.Token) == 2, "Collected observation was not repeatable");
            Forget(observation.Token);
            Forget(observation.Token, 5);
            State(observation.Token, 5);
            Check(ComponentRegistry.Count == baseline && ComponentRegistry.TrackingCount == trackingBaseline,
                  "Collection/forget cycle leaked registrations or weak tracking records");
        }
        Console.WriteLine("Managed unloading: eight load/invoke/release cycles collected contexts, component assemblies and private dependencies.");

        // A leaked object is dropped by a forced release and does not keep the component alive.
        Observation leaked = LoadInvokeRelease(path, false, leakObject: true);
        WaitForCollection(leaked);
        Forget(leaked.Token);
        Console.WriteLine("Managed unloading: a component collects after its last object is released, or after a forced release drops a leaked object.");

        Observation held = LoadInvokeRelease(path, true);
        try
        {
            for (int cycle = 0; cycle < 3; ++cycle) CollectCycle();
            Check(State(held.Token) == 1 && held.Assembly.IsAlive && held.Dependency.IsAlive && retainedType != null,
                  "An intentionally retained Type did not keep unloading Pending");
            Console.WriteLine("Managed unloading: retained Type keeps the released context Pending.");
        }
        finally { DropRetainedType(); }
        WaitForCollection(held);
        Forget(held.Token);
        Console.WriteLine("Managed unloading: dropping the retained Type permits collection.");

        Observation forgotten = LoadInvokeRelease(path, true);
        Forget(forgotten.Token); // Dropping observation must not cancel or retain unloading.
        State(forgotten.Token, 5);
        DropRetainedType();
        WaitForCollection(forgotten, forgotten: true);

        delegate* unmanaged[Cdecl]<ulong, int*, byte*, int, int*, int*, int> query = &Bootstrap.GetComponentUnloadState;
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> forget = &Bootstrap.ForgetComponentUnload;
        int state = 99, written = 99, truncated = 99;
        Check(query(0, null, null, 0, &written, &truncated) == 1 && written == 0 && truncated == 0, "Null state pointer accepted");
        Check(query(0, &state, null, 1, &written, &truncated) == 1 && state == -1, "Invalid query diagnostic pointer accepted");
        Check(query(0, &state, null, -1, &written, &truncated) == 1 && state == -1, "Negative query capacity accepted");
        Check(query(0, &state, null, 0, null, &truncated) == 1 && state == -1, "Null query diagnostic count accepted");
        Check(query(0, &state, null, 0, &written, null) == 1 && state == -1, "Null query truncation flag accepted");
        State(0, 5);
        Check(forget(0, null, 1, &written, &truncated) == 1, "Invalid forget diagnostic storage accepted");
        Forget(0, 5);
        Check(ComponentRegistry.Count == baseline && ComponentRegistry.TrackingCount == trackingBaseline, "Unload checks leaked records");
        InvocationChecks.CheckResidentBridge(componentPath);
        Console.WriteLine("Managed unloading: forgotten observers do not retain assemblies; stale tokens reject access; resident bridge remains usable.");
    }
}
