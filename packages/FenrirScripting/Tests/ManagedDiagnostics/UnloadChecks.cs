using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using System.Text;
using Fenrir.ManagedHost;

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
    private static unsafe Observation LoadInvokeRelease(string path, bool retain)
    {
        delegate* unmanaged[Cdecl]<byte*, int, ulong*, int*, byte*, int, int*, int*, int> load = &Bootstrap.LoadComponent;
        delegate* unmanaged[Cdecl]<ulong, byte*, int, int*, int*, int> release = &Bootstrap.ReleaseComponent;
        byte[] bytes = Encoding.UTF8.GetBytes(path);
        ulong token = 0;
        int collectible = 0, written = 0, truncated = 0;
        fixed (byte* pointer = bytes)
            Check(load(pointer, bytes.Length, &token, &collectible, null, 0, &written, &truncated) == 0 && token != 0 && collectible == 1,
                  "Unload fixture load failed");
        Check(State(token) == 0, "Live registration did not report NotRequested");
        Forget(token, 5); // A weak observer cannot be forgotten while it owns a live registration.
        Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Add").Value == 42, "Unload fixture invocation failed");
        Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Throw").Status == 3, "Unload fixture exception was not contained");
        Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Add").Value == 42, "Unload fixture recovery failed");
        Assembly assembly = AssemblyLoadContext.All.SelectMany(context => context.Assemblies).Single(item => item.Location == path);
        AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(assembly)!;
        Assembly dependency = context.Assemblies.Single(item => item.GetName().Name == "Fenrir.HostingDependency");
        var observation = new Observation(token, new WeakReference(assembly, true), new WeakReference(dependency, true));
        if (retain) retainedType = assembly.GetType("Fenrir.HostingComponent.ComponentMarker", throwOnError: true);
        Check(release(token, null, 0, &written, &truncated) == 0 && written == 0 && truncated == 0,
              "Unload request failed");
        Check(InvocationChecks.Invoke(token, InvocationChecks.TypeName, "Add").Status == 5, "Release did not invalidate invocation");
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
