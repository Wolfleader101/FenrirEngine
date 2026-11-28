using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using System.Text;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using static Fenrir.Scripting.Generators.GeneratorSymbols;

namespace Fenrir.Scripting.Generators;

/// <summary>
/// For every class implementing <c>Fenrir.Scripting.IDataAsset</c> (ADR-0083 §1), writes its codecs and description
/// entry, and the script's <c>DataAssetTable</c>, which registers them with <c>DataAssetCodecs</c>:
/// <list type="bullet">
/// <item>the entry: canonical name (the full C# name), version, former names, base, flags, fields with kinds, targets
/// and metadata, the structs they hold, the upgrader versions, and the default, a <c>new T()</c> through the codec;</item>
/// <item><c>ToTree</c> and <c>FromTree</c>, base fields first, a field at its default left out, an absent field
/// defaulted and an unknown one dropped;</item>
/// <item>the <c>[Upgrader]</c> methods, by the version they take.</item>
/// </list>
/// A class marked <c>[NativeDataAsset]</c> is the generated class of a C++ type and has its codecs already, so it is
/// skipped. What a data asset may not hold is refused here, each with its own error at the member.
/// </summary>
[Generator]
public sealed class DataAssetGenerator : IIncrementalGenerator
{
    private const string DataAssetInterface = "Fenrir.Scripting.IDataAsset";
    private const string NativeDataAssetAttribute = "Fenrir.Scripting.NativeDataAssetAttribute";
    private const string NativeComponentAttribute = "Fenrir.Scripting.NativeComponentAttribute";
    private const string UpgraderAttribute = "Fenrir.Scripting.UpgraderAttribute";
    private const string VersionAttribute = "Fenrir.Scripting.VersionAttribute";
    private const string FormerlyAttribute = "Fenrir.Scripting.FormerlyAttribute";
    private const string PathAttribute = "Fenrir.Scripting.PathAttribute";
    private const string TransientAttribute = "Fenrir.Scripting.TransientAttribute";
    private const string EditorOnlyAttribute = "Fenrir.Scripting.EditorOnlyAttribute";
    private const string FixedStringAttribute = "Fenrir.Scripting.FixedStringAttribute";
    private const string InlineArrayAttribute = "System.Runtime.CompilerServices.InlineArrayAttribute";
    private const string TreeNodeType = "Fenrir.Scripting.TreeNode";

    private const string Node = "global::Fenrir.Scripting.TreeNode";
    private const string Codec = "global::Fenrir.Scripting.TreeCodec";
    private const string Codecs = "global::Fenrir.Scripting.DataAssetCodecs";
    private const string Reports = "global::Fenrir.Scripting.ScriptDataAssets";

