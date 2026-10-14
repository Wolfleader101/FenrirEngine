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
/// also implements). The body turns the arguments into what crosses (a string into UTF-8 bytes and a length, an
/// <c>Entity</c> into its value, an <c>in</c>/<c>ref</c>/<c>out</c> struct into a pointer) and calls the field.
/// </summary>
/// <remarks>
/// A declaration it refuses still gets a body (one that throws), so the build reports the FENRIR error and not also a
/// partial method without an implementation. The exception is a native function inside a type that is not partial, where
/// nothing can be written.
/// </remarks>
[Generator]
public sealed class NativeFunctionGenerator : IIncrementalGenerator
{
    private const string AttributeName = "Fenrir.Scripting.NativeFunctionAttribute";
    private const string EntityName = "Fenrir.Scripting.Entity";
    private const string FieldPrefix = "__native_";
    private const int StackBytes = 256;

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
        "Native function '{0}' uses '{1}' ({2}), which cannot cross: use a number, bool, char, pointer, Entity, a string parameter, or an in/ref/out unmanaged struct");

    private static DiagnosticDescriptor Error(string id, string title, string message) =>
        new(id, title, message, "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    // A diagnostic, as plain data so the model stays equatable.
    private sealed record Problem(string Id, string First, string Second, string Third, string Path,
        Microsoft.CodeAnalysis.Text.TextSpan Span, Microsoft.CodeAnalysis.Text.LinePositionSpan LineSpan);

    // What one [NativeFunction] declaration contributes to its type's generated part.
    private sealed record Model(string TypeKey, string Namespace, string Containers, string Name, string? Field,
        string? Implementation, string? Stub, Problem? Problem, Problem Overload, string FileUsings, string NamespaceUsings);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        IncrementalValuesProvider<Model> functions = context.SyntaxProvider
            .ForAttributeWithMetadataName(AttributeName,
                static (node, _) => node is MethodDeclarationSyntax,
                static (attributed, cancellation) => Describe(attributed, cancellation))
            .Where(static model => model is not null)!;

        context.RegisterSourceOutput(functions.Collect(), static (output, models) =>
        {
            foreach (var type in models.GroupBy(static model => model.TypeKey))
            {
                var parts = new List<(string? Field, string Body)>();
                var named = new HashSet<string>(StringComparer.Ordinal);
                foreach (Model model in type)
                {
                    Problem? problem = model.Problem;
                    bool first = named.Add(model.Name);
                    if (problem == null && !first)
                        problem = model.Overload;
                    if (problem != null)
                        Report(output, problem);
                    if (problem == null && model.Implementation != null)
                        parts.Add((model.Field, model.Implementation));
                    else if (model.Stub != null)
                        parts.Add((null, model.Stub));
                }
                if (parts.Count == 0)
                    continue;
                Model sample = type.First();
                var source = new StringBuilder();
                source.Append("// <auto-generated>Fenrir.Scripting.Generators: native function bodies and the fields the host binds.</auto-generated>\n");
                source.Append("#pragma warning disable CS0649 // The host assigns the fields when it shares this assembly.\n");
                // The declarations' own using directives, so the copied signatures mean what they mean there.
                foreach (string line in Lines(type.Select(static model => model.FileUsings)))
                    source.Append(line).Append('\n');
                if (sample.Namespace.Length > 0)
                    source.Append("namespace ").Append(sample.Namespace).Append("\n{\n");
                foreach (string line in Lines(type.Select(static model => model.NamespaceUsings)))
                    source.Append(line).Append('\n');
                source.Append(sample.Containers).Append("{\n");
                foreach ((string? field, string body) in parts)
                {
                    if (field != null)
                        source.Append("    ").Append(field).Append('\n');
                    source.Append("    ").Append(body.Replace("\n", "\n    ")).Append("\n\n");
                }
                source.Append(new string('}', CountOpenings(sample.Containers))).Append('\n');
                if (sample.Namespace.Length > 0)
                    source.Append("}\n");
                output.AddSource(HintName(type.Key), source.ToString());
            }
        });
    }

    private static Model? Describe(GeneratorAttributeSyntaxContext attributed, System.Threading.CancellationToken cancellation)
    {
        var declaration = (MethodDeclarationSyntax)attributed.TargetNode;
        if (attributed.TargetSymbol is not IMethodSymbol method)
            return null;
        INamedTypeSymbol type = method.ContainingType;
        string name = method.Name;
        string display = $"{type.ToDisplayString()}.{name}";
        Location at = declaration.Identifier.GetLocation();
        string typeKey = type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        string ns = type.ContainingNamespace.IsGlobalNamespace ? "" : type.ContainingNamespace.ToDisplayString();
        Problem overload = Make(Overloaded, at, display);
        string fileUsings = string.Join("\n", declaration.SyntaxTree.GetCompilationUnitRoot(cancellation).Usings
            .Where(static directive => directive.GlobalKeyword.IsKind(SyntaxKind.None))
            .Select(static directive => directive.WithoutTrivia().ToString()));
        string namespaceUsings = string.Join("\n", declaration.Ancestors().OfType<BaseNamespaceDeclarationSyntax>().Reverse()
            .SelectMany(static space => space.Usings).Select(static directive => directive.WithoutTrivia().ToString()));

        // A type the generator cannot add to: nothing can be written at all.
        for (INamedTypeSymbol? container = type; container != null; container = container.ContainingType)
        {
            bool partial = container.DeclaringSyntaxReferences.All(reference =>
                reference.GetSyntax(cancellation) is TypeDeclarationSyntax typeDeclaration &&
                typeDeclaration.Modifiers.Any(SyntaxKind.PartialKeyword));
            if (!partial)
                return new Model(typeKey, ns, "", name, null, null, null, Make(ContainerNotPartial, at, display, container.ToDisplayString()), overload, fileUsings, namespaceUsings);
        }
        string containers = Containers(type);

        bool isPartial = declaration.Modifiers.Any(SyntaxKind.PartialKeyword);
        bool hasBody = declaration.Body != null || declaration.ExpressionBody != null;
        if (!isPartial || hasBody)
            return new Model(typeKey, ns, containers, name, null, null, null, Make(NotPartial, at, display), overload, fileUsings, namespaceUsings);

        string stub = Signature(declaration) + "\n    => throw new global::System.NotSupportedException(\"This native function was refused when it was compiled.\");";
        Model Refused(Problem problem) => new(typeKey, ns, containers, name, null, null, stub, problem, overload, fileUsings, namespaceUsings);

        if (!method.IsStatic)
            return Refused(Make(NotStatic, at, display));
        bool generic = method.IsGenericMethod;
        for (INamedTypeSymbol? container = type; container != null && !generic; container = container.ContainingType)
            generic = container.IsGenericType;
        if (generic)
            return Refused(Make(Generic, at, display));

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
                    return Refused(Make(Unsupported, at, display, parameterType.ToDisplayString(), "parameter " + p));
                string pointer = parameterType.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat) + "*";
                fieldTypes.Add(pointer);
                if (parameter.RefKind == RefKind.Out)
                    prelude.Append(p).Append(" = default;\n");
                string address = parameter.RefKind == RefKind.In || parameter.RefKind == (RefKind)4 // ref readonly
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
            else if (IsEntity(parameterType))
            {
                fieldTypes.Add("uint");
                arguments.Add(p + ".Value");
            }
            else if (PassesThrough(parameterType) is string passed)
            {
                fieldTypes.Add(passed);
                arguments.Add(p);
            }
            else
            {
                return Refused(Make(Unsupported, at, display, parameterType.ToDisplayString(), "parameter " + p));
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
            return Refused(Make(Unsupported, at, display, result.ToDisplayString(), "a ref result"));
        }
        else if (result.SpecialType == SpecialType.System_String)
        {
            return Refused(Make(StringResult, at, display));
        }
        else if (IsEntity(result))
        {
            fieldResult = "uint";
            statement = $"return new global::{EntityName}({call});";
        }
        else if (PassesThrough(result) is string passed)
        {
            fieldResult = passed;
            statement = $"return {call};";
        }
        else
        {
            return Refused(Make(Unsupported, at, display, result.ToDisplayString(), "the result"));
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
        return new Model(typeKey, ns, containers, name, field, body.ToString(), stub, null, overload, fileUsings, namespaceUsings);
    }

    // The declaration's signature as the implementing part repeats it: modifiers, result, name, parameters and constraints,
    // without its attributes, documentation or trailing semicolon.
    private static string Signature(MethodDeclarationSyntax declaration) =>
        declaration.WithAttributeLists(default).WithSemicolonToken(default).WithoutTrivia().NormalizeWhitespace().ToFullString();

    // The opening of every type around the method, outermost first; the innermost is unsafe, for its pointers.
    private static string Containers(INamedTypeSymbol type)
    {
        var chain = new List<INamedTypeSymbol>();
        for (INamedTypeSymbol? container = type; container != null; container = container.ContainingType)
            chain.Insert(0, container);
        var text = new StringBuilder();
        foreach (INamedTypeSymbol container in chain)
        {
            string kind = container.TypeKind switch
            {
                TypeKind.Struct => container.IsRecord ? "record struct" : "struct",
                TypeKind.Interface => "interface",
                _ => container.IsRecord ? "record" : "class",
            };
            text.Append(SymbolEqualityComparer.Default.Equals(container, type) ? "unsafe partial " : "partial ")
                .Append(kind).Append(' ').Append(container.Name).Append('\n');
            if (!SymbolEqualityComparer.Default.Equals(container, type))
                text.Append("{\n");
        }
        return text.ToString();
    }

    private static IEnumerable<string> Lines(IEnumerable<string> blocks) =>
        blocks.SelectMany(static block => block.Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries)).Distinct(StringComparer.Ordinal);

    private static int CountOpenings(string containers) => containers.Count(static c => c == '{') + 1;

    private static bool IsText(ITypeSymbol type) =>
        type.SpecialType == SpecialType.System_String ||
        (type is INamedTypeSymbol named && named.OriginalDefinition.ToDisplayString() == "System.ReadOnlySpan<T>" &&
         named.TypeArguments[0].SpecialType == SpecialType.System_Char);

    private static bool IsEntity(ITypeSymbol type) => type.ToDisplayString() == EntityName;

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

    private static Problem Make(DiagnosticDescriptor descriptor, Location at, string first, string second = "", string third = "") =>
        new(descriptor.Id, first, second, third, at.SourceTree?.FilePath ?? "", at.SourceSpan, at.GetLineSpan().Span);

    private static void Report(SourceProductionContext output, Problem problem)
    {
        DiagnosticDescriptor descriptor = problem.Id switch
        {
            "FENRIR0020" => NotStatic,
            "FENRIR0021" => NotPartial,
            "FENRIR0022" => ContainerNotPartial,
            "FENRIR0023" => Generic,
            "FENRIR0024" => Overloaded,
            "FENRIR0025" => StringResult,
            _ => Unsupported,
        };
        var location = Location.Create(problem.Path, problem.Span, problem.LineSpan);
        output.ReportDiagnostic(Diagnostic.Create(descriptor, location, problem.First, problem.Second, problem.Third));
    }

    private static string HintName(string typeKey)
    {
        var name = new StringBuilder("Fenrir.Scripting.NativeFunctions.");
        foreach (char c in typeKey.Replace("global::", ""))
            name.Append(char.IsLetterOrDigit(c) || c == '.' ? c : '_');
        return name.Append(".g.cs").ToString();
    }
}
