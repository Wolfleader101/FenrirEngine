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
        Check(ComponentRegistry.TryCreateArray(token, ((int)ValueKind.Int32 << 8) | (int)ValueKind.Array, 8, out ulong arrayId) && arrayId != 0, "Unload fixture array creation failed");
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


    // Everything one version of the component has handed out, kept as plain ids so nothing here roots it.
    private sealed record Handles(ulong Token, ulong Type, ulong Method, ulong Member, ulong Object, ulong Array, ulong StructId, Observation Observation);

    private static string? Take(ulong block)
    {
        if (block == 0) return null;
        unsafe
        {
            string text = new UTF8Encoding(false, true).GetString((byte*)block + 8, checked((int)*(long*)block));
            ValueCodec.FreeString(block);
            return text;
        }
    }

    // Loads one version, uses every kind of API feature and returns the ids it handed out; the component is still live.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static unsafe Handles LoadUse(string path, int number, string name)
    {
        ulong token = ComponentRegistry.Load(path);
        const string ns = "Iris.Examples.Component.";
        ulong version = 0, numberMethod = 0, nameMethod = 0, scaleMethod = 0;
        Check(ComponentRegistry.TryFindType(token, ns + "ComponentVersion", out version) == ComponentRegistry.FindResult.Found, "Version type missing");
        Check(ComponentRegistry.TryFindMethod(token, version, "Number", 6, Array.Empty<int>(), true, out numberMethod) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryInvokeMethod(token, numberMethod, ReadOnlySpan<ulong>.Empty, out ulong observed) && (int)observed == number,
              "The loaded version was not " + name);
        Check(ComponentRegistry.TryFindMethod(token, version, "Name", 13, Array.Empty<int>(), true, out nameMethod) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryInvokeMethod(token, nameMethod, ReadOnlySpan<ulong>.Empty, out ulong block) && Take(block) == name, "A string result was wrong for " + name);
        Check(ComponentRegistry.TryFindMethod(token, version, "Scale", 6, new[] { 6 }, true, out scaleMethod) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryInvokeMethod(token, scaleMethod, new ulong[] { 3 }, out ulong scaled) && (int)scaled == (number == 1 ? 6 : 30), "Version behaviour differed from expectation");
        // Only the loaded version has its own type.
        Check(ComponentRegistry.TryFindType(token, ns + (number == 1 ? "OnlyInA" : "OnlyInB"), out _) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryFindType(token, ns + (number == 1 ? "OnlyInB" : "OnlyInA"), out _) == ComponentRegistry.FindResult.NotFound, "The other version's type was visible");

        // Fields, objects, arrays, structs, strings, internal calls, attributes and listing.
        ComponentRegistry.TryFindType(token, ns + "Settings", out ulong settings);
        Check(ComponentRegistry.TryFindMember(token, settings, "StaticCount", 0, 6, out ulong member) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TrySetMemberValue(token, member, 0, 7) && ComponentRegistry.TryGetMemberValue(token, member, 0, out ulong memberValue) && memberValue == 7, "Member use failed");
        ComponentRegistry.TryFindType(token, ns + "Counter", out ulong counter);
        Check(ComponentRegistry.TryCreateObject(token, counter, Array.Empty<int>(), ReadOnlySpan<ulong>.Empty, out ulong obj) == ComponentRegistry.CreateResult.Created, "Object creation failed");
        Check(ComponentRegistry.TryCreateArray(token, ((int)ValueKind.Int32 << 8) | (int)ValueKind.Array, 4, out ulong array) && ComponentRegistry.TrySetArrayElement(token, array, ((int)ValueKind.Int32 << 8) | (int)ValueKind.Array, 1, 5), "Array use failed");
        Check(ComponentRegistry.TryRegisterStruct(token, $"{ns}Vec3|12\nX|0|10|\nY|4|10|\nZ|8|10|", out ulong structId) == ComponentRegistry.FindResult.Found, "Struct registration failed");
        ComponentRegistry.TryFindType(token, ns + "Strings", out ulong strings);
        Check(ComponentRegistry.TryFindMethod(token, strings, "Greeting", 13, Array.Empty<int>(), true, out ulong greeting) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryInvokeMethod(token, greeting, ReadOnlySpan<ulong>.Empty, out ulong greetingBlock) && Take(greetingBlock) != null, "String use failed");
        ComponentRegistry.TryFindType(token, ns + "Natives", out ulong natives);
        Check(ComponentRegistry.TryFindMethod(token, natives, "CallAdd", 6, new[] { 6, 6 }, true, out ulong callAdd) == ComponentRegistry.FindResult.Found &&
              ComponentRegistry.TryInvokeMethod(token, callAdd, new ulong[] { 2, 3 }, out ulong sum) && sum == 5, "Internal call use failed");
        ComponentRegistry.TryFindType(token, ns + "Player", out ulong player);
        foreach (TypeQueries.Query query in new[] { TypeQueries.Query.Base, TypeQueries.Query.Interfaces, TypeQueries.Query.Attributes })
            Check(ComponentRegistry.TryQueryType(token, player, query, out byte[] snapshot) && snapshot.Length > 0, "A type query failed");
        Check(ComponentRegistry.TryListTypes(token, out ulong[] listed) && listed.Length > 10, "Listing failed");
        Check(ComponentRegistry.TryIsAssignable(token, player, 0, "System.IDisposable") == ComponentRegistry.AssignResult.Assignable, "Assignability failed");

        Assembly assembly = AssemblyLoadContext.All.SelectMany(context => context.Assemblies).Single(item => item.Location == path);
        AssemblyLoadContext loadContext = AssemblyLoadContext.GetLoadContext(assembly)!;
        Assembly dependency = loadContext.Assemblies.Single(item => item.GetName().Name == "Iris.Examples.Dependency");
        var observation = new Observation(token, new WeakReference(assembly, true), new WeakReference(dependency, true));
        GC.KeepAlive(loadContext);
        return new Handles(token, version, numberMethod, member, obj, array, structId, observation);
    }

    // Releases arrays and objects first (a live one refuses release), then the component.
    private static void ReleaseAll(Handles handles)
    {
        Check(ComponentRegistry.TryRelease(handles.Token, force: false) == ComponentRegistry.ReleaseResult.InUse, "Live objects did not block release");
        Check(ComponentRegistry.ReleaseObject(handles.Token, handles.Object) && ComponentRegistry.ReleaseObject(handles.Token, handles.Array), "Releasing objects failed");
        Check(ComponentRegistry.Release(handles.Token), "Release failed");
    }

    private static Handles LoadUseRelease(string path, int number, string name)
    {
        Handles handles = LoadUse(path, number, name);
        ReleaseAll(handles);
        return handles;
    }

    // Every id the old version handed out is dead, and none of them selects anything in the new one.
    private static void CheckStale(Handles old, ulong newToken)
    {
        Check(newToken != old.Token, "A token was reused");
        Check(!ComponentRegistry.TryGetTypeName(old.Token, old.Type, 0, out _), "A released component's type handle worked");
        Check(!ComponentRegistry.TryGetTypeName(newToken, old.Type, 0, out _), "An old type handle selected a type of the new version");
        Check(!ComponentRegistry.TryInvokeMethod(old.Token, old.Method, ReadOnlySpan<ulong>.Empty, out _) &&
              !ComponentRegistry.TryInvokeMethod(newToken, old.Method, ReadOnlySpan<ulong>.Empty, out _), "An old method handle worked");
        Check(!ComponentRegistry.TryGetMemberValue(old.Token, old.Member, 0, out _) && !ComponentRegistry.TryGetMemberValue(newToken, old.Member, 0, out _), "An old member handle worked");
        Check(!ComponentRegistry.ReleaseObject(old.Token, old.Object) && !ComponentRegistry.ReleaseObject(newToken, old.Object), "An old object handle worked");
        Check(!ComponentRegistry.TryGetArrayLength(old.Token, old.Array, out _) && !ComponentRegistry.TryGetArrayLength(newToken, old.Array, out _), "An old array handle worked");
        Check(!ComponentRegistry.TryListTypes(old.Token, out _) && ComponentRegistry.TryRegisterStruct(old.Token, "X|1\nA|0|2|", out _) == ComponentRegistry.FindResult.InvalidHandle, "A released component still answered");
    }

    // Positive controls: something the host holds keeps the released component Pending until it is cleared.
    private static object? heldObject;

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static Observation LoadAndHold(string path, bool holdArray)
    {
        Handles handles = LoadUse(path, 1, "A");
        ulong heldId = handles.Object;
        if (holdArray)
        {
            // An int[] references no component type, so it could not pin anything; an array of the component's own struct does.
            Check(ComponentRegistry.TryCreateArray(handles.Token, ValueKinds.ArrayKindOfStruct(handles.StructId), 2, out heldId), "Struct array creation failed");
        }
        heldObject = ComponentRegistry.PeekObject(handles.Token, heldId);
        Check(heldObject != null, "The live object could not be observed");
        if (holdArray) Check(ComponentRegistry.ReleaseObject(handles.Token, heldId), "Releasing the struct array failed");
        ReleaseAll(handles);
        return handles.Observation;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void DropHeldObject() => heldObject = null;

    // A load that fails after the context and assembly exist (the internal calls are unbound) roots nothing.
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static bool AssemblyStillLoaded(string path) =>
        AssemblyLoadContext.All.SelectMany(context => context.Assemblies).Any(item => item.Location == path);

    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void FailLoadAfterContextCreated(string path)
    {
        InternalCalls.Clear();
        try { ComponentRegistry.Load(path); throw new Exception("Loading with unbound internal calls succeeded"); }
        catch (BridgeArgumentException) { }
        finally { InternalCallChecks.RegisterAll(); }
    }

    internal static void RunReload(string pathA, string pathB)
    {
        int baseline = ComponentRegistry.Count, trackingBaseline = ComponentRegistry.TrackingCount;
        for (int cycle = 0; cycle < 4; ++cycle)
        {
            if (cycle % 2 == 0)
            {
                // B loads only after A has been collected.
                Handles a = LoadUseRelease(pathA, 1, "A");
                WaitForCollection(a.Observation);
                Forget(a.Token);
                CheckReplacedBy(a, pathB, aAlreadyCollected: true);
            }
            else
            {
                // B loads while A is merely released (Pending).
                CheckReplacedBy(LoadUseRelease(pathA, 1, "A"), pathB, aAlreadyCollected: false);
            }
        }
        Console.WriteLine("Managed reload: four A/B replacements (B after A collected, and B while A pending) observed the new behaviour, killed every old handle against a live B, and collected each version.");

        // A load that fails after the context exists leaves no roots, and the host loads the next version.
        int tracking = ComponentRegistry.TrackingCount, count = ComponentRegistry.Count;
        FailLoadAfterContextCreated(pathA);
        Check(ComponentRegistry.TrackingCount == tracking && ComponentRegistry.Count == count, "A failed load left a registration");
        for (int cycle = 0; cycle < 10 && AssemblyStillLoaded(pathA); ++cycle) CollectCycle();
        Check(!AssemblyStillLoaded(pathA), "A failed load left its assembly and context alive");
        // A file that does not exist fails earlier still.
        bool failed = false;
        try { ComponentRegistry.Load(Path.Combine(Path.GetDirectoryName(pathB)!, "Missing.Component.dll")); }
        catch (Exception) { failed = true; }
        Check(failed && ComponentRegistry.TrackingCount == tracking, "Loading a missing assembly did not fail cleanly");
        Handles retry = LoadUseRelease(pathB, 2, "B");
        WaitForCollection(retry.Observation);
        Forget(retry.Token);

        // Positive controls: an object or array the host still holds keeps the released component Pending until it lets go.
        foreach (bool array in new[] { false, true })
        {
            Observation held = LoadAndHold(pathA, array);
            try
            {
                for (int cycle = 0; cycle < 3; ++cycle) CollectCycle();
                Check(State(held.Token) == 1 && held.Assembly.IsAlive && held.Dependency.IsAlive, $"A held {(array ? "array" : "object")} did not keep unloading Pending");
            }
            finally { DropHeldObject(); }
            WaitForCollection(held);
            Forget(held.Token);
        }
        Check(ComponentRegistry.Count == baseline && ComponentRegistry.TrackingCount == trackingBaseline, "Reload checks leaked registrations");
        Console.WriteLine("Managed reload: a load failing after its context exists left no roots; a held object or array keeps unloading Pending until cleared.");
    }

    // Loads B while checking A's ids against B's live token, then releases and collects B (and A if needed).
    [MethodImpl(MethodImplOptions.NoInlining)]
    private static void CheckReplacedBy(Handles a, string pathB, bool aAlreadyCollected)
    {
        Handles b = LoadUse(pathB, 2, "B");
        CheckStale(a, b.Token);
        // B's own ids are newer than every id A handed out, so an old id can never alias a new one.
        Check(b.Type > a.Type && b.Method > a.Method && b.Member > a.Member && b.Object > a.Object && b.Array > a.Array, "Ids were reused across versions");
        ReleaseAll(b);
        CheckStale(b, a.Token);
        if (!aAlreadyCollected) WaitForCollection(a.Observation);
        WaitForCollection(b.Observation);
        if (!aAlreadyCollected) Forget(a.Token);
        Forget(b.Token);
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

        string? versionB = Environment.GetEnvironmentVariable("IRIS_TEST_COMPONENT_B");
        Check(!string.IsNullOrEmpty(versionB) && File.Exists(versionB), "IRIS_TEST_COMPONENT_B does not name the second component build");
        RunReload(path, Path.GetFullPath(versionB!));
    }
}
