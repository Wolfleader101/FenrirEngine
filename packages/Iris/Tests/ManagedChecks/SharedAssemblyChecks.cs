using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using Iris.Bridge;

// Runs after the other groups: sharing is process-wide and permanent.
static class SharedAssemblyChecks
{
    private const string ContractName = "Iris.Examples.Contracts";
    private const string CalculatorType = "Iris.Examples.Component.SharedCalculator";

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    // The contract type as seen by the component loaded since `known` was taken. Contexts from earlier
    // checks may still be waiting for collection, so the new one is identified by being new.
    private static Type ContractTypeSeenBy(string componentPath, HashSet<AssemblyLoadContext> known, out AssemblyLoadContext context)
    {
        context = AssemblyLoadContext.All.Single(candidate => !known.Contains(candidate) &&
            candidate.Assemblies.Any(assembly => assembly.Location == componentPath));
        known.Add(context);
        Assembly component = context.Assemblies.Single(assembly => assembly.Location == componentPath);
        return component.GetType(CalculatorType, throwOnError: true)!.BaseType!;
    }

    internal static void Run(string componentPath)
    {
        string path = Path.GetFullPath(componentPath);
        string contract = Path.Combine(Path.GetDirectoryName(path)!, ContractName + ".dll");

        SharedAssemblies.Register(contract);
        SharedAssemblies.Register(contract); // The same file again is a no-op.

        var known = new HashSet<AssemblyLoadContext>(AssemblyLoadContext.All);
        ulong first = ComponentRegistry.Load(path);
        Type firstContract = ContractTypeSeenBy(path, known, out AssemblyLoadContext firstContext);
        ulong second = ComponentRegistry.Load(path);
        Type secondContract = ContractTypeSeenBy(path, known, out AssemblyLoadContext secondContext);
        try
        {
            Check(firstContext != secondContext, "The two loads shared a component context");
            Check(ReferenceEquals(firstContract, secondContract), "Components saw different contract types");
            Check(AssemblyLoadContext.GetLoadContext(firstContract.Assembly) == AssemblyLoadContext.Default,
                  "The shared contract is not in the default context");
            Check(!firstContext.Assemblies.Concat(secondContext.Assemblies).Any(assembly => assembly.GetName().Name == ContractName),
                  "A component loaded a private copy of the shared contract");
        }
        finally
        {
            Check(ComponentRegistry.Release(first) && ComponentRegistry.ForgetUnload(first), "First release failed");
            Check(ComponentRegistry.Release(second) && ComponentRegistry.ForgetUnload(second), "Second release failed");
        }

        // A different file under a shared name, and a shared assembly loaded as a component, are rejected.
        string copy = Path.Combine(Path.GetTempPath(), "Iris-shared-" + Guid.NewGuid().ToString("N"), ContractName + ".dll");
        Directory.CreateDirectory(Path.GetDirectoryName(copy)!);
        File.Copy(contract, copy);
        try
        {
            bool rejected = false;
            try { SharedAssemblies.Register(copy); }
            catch (BridgeArgumentException) { rejected = true; }
            Check(rejected, "A second file under a shared name was accepted");
        }
        finally { Directory.Delete(Path.GetDirectoryName(copy)!, recursive: true); }

        // References the shared copy cannot satisfy fail instead of binding to it.
        foreach (string reference in new[]
        {
            ContractName + ", Version=99.0.0.0",
            ContractName + ", Culture=fr",
            ContractName + ", PublicKeyToken=b77a5c561934e089"
        })
        {
            bool mismatchRejected = false;
            try { SharedAssemblies.Resolve(new AssemblyName(reference)); }
            catch (FileLoadException) { mismatchRejected = true; }
            Check(mismatchRejected, "An incompatible reference bound to the shared copy: " + reference);
        }
        Check(SharedAssemblies.Resolve(new AssemblyName(ContractName)) == firstContract.Assembly,
              "A compatible reference did not bind to the shared copy");

        bool componentRejected = false;
        try { ComponentRegistry.Load(contract); }
        catch (BridgeArgumentException) { componentRejected = true; }
        Check(componentRejected, "A shared assembly was loaded as a component");
        Console.WriteLine("Shared assemblies: one contract copy and type identity across components; conflicting names rejected.");
    }
}
