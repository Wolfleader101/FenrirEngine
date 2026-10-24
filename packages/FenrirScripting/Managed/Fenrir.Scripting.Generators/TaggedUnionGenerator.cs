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
/// <remarks>
/// A union it cannot write for is refused with its own error (FENRIR0030 to FENRIR0038) and gets nothing, so the build
/// shows that error and not the generated code's.
/// </remarks>
[Generator]
public sealed class TaggedUnionGenerator : IIncrementalGenerator
{
    private const string AttributeName = "Fenrir.Scripting.TaggedUnionAttribute";
    private const string FieldOffsetName = "System.Runtime.InteropServices.FieldOffsetAttribute";

    private static readonly DiagnosticDescriptor NotPartialStruct = Error("FENRIR0030", "A tagged union must be a partial struct",
        "Tagged union '{0}' {1}: declare it 'partial struct', inside types that are all partial, so the generator can add to it");
    private static readonly DiagnosticDescriptor CannotHoldViews = Error("FENRIR0031", "A tagged union cannot be readonly, generic or a ref struct",
        "Tagged union '{0}' is {1}: write it as a plain, non-generic 'partial struct'");
    private static readonly DiagnosticDescriptor NoTag = Error("FENRIR0032", "A tagged union needs one tag at offset 0",
        "Tagged union '{0}' has {1} enum-typed instance field at [FieldOffset(0)]: declare exactly one, the tag, such as '[FieldOffset(0)] private ShapeType _type;'");
    private static readonly DiagnosticDescriptor BadKindField = Error("FENRIR0033", "A tagged union's kind field is wrong",
        "Field '{1}' of tagged union '{0}' {2}: each kind is a private, non-readonly instance field named '_' plus its member of '{3}' with the first letter lowered (Box is _box)");
    private static readonly DiagnosticDescriptor MissingKind = Error("FENRIR0034", "A tag member has no kind",
        "Tagged union '{0}' has no field for {1}.{2}: declare 'private <kind type> {3};' at the kinds' offset");
    private static readonly DiagnosticDescriptor BadOffset = Error("FENRIR0035", "A tagged union's kinds must share one offset after the tag",
        "Field '{1}' of tagged union '{0}' is at offset {2}, but {3}: put every kind at the same [FieldOffset], the C++ union's offset");
    private static readonly DiagnosticDescriptor BadKindType = Error("FENRIR0036", "A tagged union's kind has the wrong type",
        "Field '{1}' of tagged union '{0}' has type '{2}', which {3}: each kind is an unmanaged struct of its own");
    private static readonly DiagnosticDescriptor WrittenMember = Error("FENRIR0037", "A tagged union declares a member the generator writes",
        "Tagged union '{0}' declares {1}, which the generator writes: remove it");
    private static readonly DiagnosticDescriptor BadTagValues = Error("FENRIR0038", "A tagged union's tag values are ambiguous",
        "The tag '{1}' of tagged union '{0}' {2}: give it one member with value 0, the kind 'new()' makes, and a distinct value for each member");

    private static DiagnosticDescriptor Error(string id, string title, string message) =>
        new(id, title, message, "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    // What one union contributes: its members, in a file of their own, or the reasons it was refused. Plain data, so an
    // unchanged union is not regenerated.
    private sealed record Model(string HintName, string? Source, DiagnosticInfo[] Refusals)
    {
        public bool Equals(Model? other) =>
            other is not null && HintName == other.HintName && Source == other.Source && Refusals.SequenceEqual(other.Refusals);

        public override int GetHashCode() => (HintName, Source, Refusals.Length).GetHashCode();
    }

    // A kind: its tag member (and view), its type and its field.
    private sealed record Kind(string Member, string Type, string Field);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        // Classes too, so a class marked by mistake is told what to write.
        IncrementalValuesProvider<Model> unions = context.SyntaxProvider
            .ForAttributeWithMetadataName(AttributeName,
                static (node, _) => node is TypeDeclarationSyntax,
                static (attributed, cancellation) => Describe(attributed, cancellation))
            .Where(static model => model is not null)!;

        context.RegisterSourceOutput(unions, static (output, model) =>
        {
            foreach (DiagnosticInfo refusal in model.Refusals)
                output.ReportDiagnostic(refusal.ToDiagnostic());
            // A refused union gets nothing, so its FENRIR errors are the only ones.
            if (model.Source != null)
                output.AddSource(model.HintName, model.Source);
        });
    }