    private static readonly DiagnosticDescriptor StructDataAsset = new(
        "FENRIR0007", "A data asset is a class",
        "'{0}' is a struct, but a data asset type is a class: the store holds its tree and hands each reader an instance (ADR-0083)",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor RefusedBase = new(
        "FENRIR0008", "A data asset extends a data asset",
        "Data asset '{0}' extends '{1}', but a data asset extends only object, another IDataAsset class, or the generated class of a C++ data asset type",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor RefusedKind = new(
        "FENRIR0009", "A data asset field of a kind no file holds",
        "Field '{0}' of data asset '{1}' is {2}, which a data asset cannot hold; use a number, bool, enum, string, List<T>, T?, AssetHandle<T> or a struct of those",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor EntityField = new(
        "FENRIR0017", "A data asset never holds an entity",
        "Field '{0}' of data asset '{1}' holds an Entity, but an entity belongs to one scene and a data asset to none (ADR-0075)",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor ClassByValue = new(
        "FENRIR0018", "A data asset holds no class by value",
        "Field '{0}' of data asset '{1}' holds the class '{2}' by value; a data asset names another through an AssetHandle<T> (ADR-0075)",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor BadUpgrader = new(
        "FENRIR0019", "An upgrader is a static method from TreeNode to TreeNode",
        "Upgrader '{0}' must be a static, non-generic, non-private method of a data asset class taking one TreeNode and returning a TreeNode",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor TwoUpgraders = new(
        "FENRIR0027", "One upgrader per version",
        "Upgrader '{0}' of data asset '{1}' takes version {2}, which '{3}' already does",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor GenericDataAsset = new(
        "FENRIR0028", "Generic data assets are not supported",
        "Data asset '{0}' is generic, or inside a generic type; a data asset type has one canonical name and one description",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor InaccessibleDataAsset = new(
        "FENRIR0029", "A data asset must be reachable from its codecs",
        "Data asset '{0}' is nested inside a private or protected type, so no codecs can be generated for it; make it internal or public",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor NotConstructible = new(
        "FENRIR0039", "A data asset is made with new T()",
        "Data asset '{0}' must be a non-abstract class with a public or internal parameterless constructor: its default and every value read are a new instance",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    // FenrirMath's bound types (glm's, ADR-0043): written as an array of their components in the order each lists them,
    // as the engine writes them, so a data asset holds them as C++ does.
    private static readonly Dictionary<string, (string Engine, string[] Components)> BoundTypes = new(StringComparer.Ordinal)
    {
        ["Fenrir.Math.Vector2"] = ("Fenrir::Math::Vec2", new[] { "X", "Y" }),
        ["Fenrir.Math.Vector3"] = ("Fenrir::Math::Vec3", new[] { "X", "Y", "Z" }),
        ["Fenrir.Math.Vector4"] = ("Fenrir::Math::Vec4", new[] { "X", "Y", "Z", "W" }),
        ["Fenrir.Math.Quaternion"] = ("Fenrir::Math::Quat", new[] { "X", "Y", "Z", "W" }),
        ["Fenrir.Math.Matrix4x4"] = ("Fenrir::Math::Mat4", Columns()),
    };

    private static string[] Columns()
    {
        var components = new List<string>();
        for (int column = 0; column < 4; ++column)
            foreach (string axis in new[] { "X", "Y", "Z", "W" })
                components.Add("Column" + column + "." + axis);
        return components.ToArray();
    }

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        IncrementalValuesProvider<Model> models = context.SyntaxProvider
            .CreateSyntaxProvider(
                static (node, _) => node is TypeDeclarationSyntax { BaseList: not null } and (ClassDeclarationSyntax or StructDeclarationSyntax or RecordDeclarationSyntax),
                static (syntax, cancellation) => Describe(syntax, cancellation))
            .Where(static model => model is not null)!;

        // An [Upgrader] anywhere but on a data asset class's method is refused there.
        IncrementalValuesProvider<DiagnosticInfo> strayUpgraders = context.SyntaxProvider
            .ForAttributeWithMetadataName(UpgraderAttribute,
                static (node, _) => node is MethodDeclarationSyntax,
                static (attribute, _) => StrayUpgrader(attribute))
            .Where(static diagnostic => diagnostic is not null)!;
        context.RegisterSourceOutput(strayUpgraders, static (output, diagnostic) => output.ReportDiagnostic(diagnostic.ToDiagnostic()));

        context.RegisterSourceOutput(models, static (output, model) =>
        {
            foreach (DiagnosticInfo diagnostic in model.Diagnostics)
                output.ReportDiagnostic(diagnostic.ToDiagnostic());
        });

        // The codecs, each struct's once however many data assets hold it, and the table registering them, bases first.
        context.RegisterSourceOutput(models.Collect(), static (output, all) =>
        {
            Model[] written = all.Where(static model => model.Codec != null).OrderBy(static model => model.Depth)
                                 .ThenBy(static model => model.Key, StringComparer.Ordinal).ToArray();
            if (written.Length == 0)
                return;
            var sources = new SortedDictionary<string, string>(StringComparer.Ordinal);
            foreach (Model model in written)
            {
                sources[model.ClassName] = model.Codec!;
                foreach (GeneratedSource structCodec in model.Structs)
                    sources[structCodec.ClassName] = structCodec.Source;
            }
            foreach (KeyValuePair<string, string> source in sources)
                output.AddSource(GeneratedNames.HintName(source.Key), source.Value);

            var table = new StringBuilder();
            table.Append("// <auto-generated>Fenrir.Scripting.Generators: the data asset types this script assembly defines.</auto-generated>\n#nullable enable\n");
            table.Append("namespace ").Append(GeneratedNames.Namespace).Append("\n{\n");
            table.Append("    public static class ").Append(GeneratedNames.DataAssetTable).Append("\n    {\n");
            table.Append("        /// <summary>\n");
            table.Append("        /// Registers every data asset type's codecs with DataAssetCodecs and reports the type to the engine, each base\n");
            table.Append("        /// before the types extending it. Only meaningful while the script assembly is loading.\n");
            table.Append("        /// </summary>\n");
            table.Append("        public static void ").Append(GeneratedNames.Register).Append("()\n        {\n");
            foreach (Model model in written)
            {
                table.Append("            ").Append(Codecs).Append(".Register(").Append(model.ClassName).Append(".Codec);\n");
                table.Append("            ").Append(Reports).Append(".Report<").Append(model.TypeCode).Append(">();\n");
            }
            table.Append("        }\n\n");
            // The engine binds this by name (ADR-0083): upgraders cross as one method, not as functions.
            table.Append("        /// <summary>\n");
            table.Append("        /// The tree, in the tree encoding, of a value of the data asset type <paramref name=\"name\"/> written at\n");
            table.Append("        /// <paramref name=\"fromVersion\"/>, upgraded one version by its upgrader; null when the script defines no type of\n");
            table.Append("        /// that name. The engine calls it for each step it upgrades a tree by.\n");
            table.Append("        /// </summary>\n");
            table.Append("        public static byte[]? ").Append(GeneratedNames.Upgrade).Append("(string name, int fromVersion, byte[] tree)\n        {\n");
            table.Append("            switch (name)\n            {\n");
            foreach (Model model in written)
            {
                table.Append("            case ").Append(Literal(model.Name)).Append(":\n");
                table.Append("                return ").Append(Reports).Append(".Upgrade<").Append(model.TypeCode).Append(">(fromVersion, tree);\n");
            }
            table.Append("            default:\n                return null;\n            }\n        }\n\n");
            table.Append("        /// <summary>\n");
            table.Append("        /// Drops this script's codecs from DataAssetCodecs, so nothing there keeps the assembly alive once its generation\n");
            table.Append("        /// is refused or stops. The engine calls it then.\n");
            table.Append("        /// </summary>\n");
            table.Append("        public static void ").Append(GeneratedNames.Forget).Append("() => ").Append(Codecs)
                 .Append(".Forget(typeof(").Append(GeneratedNames.DataAssetTable).Append(").Assembly);\n");
            table.Append("    }\n}\n");
            output.AddSource(GeneratedNames.HintName(GeneratedNames.DataAssetTable), table.ToString());
        });
    }

    private static DiagnosticInfo? StrayUpgrader(GeneratorAttributeSyntaxContext attribute)
    {
        if (attribute.TargetSymbol is not IMethodSymbol method)
            return null;
        INamedTypeSymbol? owner = method.ContainingType;
        if (owner != null && owner.TypeKind == TypeKind.Class && IsDataAsset(owner) && !HasAttribute(owner, NativeDataAssetAttribute))
            return null;
        return new DiagnosticInfo(BadUpgrader, LocationInfo.From(method.Locations[0]), new[] { method.Name });
    }

    private static Model? Describe(GeneratorSyntaxContext context, CancellationToken cancellation)
    {
        var declaration = (TypeDeclarationSyntax)context.Node;
        if (context.SemanticModel.GetDeclaredSymbol(declaration, cancellation) is not INamedTypeSymbol symbol)
            return null;
        if (!IsDataAsset(symbol))
            return null;
        // A partial type is seen once per declaration; only the first one counts.
        if (symbol.DeclaringSyntaxReferences[0].GetSyntax(cancellation) != declaration)
            return null;

        string key = symbol.ToDisplayString();
        LocationInfo location = LocationInfo.From(declaration.Identifier.GetLocation());
        var diagnostics = new List<DiagnosticInfo>();

        if (symbol.TypeKind == TypeKind.Struct)
            return Refused(key, diagnostics, StructDataAsset, location, symbol.Name);
        if (symbol.TypeKind != TypeKind.Class || HasAttribute(symbol, NativeDataAssetAttribute))
            return null;
        if (Chain(symbol).Any(static type => type.IsGenericType))
            return Refused(key, diagnostics, GenericDataAsset, location, symbol.Name);
        if (HasHiddenContainer(symbol))
            return Refused(key, diagnostics, InaccessibleDataAsset, location, symbol.Name);
        bool constructible = !symbol.IsAbstract && symbol.InstanceConstructors.Any(static constructor =>
            constructor.Parameters.Length == 0 &&
            constructor.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal or Accessibility.ProtectedOrInternal);
        if (!constructible)
            return Refused(key, diagnostics, NotConstructible, location, symbol.Name);

        // The base: object, an IDataAsset class (a script's, or the generated class of a C++ type), and nothing else.
        INamedTypeSymbol? baseType = symbol.BaseType;
        string? baseName = null;
        string? baseCode = null;
        if (baseType != null && baseType.SpecialType != SpecialType.System_Object)
        {
            if (baseType.TypeKind != TypeKind.Class || !IsDataAsset(baseType))
            {
                LocationInfo at = declaration.BaseList is { Types.Count: > 0 } list ? LocationInfo.From(list.Types[0].GetLocation()) : location;
                diagnostics.Add(new DiagnosticInfo(RefusedBase, at, new[] { symbol.Name, baseType.ToDisplayString() }));
                return Refused(key, diagnostics);
            }
            baseName = NativeName(baseType) ?? CanonicalName(baseType);
            baseCode = baseType.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        }

        var walk = new Walk(symbol.Name, diagnostics);
        var fields = new List<Member>();
        foreach (Member member in Members(symbol))
        {
            Shape? shape = walk.Classify(member, member.Name);
            if (shape != null)
                fields.Add(member with { Shape = shape });
        }

        // Upgraders: static TreeNode M(TreeNode), one per version.
        var upgraders = new List<(int Version, string Method)>();
        var byVersion = new Dictionary<int, string>();
        foreach (IMethodSymbol method in symbol.GetMembers().OfType<IMethodSymbol>())
        {
            AttributeData? upgrader = method.GetAttributes().FirstOrDefault(static a => a.AttributeClass?.ToDisplayString() == UpgraderAttribute);
            if (upgrader == null)
                continue;
            LocationInfo at = LocationInfo.From(method.Locations[0]);
            bool wellTyped = method.IsStatic && !method.IsGenericMethod && method.Parameters.Length == 1 &&
                             method.Parameters[0].RefKind == RefKind.None && method.Parameters[0].Type.ToDisplayString() == TreeNodeType &&
                             !method.ReturnsByRef && !method.ReturnsByRefReadonly && method.ReturnType.ToDisplayString() == TreeNodeType &&
                             method.DeclaredAccessibility is Accessibility.Public or Accessibility.Internal or Accessibility.ProtectedOrInternal;
            if (!wellTyped)
            {
                diagnostics.Add(new DiagnosticInfo(BadUpgrader, at, new[] { method.Name }));
                continue;
            }
            if (upgrader.ConstructorArguments.Length != 1 || upgrader.ConstructorArguments[0].Value is not int from)
                continue;
            if (byVersion.TryGetValue(from, out string? first))
            {
                diagnostics.Add(new DiagnosticInfo(TwoUpgraders, at,
                    new[] { method.Name, symbol.Name, from.ToString(CultureInfo.InvariantCulture), first }));
                continue;
            }
            byVersion.Add(from, method.Name);
            upgraders.Add((from, symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat) + "." + Identifier(method.Name)));
        }

        if (diagnostics.Count > 0)
            return Refused(key, diagnostics);

        int version = 1;
        if (symbol.GetAttributes().FirstOrDefault(static a => a.AttributeClass?.ToDisplayString() == VersionAttribute) is { } versionAttribute &&
            versionAttribute.ConstructorArguments.Length == 1 && versionAttribute.ConstructorArguments[0].Value is int given)
            version = given;

        int depth = 0;
        for (INamedTypeSymbol? at = symbol.BaseType; at != null && at.SpecialType != SpecialType.System_Object; at = at.BaseType)
            ++depth;

        string className = "DataAsset_" + Mangle(symbol);
        string typeCode = symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        string codec = ClassCodec(symbol, className, typeCode, CanonicalName(symbol), version, baseName, baseCode, fields, upgraders, walk);
        return new Model(key, className, typeCode, CanonicalName(symbol), codec, walk.Structs.ToImmutableArray(), depth,
                         diagnostics.ToImmutableArray());
    }

    // ---- What a data asset holds ----

    // A described member: a field or an auto-property, with its attributes read.
    private sealed record Member(string Name, ITypeSymbol Type, LocationInfo Location, bool Path, bool FixedString,
        string[] Formerly, string? Metadata, bool FixedBuffer, Shape? Shape = null);

    // The public instance fields that are not readonly, and the public read-write auto-properties, in declaration order,
    // but those marked [Transient].
    private static IEnumerable<Member> Members(INamedTypeSymbol type)
    {
        foreach (ISymbol symbol in type.GetMembers())
        {
            if (symbol.IsStatic || symbol.IsImplicitlyDeclared || symbol.DeclaredAccessibility != Accessibility.Public)
                continue;
            // A [Transient] field of a data asset is code wiring no file holds: it is left out of the description
            // altogether (ADR-0084), so it is neither checked nor written.
            if (HasAttribute(symbol, TransientAttribute))
                continue;
            ITypeSymbol memberType;
            bool fixedBuffer = false;
            if (symbol is IFieldSymbol field)
            {
                if (field.IsConst || field.IsReadOnly)
                    continue;
                memberType = field.Type;
                fixedBuffer = field.IsFixedSizeBuffer;
            }
            else if (symbol is IPropertySymbol property)
            {
                if (property.IsIndexer || property.GetMethod is not { DeclaredAccessibility: Accessibility.Public } ||
                    property.SetMethod is not { DeclaredAccessibility: Accessibility.Public, IsInitOnly: false } || !IsAutoProperty(property))
                    continue;
                memberType = property.Type;
            }
            else
            {
                continue;
            }
            LocationInfo location = LocationInfo.From(symbol.Locations[0]);
            string[] formerly = FormerNames(symbol);
            yield return new Member(symbol.Name, memberType, location, HasAttribute(symbol, PathAttribute),
                HasAttribute(symbol, FixedStringAttribute), formerly, MetadataCode(symbol), fixedBuffer);
        }
    }

    private static bool IsAutoProperty(IPropertySymbol property) =>
        property.ContainingType.GetMembers().OfType<IFieldSymbol>().Any(field => SymbolEqualityComparer.Default.Equals(field.AssociatedSymbol, property));

    // What a member's value is, as the codecs write and read it.
    private abstract record Shape;
    // A number or bool, as `Kind` (i32, u8, f32, bool, char16...) with its C# type.
    private sealed record PlainShape(string Kind, string Type) : Shape;
    private sealed record TextShape(bool Path) : Shape;
    // An enum: its underlying kind, its C# type and its canonical name.
    private sealed record EnumShape(string Kind, string Type, string Name) : Shape;
    private sealed record ListShape(Shape Element, string ElementType) : Shape;
    private sealed record OptionalShape(Shape Held, string HeldType) : Shape;
    private sealed record ReferenceShape(string Target, string Type) : Shape;
    // A struct the script defines: its own codec class.
    private sealed record StructShape(string Type, string Name, string ClassName) : Shape;
    // One of FenrirMath's bound types: an array of components.
    private sealed record BoundShape(string Type, string Engine, string[] Components) : Shape;
    // The generated mirror of a C++ struct, whose codecs its contract registers.
    private sealed record MirrorShape(string Type) : Shape;

    // Classifies members, writing the codec of each struct the script defines once.
    private sealed class Walk
    {
        private readonly string m_owner;
        private readonly List<DiagnosticInfo> m_diagnostics;
        private readonly Dictionary<string, StructShape> m_structs = new(StringComparer.Ordinal);

        public Walk(string owner, List<DiagnosticInfo> diagnostics)
        {
            m_owner = owner;
            m_diagnostics = diagnostics;
        }

        public List<GeneratedSource> Structs { get; } = new();

        // The entry of each enum the type's fields hold, once each, as code.
        public List<string> Enums { get; } = new();

        private readonly HashSet<string> m_enums = new(StringComparer.Ordinal);


        // The member's shape, or null with a diagnostic at `outer` (the data asset's own member) when it is refused.
        public Shape? Classify(Member member, string path, LocationInfo? outer = null)
        {
            LocationInfo at = outer ?? member.Location;
            if (member.FixedString && !(member.FixedBuffer || IsInlineArray(member.Type)))
                return Fail(SharedDiagnostics.FixedStringNotBytes, at, path, m_owner);
            if (member.FixedBuffer || IsInlineArray(member.Type))
                return Refuse(at, path, "a fixed-size buffer");
            Shape? shape = Classify(member.Type, path, at);
            if (shape == null)
                return null;
            if (member.Path)
            {
                if (shape is not TextShape)
                    return Refuse(at, path, "marked [Path] but not a string");
                return new TextShape(true);
            }
            return shape;
        }

        private Shape? Classify(ITypeSymbol type, string path, LocationInfo at)
        {
            string code = type.WithNullableAnnotation(NullableAnnotation.NotAnnotated).ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
            if (type is INamedTypeSymbol { OriginalDefinition.SpecialType: SpecialType.System_Nullable_T } nullable)
            {
                ITypeSymbol held = nullable.TypeArguments[0];
                Shape? heldShape = Classify(held, path, at);
                return heldShape == null ? null : new OptionalShape(heldShape, held.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat));
            }
            if (type.ToDisplayString() == "Fenrir.Scripting.Entity")
                return Fail(EntityField, at, path, m_owner);
            if (type is INamedTypeSymbol { TypeKind: TypeKind.Enum, EnumUnderlyingType: { } underlying } enumType)
            {
                var shape = new EnumShape(PlainKind(underlying)!, code, CanonicalName(enumType));
                if (m_enums.Add(shape.Name))
                    Enums.Add(EnumEntry(enumType, shape));
                return shape;
            }
            if (PlainKind(type) is { } plain)
                return new PlainShape(plain, code);
            if (type.SpecialType == SpecialType.System_String)
                return new TextShape(false);
            if (type is INamedTypeSymbol named && named.IsGenericType)
            {
                string definition = named.OriginalDefinition.ToDisplayString();
                if (definition == "System.Collections.Generic.List<T>")
                {
                    ITypeSymbol element = named.TypeArguments[0];
                    Shape? elementShape = Classify(element, path + "[]", at);
                    return elementShape == null
                        ? null
                        : new ListShape(elementShape, element.WithNullableAnnotation(NullableAnnotation.NotAnnotated).ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat));
                }
                if (definition == "Fenrir.Assets.AssetHandle<T>")
                    return new ReferenceShape(ReferenceTarget(named.TypeArguments[0]), code);
            }
            switch (type.TypeKind)
            {
            case TypeKind.Pointer:
            case TypeKind.FunctionPointer:
                return Refuse(at, path, "a pointer");
            case TypeKind.Array:
                return Refuse(at, path, "an array");
            case TypeKind.Delegate:
                return Refuse(at, path, "a delegate");
            case TypeKind.Interface:
                return Refuse(at, path, "an interface");
            case TypeKind.TypeParameter:
            case TypeKind.Dynamic:
                return Refuse(at, path, "of a type no description names");
            case TypeKind.Class:
                if (type.AllInterfaces.Any(static i => i.ToDisplayString() == "System.Collections.IEnumerable"))
                    return Refuse(at, path, "a collection other than List<T>");
                return Fail(ClassByValue, at, path, m_owner, type.ToDisplayString());
            case TypeKind.Struct:
                return Struct((INamedTypeSymbol)type, code, path, at);
            default:
                return Refuse(at, path, "of a type no description names");
            }
        }

        private Shape? Struct(INamedTypeSymbol type, string code, string path, LocationInfo at)
        {
            string name = type.ToDisplayString();
            if (BoundTypes.TryGetValue(name, out (string Engine, string[] Components) bound))
                return new BoundShape(code, bound.Engine, bound.Components);
            if (type.SpecialType != SpecialType.None || type.IsGenericType || type.IsRefLikeType)
                return Refuse(at, path, "a '" + name + "'");
            bool own = type.DeclaringSyntaxReferences.Length > 0 && !HasAttribute(type, NativeComponentAttribute);
            if (!own)
            {
                // The generated mirror of a C++ struct, in a Fenrir contract: its codecs come with it. An entity in it is
                // refused here all the same.
                string ns = type.ContainingNamespace?.ToDisplayString() ?? "";
                if (ns != "Fenrir" && !ns.StartsWith("Fenrir.", StringComparison.Ordinal))
                    return Refuse(at, path, "a '" + name + "', which no description names");
                if (HoldsEntity(type, new HashSet<string>(StringComparer.Ordinal)) is { } inner)
                    return Fail(EntityField, at, path + "." + inner, m_owner);
                return new MirrorShape(code);
            }
            if (HasHiddenContainer(type))
                return Refuse(at, path, "a struct inside a private or protected type");
            if (m_structs.TryGetValue(code, out StructShape? seen))
                return seen;
            var shape = new StructShape(code, CanonicalName(type), "DataAssetStruct_" + Mangle(type));
            // Seen before its fields are, so a struct holding a list of itself ends.
            m_structs.Add(code, shape);
            int errors = m_diagnostics.Count;
            var fields = new List<Member>();
            foreach (Member member in Members(type))
            {
                Shape? fieldShape = Classify(member, path + "." + member.Name, at);
                if (fieldShape != null)
                    fields.Add(member with { Shape = fieldShape });
            }
            if (m_diagnostics.Count != errors)
                return null;
            Structs.Add(new GeneratedSource(shape.ClassName, StructCodec(shape, fields)));
            return shape;
        }

        private Shape? Refuse(LocationInfo at, string path, string what) => Fail(RefusedKind, at, path, m_owner, what);

        private Shape? Fail(DiagnosticDescriptor descriptor, LocationInfo at, params string[] arguments)
        {
            m_diagnostics.Add(new DiagnosticInfo(descriptor, at, arguments));
            return null;
        }

        // The path to an Entity among a mirror's public fields, if one holds one.
        private static string? HoldsEntity(INamedTypeSymbol type, HashSet<string> seen)
        {
            if (!seen.Add(type.ToDisplayString()))
                return null;
            foreach (IFieldSymbol field in type.GetMembers().OfType<IFieldSymbol>())
            {
                if (field.IsStatic || field.DeclaredAccessibility != Accessibility.Public)
                    continue;
                if (field.Type.ToDisplayString() == "Fenrir.Scripting.Entity")
                    return field.Name;
                if (field.Type is INamedTypeSymbol { TypeKind: TypeKind.Struct } inner && HoldsEntity(inner, seen) is { } below)
                    return field.Name + "." + below;
            }
            return null;
        }
    }

    // ---- The code written ----

    // An enum's entry, as code: its canonical name, its underlying kind and its enumerators in declaration order.
    private static string EnumEntry(INamedTypeSymbol type, EnumShape shape)
    {
        bool unsigned = shape.Kind.StartsWith("u", StringComparison.Ordinal);
        var enumerators = new StringBuilder(Node + ".NewArray()");
        foreach (IFieldSymbol member in type.GetMembers().OfType<IFieldSymbol>())
        {
            if (!member.HasConstantValue || member.ConstantValue is null)
                continue;
            string number = unsigned
                ? Convert.ToUInt64(member.ConstantValue, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture) + "UL"
                : Convert.ToInt64(member.ConstantValue, CultureInfo.InvariantCulture).ToString(CultureInfo.InvariantCulture) + "L";
            enumerators.Append(".Add(").Append(Node).Append(".NewObject().Add(\"name\", ").Append(Node).Append(".From(").Append(Literal(member.Name))
                       .Append(")).Add(\"value\", ").Append(Node).Append(".From(").Append(number).Append(")))");
        }
        return Node + ".NewObject().Add(\"name\", " + Node + ".From(" + Literal(shape.Name) + ")).Add(\"kind\", " + Node + ".From(" +
               Literal(shape.Kind) + ")).Add(\"enumerators\", " + enumerators + ")";
    }

    private static string ClassCodec(INamedTypeSymbol symbol, string className, string typeCode, string name, int version, string? baseName,
        string? baseCode, List<Member> fields, List<(int Version, string Method)> upgraders, Walk walk)
    {
        var source = new StringBuilder();
        source.Append("// <auto-generated>Fenrir.Scripting.Generators: the codecs and description of the data asset ").Append(symbol.ToDisplayString())
              .Append(".</auto-generated>\n#nullable enable\n");
        source.Append("namespace ").Append(GeneratedNames.Namespace).Append("\n{\n");
        source.Append("    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]\n");
        source.Append("    internal static class ").Append(className).Append("\n    {\n");
        string codecType = "global::Fenrir.Scripting.DataAssetCodec<" + typeCode + ">";
        source.Append("        private static ").Append(codecType).Append("? s_codec;\n\n");
        source.Append("        public static ").Append(codecType).Append(" Codec => s_codec ??= new ").Append(codecType).Append("(\n");
        source.Append("            ").Append(Literal(name)).Append(", ").Append(version.ToString(CultureInfo.InvariantCulture)).Append(", ")
              .Append(baseName == null ? "null" : Literal(baseName)).Append(", Entry, WriteFields, ReadFields, static () => new ").Append(typeCode).Append("(),\n");
        if (upgraders.Count == 0)
        {
            source.Append("            null);\n\n");
        }
        else
        {
            source.Append("            new global::System.Collections.Generic.KeyValuePair<int, global::System.Func<").Append(Node).Append(", ").Append(Node).Append(">>[]\n            {\n");
            foreach ((int from, string method) in upgraders.OrderBy(static u => u.Version))
                source.Append("                new(").Append(from.ToString(CultureInfo.InvariantCulture)).Append(", ").Append(method).Append("),\n");
            source.Append("            });\n\n");
        }

        // The entry.
        source.Append("        private static ").Append(Node).Append(" Entry()\n        {\n");
        source.Append("            var fields = ").Append(Node).Append(".NewArray();\n");
        foreach (Member field in fields)
            source.Append(FieldEntry(field, "            "));
        source.Append("            var structs = ").Append(Node).Append(".NewArray();\n");
        foreach (string structClass in walk.Structs.Select(static s => s.ClassName))
            source.Append("            structs.Add(").Append(structClass).Append(".Entry());\n");
        source.Append("            var enums = ").Append(Node).Append(".NewArray();\n");
        foreach (string enumEntry in walk.Enums)
            source.Append("            enums.Add(").Append(enumEntry).Append(");\n");
        source.Append("            var upgraders = ").Append(Node).Append(".NewArray();\n");
        foreach ((int from, _) in upgraders.OrderBy(static u => u.Version))
            source.Append("            upgraders.Add(").Append(Node).Append(".From(").Append(from.ToString(CultureInfo.InvariantCulture)).Append("L));\n");
        string[] formerly = FormerNames(symbol);
        source.Append("            return ").Append(Node).Append(".NewObject()\n");
        source.Append("                .Add(\"name\", ").Append(Node).Append(".From(").Append(Literal(name)).Append("))\n");
        source.Append("                .Add(\"version\", ").Append(Node).Append(".From(").Append(version.ToString(CultureInfo.InvariantCulture)).Append("L))\n");
        source.Append("                .Add(\"formerly\", ").Append(Codec).Append(".Strings(").Append(string.Join(", ", formerly.Select(Literal))).Append("))\n");
        source.Append("                .Add(\"base\", ").Append(baseName == null ? Node + ".Null" : Node + ".From(" + Literal(baseName) + ")").Append(")\n");
        source.Append("                .Add(\"transient\", ").Append(Node).Append(".From(").Append(HasAttribute(symbol, TransientAttribute) ? "true" : "false").Append("))\n");
        source.Append("                .Add(\"editorOnly\", ").Append(Node).Append(".From(").Append(HasAttribute(symbol, EditorOnlyAttribute) ? "true" : "false").Append("))\n");
        source.Append("                .Add(\"fields\", fields)\n");
        source.Append("                .Add(\"structs\", structs)\n");
        source.Append("                .Add(\"enums\", enums)\n");
        source.Append("                .Add(\"upgraders\", upgraders)\n");
        source.Append("                .Add(\"default\", Codec.ToTree(new ").Append(typeCode).Append("(), null));\n");
        source.Append("        }\n\n");

        // The codecs.
        source.Append("        private static void WriteFields(").Append(Node).Append(" tree, ").Append(typeCode).Append(" value, ").Append(typeCode).Append("? defaults)\n        {\n");
        if (baseCode != null)
            source.Append("            ").Append(Codecs).Append(".Require<").Append(baseCode).Append(">().WriteFields(tree, value, defaults);\n");
        source.Append("            bool full = defaults is null;\n");
        foreach (Member field in fields)
            source.Append(WriteField(field, "defaults!", "            "));
        source.Append("        }\n\n");
        source.Append("        private static void ReadFields(").Append(Node).Append(" tree, ").Append(typeCode).Append(" value)\n        {\n");
        if (baseCode != null)
            source.Append("            ").Append(Codecs).Append(".Require<").Append(baseCode).Append(">().ReadFields(tree, value);\n");
        foreach (Member field in fields)
            source.Append(ReadField(field, "            "));
        source.Append("        }\n    }\n}\n");
        return source.ToString();
    }

    private static string StructCodec(StructShape shape, List<Member> fields)
    {
        var source = new StringBuilder();
        source.Append("// <auto-generated>Fenrir.Scripting.Generators: the codecs of the struct ").Append(shape.Name).Append(" in data assets.</auto-generated>\n#nullable enable\n");
        source.Append("namespace ").Append(GeneratedNames.Namespace).Append("\n{\n");
        source.Append("    [global::System.ComponentModel.EditorBrowsable(global::System.ComponentModel.EditorBrowsableState.Never)]\n");
        source.Append("    internal static class ").Append(shape.ClassName).Append("\n    {\n");
        source.Append("        public static ").Append(Node).Append(" Entry()\n        {\n");
        source.Append("            var fields = ").Append(Node).Append(".NewArray();\n");
        foreach (Member field in fields)
            source.Append(FieldEntry(field, "            "));
        source.Append("            return ").Append(Node).Append(".NewObject().Add(\"name\", ").Append(Node).Append(".From(").Append(Literal(shape.Name))
              .Append(")).Add(\"fields\", fields);\n        }\n\n");
        source.Append("        public static ").Append(Node).Append(" ToTree(").Append(shape.Type).Append(" value, ").Append(shape.Type).Append(" defaults, bool full)\n        {\n");
        source.Append("            var tree = ").Append(Node).Append(".NewObject();\n");
        foreach (Member field in fields)
            source.Append(WriteField(field, "defaults", "            "));
        source.Append("            return tree;\n        }\n\n");
        source.Append("        public static ").Append(shape.Type).Append(" FromTree(").Append(Node).Append(" tree, ").Append(shape.Type).Append(" fallback)\n        {\n");
        source.Append("            if (tree.Kind != global::Fenrir.Scripting.TreeKind.Object)\n                return fallback;\n");
        source.Append("            var value = fallback;\n");
        foreach (Member field in fields)
            source.Append(ReadField(field, "            "));
        source.Append("            return value;\n        }\n    }\n}\n");
        return source.ToString();
    }

    // Adds one field to `tree`: every field when `full`, else one whose whole tree differs from its default's, written
    // against that default. A disengaged optional is left out, but for null when its default holds a value. The field's
    // tree is a local function, written once.
    private static string WriteField(Member field, string defaults, string indent)
    {
        string key = Literal(field.Name);
        string value = "value." + Identifier(field.Name);
        string fallback = defaults + "." + Identifier(field.Name);
        bool optional = field.Shape is OptionalShape;
        Shape shape = field.Shape is OptionalShape held ? held.Held : field.Shape!;
        string type = field.Shape is OptionalShape heldType ? heldType.HeldType : TypeOf(shape);
        string fresh = New(shape, type);
        string inner = indent + "    ";
        var code = new StringBuilder();
        code.Append(indent).Append("{\n");
        code.Append(inner).Append("static ").Append(Node).Append(" Write(").Append(type).Append(" v, ").Append(type).Append(" d, bool f) => ")
            .Append(Write(shape, "v", "d", "f", 0)).Append(";\n");
        if (optional)
        {
            code.Append(inner).Append("if (").Append(value).Append(" is { } held)\n").Append(inner).Append("{\n");
            code.Append(inner).Append("    if (full)\n");
            code.Append(inner).Append("        tree.Add(").Append(key).Append(", Write(held, ").Append(fresh).Append(", true));\n");
            code.Append(inner).Append("    else if (").Append(fallback).Append(" is not { } heldDefault)\n");
            code.Append(inner).Append("        tree.Add(").Append(key).Append(", Write(held, ").Append(fresh).Append(", false));\n");
            code.Append(inner).Append("    else if (!").Append(Same(shape, "held", "heldDefault", fresh)).Append(")\n");
            code.Append(inner).Append("        tree.Add(").Append(key).Append(", Write(held, heldDefault, false));\n");
            code.Append(inner).Append("}\n");
            code.Append(inner).Append("else if (full || ").Append(fallback).Append(" is not null)\n");
            code.Append(inner).Append("{\n");
            code.Append(inner).Append("    tree.Add(").Append(key).Append(", ").Append(Node).Append(".Null);\n");
            code.Append(inner).Append("}\n");
        }
        else
        {
            code.Append(inner).Append("if (full)\n");
            code.Append(inner).Append("    tree.Add(").Append(key).Append(", Write(").Append(value).Append(", ").Append(fresh).Append(", true));\n");
            code.Append(inner).Append("else if (!").Append(Same(shape, value, fallback, fresh)).Append(")\n");
            code.Append(inner).Append("    tree.Add(").Append(key).Append(", Write(").Append(value).Append(", ").Append(fallback).Append(", false));\n");
        }
        code.Append(indent).Append("}\n");
        return code.ToString();
    }

    // Whether two values of a shape are the same, as the bridge compares them: a reference by its handle, anything else
    // by its whole tree, a float by its bits.
    private static string Same(Shape shape, string a, string b, string fresh) => shape is ReferenceShape
        ? "((" + a + ").Index == (" + b + ").Index && (" + a + ").Generation == (" + b + ").Generation)"
        : "Write(" + a + ", " + fresh + ", true).SameAs(Write(" + b + ", " + fresh + ", true))";

    // The C# type a shape's values have.
    private static string TypeOf(Shape shape) => shape switch
    {
        PlainShape plain => plain.Type,
        TextShape => "string?",
        EnumShape enumShape => enumShape.Type,
        ReferenceShape reference => reference.Type,
        ListShape list => "global::System.Collections.Generic.List<" + list.ElementType + ">?",
        OptionalShape optional => optional.HeldType + "?",
        StructShape structShape => structShape.Type,
        BoundShape bound => bound.Type,
        MirrorShape mirror => mirror.Type,
        _ => throw new InvalidOperationException("Unknown shape"),
    };

    private static string ReadField(Member field, string indent)
    {
        string value = "value." + Identifier(field.Name);
        string formerly = string.Concat(field.Formerly.Select(static name => ", " + Literal(name)));
        return indent + "{\n" +
               indent + "    if (" + Codec + ".Find(tree, " + Literal(field.Name) + formerly + ") is { } node)\n" +
               indent + "        " + value + " = " + Read(field.Shape!, "node", value, 0) + ";\n" +
               indent + "}\n";
    }

    // The tree of `value`, of `shape`, written against `defaults` (an expression of its type) unless `full`.
    private static string Write(Shape shape, string value, string defaults, string full, int depth)
    {
        switch (shape)
        {
        case PlainShape plain:
            return Node + ".From((" + Cast(plain.Kind) + ")(" + value + "))";
        case TextShape:
            return Node + ".From((string?)(" + value + "))";
        case EnumShape:
            return Codec + ".WriteEnum(" + value + ")";
        case ReferenceShape reference:
            return Codec + ".WriteReference((" + value + ").Index, (" + value + ").Generation, " + Literal(reference.Target) + ")";
        case ListShape list:
        {
            string element = "e" + depth;
            string elementFull = "f" + depth;
            return Codec + ".WriteList(" + value + ", " + full + ", static (" + element + ", " + elementFull + ") => " +
                   Write(list.Element, element, New(list.Element, list.ElementType), elementFull, depth + 1) + ")";
        }
        case OptionalShape optional:
        {
            string held = "h" + depth;
            return "((" + value + ") is { } " + held + " ? " + Write(optional.Held, held, New(optional.Held, optional.HeldType), full, depth + 1) +
                   " : " + Node + ".Null)";
        }
        case StructShape structShape:
            return structShape.ClassName + ".ToTree(" + value + ", " + defaults + ", " + full + ")";
        case BoundShape bound:
            return Node + ".NewArray()" + string.Concat(bound.Components.Select(c => ".Add(" + Node + ".From((float)(" + value + ")." + c + "))"));
        case MirrorShape mirror:
            return Codecs + ".RequireValue<" + mirror.Type + ">().ToTree(" + value + ", " + defaults + ", " + full + ")";
        default:
            throw new InvalidOperationException("Unknown shape");
        }
    }

    // The value of `shape` that `node` holds, or `fallback` when it holds none it can take.
    private static string Read(Shape shape, string node, string fallback, int depth)
    {
        switch (shape)
        {
        case PlainShape plain:
            return PlainRead(plain, node, fallback);
        case TextShape:
            return Codec + ".ReadString(" + node + ", " + fallback + " ?? \"\")";
        case EnumShape:
            return Codec + ".ReadEnum(" + node + ", " + fallback + ")";
        case ReferenceShape reference:
            return Codec + ".ReadReference(" + node + ", " + Literal(reference.Target) + ", " + fallback + ", static (i, g) => new " + reference.Type + "(i, g))";
        case ListShape list:
        {
            string element = "e" + depth;
            string elementFallback = "f" + depth;
            // A list read over a null fallback from a node that is no array stays null, as it was.
            return Codec + ".ReadList(" + node + ", " + fallback + ", " + New(list.Element, list.ElementType) + ", static (" + element + ", " + elementFallback + ") => " +
                   Read(list.Element, element, elementFallback, depth + 1) + ")!";
        }
        case OptionalShape optional:
        {
            string held = "n" + depth;
            string heldFallback = "f" + depth;
            return Codec + ".ReadOptional(" + node + ", " + fallback + ", " + New(optional.Held, optional.HeldType) + ", static (" + held + ", " + heldFallback + ") => " +
                   Read(optional.Held, held, heldFallback, depth + 1) + ")";
        }
        case StructShape structShape:
            return structShape.ClassName + ".FromTree(" + node + ", " + fallback + ")";
        case BoundShape bound:
        {
            string components = "c" + depth;
            return "(" + Codec + ".ReadFloats(" + node + ", " + bound.Components.Length + ") is { } " + components + " ? " +
                   BoundValue(bound, components) + " : " + fallback + ")";
        }
        case MirrorShape mirror:
            return Codecs + ".RequireValue<" + mirror.Type + ">().FromTree(" + node + ", " + fallback + ")";
        default:
            throw new InvalidOperationException("Unknown shape");
        }
    }

    // A bound type made from its components: Matrix4x4's columns are each a Vector4.
    private static string BoundValue(BoundShape bound, string components)
    {
        if (bound.Components.Any(static c => c.Contains('.')))
        {
            var columns = new List<string>();
            for (int column = 0; column < 4; ++column)
            {
                IEnumerable<string> axes = new[] { "X", "Y", "Z", "W" }.Select((axis, i) => axis + " = " + components + "[" + (column * 4 + i) + "]");
                columns.Add("Column" + column + " = new global::Fenrir.Math.Vector4 { " + string.Join(", ", axes) + " }");
            }
            return "new " + bound.Type + " { " + string.Join(", ", columns) + " }";
        }
        return "new " + bound.Type + " { " + string.Join(", ", bound.Components.Select((c, i) => c + " = " + components + "[" + i + "]")) + " }";
    }

    private static string PlainRead(PlainShape plain, string node, string fallback)
    {
        string type = plain.Type;
        switch (plain.Kind)
        {
        case "f32":
            return Codec + ".ReadSingle(" + node + ", " + fallback + ")";
        case "f64":
            return Codec + ".ReadDouble(" + node + ", " + fallback + ")";
        case "bool":
            return Codec + ".ReadBool(" + node + ", " + fallback + ")";
        case "u8":
        case "u16":
        case "u32":
        case "u64":
        case "char16":
            return "(" + type + ")" + Codec + ".ReadUnsigned(" + node + ", " + type + ".MaxValue, " + fallback + ")";
        default:
            return "(" + type + ")" + Codec + ".ReadSigned(" + node + ", " + type + ".MinValue, " + type + ".MaxValue, " + fallback + ")";
        }
    }

    // The C# type a plain kind is written through.
    private static string Cast(string kind) => kind switch
    {
        "f32" => "float",
        "f64" => "double",
        "bool" => "bool",
        "u8" or "u16" or "u32" or "u64" or "char16" => "ulong",
        _ => "long",
    };

    // A new value of the shape: the default a field or element takes when its tree cannot be read.
    private static string New(Shape shape, string type) => shape switch
    {
        TextShape => "\"\"",
        PlainShape plain => "default(" + plain.Type + ")",
        EnumShape enumShape => "default(" + enumShape.Type + ")",
        ReferenceShape reference => "default(" + reference.Type + ")",
        ListShape list => "new global::System.Collections.Generic.List<" + list.ElementType + ">()",
        OptionalShape optional => "(" + optional.HeldType + "?)null",
        StructShape structShape => "new " + structShape.Type + "()",
        BoundShape bound => "new " + bound.Type + "()",
        MirrorShape mirror => "new " + mirror.Type + "()",
        _ => "default(" + type + ")",
    };

    // The field's description in the entry: name, type, former names, transient, metadata.
    private static string FieldEntry(Member field, string indent)
    {
        var code = new StringBuilder();
        code.Append(indent).Append("{\n");
        code.Append(indent).Append("    var field = ").Append(Node).Append(".NewObject().Add(\"name\", ").Append(Node).Append(".From(").Append(Literal(field.Name))
            .Append(")).Add(\"type\", ").Append(TypeEntry(field.Shape!)).Append(");\n");
        if (field.Formerly.Length > 0)
            code.Append(indent).Append("    field.Add(\"formerly\", ").Append(Codec).Append(".Strings(").Append(string.Join(", ", field.Formerly.Select(Literal))).Append("));\n");
        if (field.Metadata != null)
            code.Append(field.Metadata.Replace("$indent", indent + "    "));
        code.Append(indent).Append("    fields.Add(field);\n");
        code.Append(indent).Append("}\n");
        return code.ToString();
    }

    private static string TypeEntry(Shape shape) => shape switch
    {
        PlainShape plain => Codec + ".Type(" + Literal(plain.Kind) + ")",
        EnumShape enumShape => Codec + ".Type(\"enum\", " + Literal(enumShape.Name) + ")",
        TextShape text => Codec + ".Type(" + Literal(text.Path ? "path" : "string") + ")",
        ReferenceShape reference => Codec + ".Type(\"reference\", " + Literal(reference.Target) + ")",
        ListShape list => Codec + ".Type(\"list\", null, " + TypeEntry(list.Element) + ")",
        OptionalShape optional => Codec + ".Type(\"optional\", null, " + TypeEntry(optional.Held) + ")",
        StructShape structShape => Codec + ".Type(\"struct\", " + Literal(structShape.Name) + ")",
        BoundShape bound => Codec + ".Type(\"struct\", " + Literal(bound.Engine) + ")",
        MirrorShape mirror => Codec + ".Type(\"struct\", " + Codecs + ".RequireValue<" + mirror.Type + ">().Name)",
        _ => throw new InvalidOperationException("Unknown shape"),
    };

    // The metadata attributes' entries, as code with "$indent" for the indent; null when the member has none.
    private static string? MetadataCode(ISymbol member)
    {
        var code = new StringBuilder();
        foreach (AttributeData attribute in member.GetAttributes())
        {
            ImmutableArray<TypedConstant> arguments = attribute.ConstructorArguments;
            switch (attribute.AttributeClass?.ToDisplayString())
            {
            case "Fenrir.Scripting.RangeAttribute" when arguments.Length == 2:
                code.Append("$indentfield.Add(\"range\", ").Append(Node).Append(".NewArray().Add(").Append(Node).Append(".From(").Append(Double(arguments[0]))
                    .Append(")).Add(").Append(Node).Append(".From(").Append(Double(arguments[1])).Append(")));\n");
                break;
            case "Fenrir.Scripting.StepAttribute" when arguments.Length == 1:
                code.Append("$indentfield.Add(\"step\", ").Append(Node).Append(".From(").Append(Double(arguments[0])).Append("));\n");
                break;
            case "Fenrir.Scripting.DisplayNameAttribute" when arguments.Length == 1:
                code.Append("$indentfield.Add(\"display\", ").Append(Node).Append(".From(").Append(Literal(arguments[0].Value as string ?? "")).Append("));\n");
                break;
            case "Fenrir.Scripting.GroupAttribute" when arguments.Length == 1:
                code.Append("$indentfield.Add(\"group\", ").Append(Node).Append(".From(").Append(Literal(arguments[0].Value as string ?? "")).Append("));\n");
                break;
            case "Fenrir.Scripting.UnitsAttribute" when arguments.Length == 1:
                code.Append("$indentfield.Add(\"units\", ").Append(Node).Append(".From(").Append(Literal(arguments[0].Value as string ?? "")).Append("));\n");
                break;
            case "Fenrir.Scripting.ReadOnlyAttribute":
                code.Append("$indentfield.Add(\"readonly\", ").Append(Node).Append(".From(true));\n");
                break;
            case "Fenrir.Scripting.HiddenAttribute":
                code.Append("$indentfield.Add(\"hidden\", ").Append(Node).Append(".From(true));\n");
                break;
            case "Fenrir.Scripting.MultilineAttribute":
                code.Append("$indentfield.Add(\"multiline\", ").Append(Node).Append(".From(true));\n");
                break;
            }
        }
        return code.Length == 0 ? null : code.ToString();
    }

    // A double constant as code, exactly.
    private static string Double(TypedConstant constant)
    {
        double value = constant.Value switch
        {
            double d => d,
            float f => f,
            int i => i,
            long l => l,
            _ => 0,
        };
        if (double.IsNaN(value))
            return "double.NaN";
        if (double.IsPositiveInfinity(value))
            return "double.PositiveInfinity";
        if (double.IsNegativeInfinity(value))
            return "double.NegativeInfinity";
        return value.ToString("R", CultureInfo.InvariantCulture) + "d";
    }

    // ---- Names ----

    // The names a type's or member's [Formerly] attributes give, in the order written.
    private static string[] FormerNames(ISymbol symbol) =>
        symbol.GetAttributes().Where(static a => a.AttributeClass?.ToDisplayString() == FormerlyAttribute)
              .Select(static a => a.ConstructorArguments.Length == 1 ? a.ConstructorArguments[0].Value as string : null)
              .Where(static name => name != null).Select(static name => name!).ToArray();

    private static bool IsDataAsset(INamedTypeSymbol type) => type.AllInterfaces.Any(static i => i.ToDisplayString() == DataAssetInterface);

    private static string? NativeName(INamedTypeSymbol type) =>
        type.GetAttributes().FirstOrDefault(static a => a.AttributeClass?.ToDisplayString() == NativeDataAssetAttribute) is { ConstructorArguments.Length: 1 } attribute
            ? attribute.ConstructorArguments[0].Value as string
            : null;

    // The canonical name of a type the script defines: its full name as the runtime spells it (`MyGame.Outer+Inner`),
    // so it never depends on how code spells it.
    private static string CanonicalName(INamedTypeSymbol type)
    {
        string ns = type.ContainingNamespace is { IsGlobalNamespace: false } containing ? containing.ToDisplayString() : "";
        string nested = string.Join("+", Chain(type).Select(static t => t.Name));
        return ns.Length == 0 ? nested : ns + "." + nested;
    }

    // The canonical name an asset handle names: an imported asset kind's engine type, or a data asset's.
    private static string ReferenceTarget(ITypeSymbol target)
    {
        if (target is not INamedTypeSymbol named)
            return target.ToDisplayString();
        string ns = named.ContainingNamespace is { IsGlobalNamespace: false } containing ? containing.ToDisplayString() : "";
        if (ns == "Fenrir.Assets" && !IsDataAsset(named))
            return "Fenrir::" + named.Name;
        return NativeName(named) ?? CanonicalName(named);
    }

    // A generated class name for a type: its namespace and containers, `_` doubled so no two types share one.
    private static string Mangle(INamedTypeSymbol type)
    {
        string ns = type.ContainingNamespace is { IsGlobalNamespace: false } containing ? containing.ToDisplayString() : "";
        IEnumerable<string> parts = (ns.Length == 0 ? Array.Empty<string>() : ns.Split('.')).Concat(Chain(type).Select(static t => t.Name));
        return string.Join("_", parts.Select(static part => part.Replace("_", "__")));
    }

    private static bool IsInlineArray(ITypeSymbol type) =>
        type.GetAttributes().Any(static a => a.AttributeClass?.ToDisplayString() == InlineArrayAttribute);

    // The kind of a number or bool, as the API description spells it; null for anything else.
    private static string? PlainKind(ITypeSymbol type) => type.SpecialType switch
    {
        SpecialType.System_SByte => "i8",
        SpecialType.System_Int16 => "i16",
        SpecialType.System_Int32 => "i32",
        SpecialType.System_Int64 => "i64",
        SpecialType.System_IntPtr => "i64",
        SpecialType.System_Byte => "u8",
        SpecialType.System_UInt16 => "u16",
        SpecialType.System_UInt32 => "u32",
        SpecialType.System_UInt64 => "u64",
        SpecialType.System_UIntPtr => "u64",
        SpecialType.System_Single => "f32",
        SpecialType.System_Double => "f64",
        SpecialType.System_Boolean => "bool",
        SpecialType.System_Char => "char16",
        _ => null,
    };

    private static Model Refused(string key, List<DiagnosticInfo> diagnostics, DiagnosticDescriptor descriptor, LocationInfo location, string name)
    {
        diagnostics.Add(new DiagnosticInfo(descriptor, location, new[] { name }));
        return Refused(key, diagnostics);
    }

    // A class that gets no codecs, only its diagnostics.
    private static Model Refused(string key, List<DiagnosticInfo> diagnostics) =>
        new(key, "", "", "", null, ImmutableArray<GeneratedSource>.Empty, 0, diagnostics.ToImmutableArray());

    // One generated codec class: its name and its source.
    private sealed record GeneratedSource(string ClassName, string Source);

    // What one data asset class contributes: its C# type and canonical name, its codecs (null when refused), the structs it
    // holds, how many data asset classes it extends, and its diagnostics. It compares by value, so an unchanged class is
    // not regenerated.
    private sealed record Model(string Key, string ClassName, string TypeCode, string Name, string? Codec,
        ImmutableArray<GeneratedSource> Structs, int Depth, ImmutableArray<DiagnosticInfo> Diagnostics)
    {
        public bool Equals(Model? other) =>
            other is not null && Key == other.Key && ClassName == other.ClassName && TypeCode == other.TypeCode && Name == other.Name &&
            Codec == other.Codec && Depth == other.Depth && Structs.SequenceEqual(other.Structs) && Diagnostics.SequenceEqual(other.Diagnostics);

        public override int GetHashCode() => (Key, Codec).GetHashCode();
    }
}
