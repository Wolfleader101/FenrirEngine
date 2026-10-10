using System;
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

    private static Type ContractTypeSeenBy(string componentPath, out AssemblyLoadContext context)
    {
        Assembly component = AssemblyLoadContext.All.SelectMany(candidate => candidate.Assemblies)
            .Last(assembly => assembly.Location == componentPath);
        context = AssemblyLoadContext.GetLoadContext(component)!;
        return component.GetType(CalculatorType, throwOnError: true)!.BaseType!;
    }

    internal static void Run(string componentPath)
    {
        string path = Path.GetFullPath(componentPath);
        string contract = Path.Combine(Path.GetDirectoryName(path)!, ContractName + ".dll");

        SharedAssemblies.Register(contract);
        SharedAssemblies.Register(contract); // The same file again is a no-op.

        ulong first = ComponentRegistry.Load(path);
        Type firstContract = ContractTypeSeenBy(path, out AssemblyLoadContext firstContext);
        ulong second = ComponentRegistry.Load(path);
        Type secondContract = ContractTypeSeenBy(path, out AssemblyLoadContext secondContext);
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

        bool componentRejected = false;
        try { ComponentRegistry.Load(contract); }
        catch (BridgeArgumentException) { componentRejected = true; }
        Check(componentRejected, "A shared assembly was loaded as a component");
        Console.WriteLine("Shared assemblies: one contract copy and type identity across components; conflicting names rejected.");
    }
}