    private static Model? Describe(GeneratorAttributeSyntaxContext attributed, CancellationToken cancellation)
    {
        if (attributed.TargetSymbol is not INamedTypeSymbol union)
            return null;
        string display = union.ToDisplayString();
        string hintName = HintName(union.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat));
        LocationInfo at = LocationInfo.From(((TypeDeclarationSyntax)attributed.TargetNode).Identifier.GetLocation());
        var refusals = new List<DiagnosticInfo>();
        Model Refused() => new(hintName, null, refusals.ToArray());
        void Refuse(DiagnosticDescriptor descriptor, LocationInfo where, params string[] arguments) =>
            refusals.Add(new DiagnosticInfo(descriptor, where, arguments));

        // The shape of the type: nothing else is checked until the generator could add to it.
        var chain = new List<INamedTypeSymbol>();
        for (INamedTypeSymbol? container = union; container != null; container = container.ContainingType)
            chain.Insert(0, container);
        INamedTypeSymbol? notPartial = chain.LastOrDefault(container => !IsPartial(container, cancellation));
        if (union.TypeKind != TypeKind.Struct)
            Refuse(NotPartialStruct, at, display, "is not a struct");
        else if (SymbolEqualityComparer.Default.Equals(notPartial, union))
            Refuse(NotPartialStruct, at, display, "is not partial");
        else if (notPartial != null)
            Refuse(NotPartialStruct, at, display, $"is inside '{notPartial.ToDisplayString()}', which is not partial");
        if (union.TypeKind == TypeKind.Struct && union.IsReadOnly)
            Refuse(CannotHoldViews, at, display, "readonly, so it cannot hand out writable views");
        if (chain.Any(static container => container.IsGenericType))
            Refuse(CannotHoldViews, at, display, "generic, or inside a generic type");
        if (union.IsRefLikeType)
            Refuse(CannotHoldViews, at, display, "a ref struct, which cannot be stored in a component");
        if (refusals.Count > 0)
            return Refused();

        string? noun = attributed.Attributes[0].NamedArguments
            .Where(static argument => argument.Key == "Noun")
            .Select(static argument => argument.Value.Value as string)
            .FirstOrDefault();

        IFieldSymbol[] fields = union.GetMembers().OfType<IFieldSymbol>()
            .Where(static field => !field.IsStatic && !field.IsConst && field.AssociatedSymbol == null)
            .ToArray();
        IFieldSymbol[] tags = fields.Where(static field => field.Type.TypeKind == TypeKind.Enum && OffsetOf(field) == 0).ToArray();
        if (tags.Length != 1)
        {
            Refuse(NoTag, at, display, tags.Length == 0 ? "no" : "more than one");
            return Refused();
        }
        IFieldSymbol tag = tags[0];
        var tagType = (INamedTypeSymbol)tag.Type;
        string tagDisplay = tagType.ToDisplayString();
        IFieldSymbol[] members = tagType.GetMembers().OfType<IFieldSymbol>().Where(static member => member.HasConstantValue).ToArray();

        IFieldSymbol? zero = members.FirstOrDefault(static member => System.Convert.ToDecimal(member.ConstantValue) == 0);
        if (zero == null)
            Refuse(BadTagValues, LocationOf(tag, at), display, tagDisplay, "has no member with value 0");
        foreach (var shared in members.GroupBy(static member => System.Convert.ToDecimal(member.ConstantValue))
                     .Where(static group => group.Count() > 1))
            Refuse(BadTagValues, LocationOf(tag, at), display, tagDisplay,
                $"gives {string.Join(" and ", shared.Select(static member => member.Name))} the same value, {shared.Key}");

