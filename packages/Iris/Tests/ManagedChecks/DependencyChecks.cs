using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;
using Iris.Bridge;

static class DependencyChecks
{
    private const string ComponentType = "Iris.Examples.Component.ComponentMarker";
    private const string DependencyName = "Iris.Examples.Dependency";

    private static void Check(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    private static void Release(ulong token)
    {
        Check(ComponentRegistry.Release(token), "Dependency component release failed");
        Check(ComponentRegistry.ForgetUnload(token), "Dependency unload observation cleanup failed");
    }

    private static string Deploy(string componentPath, string directory, bool includeDependency)
    {
        Directory.CreateDirectory(directory);
        string sourceDirectory = Path.GetDirectoryName(componentPath)!;
        string fileName = Path.GetFileName(componentPath);
        File.Copy(componentPath, Path.Combine(directory, fileName));
        string manifestName = Path.ChangeExtension(fileName, ".deps.json");
        File.Copy(Path.Combine(sourceDirectory, manifestName), Path.Combine(directory, manifestName));
        if (includeDependency)
            File.Copy(Path.Combine(sourceDirectory, DependencyName + ".dll"), Path.Combine(directory, DependencyName + ".dll"));
        return Path.Combine(directory, fileName);
    }

    private static Assembly FindComponent(string path)
    {
        // Inspect the real registry-loaded assembly through runtime APIs. Unique staging
        // paths distinguish contexts without exposing registry entries or CLR objects in the ABI.
        return AssemblyLoadContext.All.SelectMany(context => context.Assemblies)
            .Single(assembly => assembly.Location == path);
    }

    private static Assembly CheckResolution(string path)
    {
        Assembly component = FindComponent(path);
        AssemblyLoadContext context = AssemblyLoadContext.GetLoadContext(component)!;
        Check(context.IsCollectible && context != AssemblyLoadContext.Default, "Component is not isolated");
        Type marker = component.GetType(ComponentType, throwOnError: true)!;
        Assembly dependency = marker.BaseType!.Assembly;
        Check(dependency.GetName().Name == DependencyName && dependency.IsCollectible &&
              AssemblyLoadContext.GetLoadContext(dependency) == context &&
              dependency.Location == Path.Combine(Path.GetDirectoryName(path)!, DependencyName + ".dll"),
              "Private dependency resolved outside its component context/deployment");
        Check(!AssemblyLoadContext.Default.Assemblies.Any(assembly => assembly.GetName().Name == DependencyName),
              "Private dependency polluted the default context");

        Assembly bridge = typeof(Bootstrap).Assembly;
        AssemblyName bridgeReference = component.GetReferencedAssemblies().Single(name => name.Name == bridge.GetName().Name);
        Check(ReferenceEquals(context.LoadFromAssemblyName(bridgeReference), bridge) && !bridge.IsCollectible &&
              !context.Assemblies.Any(assembly => assembly.GetName().Name == bridgeReference.Name),
              "Component loaded a separate bridge assembly");
        var incompatibleBridge = new AssemblyName(bridge.FullName!)
        {
            Version = new Version(bridge.GetName().Version!.Major + 1, 0, 0, 0)
        };
        var differentCulture = new AssemblyName(bridge.FullName!) { CultureName = "fr" };
        var differentKey = new AssemblyName(bridge.FullName!);
        differentKey.SetPublicKeyToken(new byte[] { 1, 2, 3, 4, 5, 6, 7, 8 });
        foreach (AssemblyName incompatible in new[] { incompatibleBridge, differentCulture, differentKey })
        {
            try
            {
                _ = context.LoadFromAssemblyName(incompatible);
                throw new Exception("An incompatible bridge reference accepted the resident assembly");
            }
            catch (FileLoadException exception)
            {
                Check(exception.ToString().Contains("cannot satisfy reference", StringComparison.Ordinal),
                      "Incompatible bridge failure omitted the loader's compatibility diagnostic");
            }
        }
        Check(ReferenceEquals(context.LoadFromAssemblyName(typeof(object).Assembly.GetName()), typeof(object).Assembly),
              "Framework fallback did not share CoreLib");
        return dependency;
    }

    internal static void Run(string componentPath)
    {
        componentPath = Path.GetFullPath(componentPath);
        string staging = Path.Combine(Path.GetTempPath(), "Iris-dependencies-\u03C0-" + Guid.NewGuid().ToString("N"));
        int baseline = ComponentRegistry.Count;
        try
        {
            string first = Deploy(componentPath, Path.Combine(staging, "first"), true);
            string second = Deploy(componentPath, Path.Combine(staging, "second"), true);
            // Include a redundant bridge copy to prove the sharing override takes precedence.
            File.Copy(typeof(Bootstrap).Assembly.Location, Path.Combine(Path.GetDirectoryName(first)!, "Iris.Bridge.dll"));
            ulong firstToken = ComponentRegistry.Load(first);
            try
            {
                Assembly firstDependency = CheckResolution(first);
                ulong secondToken = ComponentRegistry.Load(second);
                try
                {
                    Assembly secondDependency = CheckResolution(second);
                    Check(!ReferenceEquals(firstDependency, secondDependency) &&
                          AssemblyLoadContext.GetLoadContext(firstDependency) != AssemblyLoadContext.GetLoadContext(secondDependency),
                          "Separate components shared a private dependency instance");
                }
                finally { Release(secondToken); }
            }
            finally { Release(firstToken); }
            Check(ComponentRegistry.Count == baseline, "Complete deployments left registrations");

            string missing = Deploy(componentPath, Path.Combine(staging, "missing"), false);
            ulong missingToken = ComponentRegistry.Load(missing);
            try
            {
                Check(ComponentRegistry.Count == baseline + 1, "Initial lazy load did not register the component");
                try
                {
                    _ = FindComponent(missing).GetType(ComponentType, throwOnError: true);
                    throw new Exception("Missing private dependency unexpectedly resolved");
                }
                catch (FileNotFoundException exception)
                {
                    Check(exception.FileName?.Contains(DependencyName, StringComparison.Ordinal) == true,
                          "Failure did not identify the missing private dependency");
                }
                var failedCall = InvocationChecks.Invoke(missingToken, InvocationChecks.TypeName, "Add");
                Check(failedCall.Status == 3 && failedCall.Value == 0 && failedCall.Diagnostic.Contains("FileNotFoundException") &&
                      failedCall.Diagnostic.Contains(DependencyName), "Missing dependency escaped the invocation ABI or lost diagnostics");
                var localCall = InvocationChecks.Invoke(missingToken, InvocationChecks.TypeName, "LocalAdd");
                Check(localCall.Status == 0 && localCall.Value == 42 && localCall.Diagnostic == "",
                      "Dependency failure invalidated an otherwise usable component");
            }
            finally { Release(missingToken); }
            Check(ComponentRegistry.Count == baseline, "Failed resolution retained a registry root after release");

            string recovery = Deploy(componentPath, Path.Combine(staging, "recovery"), true);
            ulong recoveryToken = ComponentRegistry.Load(recovery);
            try { _ = CheckResolution(recovery); }
            finally { Release(recoveryToken); }
            Check(ComponentRegistry.Count == baseline, "Recovery retained a registry root");
        }
        finally
        {
            // Unload is cooperative. Loaded files can remain locked until collection,
            // so do not turn temp-file cleanup into an implicit unload-completion check.
            try { Directory.Delete(staging, recursive: true); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        Console.WriteLine("Managed dependencies: private isolation, shared bridge/framework identity, missing dependency and recovery passed.");
    }
}
