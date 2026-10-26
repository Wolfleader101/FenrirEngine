using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Fenrir.Scripting.Generators;

/// <summary>
/// Writes the body of every <c>[NativeFunction]</c> method, and the unmanaged function pointer field the host binds for it
/// (<c>__native_Method</c> in the same type, the binding-name rule FenrirScripting's <c>NativeFunction::BindingNameFor</c>
/// also implements). The body turns the arguments into what crosses (a string into UTF-8 bytes and a length, an enum into
/// its underlying integer, a wrapper such as <c>Entity</c> into its one member, an <c>in</c>/<c>ref</c>/<c>out</c> struct
/// into a pointer), calls the field, and turns its result back.
/// </summary>
/// <remarks>
/// A wrapper is an unmanaged struct whose only instance field is a field, or an auto-property's backing field, of a
/// number, bool, char, pointer or enum, which the declaring type can read. A wrapper result is built with a constructor
/// taking that member's type if the declaring type can call one, and otherwise with an object initializer setting the
/// member if it can set it; a wrapper with neither is refused as a result.
/// </remarks>
/// <remarks>
/// A declaration it refuses still gets a body (one that throws), so the build reports the FENRIR error and not also a
/// partial method without an implementation. The exception is a native function inside a type that is not partial, where
/// nothing can be written.
///
/// One part is written for each file a type's native functions are declared in, with that file's <c>using</c> directives,
/// so a signature copied from a file means what it means there even when another file of the same type says otherwise.
/// </remarks>
[Generator]
public sealed class NativeFunctionGenerator : IIncrementalGenerator
{
    private const string AttributeName = "Fenrir.Scripting.NativeFunctionAttribute";
    private const string FieldPrefix = "__native_";

    // Text up to this many UTF-8 bytes is encoded on the stack (as Log does), longer text into a new array.
    private const int StackBytes = 512;

    // RefKind.RefReadOnlyParameter in newer versions of Roslyn than the one this generator builds against.
    private const RefKind RefReadOnlyParameter = (RefKind)4;

    private static readonly DiagnosticDescriptor NotStatic = Error("FENRIR0020", "A native function must be static",
        "Native function '{0}' must be static");
    private static readonly DiagnosticDescriptor NotPartial = Error("FENRIR0021", "A native function must be a partial declaration",
        "Native function '{0}' must be declared 'static partial' with no body: the generator writes the body");
    private static readonly DiagnosticDescriptor ContainerNotPartial = Error("FENRIR0022", "A native function's types must be partial",
        "Native function '{0}' is inside '{1}', which must be partial so the generator can add to it");
    private static readonly DiagnosticDescriptor Generic = Error("FENRIR0023", "A native function cannot be generic",
        "Native function '{0}' must not be generic or inside a generic type: one C++ function cannot serve every instantiation");
    private static readonly DiagnosticDescriptor Overloaded = Error("FENRIR0024", "A native function cannot be overloaded",
        "Native function '{0}' is declared twice in its type: a native function is named by its type and method name alone");
    private static readonly DiagnosticDescriptor StringResult = Error("FENRIR0025", "A native function cannot return a string",
        "Native function '{0}' returns a string, which cannot cross from C++: return a length and fill a buffer instead");
    private static readonly DiagnosticDescriptor Unsupported = Error("FENRIR0026", "A type cannot cross to a native function",
        "Native function '{0}' uses '{1}' ({2}), which cannot cross: use a number, bool, char, pointer, enum, a wrapper (a struct of one such field or auto-property, as Entity is), a string parameter, or an in/ref/out unmanaged struct");