        var kinds = new List<Kind>();
        var kindFields = new List<IFieldSymbol>();
        var misnamed = new List<IFieldSymbol>();
        foreach (IFieldSymbol field in fields)
        {
            cancellation.ThrowIfCancellationRequested();
            if (SymbolEqualityComparer.Default.Equals(field, tag))
                continue;
            IFieldSymbol? member = members.FirstOrDefault(member => FieldNameFor(member.Name) == field.Name);
            if (field.DeclaredAccessibility != Accessibility.Private)
                Refuse(BadKindField, LocationOf(field, at), display, field.Name, "is not private", tagDisplay);
            else if (field.IsReadOnly)
                Refuse(BadKindField, LocationOf(field, at), display, field.Name, "is readonly", tagDisplay);
            if (member == null)
            {
                misnamed.Add(field);
                continue;
            }
            kinds.Add(new Kind(member.Name, field.Type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat), field.Name));
            kindFields.Add(field);
        }
        // A misnamed field is usually the missing kind's, so the members without one are named in its error instead.
        string[] unpaired = members.Where(member => !kinds.Any(kind => kind.Member == member.Name))
            .Select(static member => member.Name).ToArray();
        foreach (IFieldSymbol field in misnamed)
            Refuse(BadKindField, LocationOf(field, at), display, field.Name,
                "is not named after a tag member" + (unpaired.Length == 0
                    ? ""
                    : $" ({string.Join(", ", unpaired.Select(member => $"{tagType.Name}.{member} would be {FieldNameFor(member)}"))})"),
                tagDisplay);
        if (misnamed.Count == 0)
            foreach (string member in unpaired)
                Refuse(MissingKind, at, display, tagDisplay, member, FieldNameFor(member));

        // Every kind at the first kind's offset, which is past the tag.
        int? offset = kindFields.Select(OffsetOf).FirstOrDefault();
        int tagSize = SizeOf(tagType.EnumUnderlyingType);
        foreach (IFieldSymbol field in kindFields)
        {
            int? fieldOffset = OffsetOf(field);
            if (fieldOffset == null || offset == null)
                continue;
            if (fieldOffset != offset)
                Refuse(BadOffset, LocationOf(field, at), display, field.Name, fieldOffset.ToString(), $"the first kind is at {offset}");
            else if (fieldOffset < tagSize)
                Refuse(BadOffset, LocationOf(field, at), display, field.Name, fieldOffset.ToString(),
                    $"the tag '{tag.Name}' takes {tagSize} byte{(tagSize == 1 ? "" : "s")}");
        }

        var types = new HashSet<ITypeSymbol>(SymbolEqualityComparer.Default);
        foreach (IFieldSymbol field in kindFields)
        {
            string type = field.Type.ToDisplayString();
            if (SymbolEqualityComparer.Default.Equals(field.Type, union))
                Refuse(BadKindType, LocationOf(field, at), display, field.Name, type, "is the union itself");
            else if (field.Type.TypeKind != TypeKind.Struct || !field.Type.IsUnmanagedType)
                Refuse(BadKindType, LocationOf(field, at), display, field.Name, type, "is not an unmanaged struct");
            else if (!types.Add(field.Type))
                Refuse(BadKindType, LocationOf(field, at), display, field.Name, type,
                    "another kind has too, so the two conversions from it would clash");
        }

        // The members the generator writes: the constructor, Type, the views, a conversion from each kind and Expect.
        var written = new HashSet<string>(kinds.Select(static kind => kind.Member)) { "Type", "Expect" };
        foreach (ISymbol declared in union.GetMembers())
        {
            if (declared.IsImplicitlyDeclared)
                continue;
            string? what = declared switch
            {
                IMethodSymbol { MethodKind: MethodKind.Constructor, Parameters.Length: 0 } => "a parameterless constructor",
                IMethodSymbol { MethodKind: MethodKind.Conversion } conversion
                    when conversion.Parameters.Length == 1 && types.Contains(conversion.Parameters[0].Type) =>
                    $"a conversion from '{conversion.Parameters[0].Type.ToDisplayString()}'",
                IMethodSymbol { MethodKind: not MethodKind.Ordinary } => null,
                _ when written.Contains(declared.Name) => $"'{declared.Name}'",
                _ => null,
            };
            if (what != null)
                Refuse(WrittenMember, LocationOf(declared, at), display, what);
        }
        if (refusals.Count > 0)
            return Refused();
        Kind initial = kinds.First(kind => kind.Member == zero!.Name);

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

        return new Model(hintName, Source(union, body.ToString()), System.Array.Empty<DiagnosticInfo>());
    }

    private static LocationInfo LocationOf(ISymbol symbol, LocationInfo fallback) =>
        symbol.Locations.Length > 0 && symbol.Locations[0].IsInSource ? LocationInfo.From(symbol.Locations[0]) : fallback;

    private static bool IsPartial(INamedTypeSymbol type, CancellationToken cancellation) =>
        type.DeclaringSyntaxReferences.All(reference =>
            reference.GetSyntax(cancellation) is TypeDeclarationSyntax declaration &&
            declaration.Modifiers.Any(SyntaxKind.PartialKeyword));

    // The size of an enum's underlying type.
    private static int SizeOf(INamedTypeSymbol? type) => type?.SpecialType switch
    {
        SpecialType.System_Byte or SpecialType.System_SByte => 1,
        SpecialType.System_Int16 or SpecialType.System_UInt16 => 2,
        SpecialType.System_Int64 or SpecialType.System_UInt64 => 8,
        _ => 4,
    };

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
