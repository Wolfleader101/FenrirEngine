using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace Iris.Bridge;

// Internal calls: native functions that component code reaches through static fields of unmanaged
// function pointer type. The host registers named functions with their signatures (process-wide, before
// or after any component loads); each component load then binds every such field in the component
// assembly by its name, "Namespace.Type.Field", and fails with a diagnostic naming every field it could
// not bind. A registration holds only a name, kinds and a native address, never a managed reference, so
// it cannot keep a released component alive.
internal static class InternalCalls
{
    // Kinds an internal call signature can use: the sized numbers, bool (one byte) and char (two bytes), void
    // for a return, Pointer for any pointer (structs travel as a pointer), and String for a parameter only: a
    // native std::string_view, which the field declares as two parameters, a pointer to UTF-8 bytes and an Int32
    // length. A signature with bool or char needs [assembly: DisableRuntimeMarshalling] (see Bind), since
    // otherwise the runtime marshals them by its own rules.
    private sealed record Registration(string Name, int ReturnKind, int[] ParameterKinds, IntPtr Function);

    private static readonly object Gate = new();
    private static readonly Dictionary<string, Registration> Table = new();

    internal static bool IsValidKind(int kind, bool isReturn) =>
        kind == (int)ValueKind.Pointer || (isReturn ? kind == (int)ValueKind.Void : kind == (int)ValueKind.String) ||
        (kind >= (int)ValueKind.Int8 && kind <= (int)ValueKind.Double) || kind == (int)ValueKind.Bool || kind == (int)ValueKind.Char;

    // Registering the same name with the same signature and address again is a no-op, so a host can
    // register on every start-up; anything else under an existing name is an argument error.
    internal static void Register(string name, int returnKind, int[] parameterKinds, IntPtr function)
    {
        if (name.Length == 0 || name.Contains('\0')) throw new BridgeArgumentException("An internal call name must be non-empty and contain no null characters.");
        if (function == IntPtr.Zero) throw new BridgeArgumentException("An internal call needs a function.");
        if (!IsValidKind(returnKind, isReturn: true)) throw new BridgeArgumentException($"Unsupported internal call return kind {returnKind}.");
        foreach (int kind in parameterKinds)
            if (!IsValidKind(kind, isReturn: false)) throw new BridgeArgumentException($"Unsupported internal call parameter kind {kind}.");
        lock (Gate)
        {
            if (Table.TryGetValue(name, out Registration? existing))
            {
                if (existing.Function == function && existing.ReturnKind == returnKind && existing.ParameterKinds.AsSpan().SequenceEqual(parameterKinds)) return;
                throw new BridgeArgumentException($"The internal call '{name}' is already registered with a different function or signature.");
            }
            Table.Add(name, new Registration(name, returnKind, (int[])parameterKinds.Clone(), function));
        }
    }

    // Verification seam, never native-visible.
    internal static void Clear()
    {
        lock (Gate) Table.Clear();
    }

    // The wire kind of a managed function pointer type, or -1 when it has none.
    private static int KindOf(Type type)
    {
        if (type == typeof(void)) return (int)ValueKind.Void;
        if (type.IsPointer || type.IsFunctionPointer || type == typeof(IntPtr) || type == typeof(UIntPtr)) return (int)ValueKind.Pointer;
        return StructMapping.PrimitiveKindOf(type);
    }

    // The field parameters a registered signature's parameters take: one each, except a string, which is a pointer
    // and an Int32 length.
    private static IEnumerable<int> FieldKinds(IEnumerable<int> parameterKinds) =>
        parameterKinds.SelectMany(kind => kind == (int)ValueKind.String ? new[] { (int)ValueKind.Pointer, (int)ValueKind.Int32 } : new[] { kind });

    private static string Describe(int kind) => (ValueKind)kind switch
    {
        ValueKind.Void => "void",
        ValueKind.Pointer => "a pointer",
        ValueKind.String => "a string (a pointer, then an Int32 length)",
        _ => ValueKinds.ToType(kind, allowVoid: false).Name
    };

