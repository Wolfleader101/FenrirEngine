using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Fenrir.Scripting.Generators;

/// <summary>
/// Writes the members of every <c>[TaggedUnion]</c> struct: a parameterless constructor making the kind whose tag value is
/// 0, <c>Type</c>, one checked <c>ref</c> view per kind named after its tag member, an implicit conversion from each kind,
/// and the <c>Expect</c> the views call, which throws <c>InvalidOperationException</c> for the wrong kind. The author
/// writes the tag enum, the tag field at offset 0 and one private field per kind, named <c>_</c> plus its tag member with
/// the first letter lowered.
/// </summary>
[Generator]
public sealed class TaggedUnionGenerator : IIncrementalGenerator
{
    private const string AttributeName = "Fenrir.Scripting.TaggedUnionAttribute";
    private const string FieldOffsetName = "System.Runtime.InteropServices.FieldOffsetAttribute";

    // What one union contributes: its members, in a file of their own. Plain data, so an unchanged union is not regenerated.
    private sealed record Model(string HintName, string Source);

    // A kind: its tag member (and view), its type and its field.
    private sealed record Kind(string Member, string Type, string Field);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        IncrementalValuesProvider<Model> unions = context.SyntaxProvider
            .ForAttributeWithMetadataName(AttributeName,
                static (node, _) => node is StructDeclarationSyntax,
                static (attributed, cancellation) => Describe(attributed, cancellation))
            .Where(static model => model is not null)!;

