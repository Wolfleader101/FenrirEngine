using System;
using System.Reflection;
using System.Runtime.InteropServices;

namespace Iris.Bridge;

internal static class ComponentInvocation
{
    internal static int InvokeInt32(Assembly assembly, string typeName, string methodName, int left, int right)
    {
        Type type = assembly.GetType(typeName, throwOnError: true, ignoreCase: false)!;
        if (type.Assembly != assembly || !type.IsVisible || type.ContainsGenericParameters)
            throw new MissingMethodException($"Type '{typeName}' must be public, closed, and defined in the component assembly.");

        MethodInfo? match = null;
        foreach (MethodInfo method in type.GetMethods(BindingFlags.Public | BindingFlags.Static | BindingFlags.DeclaredOnly))
        {
            if (method.Name != methodName || method.IsGenericMethod || method.ReturnType != typeof(int) ||
                (method.CallingConvention & CallingConventions.VarArgs) != 0 ||
                method.IsDefined(typeof(UnmanagedCallersOnlyAttribute), inherit: false)) continue;
            ParameterInfo[] parameters = method.GetParameters();
            if (parameters.Length != 2 || parameters[0].ParameterType != typeof(int) || parameters[1].ParameterType != typeof(int)) continue;
            if (match != null) throw new AmbiguousMatchException($"Multiple matching methods '{typeName}.{methodName}'.");
            match = method;
        }
        if (match == null)
            throw new MissingMethodException($"No public static non-generic int {typeName}.{methodName}(int, int) declared in the component; UnmanagedCallersOnly methods are excluded.");

        // A typed managed call avoids boxing/conversions and TargetInvocationException.
        // No MethodInfo or delegate is kept in a resident/static cache.
        return match.CreateDelegate<Func<int, int, int>>()(left, right);
    }
}