    // Binds every static function pointer field of the assembly's types to the registered function of the
    // same name. All problems are collected, and nothing is bound unless there are none.
    internal static void Bind(Assembly assembly)
    {
        var fields = new List<(FieldInfo Field, Registration Registration)>();
        var problems = new List<string>();
        Type[] types;
        try { types = assembly.GetTypes(); }
        catch (ReflectionTypeLoadException exception)
        {
            // A type that cannot load (a missing dependency, say) cannot be used either, so it holds no live
            // binding; like any use of it, the failure is reported when something touches it.
            types = Array.FindAll(exception.Types, type => type != null)!;
        }
        // Types without a static constructor first: binding a field runs its type's static constructor, and one
        // that calls an internal call must find the others already bound.
        Array.Sort(types, (left, right) => (left.TypeInitializer != null).CompareTo(right.TypeInitializer != null));
        foreach (Type type in types)
        {
            // Only public static fields are bindings; private function pointers are the component's own.
            foreach (FieldInfo field in type.GetFields(BindingFlags.Static | BindingFlags.Public | BindingFlags.DeclaredOnly))
            {
                if (!field.FieldType.IsFunctionPointer || !field.FieldType.IsUnmanagedFunctionPointer) continue;
                string name = $"{type.FullName}.{field.Name}";
                if (type.ContainsGenericParameters) { problems.Add($"{name}: a generic type cannot hold an internal call (each instantiation has its own copy)."); continue; }
                if (field.IsInitOnly || field.IsLiteral) { problems.Add($"{name}: must not be readonly."); continue; }
                if (field.IsDefined(typeof(ThreadStaticAttribute), inherit: false)) { problems.Add($"{name}: must not be thread-static."); continue; }
                Type[] conventions = field.FieldType.GetFunctionPointerCallingConventions();
                Type[] unsupported = Array.FindAll(conventions, convention => convention != typeof(System.Runtime.CompilerServices.CallConvCdecl));
                if (unsupported.Length > 0)
                { problems.Add($"{name}: only the Cdecl calling convention is supported, not {string.Join(", ", unsupported.Select(convention => convention.Name))}."); continue; }
                Registration? registration;
                lock (Gate) Table.TryGetValue(name, out registration);
                if (registration == null) { problems.Add($"{name}: no internal call is registered under this name."); continue; }
                Type[] parameters = field.FieldType.GetFunctionPointerParameterTypes();
                int returnKind = KindOf(field.FieldType.GetFunctionPointerReturnType());
                if (returnKind != registration.ReturnKind || !parameters.Select(KindOf).SequenceEqual(FieldKinds(registration.ParameterKinds)))
                {
                    problems.Add($"{name}: the field's signature ({Signature(returnKind, parameters.Select(KindOf))}) does not match the registered one ({Signature(registration.ReturnKind, registration.ParameterKinds)}).");
                    continue;
                }
                if ((registration.ReturnKind is (int)ValueKind.Bool or (int)ValueKind.Char || registration.ParameterKinds.Any(kind => kind is (int)ValueKind.Bool or (int)ValueKind.Char)) &&
                    !assembly.IsDefined(typeof(System.Runtime.CompilerServices.DisableRuntimeMarshallingAttribute)))
                {
                    problems.Add($"{name}: uses bool or char, whose unmanaged size is only fixed when the assembly has [assembly: DisableRuntimeMarshalling].");
                    continue;
                }
                fields.Add((field, registration));
            }
        }
        if (problems.Count > 0)
            throw new BridgeArgumentException($"{problems.Count} internal call(s) could not be bound:\n  " + string.Join("\n  ", problems));
        // Setting a field runs its type's static constructor, which is component code and can throw.
        foreach ((FieldInfo field, Registration registration) in fields)
        {
            try { field.SetValue(null, registration.Function); }
            catch (Exception exception)
            {
                throw new BridgeArgumentException($"{field.DeclaringType!.FullName}.{field.Name} could not be bound: {(exception is TypeInitializationException or TargetInvocationException ? exception.InnerException ?? exception : exception).Message}");
            }
        }
    }

    private static string Signature(int returnKind, IEnumerable<int> parameters) =>
        $"{(returnKind < 0 ? "unsupported" : Describe(returnKind))}({string.Join(", ", parameters.Select(kind => kind < 0 ? "unsupported" : Describe(kind)))})";
}