    private static DiagnosticDescriptor Error(string id, string title, string message) =>
        new(id, title, message, "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    // Where a declaration's generated code goes: a part of its type, in a file of its own, opened the way the declaration
    // opened it. Plain data, so an unchanged declaration is not regenerated.
    private sealed record Part(string TypeKey, string FilePath, string Namespace, string Containers, int Depth,
        string FileUsings, string NamespaceUsings);

    // What one [NativeFunction] declaration contributes: its field and body, a body that throws in their place if it was
    // refused, or nothing at all if even that could not be written.
    private sealed record Model(Part Part, string Name, string Display, LocationInfo At, string? Field, string? Implementation,
        string? Stub, DiagnosticInfo? Refusal);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        IncrementalValuesProvider<Model> functions = context.SyntaxProvider
            .ForAttributeWithMetadataName(AttributeName,
                static (node, _) => node is MethodDeclarationSyntax,
                static (attributed, cancellation) => Describe(attributed, cancellation))
            .Where(static model => model is not null)!;

        context.RegisterSourceOutput(functions.Collect(), static (output, models) =>
        {
            foreach (var type in models.GroupBy(static model => model.Part.TypeKey))
            {
                // Overloads are found across every file of the type; the code is written file by file.
                var written = new List<(Part Part, string? Field, string Body)>();
                var named = new HashSet<string>(StringComparer.Ordinal);
                foreach (Model model in type)
                {
                    DiagnosticInfo? problem = model.Refusal;
                    if (!named.Add(model.Name) && problem == null)
                        problem = Refuse(Overloaded, model.At, model.Display);
                    if (problem != null)
                        output.ReportDiagnostic(problem.ToDiagnostic());
                    if (problem == null && model.Implementation != null)
                        written.Add((model.Part, model.Field, model.Implementation));
                    else if (model.Stub != null)
                        written.Add((model.Part, null, model.Stub));
                }
                foreach (var file in written.GroupBy(static member => member.Part))
                    output.AddSource(HintName(file.Key), Source(file.Key, file.Select(static member => (member.Field, member.Body))));
            }
        });
    }

    private static string Source(Part part, IEnumerable<(string? Field, string Body)> members)
    {
        var source = new StringBuilder();
        source.Append("// <auto-generated>Fenrir.Scripting.Generators: native function bodies and the fields the host binds.</auto-generated>\n");
        source.Append("#pragma warning disable CS0649 // The host assigns the fields when it shares this assembly.\n");
        // The declarations' own using directives, so the copied signatures mean what they mean there.
        AppendLines(source, part.FileUsings);
        if (part.Namespace.Length > 0)
            source.Append("namespace ").Append(part.Namespace).Append("\n{\n");
        AppendLines(source, part.NamespaceUsings);
        source.Append(part.Containers).Append("{\n");
        foreach ((string? field, string body) in members)
        {
            if (field != null)
                source.Append("    ").Append(field).Append('\n');
            source.Append("    ").Append(body.Replace("\n", "\n    ")).Append("\n\n");
        }
        source.Append(new string('}', part.Depth)).Append('\n');
        if (part.Namespace.Length > 0)
            source.Append("}\n");
        return source.ToString();
    }

