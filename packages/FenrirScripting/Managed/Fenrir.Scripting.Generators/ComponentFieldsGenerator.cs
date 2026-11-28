using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text;
using System.Threading;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace Fenrir.Scripting.Generators;

/// <summary>
/// For every struct implementing <c>Fenrir.Scripting.IComponent</c>, emits C# 14 extension properties on
/// <c>Ref&lt;T&gt;</c>, one per public or internal instance field, each returning <c>ref</c> to the field in the
/// component's storage (<c>ref readonly</c> for a <c>readonly</c> field, which scripts read but never write). A script
/// then writes <c>t.Position.X += 1</c> with no <c>ref</c> at the call site. Every component
/// also gets <c>ref readonly</c> properties on <c>ReadOnlyRef&lt;T&gt;</c>, and a component that only implements
/// <c>IReadOnlyComponent</c> gets nothing else, so scripts cannot write it. Native resources (<c>IResource</c> and
/// <c>IReadOnlyResource</c>, which extend the component interfaces) get the same accessors.
/// </summary>
[Generator]
public sealed class ComponentFieldsGenerator : IIncrementalGenerator
{
    private const string ComponentInterface = "Fenrir.Scripting.IComponent";
    private const string ReadOnlyComponentInterface = "Fenrir.Scripting.IReadOnlyComponent";
    private const string ReadOnlyResourceInterface = "Fenrir.Scripting.IReadOnlyResource";

    // Members of Ref<T> itself: a component field with one of these names would be shadowed, never reached.
    private static readonly string[] RefMembers = { "Value", "IsNull" };