        context.RegisterSourceOutput(unions, static (output, model) => output.AddSource(model.HintName, model.Source));
    }

    private static Model? Describe(GeneratorAttributeSyntaxContext attributed, CancellationToken cancellation)
    {
        if (attributed.TargetSymbol is not INamedTypeSymbol union)
            return null;
        string? noun = attributed.Attributes[0].NamedArguments
            .Where(static argument => argument.Key == "Noun")
            .Select(static argument => argument.Value.Value as string)
            .FirstOrDefault();

        IFieldSymbol[] fields = union.GetMembers().OfType<IFieldSymbol>()
            .Where(static field => !field.IsStatic && !field.IsConst && field.AssociatedSymbol == null)
            .ToArray();
        IFieldSymbol? tag = fields.FirstOrDefault(static field => field.Type.TypeKind == TypeKind.Enum && OffsetOf(field) == 0);
        if (tag == null)
            return null;
        var tagType = (INamedTypeSymbol)tag.Type;
        IFieldSymbol[] members = tagType.GetMembers().OfType<IFieldSymbol>().Where(static member => member.HasConstantValue).ToArray();

        var kinds = new List<Kind>();
        foreach (IFieldSymbol field in fields)
        {
            cancellation.ThrowIfCancellationRequested();
            if (SymbolEqualityComparer.Default.Equals(field, tag))
                continue;
            IFieldSymbol? member = members.FirstOrDefault(member => FieldNameFor(member.Name) == field.Name);
            if (member == null)
                return null;
            kinds.Add(new Kind(member.Name, field.Type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat), field.Name));
        }
        IFieldSymbol? zero = members.FirstOrDefault(static member => System.Convert.ToInt64(member.ConstantValue) == 0);
        Kind? initial = zero == null ? null : kinds.FirstOrDefault(kind => kind.Member == zero.Name);
        if (initial == null)
            return null;

        string unionName = union.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        string tagName = tagType.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        string message = Interpolated(noun ?? union.Name);
        string self = union.Name;
        string t = tag.Name;

        var body = new StringBuilder();
        body.Append($"/// <summary>A {self} holding a new {initial.Member}, the kind whose tag value is 0.</summary>\n");
        body.Append($"public {self}()\n{{\n    {t} = {tagName}.{initial.Member};\n    {initial.Field} = new();\n}}\n\n");
        body.Append("/// <summary>Which kind this holds.</summary>\n");
        body.Append($"public readonly {tagName} Type => {t};\n");
        foreach (Kind kind in kinds)
        {
            string view = Identifier(kind.Member);
            body.Append("\n/// <summary>\n");
            body.Append($"/// This as its {kind.Member} kind, which it must be: a reference into it, so writes through it land where\n");
            body.Append("/// it is stored. Throws <see cref=\"global::System.InvalidOperationException\"/> if this is another kind.\n");
            body.Append("/// </summary>\n");
            body.Append("[global::System.Diagnostics.CodeAnalysis.UnscopedRef]\n");
            body.Append($"public ref {kind.Type} {view}\n{{\n    get\n    {{\n");
            body.Append($"        Expect({tagName}.{kind.Member});\n        return ref {kind.Field};\n    }}\n}}\n");
        }
        foreach (Kind kind in kinds)
        {
            body.Append($"\n/// <summary>A {self} holding <paramref name=\"value\"/>.</summary>\n");
            body.Append($"public static implicit operator {unionName}({kind.Type} value) =>\n");
            body.Append($"    new() {{ {t} = {tagName}.{kind.Member}, {kind.Field} = value }};\n");
        }
        body.Append($"\nprivate readonly void Expect({tagName} wanted)\n{{\n    if ({t} != wanted)\n");
        body.Append($"        throw new global::System.InvalidOperationException($\"The {message} is a {{{t}}}, not a {{wanted}}.\");\n}}");

        return new Model(HintName(unionName), Source(union, body.ToString()));
    }

    // The field a tag member's kind is stored in: ConvexHull is _convexHull.
    private static string FieldNameFor(string member) =>
        member.Length == 0 ? "_" : "_" + char.ToLowerInvariant(member[0]) + member.Substring(1);

    private static int? OffsetOf(IFieldSymbol field) =>
        field.GetAttributes()
            .Where(static attribute => attribute.AttributeClass?.ToDisplayString() == FieldOffsetName)
            .Select(static attribute => attribute.ConstructorArguments.FirstOrDefault().Value as int?)
            .FirstOrDefault();

    // A name as it is written in code: a keyword (a member named @fixed) keeps its @.
    private static string Identifier(string name) =>
        SyntaxFacts.GetKeywordKind(name) != SyntaxKind.None ? "@" + name : name;

    // Text as it is written inside an interpolated string.
    private static string Interpolated(string text) =>
        SymbolDisplay.FormatLiteral(text, quote: false).Replace("{", "{{").Replace("}", "}}");

    // The members, inside the union's namespace and every type around it.
    private static string Source(INamedTypeSymbol union, string body)
    {
        var chain = new List<INamedTypeSymbol>();
        for (INamedTypeSymbol? container = union; container != null; container = container.ContainingType)
            chain.Insert(0, container);

        var source = new StringBuilder();
        source.Append("// <auto-generated>Fenrir.Scripting.Generators: the members of a [TaggedUnion].</auto-generated>\n");
        source.Append("#nullable enable\n");
        bool inNamespace = !union.ContainingNamespace.IsGlobalNamespace;
        if (inNamespace)
            source.Append("namespace ").Append(union.ContainingNamespace.ToDisplayString()).Append("\n{\n");
        string indent = inNamespace ? "    " : "";
        foreach (INamedTypeSymbol container in chain)
        {
            string kind = container.TypeKind switch
            {
                TypeKind.Struct => container.IsRecord ? "record struct" : "struct",
                TypeKind.Interface => "interface",
                _ => container.IsRecord ? "record" : "class",
            };
            source.Append(indent).Append("partial ").Append(kind).Append(' ').Append(container.Name).Append('\n');
            source.Append(indent).Append("{\n");
            indent += "    ";
        }
        foreach (string line in body.Split('\n'))
            source.Append(line.Length == 0 ? "" : indent).Append(line).Append('\n');
        foreach (INamedTypeSymbol _ in chain)
        {
            indent = indent.Substring(4);
            source.Append(indent).Append("}\n");
        }
        if (inNamespace)
            source.Append("}\n");
        return source.ToString();
    }

    private static string HintName(string unionName)
    {
        var name = new StringBuilder("Fenrir.Scripting.TaggedUnions.");
        foreach (char c in unionName.Replace("global::", ""))
            name.Append(char.IsLetterOrDigit(c) || c == '.' ? c : '_');
        return name.Append(".g.cs").ToString();
    }
}