    private static void AppendLines(StringBuilder source, string lines)
    {
        foreach (string line in lines.Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries))
            source.Append(line).Append('\n');
    }

    private static Model? Describe(GeneratorAttributeSyntaxContext attributed, System.Threading.CancellationToken cancellation)
    {
        var declaration = (MethodDeclarationSyntax)attributed.TargetNode;
        if (attributed.TargetSymbol is not IMethodSymbol method)
            return null;
        INamedTypeSymbol type = method.ContainingType;
        Compilation compilation = attributed.SemanticModel.Compilation;
        List<INamedTypeSymbol> chain = ContainerChain(type);
        string name = method.Name;
        string display = $"{type.ToDisplayString()}.{name}";
        LocationInfo at = LocationInfo.From(declaration.Identifier.GetLocation());

        Part PartWith(string containers, int depth) => new(
            type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat), at.Path,
            type.ContainingNamespace.IsGlobalNamespace ? "" : type.ContainingNamespace.ToDisplayString(), containers, depth,
            string.Join("\n", declaration.SyntaxTree.GetCompilationUnitRoot(cancellation).Usings
                .Where(static directive => directive.GlobalKeyword.IsKind(SyntaxKind.None))
                .Select(static directive => directive.WithoutTrivia().ToString())),
            string.Join("\n", declaration.Ancestors().OfType<BaseNamespaceDeclarationSyntax>().Reverse()
                .SelectMany(static space => space.Usings).Select(static directive => directive.WithoutTrivia().ToString())));
        Model Unwritten(DiagnosticInfo refusal) => new(PartWith("", 0), name, display, at, null, null, null, refusal);

        // A type the generator cannot add to: nothing can be written at all.
        INamedTypeSymbol? notPartial = chain.LastOrDefault(container => !IsPartial(container, cancellation));
        if (notPartial != null)
            return Unwritten(Refuse(ContainerNotPartial, at, display, notPartial.ToDisplayString()));
        if (!declaration.Modifiers.Any(SyntaxKind.PartialKeyword) || declaration.Body != null || declaration.ExpressionBody != null)
            return Unwritten(Refuse(NotPartial, at, display));

        Part part = PartWith(Containers(chain), chain.Count);
        string stub = Signature(declaration) + "\n    => throw new global::System.NotSupportedException(\"This native function was refused when it was compiled.\");";
        Model Refused(DiagnosticInfo refusal) => new(part, name, display, at, null, null, stub, refusal);

        if (!method.IsStatic)
            return Refused(Refuse(NotStatic, at, display));
        if (method.IsGenericMethod || chain.Any(static container => container.IsGenericType))
            return Refused(Refuse(Generic, at, display));

        var fieldTypes = new List<string>();
        var arguments = new List<string>();
        var prelude = new StringBuilder();
        var fixedStatements = new List<string>();
        foreach (IParameterSymbol parameter in method.Parameters)
        {
            // A parameter named by a keyword (@class) is written with its @ wherever the body names it.
            string p = SyntaxFacts.GetKeywordKind(parameter.Name) != SyntaxKind.None ? "@" + parameter.Name : parameter.Name;
            string local = "__" + parameter.Name;
            ITypeSymbol parameterType = parameter.Type;
            if (parameter.RefKind != RefKind.None)
            {
                if (!parameterType.IsValueType || !parameterType.IsUnmanagedType)
                    return Refused(Refuse(Unsupported, at, display, parameterType.ToDisplayString(), "parameter " + p));
                string pointer = parameterType.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat) + "*";
                fieldTypes.Add(pointer);
                if (parameter.RefKind == RefKind.Out)
                    prelude.Append(p).Append(" = default;\n");
                string address = parameter.RefKind == RefKind.In || parameter.RefKind == RefReadOnlyParameter
                    ? "&global::System.Runtime.CompilerServices.Unsafe.AsRef(in " + p + ")"
                    : "&" + p;
                fixedStatements.Add($"fixed ({pointer} {local} = {address})");
                arguments.Add(local);
            }
            else if (IsText(parameterType))
            {
                fieldTypes.Add("byte*");
                fieldTypes.Add("int");
                prelude.Append($"global::System.ReadOnlySpan<char> {local}Chars = {p};\n");
                prelude.Append($"int {local}Capacity = global::System.Text.Encoding.UTF8.GetMaxByteCount({local}Chars.Length);\n");
                prelude.Append($"global::System.Span<byte> {local}Bytes = {local}Capacity <= {StackBytes} ? stackalloc byte[{StackBytes}] : new byte[{local}Capacity];\n");
                prelude.Append($"int {local}Count = global::System.Text.Encoding.UTF8.GetBytes({local}Chars, {local}Bytes);\n");
                fixedStatements.Add($"fixed (byte* {local} = {local}Bytes)");
                arguments.Add(local);
                arguments.Add(local + "Count");
            }
            else if (SpanElement(parameterType) is ITypeSymbol element)
            {
                // A span crosses as its first element's address and its length: pinned here, so the facade over the
                // declaration never does. An empty span passes null and 0.
                if (!element.IsUnmanagedType)
                    return Refused(Refuse(Unsupported, at, display, parameterType.ToDisplayString(), "parameter " + p));
                string pointer = element.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat) + "*";
                fieldTypes.Add(pointer);
                fieldTypes.Add("int");
                fixedStatements.Add($"fixed ({pointer} {local} = {p})");
                arguments.Add(local);
                arguments.Add(p + ".Length");
            }
            else if (ByValue(parameterType, compilation, type) is Crossing crossing)
            {
                fieldTypes.Add(crossing.FieldType);
                arguments.Add(crossing.Argument(p));
            }
            else
            {
                return Refused(Refuse(Unsupported, at, display, parameterType.ToDisplayString(), "parameter " + p));
            }
        }

        ITypeSymbol result = method.ReturnType;
        string fieldResult;
        string call = $"{FieldPrefix}{name}({string.Join(", ", arguments)})";
        string statement;
        if (method.ReturnsVoid)
        {
            fieldResult = "void";
            statement = call + ";";
        }
        else if (method.ReturnsByRef || method.ReturnsByRefReadonly)
        {
            return Refused(Refuse(Unsupported, at, display, result.ToDisplayString(), "a ref result"));
        }
        else if (result.SpecialType == SpecialType.System_String)
        {
            return Refused(Refuse(StringResult, at, display));
        }
        else if (ByValue(result, compilation, type) is { Result: not null } crossing)
        {
            fieldResult = crossing.FieldType;
            statement = $"return {crossing.Result(call)};";
        }
        else
        {
            return Refused(Refuse(Unsupported, at, display, result.ToDisplayString(), "the result"));
        }

        string field = $"public static delegate* unmanaged[Cdecl]<{string.Join("", fieldTypes.Select(static t => t + ", "))}{fieldResult}> {FieldPrefix}{name};";
        var body = new StringBuilder();
        body.Append(Signature(declaration)).Append("\n{\n");
        foreach (string line in prelude.ToString().Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries))
            body.Append("    ").Append(line).Append('\n');
        foreach (string statementLine in fixedStatements)
            body.Append("    ").Append(statementLine).Append('\n');
        body.Append(fixedStatements.Count > 0 ? "    {\n        " + statement + "\n    }\n" : "    " + statement + "\n");
        body.Append('}');
        return new Model(part, name, display, at, field, body.ToString(), stub, null);
    }

    // The declaration's signature as the implementing part repeats it: modifiers, result, name, parameters and constraints,
    // without its attributes, documentation or trailing semicolon.
    private static string Signature(MethodDeclarationSyntax declaration) =>
        declaration.WithAttributeLists(default).WithSemicolonToken(default).WithoutTrivia().NormalizeWhitespace().ToFullString();

    // The type and every type around it, outermost first.
    private static List<INamedTypeSymbol> ContainerChain(INamedTypeSymbol type)
    {
        var chain = new List<INamedTypeSymbol>();
        for (INamedTypeSymbol? container = type; container != null; container = container.ContainingType)
            chain.Insert(0, container);
        return chain;
    }

    private static bool IsPartial(INamedTypeSymbol type, System.Threading.CancellationToken cancellation) =>
        type.DeclaringSyntaxReferences.All(reference =>
            reference.GetSyntax(cancellation) is TypeDeclarationSyntax declaration &&
            declaration.Modifiers.Any(SyntaxKind.PartialKeyword));

    // The opening of every type around the method, outermost first, each with its type parameters (the declarations state
    // the constraints); the innermost is unsafe, for its pointers.
    private static string Containers(List<INamedTypeSymbol> chain)
    {
        var text = new StringBuilder();
        for (int index = 0; index < chain.Count; index++)
        {
            INamedTypeSymbol container = chain[index];
            string kind = container.TypeKind switch
            {
                TypeKind.Struct => container.IsRecord ? "record struct" : "struct",
                TypeKind.Interface => "interface",
                _ => container.IsRecord ? "record" : "class",
            };
            string parameters = container.TypeParameters.Length == 0
                ? ""
                : "<" + string.Join(", ", container.TypeParameters.Select(static parameter => parameter.Name)) + ">";
            bool innermost = index == chain.Count - 1;
            text.Append(innermost ? "unsafe partial " : "partial ").Append(kind).Append(' ').Append(container.Name).Append(parameters).Append('\n');
            if (!innermost)
                text.Append("{\n");
        }
        return text.ToString();
    }

    // The element type of a System.Span<T> or System.ReadOnlySpan<T>, or null. Text (ReadOnlySpan<char>) is checked first
    // by the caller, so a Span<char> here is a UTF-16 buffer the native fills.
    private static ITypeSymbol? SpanElement(ITypeSymbol type) =>
        type is INamedTypeSymbol { TypeArguments.Length: 1 } named &&
        named.OriginalDefinition.ToDisplayString() is "System.Span<T>" or "System.ReadOnlySpan<T>"
            ? named.TypeArguments[0]
            : null;

    private static bool IsText(ITypeSymbol type) =>
        type.SpecialType == SpecialType.System_String ||
        (type is INamedTypeSymbol named && named.OriginalDefinition.ToDisplayString() == "System.ReadOnlySpan<T>" &&
         named.TypeArguments[0].SpecialType == SpecialType.System_Char);

    // How a value crosses by value: the field's type for it, what an argument becomes, and what a result is built from the
    // field's (null when it cannot be a result).
    private sealed class Crossing
    {
        public Crossing(string fieldType, Func<string, string> argument, Func<string, string>? result)
        {
            FieldType = fieldType;
            Argument = argument;
            Result = result;
        }

        public string FieldType { get; }
        public Func<string, string> Argument { get; }
        public Func<string, string>? Result { get; }
    }

    // A value that crosses as itself, an enum as its underlying integer, or a wrapper as its member; or null. `within` is the
    // type the generated body is in, which must be able to read the wrapper's member and build the wrapper.
    private static Crossing? ByValue(ITypeSymbol type, Compilation compilation, INamedTypeSymbol within)
    {
        if (Scalar(type) is Crossing scalar)
            return scalar;
        if (WrapperMember(type, compilation, within) is not (ISymbol member, ITypeSymbol memberType) ||
            Scalar(memberType) is not Crossing field)
            return null;
        string wrapper = type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        string name = SyntaxFacts.GetKeywordKind(member.Name) != SyntaxKind.None ? "@" + member.Name : member.Name;
        Func<string, string>? build = null;
        if (type is INamedTypeSymbol named && named.InstanceConstructors.Any(constructor =>
                constructor.Parameters.Length == 1 && constructor.Parameters[0].RefKind == RefKind.None &&
                SymbolEqualityComparer.Default.Equals(constructor.Parameters[0].Type, memberType) &&
                compilation.IsSymbolAccessibleWithin(constructor, within)))
            build = call => $"new {wrapper}({field.Result!(call)})";
        else if (member is IFieldSymbol { IsReadOnly: false } ||
                 member is IPropertySymbol { SetMethod: IMethodSymbol setter } && compilation.IsSymbolAccessibleWithin(setter, within))
            build = call => $"new {wrapper} {{ {name} = {field.Result!(call)} }}";
        return new Crossing(field.FieldType, value => field.Argument(value + "." + name), build);
    }

    // A value that crosses as itself, or an enum as its underlying integer; or null.
    private static Crossing? Scalar(ITypeSymbol type)
    {
        if (PassesThrough(type) is string passed)
            return new Crossing(passed, static value => value, static call => call);
        if (type is INamedTypeSymbol { TypeKind: TypeKind.Enum, EnumUnderlyingType: INamedTypeSymbol underlying })
        {
            string enumType = type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
            string integer = underlying.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
            return new Crossing(integer, value => $"({integer}){value}", call => $"({enumType}){call}");
        }
        return null;
    }

    // The member a wrapper crosses as, and its type: the only instance field of an unmanaged struct, or the auto-property
    // whose backing field it is, when `within` can read it. Null for any other type.
    private static (ISymbol Member, ITypeSymbol Type)? WrapperMember(ITypeSymbol type, Compilation compilation, INamedTypeSymbol within)
    {
        if (type is not INamedTypeSymbol { TypeKind: TypeKind.Struct, IsRefLikeType: false } named || !named.IsUnmanagedType)
            return null;
        IFieldSymbol[] fields = named.GetMembers().OfType<IFieldSymbol>().Where(static field => !field.IsStatic && !field.IsConst).ToArray();
        if (fields.Length != 1 || fields[0].IsFixedSizeBuffer)
            return null;
        IFieldSymbol only = fields[0];
        // A backing field is named "<Property>k__BackingField"; from source it also names its property.
        const string backing = ">k__BackingField";
        IPropertySymbol? property = only.AssociatedSymbol as IPropertySymbol ??
            (only.Name.StartsWith("<", StringComparison.Ordinal) && only.Name.EndsWith(backing, StringComparison.Ordinal)
                ? named.GetMembers(only.Name.Substring(1, only.Name.Length - 1 - backing.Length)).OfType<IPropertySymbol>().FirstOrDefault()
                : null);
        if (property != null)
            return !property.IsStatic && property.Parameters.Length == 0 && property.GetMethod is IMethodSymbol getter &&
                   compilation.IsSymbolAccessibleWithin(getter, within)
                ? (property, property.Type)
                : null;
        return !only.IsImplicitlyDeclared && compilation.IsSymbolAccessibleWithin(only, within) ? (only, only.Type) : null;
    }

    // The field type of a value that crosses as itself, or null.
    private static string? PassesThrough(ITypeSymbol type)
    {
        switch (type.SpecialType)
        {
            case SpecialType.System_Boolean:
            case SpecialType.System_Char:
            case SpecialType.System_SByte:
            case SpecialType.System_Byte:
            case SpecialType.System_Int16:
            case SpecialType.System_UInt16:
            case SpecialType.System_Int32:
            case SpecialType.System_UInt32:
            case SpecialType.System_Int64:
            case SpecialType.System_UInt64:
            case SpecialType.System_Single:
            case SpecialType.System_Double:
            case SpecialType.System_IntPtr:
            case SpecialType.System_UIntPtr:
                return type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        }
        return type is IPointerTypeSymbol or IFunctionPointerTypeSymbol
            ? type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat)
            : null;
    }

    private static DiagnosticInfo Refuse(DiagnosticDescriptor descriptor, LocationInfo at, params string[] arguments) =>
        new(descriptor, at, arguments);

    // The type's name with a hash of what tells its parts apart: the file's whole path (so two files of one name in
    // different folders never share a hint name) and the usings of its namespace blocks (a file can open one namespace
    // twice, each time with usings of its own).
    private static string HintName(Part part)
    {
        var name = new StringBuilder("Fenrir.Scripting.NativeFunctions.");
        foreach (char c in part.TypeKey.Replace("global::", ""))
            name.Append(char.IsLetterOrDigit(c) || c == '.' ? c : '_');
        return name.Append('.').Append(Hash(part.FilePath + "\n" + part.NamespaceUsings).ToString("x8")).Append(".g.cs").ToString();
    }

    // 32-bit FNV-1a, so a hint name is the same on every build.
    private static uint Hash(string text)
    {
        uint hash = 2166136261U;
        foreach (char c in text)
            hash = (hash ^ c) * 16777619U;
        return hash;
    }
}