    private static readonly DiagnosticDescriptor ShadowedField = new(
        "FENRIR0001", "A component field is hidden by a member of Ref<T>",
        "Field '{0}' of component '{1}' is not exposed on Ref<{1}> because Ref<T> already has a member with that name; use Value.{0}",
        "Fenrir.Scripting", DiagnosticSeverity.Warning, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor GenericComponent = new(
        "FENRIR0002", "Generic components are not supported",
        "Component '{0}' is generic; its fields are not exposed on Ref<T>. Components must be closed, unmanaged structs",
        "Fenrir.Scripting", DiagnosticSeverity.Warning, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor InaccessibleComponent = new(
        "FENRIR0003", "A component must be reachable from its accessors",
        "Component '{0}' is nested inside a private or protected type, so no accessors can be generated for it; make it internal or public",
        "Fenrir.Scripting", DiagnosticSeverity.Warning, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor ManagedComponent = new(
        "FENRIR0004", "A component must be an unmanaged struct",
        "Component '{0}' is not unmanaged: it holds a reference type or a managed field, which cannot live in the engine's storage",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static readonly DiagnosticDescriptor ReadOnlyComponent = new(
        "FENRIR0005", "A readonly component has nothing to write through",
        "Component '{0}' is a readonly struct, so none of its fields can be exposed as writable on Ref<T>; remove 'readonly' or make it an IReadOnlyComponent",
        "Fenrir.Scripting", DiagnosticSeverity.Warning, isEnabledByDefault: true);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        // Every struct is looked at, not only those with a base list: a partial struct may name IComponent on any part.
        IncrementalValuesProvider<ComponentModel> components = context.SyntaxProvider
            .CreateSyntaxProvider(
                static (node, _) => node is StructDeclarationSyntax,
                static (syntax, cancellation) => Describe(syntax, cancellation))
            .Where(static model => model is not null)!;

        // One table per script assembly: every component the script defines, reported to the engine as it loads with its
        // fields, and every struct those components hold, reported once. Offsets, sizes and defaults are measured on a
        // `new T()` at run time, since the generator computes no layout.
        context.RegisterSourceOutput(components.Collect(), static (output, models) =>
        {
            TableEntry[] entries = models.Where(static model => model.TableEntry != null).Select(static model => model.TableEntry!)
                                         .OrderBy(static entry => entry.TypeName, StringComparer.Ordinal).ToArray();
            if (entries.Length == 0)
                return;
            // A component another holds by value is reported as a component, never again as a struct.
            var reported = new HashSet<string>(entries.Select(static entry => entry.Key), StringComparer.Ordinal);
            bool needsUnsafe = entries.Any(static entry => entry.NeedsUnsafe);
            var table = new StringBuilder();
            table.Append("// <auto-generated>Fenrir.Scripting.Generators: the components this script assembly defines.</auto-generated>\n");
            table.Append("namespace ").Append(GeneratedNames.Namespace).Append("\n{\n");
            table.Append("    public static").Append(needsUnsafe ? " unsafe" : "").Append(" class ").Append(GeneratedNames.ComponentTable).Append("\n    {\n");
            table.Append("        public static void ").Append(GeneratedNames.Register).Append("()\n        {\n");
            foreach (TableEntry entry in entries)
            {
                table.Append("            {\n");
                table.Append("                var probe = new ").Append(entry.TypeName).Append("();\n");
                foreach (NestedReport nested in entry.Nested)
                {
                    if (reported.Add(nested.Key))
                        table.Append("                ").Append(nested.Code).Append('\n');
                }
                table.Append("                ").Append(entry.Report).Append('\n');
                table.Append("            }\n");
            }
            table.Append("        }\n    }\n}\n");
            output.AddSource(GeneratedNames.HintName(GeneratedNames.ComponentTable), table.ToString());
        });

        context.RegisterSourceOutput(components, static (output, model) =>
        {
            foreach (DiagnosticInfo diagnostic in model.Diagnostics)
                output.ReportDiagnostic(diagnostic.ToDiagnostic());
            if (model.Source.Length > 0)
                output.AddSource(model.HintName, model.Source);
        });
    }

    private static ComponentModel? Describe(GeneratorSyntaxContext context, CancellationToken cancellation)
    {
        var declaration = (StructDeclarationSyntax)context.Node;
        if (context.SemanticModel.GetDeclaredSymbol(declaration, cancellation) is not INamedTypeSymbol symbol)
            return null;
        // IComponent extends IReadOnlyComponent, so this finds both kinds.
        if (!symbol.AllInterfaces.Any(static i => i.ToDisplayString() == ReadOnlyComponentInterface))
            return null;
        // A writable component gets accessors on Ref<T> too; a read-only one only on ReadOnlyRef<T>.
        bool writable = symbol.AllInterfaces.Any(static i => i.ToDisplayString() == ComponentInterface);
        // A partial struct is seen once per declaration; only the first one emits.
        if (symbol.DeclaringSyntaxReferences[0].GetSyntax(cancellation) != declaration)
            return null;

        string key = symbol.ToDisplayString();
        LocationInfo location = LocationInfo.From(declaration.Identifier.GetLocation());
        var diagnostics = new List<DiagnosticInfo>();

        // Reasons there is nothing to generate. Each says so once, at the struct.
        if (symbol.IsGenericType)
            return Refused(key, diagnostics, GenericComponent, location, symbol.Name);
        if (!symbol.IsUnmanagedType)
            return Refused(key, diagnostics, ManagedComponent, location, symbol.Name);
        if (HasHiddenContainer(symbol))
            return Refused(key, diagnostics, InaccessibleComponent, location, symbol.Name);

        string fullName = symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
        bool needsUnsafe = false;
        // Two views of the same fields: accessors on Ref<T> (writable except for readonly fields), and ref readonly ones on
        // ReadOnlyRef<T>.
        var writableMembers = new StringBuilder();
        var readOnlyMembers = new StringBuilder();
        foreach (IFieldSymbol field in symbol.GetMembers().OfType<IFieldSymbol>())
        {
            // Backing fields, statics, constants and fixed buffers have no property.
            if (field.IsStatic || field.IsConst || field.IsImplicitlyDeclared || field.IsFixedSizeBuffer)
                continue;
            if (field.DeclaredAccessibility is not (Accessibility.Public or Accessibility.Internal or Accessibility.ProtectedOrInternal))
                continue;
            if (Array.IndexOf(RefMembers, field.Name) >= 0)
            {
                LocationInfo at = field.Locations.Length > 0 ? LocationInfo.From(field.Locations[0]) : location;
                diagnostics.Add(new DiagnosticInfo(ShadowedField, at, new[] { field.Name, symbol.Name }));
                continue;
            }
            // A pointer in a field's type needs the accessors class to be unsafe (the script project allows it).
            needsUnsafe |= ContainsPointer(field.Type);
            // An internal field is exposed only as widely as it is declared, so a public accessor never names an
            // internal type.
            string access = field.DeclaredAccessibility == Accessibility.Public ? "public" : "internal";
            string type = field.Type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat);
            string name = Identifier(field.Name);
            readOnlyMembers.Append("        ").Append(access).Append(" ref readonly ").Append(type).Append(' ').Append(name)
                           .Append(" => ref self.Value.").Append(name).Append(";\n");
            // A readonly field has no ref to give out for writing, so Ref<T> reads it in place as ReadOnlyRef<T> does. A
            // readonly struct gets nothing on Ref<T> (the generator warns below).
            if (writable && !symbol.IsReadOnly)
                writableMembers.Append("        ").Append(access).Append(field.IsReadOnly ? " ref readonly " : " ref ")
                               .Append(type).Append(' ').Append(name).Append(" => ref self.Value.").Append(name).Append(";\n");
        }
        if (writable && symbol.IsReadOnly)
            diagnostics.Add(new DiagnosticInfo(ReadOnlyComponent, location, new[] { symbol.Name }));

        string visibility = IsPubliclyVisible(symbol) ? "public" : "internal";
        string? ns = symbol.ContainingNamespace.IsGlobalNamespace ? null : symbol.ContainingNamespace.ToDisplayString();
        var source = new StringBuilder();
        source.Append("// <auto-generated>Fenrir.Scripting.Generators: field accessors for ").Append(symbol.Name).Append(".</auto-generated>\n");
        source.Append("#nullable enable\n");
        if (ns != null)
            source.Append("namespace ").Append(ns).Append("\n{\n");
        source.Append(visibility).Append(needsUnsafe ? " static unsafe class " : " static class ").Append(ClassName(symbol)).Append("\n{\n");
        if (writable && writableMembers.Length > 0)
        {
            source.Append("    extension(global::Fenrir.Scripting.Ref<").Append(fullName).Append("> self)\n    {\n");
            source.Append(writableMembers);
            source.Append("    }\n");
        }
        source.Append("    extension(global::Fenrir.Scripting.ReadOnlyRef<").Append(fullName).Append("> self)\n    {\n");
        source.Append(readOnlyMembers);
        source.Append("    }\n}\n");
        if (ns != null)
            source.Append("}\n");

        // A component the script defines (not one mirroring a native component, and not a native resource, which shares
        // the accessors but is no component) is reported to the engine as it loads.
        bool isNative = HasAttribute(symbol, NativeComponentAttribute);
        bool isResource = symbol.AllInterfaces.Any(static i => i.ToDisplayString() == ReadOnlyResourceInterface);
        TableEntry? tableEntry = writable && !isNative && !isResource && !symbol.IsReadOnly
            ? Report(symbol, key, fullName, diagnostics)
            : null;
        return new ComponentModel(key, source.ToString(), diagnostics.ToImmutableArray(), tableEntry);
    }

    // The table's report of one component: its fields, the structs they hold, and the flags its attributes give it.
    private static TableEntry Report(INamedTypeSymbol symbol, string key, string fullName, List<DiagnosticInfo> diagnostics)
    {
        var walk = new FieldWalk(diagnostics);
        string fields = walk.Fields(symbol, Probe);
        var flags = new List<string>();
        if (HasAttribute(symbol, TransientAttribute))
            flags.Add("global::Fenrir.Scripting.ScriptTypeFlags.Transient");
        if (HasAttribute(symbol, EditorOnlyAttribute))
            flags.Add("global::Fenrir.Scripting.ScriptTypeFlags.EditorOnly");
        string flagText = flags.Count == 0 ? "global::Fenrir.Scripting.ScriptTypeFlags.None" : string.Join(" | ", flags);
        string report = "global::Fenrir.Scripting.ScriptComponents.Report(in " + Probe + ", " + flagText + ", " + FieldArray(fields) + ");";
        return new TableEntry(fullName, key, walk.Nested.ToImmutableArray(), report, walk.NeedsUnsafe);
    }

    private const string Probe = "probe";
    private const string NativeComponentAttribute = "Fenrir.Scripting.NativeComponentAttribute";
    private const string TransientAttribute = "Fenrir.Scripting.TransientAttribute";
    private const string EditorOnlyAttribute = "Fenrir.Scripting.EditorOnlyAttribute";
    private const string FixedStringAttribute = "Fenrir.Scripting.FixedStringAttribute";
    private const string InlineArrayAttribute = "System.Runtime.CompilerServices.InlineArrayAttribute";

    // The engine's types that a C# mirror in Fenrir.Math lays out exactly, by canonical name: a field of one is described
    // by the engine's description, so it is written as the engine writes it.
    private static readonly Dictionary<string, string> MathMirrors = new(StringComparer.Ordinal)
    {
        ["Fenrir.Math.Vector2"] = "Fenrir::Math::Vec2",
        ["Fenrir.Math.Vector3"] = "Fenrir::Math::Vec3",
        ["Fenrir.Math.Vector4"] = "Fenrir::Math::Vec4",
        ["Fenrir.Math.Quaternion"] = "Fenrir::Math::Quat",
        ["Fenrir.Math.Matrix4x4"] = "Fenrir::Math::Mat4",
        ["Fenrir.Math.Colour"] = "Fenrir::Colour",
        ["Fenrir.Math.Bounds"] = "Fenrir::Math::AABB",
        ["Fenrir.Math.Ray"] = "Fenrir::Math::Ray",
    };

    private static readonly DiagnosticDescriptor FixedStringNotBytes = new(
        "FENRIR0006", "[FixedString] needs a byte buffer",
        "Field '{0}' of '{1}' is marked [FixedString], but only a fixed byte buffer or an [InlineArray] struct of one byte field holds UTF-8 text",
        "Fenrir.Scripting", DiagnosticSeverity.Error, isEnabledByDefault: true);

    private static bool HasAttribute(ISymbol symbol, string attribute) =>
        symbol.GetAttributes().Any(a => a.AttributeClass?.ToDisplayString() == attribute);

    private static string FieldArray(string fields) =>
        fields.Length == 0
            ? "global::System.ReadOnlySpan<global::Fenrir.Scripting.ScriptField>.Empty"
            : "new global::Fenrir.Scripting.ScriptField[] { " + fields + " }";

    // A ref to the variable `expression` names: the probe itself, or a field below it, which may be readonly.
    private static string RefTo(string expression) =>
        expression == Probe ? "ref " + Probe : "ref global::System.Runtime.CompilerServices.Unsafe.AsRef(in " + expression + ")";

    private static string Literal(string text) => SymbolDisplay.FormatLiteral(text, quote: true);

    // The kind of a plain value, as the API description spells it; null for anything that is not one.
    private static string? PlainKind(ITypeSymbol type)
    {
        if (type is INamedTypeSymbol { TypeKind: TypeKind.Enum, EnumUnderlyingType: { } underlying })
            type = underlying;
        return type.SpecialType switch
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
            _ => type.TypeKind is TypeKind.Pointer or TypeKind.FunctionPointer ? "pointer" : null,
        };
    }

    private static int PlainSize(ITypeSymbol type)
    {
        if (type is INamedTypeSymbol { TypeKind: TypeKind.Enum, EnumUnderlyingType: { } underlying })
            type = underlying;
        return type.SpecialType switch
        {
            SpecialType.System_SByte or SpecialType.System_Byte or SpecialType.System_Boolean => 1,
            SpecialType.System_Int16 or SpecialType.System_UInt16 or SpecialType.System_Char => 2,
            SpecialType.System_Int32 or SpecialType.System_UInt32 or SpecialType.System_Single => 4,
            _ => 8,
        };
    }

    private static bool IsPointer(ITypeSymbol type) => type.TypeKind is TypeKind.Pointer or TypeKind.FunctionPointer;

    // The element type and length of an [InlineArray] struct; null for any other type.
    private static (ITypeSymbol Element, int Length)? InlineArray(ITypeSymbol type)
    {
        AttributeData? attribute = type.GetAttributes().FirstOrDefault(static a => a.AttributeClass?.ToDisplayString() == InlineArrayAttribute);
        if (attribute == null || attribute.ConstructorArguments.Length != 1 || attribute.ConstructorArguments[0].Value is not int length)
            return null;
        IFieldSymbol? element = type.GetMembers().OfType<IFieldSymbol>().FirstOrDefault(static f => !f.IsStatic);
        return element == null ? null : (element.Type, length);
    }

    // Walks the public fields and fixed buffers of a component, and of every struct it holds, into the table's
    // ScriptField constructions, collecting the structs to report.
    private sealed class FieldWalk
    {
        private readonly List<DiagnosticInfo> m_diagnostics;
        private readonly HashSet<string> m_seen = new(StringComparer.Ordinal);

        public FieldWalk(List<DiagnosticInfo> diagnostics) => m_diagnostics = diagnostics;

        public List<NestedReport> Nested { get; } = new();
        public bool NeedsUnsafe { get; private set; }

        // The fields of `type`, whose value `path` names, measured from it.
        public string Fields(INamedTypeSymbol type, string path)
        {
            var fields = new List<string>();
            foreach (IFieldSymbol field in type.GetMembers().OfType<IFieldSymbol>())
            {
                // Statics, constants, backing fields and anything not public are not part of what a file holds.
                if (field.IsStatic || field.IsConst || field.IsImplicitlyDeclared || field.DeclaredAccessibility != Accessibility.Public)
                    continue;
                string? described = Field(type, field, path);
                if (described != null)
                    fields.Add(described);
            }
            return string.Join(", ", fields);
        }

        private string? Field(INamedTypeSymbol owner, IFieldSymbol field, string path)
        {
            string access = path + "." + Identifier(field.Name);
            bool fixedString = HasAttribute(field, FixedStringAttribute);
            bool transient = HasAttribute(field, TransientAttribute);
            string offsetOf(string at) => "global::Fenrir.Scripting.ScriptComponents.OffsetOf(" + RefTo(path) + ", " + RefTo(at) + ")";
            string sizeOf = "global::Fenrir.Scripting.ScriptComponents.SizeOf(" + RefTo(access) + ")";

            string make(string kind, string elementKind, string target, string offset, string size, int count) =>
                "new global::Fenrir.Scripting.ScriptField(" + Literal(field.Name) + ", " + Literal(kind) + ", " + Literal(elementKind) + ", " +
                target + ", " + offset + ", " + size + ", " + count + ", " + (transient ? "true" : "false") + ")";

            if (field.IsFixedSizeBuffer && field.Type is IPointerTypeSymbol pointer)
            {
                NeedsUnsafe = true;
                ITypeSymbol element = pointer.PointedAtType;
                string first = access + "[0]";
                int size = field.FixedSize * PlainSize(element);
                if (fixedString)
                {
                    if (element.SpecialType != SpecialType.System_Byte)
                        return Refuse(owner, field);
                    return make("fixed-string", "", "\"\"", offsetOf(first), size.ToString(), field.FixedSize);
                }
                string? elementKind = PlainKind(element);
                return elementKind == null ? null : make("array", elementKind, "\"\"", offsetOf(first), size.ToString(), field.FixedSize);
            }

            ITypeSymbol type = field.Type;
            if (IsPointer(type))
            {
                // A pointer cannot be a type argument, so it is measured by address, in the unsafe context its struct
                // already needs.
                NeedsUnsafe = true;
                return make("pointer", "", "\"\"", "(int)((byte*)&" + access + " - (byte*)&" + path + ")", "sizeof(void*)", 0);
            }
            if (InlineArray(type) is { } inline)
            {
                if (IsPointer(inline.Element))
                    return null;
                string first = access + "[0]";
                if (fixedString)
                {
                    if (inline.Element.SpecialType != SpecialType.System_Byte)
                        return Refuse(owner, field);
                    return make("fixed-string", "", "\"\"", offsetOf(access), sizeOf, inline.Length);
                }
                (string Kind, string Target)? element = Value(inline.Element, first);
                return element == null ? null : make("array", element.Value.Kind, element.Value.Target, offsetOf(access), sizeOf, inline.Length);
            }
            if (fixedString)
                return Refuse(owner, field);
            (string Kind, string Target)? value = Value(type, access);
            return value == null ? null : make(value.Value.Kind, "", value.Value.Target, offsetOf(access), sizeOf, 0);
        }

        // The kind and target (as code) of a value of `type` that `access` names, reporting the struct it is when the
        // engine describes no type it mirrors; null when it is nothing a field can describe.
        private (string Kind, string Target)? Value(ITypeSymbol type, string access)
        {
            if (PlainKind(type) is { } plain)
                return (plain, "\"\"");
            if (type is not INamedTypeSymbol named || named.TypeKind != TypeKind.Struct)
                return null;
            string name = named.ToDisplayString();
            if (name == "Fenrir.Scripting.Entity")
                return ("entity", "\"\"");
            if (named.IsGenericType && named.OriginalDefinition.ToDisplayString() == "Fenrir.Assets.AssetHandle<T>")
                return ("reference", Literal(ReferenceTarget(named.TypeArguments[0])));
            if (MathMirrors.TryGetValue(name, out string? mirrored))
                return ("struct", Literal(mirrored));
            if (InlineArray(named) != null)
                return null; // An array of arrays, which no description holds.
            if (m_seen.Add(name))
            {
                string fields = Fields(named, access);
                Nested.Add(new NestedReport(name, "global::Fenrir.Scripting.ScriptComponents.ReportStruct(" + RefTo(access) + ", " + FieldArray(fields) + ");"));
            }
            return ("struct", "global::Fenrir.Scripting.ScriptComponents.NameOf(" + RefTo(access) + ")");
        }

        // The canonical name an asset handle names: an imported asset kind's engine type, or a data asset's full name.
        private static string ReferenceTarget(ITypeSymbol target)
        {
            string ns = target.ContainingNamespace is { IsGlobalNamespace: false } containing ? containing.ToDisplayString() : "";
            if (ns == "Fenrir.Assets")
                return "Fenrir::" + target.Name;
            var chain = new List<string>();
            for (ISymbol? at = target; at is INamedTypeSymbol type; at = type.ContainingType)
                chain.Insert(0, at.Name);
            string nested = string.Join("+", chain);
            return ns.Length == 0 ? nested : ns + "." + nested;
        }

        private string? Refuse(INamedTypeSymbol owner, IFieldSymbol field)
        {
            LocationInfo at = LocationInfo.From(field.Locations[0]);
            m_diagnostics.Add(new DiagnosticInfo(FixedStringNotBytes, at, new[] { field.Name, owner.Name }));
            return null;
        }
    }

    private static ComponentModel Refused(string key, List<DiagnosticInfo> diagnostics, DiagnosticDescriptor descriptor,
        LocationInfo location, string name)
    {
        diagnostics.Add(new DiagnosticInfo(descriptor, location, new[] { name }));
        return new ComponentModel(key, "", diagnostics.ToImmutableArray(), null);
    }

    // Nested structs are named after their containers so two structs never share a class name.
    private static string ClassName(INamedTypeSymbol symbol) => string.Join("_", Chain(symbol).Select(static s => s.Name)) + "_Fields";

    // The struct and the types around it, outermost first.
    private static IEnumerable<INamedTypeSymbol> Chain(INamedTypeSymbol symbol)
    {
        var chain = new List<INamedTypeSymbol>();
        for (INamedTypeSymbol? current = symbol; current != null; current = current.ContainingType)
            chain.Insert(0, current);
        return chain;
    }

    private static bool IsPubliclyVisible(INamedTypeSymbol symbol) =>
        Chain(symbol).All(static s => s.DeclaredAccessibility == Accessibility.Public);

    // A top-level accessors class cannot name a type nested in something private or protected.
    private static bool HasHiddenContainer(INamedTypeSymbol symbol) =>
        Chain(symbol).Any(static s => s.DeclaredAccessibility is Accessibility.Private or Accessibility.Protected or Accessibility.ProtectedAndInternal);

    private static bool ContainsPointer(ITypeSymbol type) =>
        type.TypeKind is TypeKind.Pointer or TypeKind.FunctionPointer ||
        (type is INamedTypeSymbol named && named.TypeArguments.Any(ContainsPointer));

    private static string Identifier(string name) =>
        SyntaxFacts.GetKeywordKind(name) != SyntaxKind.None || SyntaxFacts.GetContextualKeywordKind(name) != SyntaxKind.None
            ? "@" + name
            : name;

    // A struct a component holds, reported once by the table: its key (its display name) and the call that reports it.
    private sealed record NestedReport(string Key, string Code);

    // What one component reports in the table: its type as code, its key, the structs it holds, the call reporting it,
    // and whether that code reads fixed buffers, which needs an unsafe context.
    private sealed record TableEntry(string TypeName, string Key, ImmutableArray<NestedReport> Nested, string Report, bool NeedsUnsafe)
    {
        public bool Equals(TableEntry? other) =>
            other is not null && TypeName == other.TypeName && Key == other.Key && Report == other.Report &&
            NeedsUnsafe == other.NeedsUnsafe && Nested.SequenceEqual(other.Nested);

        public override int GetHashCode() => (TypeName, Report).GetHashCode();
    }

    // What one component contributes. It compares by value, so an unchanged component is not regenerated.
    private sealed record ComponentModel(string TypeName, string Source, ImmutableArray<DiagnosticInfo> Diagnostics, TableEntry? TableEntry)
    {
        // The full name, so two structs of the same name in different namespaces never share a hint name.
        public string HintName => TypeName.Replace('<', '_').Replace('>', '_') + ".Fields.g.cs";

        public bool Equals(ComponentModel? other) =>
            other is not null && TypeName == other.TypeName && Source == other.Source && Equals(TableEntry, other.TableEntry) &&
            Diagnostics.SequenceEqual(other.Diagnostics);

        public override int GetHashCode() => (TypeName, Source).GetHashCode();
    }
}
