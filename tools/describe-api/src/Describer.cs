using System.Diagnostics;
using System.Globalization;
using System.Text;
using ClangSharp;
using ClangSharp.Interop;
using Type = ClangSharp.Type;

namespace DescribeApi;

/// <summary>
/// Reads a selection's headers with libclang and describes its types: layout, kinds, defaults and docs. Every type a
/// selected type's fields use is described too. Anything that is not plain data, or a default the description cannot
/// record as a value, fails with a diagnostic naming the type and member; all of them are reported at once.
/// </summary>
internal sealed class Describer
{
    private const string TaggedUnionShape = "one enum tag followed by one anonymous union";

    private readonly Selection m_selection;
    private readonly List<string> m_errors = [];

    // Described types by canonical clang spelling, so a field's type finds the name its type is described under.
    private readonly Dictionary<string, string> m_names = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Role> m_roles = new(StringComparer.Ordinal);
    private readonly Dictionary<string, EnumDecl> m_enums = new(StringComparer.Ordinal);
    private readonly Dictionary<string, TypeDescription> m_described = new(StringComparer.Ordinal);
    private readonly Queue<Pending> m_pending = new();

    private readonly Dictionary<string, string> m_sources = new(StringComparer.Ordinal);

    // Selected types that are specialisations of a class template the headers never instantiate (glm's quaternion,
    // when nothing in them holds one), which a second parse instantiates.
    private readonly IReadOnlyList<string> m_instantiate;
    private readonly List<string> m_uninstantiated = [];

    private Describer(Selection selection, IReadOnlyList<string> instantiate)
    {
        m_selection = selection;
        m_instantiate = instantiate;
    }

    private sealed record Pending(Type Type, string Name, Role Role, Decl Decl, string? ReachedFrom);

    // Which type each described type's field holds by value (the field's subject, `Ns::Type::member`), in the order met,
    // and each described type's fields of an owning kind: what the owning-kind check walks once every type is described.
    private readonly List<(string From, string Subject, string To)> m_holds = [];
    private readonly Dictionary<string, List<(FieldDecl At, string Subject, string Kind)>> m_owningFields =
        new(StringComparer.Ordinal);

    // The type being described, which a field's type is held by.
    private string? m_describing;

    /// <summary>Describes the selection, or throws <see cref="DescribeException" /> with every diagnostic.</summary>
    public static Description Describe(Selection selection, IReadOnlyList<string> compileArguments)
    {
        var describer = new Describer(selection, []);
        return describer.Run(compileArguments) ??
               new Describer(selection, describer.m_uninstantiated).Run(compileArguments)!;
    }

    // The description, or null when a selected type is a template specialisation that was never instantiated: its
    // layout is unknown until something needs it complete, so the caller parses again, instantiating it.
    private Description? Run(IReadOnlyList<string> compileArguments)
    {
        var directory = Path.GetDirectoryName(m_selection.Path)!;
        var mainPath = Path.Combine(directory, m_selection.Package + ".describe-api.cpp").Replace('\\', '/');
        var source = string.Concat(m_selection.Headers.Select(header => $"#include \"{header.Replace('\\', '/')}\"\n")) +
                     string.Concat(m_selection.Types.Select(type => type.Key).Where(IsSpecialisation)
                                       .Select(name => $"using {SpecialisationAlias(name)} = ::{name};\n")) +
                     string.Concat(m_instantiate.Select(name => $"static_assert(sizeof(::{name}) > 0);\n"));

        using var index = CXIndex.Create();
        using var unsaved = CXUnsavedFile.Create(mainPath, source);
        var arguments = Arguments(compileArguments);
        var error = CXTranslationUnit.TryParse(index, mainPath, arguments, [unsaved],
                                               CXTranslationUnit_Flags.CXTranslationUnit_None, out var handle);
        if (error != CXErrorCode.CXError_Success)
        {
            throw new DescribeException($"{m_selection.Path}: libclang could not read the headers ({error})");
        }

        using var translationUnit = TranslationUnit.GetOrCreate(handle);
        var clangErrors = new List<string>();
        for (uint i = 0; i < handle.NumDiagnostics; ++i)
        {
            using var diagnostic = handle.GetDiagnostic(i);
            if (diagnostic.Severity >= CXDiagnosticSeverity.CXDiagnostic_Error)
            {
                clangErrors.Add(diagnostic.Format(CXDiagnostic.DefaultDisplayOptions).ToString());
            }
        }

        if (clangErrors.Count > 0)
        {
            throw new DescribeException(string.Join("\n", clangErrors));
        }

        using (var target = handle.TargetInfo)
        {
            if (target.PointerWidth != Description.PointerSize * 8)
            {
                throw new DescribeException(
                    $"{m_selection.Path}: the target {target.Triple} has {target.PointerWidth}-bit pointers; " +
                    $"a description is read for {Description.PointerSize}-byte pointers");
            }
        }

        foreach (var (name, role) in m_selection.Types)
        {
            Select(translationUnit.TranslationUnitDecl, name, role);
        }

        if (m_instantiate.Count == 0 && m_uninstantiated.Count > 0)
        {
            return null;
        }

        while (m_pending.Count > 0)
        {
            var pending = m_pending.Dequeue();
            m_describing = pending.Name;
            try
            {
                m_described[pending.Name] = DescribeType(pending);
            }
            catch (DescribeException exception)
            {
                m_errors.Add(pending.ReachedFrom is null
                                 ? exception.Message
                                 : $"{exception.Message} (reached through {pending.ReachedFrom})");
            }
        }

        m_describing = null;
        CheckOwningKinds();
        MarkByMember();

        // A private-members name the selection does not select is a nested type some selected type holds, or nothing.
        foreach (var name in m_selection.PrivateMembers.Where(name => m_selection.Types.All(type => type.Key != name) &&
                                                                       !m_described.ContainsKey(name)))
        {
            m_errors.Add($"{m_selection.Path}: error: 'private-members' names {name}, which [types] does not select " +
                         "and no selected type holds");
        }

        if (m_errors.Count > 0)
        {
            throw new DescribeException(string.Join("\n", m_errors));
        }

        return new Description(m_selection.Package, InDependencyOrder());
    }

    internal static string[] Arguments(IReadOnlyList<string> compileArguments)
    {
        var arguments = new List<string> { "-x", "c++" };
        if (!compileArguments.Any(argument => argument.StartsWith("-std=", StringComparison.Ordinal)))
        {
            arguments.Add("-std=c++20");
        }

        // libclang's packages ship without clang's own headers (stddef.h and the like), which glibc's headers expect
        // the compiler to provide. Elsewhere borrow them from the clang on PATH when the arguments name none. Not on
        // Windows: the MSVC headers stand alone, and another clang version's intrinsics headers do not parse there.
        if (!OperatingSystem.IsWindows() &&
            !compileArguments.Any(argument => argument.StartsWith("-resource-dir", StringComparison.Ordinal)) &&
            ClangResourceDirectory() is { } resourceDirectory)
        {
            arguments.Add("-resource-dir");
            arguments.Add(resourceDirectory);
        }

        arguments.AddRange(compileArguments);
        return [.. arguments];
    }

    private static string? ClangResourceDirectory()
    {
        try
        {
            using var clang = Process.Start(new ProcessStartInfo("clang", "-print-resource-dir")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
            });
            if (clang is null)
            {
                return null;
            }

            var output = clang.StandardOutput.ReadToEnd().Trim();
            clang.WaitForExit();
            return clang.ExitCode == 0 && Directory.Exists(output) ? output : null;
        }
        catch (System.ComponentModel.Win32Exception)
        {
            return null;
        }
    }

    // --- Selecting and registering types ---

    private void Select(TranslationUnitDecl root, string name, Role role)
    {
        // A class template's specialisation (`Ns::Handle<Ns::Mesh>`) is found through the alias the main source declares
        // for it, since its argument list does not split into scopes.
        var found = IsSpecialisation(name)
                        ? Find(root.Decls, [SpecialisationAlias(name)], 0).FirstOrDefault()
                        : Find(root.Decls, name.Split("::"), 0).FirstOrDefault();
        if (found is null)
        {
            m_errors.Add($"{m_selection.Path}: error: {name}: no type of that name in the selected headers");
            return;
        }

        var type = found switch
        {
            TypedefNameDecl alias => alias.UnderlyingType,
            TypeDecl typeDecl => typeDecl.TypeForDecl,
            _ => throw new UnreachableException(),
        };
        var canonical = type.CanonicalType;
        var key = canonical.AsString;
        if (m_names.TryGetValue(key, out var existing))
        {
            m_errors.Add($"{Where(found)}: error: {name}: the same type as {existing}, selected twice");
            return;
        }

        if (canonical.Handle.kind == CXTypeKind.CXType_Enum)
        {
            if (role != Role.Enum)
            {
                m_errors.Add($"{Where(found)}: error: {name}: an enum takes no role; it is described when a field uses " +
                             "it, or when the selection names it as an enum");
                return;
            }

            // Described as an enum a field holds is: by its canonical name, with no role.
            RegisterReferenced(canonical, name);
            return;
        }

        if (role == Role.Enum)
        {
            m_errors.Add($"{Where(found)}: error: {name}: not an enum, so it cannot be selected as one");
            return;
        }

        var decl = found is TypedefNameDecl ? (Decl?)canonical.AsTagDecl ?? found : found;
        if (canonical.Handle.SizeOf < 0 && canonical.AsTagDecl is ClassTemplateSpecializationDecl)
        {
            m_uninstantiated.Add(name);
        }

        Register(canonical, name, role, decl, null);
        if (found is TypedefNameDecl && role == Role.BoundType && !IsSpecialisation(name))
        {
            // A bound type is often an alias: its own comment, if any, is the one the selection named.
            m_aliasDocs[name] = found;
        }
    }

    private readonly Dictionary<string, Decl> m_aliasDocs = new(StringComparer.Ordinal);

    // Whether a selected name is a class template's specialisation, which the selection spells with its arguments.
    private static bool IsSpecialisation(string name) => name.Contains('<', StringComparison.Ordinal);

    // The alias the main source declares for a selected specialisation, unique by its place in the selection.
    private string SpecialisationAlias(string name) =>
        $"FenrirDescribeApiSelected{m_selection.Types.Select(type => type.Key).ToList().IndexOf(name)}";

    private static IEnumerable<Decl> Find(IEnumerable<Decl> decls, string[] parts, int i)
    {
        var last = i == parts.Length - 1;
        foreach (var decl in decls)
        {
            switch (decl)
            {
                case NamespaceDecl ns when ns.Name == parts[i] && !last:
                    foreach (var found in Find(ns.Decls, parts, i + 1))
                    {
                        yield return found;
                    }

                    break;
                case LinkageSpecDecl linkage:
                    foreach (var found in Find(linkage.Decls, parts, i))
                    {
                        yield return found;
                    }

                    break;
                case CXXRecordDecl record when record.Name == parts[i] && !last:
                    foreach (var found in Find(record.Decls, parts, i + 1))
                    {
                        yield return found;
                    }

                    break;
                case TagDecl tag when tag.Name == parts[i] && last:
                    yield return tag;
                    break;
                case TypedefNameDecl alias when alias.Name == parts[i] && last:
                    yield return alias;
                    break;
            }
        }
    }

    private string Register(Type canonical, string name, Role role, Decl decl, string? reachedFrom)
    {
        var key = canonical.AsString;
        if (m_names.TryGetValue(key, out var existing))
        {
            return existing;
        }

        m_names[key] = name;
        m_roles[name] = role;
        m_pending.Enqueue(new Pending(canonical, name, role, decl, reachedFrom));
        return name;
    }

    // --- Describing types ---

    private TypeDescription DescribeType(Pending pending)
    {
        var type = pending.Type;
        var name = pending.Name;
        var size = type.Handle.SizeOf;
        var alignment = type.Handle.AlignOf;
        if (size < 0 || alignment < 0)
        {
            throw Error(pending.Decl, name, "an incomplete type: only declared in the selected headers");
        }

        var docDecl = m_aliasDocs.GetValueOrDefault(name) ?? pending.Decl;
        if (pending.Role == Role.BoundType && RawDoc(docDecl) is null && type.AsTagDecl is { } tagDecl)
        {
            // A bound type named through an alias with no comment of its own is documented by the type it names.
            docDecl = tagDecl;
        }

        var typeDoc = TypeDoc(docDecl, name);
        if (pending.Role == Role.BoundType)
        {
            var components = type.AsCXXRecordDecl?.Definition is CXXRecordDecl boundRecord ? BoundComponents(boundRecord) : null;
            return new BoundTypeDescription(name, size, alignment, typeDoc.Text, components) { Commands = typeDoc.Commands };
        }

        if (type.AsTagDecl?.Definition is EnumDecl enumDecl)
        {
            return DescribeEnum(enumDecl, name, size, alignment, typeDoc);
        }

        var doc = typeDoc.Text;

        if (type.AsCXXRecordDecl?.Definition is not CXXRecordDecl record)
        {
            throw Error(pending.Decl, name, $"'{type.AsString}' is not a class, struct or enum");
        }

        if (record.IsUnion)
        {
            throw Error(record, name, $"a union outside the tagged-union shape ({TaggedUnionShape})");
        }

        CheckPlainData(record, name, pending.Role);
        var baseName = pending.Role == Role.DataAsset ? DataAssetBase(record, name) : null;
        var fields = record.Fields;
        if (IsTaggedUnionShape(fields))
        {
            return DescribeTaggedUnion(record, name, pending.Role, size, alignment, doc) with { Commands = typeDoc.Commands };
        }

        var described = new List<FieldDescription>();
        var failures = new List<string>();
        foreach (var field in fields)
        {
            try
            {
                described.Add(DescribeField(name, field));
            }
            catch (DescribeException exception)
            {
                failures.Add(exception.Message);
            }
        }

        if (failures.Count > 0)
        {
            throw new DescribeException(string.Join("\n", failures));
        }

        return new StructDescription(name, size, alignment, doc, pending.Role, described)
        {
            Commands = typeDoc.Commands,
            Base = baseName,
        };
    }

    private EnumDescription DescribeEnum(EnumDecl enumDecl, string name, long size, long alignment,
                                         DocComment.TypeDoc doc)
    {
        var underlying = IntegerKind(enumDecl.IntegerType.CanonicalType) ??
                         throw Error(enumDecl, name, $"an underlying type '{enumDecl.IntegerType.AsString}' that is not an integer kind");
        var unsigned = underlying.StartsWith('u');
        var values = enumDecl.Enumerators
                         .Select(enumerator => new EnumValue(
                                     enumerator.Name,
                                     unsigned ? enumerator.UnsignedInitVal.ToString(CultureInfo.InvariantCulture)
                                              : enumerator.InitVal.ToString(CultureInfo.InvariantCulture),
                                     PlainDoc(enumerator, $"{name}::{enumerator.Name}", "an enumerator",
                                              enumDecl.Enumerators.TakeWhile(e => e != enumerator).LastOrDefault())))
                         .ToList();
        return new EnumDescription(name, size, alignment, doc.Text, underlying, values) { Commands = doc.Commands };
    }

    // The rules every role keeps, and those only the owning roles lift: a data asset's base is checked by
    // DataAssetBase, and a data asset or settings type may declare its own copy, move, assignment or destructor (a
    // generated visitor reaches it by member; nothing copies its bytes).
    private void CheckPlainData(CXXRecordDecl record, string name, Role role)
    {
        foreach (var method in record.Methods.Where(method => method.IsVirtual))
        {
            throw Error(method, $"{name}::{method.Name}", $"a virtual function: {Roles.Phrase(role)} is plain data");
        }

        if (role != Role.DataAsset)
        {
            foreach (var baseSpecifier in record.Bases)
            {
                var baseRecord = baseSpecifier.Type.CanonicalType.AsCXXRecordDecl?.Definition as CXXRecordDecl;
                if (baseSpecifier.IsVirtual || baseRecord is null || HasData(baseRecord) || IsPolymorphic(baseRecord))
                {
                    throw Error(record, name,
                                $"its base class {baseSpecifier.Type.AsString} has data members or virtual functions, " +
                                $"which {Roles.Phrase(role)}'s base may not have (only a data asset extends a type with " +
                                "data members)");
                }
            }
        }

        if (!Roles.IsOwning(role) &&
            (record.HasUserDeclaredCopyConstructor || record.HasUserDeclaredMoveConstructor ||
             record.HasUserDeclaredCopyAssignment || record.HasUserDeclaredMoveAssignment ||
             record.HasUserDeclaredDestructor))
        {
            throw Error(record, name,
                        "not trivially copyable: it declares its own copy, move, assignment or destructor, which " +
                        $"{Roles.Phrase(role)} may not (only a data asset or settings type may)");
        }
    }

    /// <summary>
    /// The data asset a data asset extends (ADR-0075), or null when it extends none: one base at most, not virtual,
    /// selected as a data asset, whose fields its own description records.
    /// </summary>
    private string? DataAssetBase(CXXRecordDecl record, string name)
    {
        if (record.Bases.Count > 1)
        {
            throw Error(record, name, $"extends {record.Bases.Count} types: a data asset extends one other data asset at most");
        }

        if (record.Bases.Count == 0)
        {
            return null;
        }

        var baseSpecifier = record.Bases[0];
        var canonical = baseSpecifier.Type.CanonicalType;
        var baseName = m_names.GetValueOrDefault(canonical.AsString);
        var baseRole = baseName is null ? (Role?)null : m_roles[baseName];
        if (baseSpecifier.IsVirtual || baseRole != Role.DataAsset)
        {
            var what = baseSpecifier.IsVirtual ? "a virtual base"
                       : baseRole is null || baseRole == Role.Nested ? "not selected"
                       : Roles.Phrase(baseRole.Value);
            throw Error(record, name,
                        $"its base class {baseName ?? canonical.AsString} is {what}: a data asset extends only another " +
                        "data asset");
        }

        // Whatever reaches this type reaches its base's fields too.
        m_holds.Add((name, $"{name}'s base", baseName!));
        return baseName;
    }

    private static bool HasData(CXXRecordDecl record) =>
        record.Fields.Count > 0 ||
        record.Bases.Any(b => b.Type.CanonicalType.AsCXXRecordDecl?.Definition is CXXRecordDecl r && HasData(r));

    private static bool IsPolymorphic(CXXRecordDecl record) =>
        record.Methods.Any(method => method.IsVirtual) ||
        record.Bases.Any(b => b.IsVirtual ||
                              (b.Type.CanonicalType.AsCXXRecordDecl?.Definition is CXXRecordDecl r && IsPolymorphic(r)));

    private static bool IsTaggedUnionShape(IReadOnlyList<FieldDecl> fields) =>
        fields.Count == 2 && fields[0].Type.CanonicalType.Handle.kind == CXTypeKind.CXType_Enum &&
        fields[1].IsAnonymousStructOrUnion && fields[1].Type.CanonicalType.AsCXXRecordDecl is { IsUnion: true };

    private FieldDescription DescribeField(string typeName, FieldDecl field)
    {
        if (field.IsAnonymousStructOrUnion)
        {
            throw Error(field, typeName, $"an anonymous struct or union outside the tagged-union shape ({TaggedUnionShape})");
        }

        var subject = $"{typeName}::{field.Name}";
        if (field.Access != CX_CXXAccessSpecifier.CX_CXXPublic && !m_selection.PrivateMembers.Contains(typeName))
        {
            var access = field.Access == CX_CXXAccessSpecifier.CX_CXXProtected ? "protected" : "private";
            throw Error(field, subject,
                        $"a {access} data member outside the tagged-union shape ({TaggedUnionShape}); a selection " +
                        "shares one only when 'private-members' names its type");
        }

        if (field.IsBitField)
        {
            throw Error(field, subject, "a bit-field: a shared type's fields are whole bytes");
        }

        var type = Classify(field.Type, field, subject);
        if (type.IsOwning)
        {
            // Whether the type may hold it depends on every type that reaches it: CheckOwningKinds decides.
            m_owningFields.TryAdd(typeName, []);
            m_owningFields[typeName].Add((field, subject, type.Kind));
        }

        var value = IsHandle(field.Type) ? HandleDefault(type, field.InClassInitializer, field, subject)
                                         : Default(type, field.InClassInitializer, field, subject);
        return Field(field, subject, type, value);
    }

    /// <summary>A field's description, with what its doc commands give; an owning kind's has no offset.</summary>
    private FieldDescription Field(FieldDecl field, string subject, FieldType type, DefaultValue value)
    {
        var doc = WithDocCommandDiagnostics(field, subject, () => DocComment.Field(RawDoc(field)));
        return new FieldDescription(field.Name, type.IsOwning ? null : Offset(field), type, value, doc.Text)
        {
            Formerly = doc.Formerly,
            Metadata = doc.Metadata,
        };
    }

    /// <summary>
    /// An enum with no enumerators (<c>enum class Id : std::uint32_t {}</c>, EnTT's <c>entt::entity</c>): a distinct
    /// integer type, not a set of named values, so it is recorded as its underlying integer.
    /// </summary>
    private static bool IsHandle(Type type) =>
        type.CanonicalType.AsTagDecl?.Definition is EnumDecl { Enumerators.Count: 0 };

    /// <summary>
    /// A handle's default: it has no enumerator to name, so its value is whatever constant libclang evaluates its
    /// initialiser to (<c>entt::null</c> is all bits set).
    /// </summary>
    private DefaultValue HandleDefault(FieldType type, Expr? init, Decl at, string subject)
    {
        if (init is null)
        {
            throw Error(at, subject, "has no default member initialiser");
        }

        return IsZeroInit(Strip(init)) ? Zero(type, at, subject) : Evaluate(init, type.Kind, at, subject);
    }

    private TaggedUnionDescription DescribeTaggedUnion(CXXRecordDecl record, string name, Role role, long size,
                                                       long alignment, string? doc)
    {
        var tagField = record.Fields[0];
        var unionField = record.Fields[1];
        var tagType = Classify(tagField.Type, tagField, $"{name}::{tagField.Name}");
        var enumDecl = (EnumDecl)tagField.Type.CanonicalType.AsTagDecl!.Definition!;
        var union = (CXXRecordDecl)unionField.Type.CanonicalType.AsCXXRecordDecl!.Definition!;

        var members = union.Fields;
        var enumerators = enumDecl.Enumerators;
        if (members.Count != enumerators.Count)
        {
            throw Error(record, name,
                        $"its tag {tagType.TypeName} has {enumerators.Count} values but its union holds {members.Count} kinds; " +
                        "the kinds pair with the tag's values in declaration order");
        }

        var kinds = members.Select((member, i) => new UnionKind(member.Name, enumerators[i].Name,
                                                                Classify(member.Type, member, $"{name}::{member.Name}"),
                                                                PlainDoc(member, $"{name}::{member.Name}", "a tagged union's kind")))
                        .ToList();
        var kindNames = kinds.Select(kind => kind.Name).ToHashSet(StringComparer.Ordinal);

        // A constructor that sets the tag and a kind must pair them as the declaration order does.
        foreach (var constructor in record.Ctors.Where(c => !c.IsCopyOrMoveConstructor && !c.IsDelegatingConstructor))
        {
            var inits = MemberInits(constructor);
            var kindInit = inits.Keys.FirstOrDefault(kindNames.Contains);
            if (kindInit is null || !inits.TryGetValue(tagField.Name, out var tagInit) ||
                Strip(tagInit) is not DeclRefExpr { Decl: EnumConstantDecl enumerator })
            {
                continue;
            }

            var kind = kinds.First(k => k.Name == kindInit);
            if (enumerator.Name != kind.Tag)
            {
                throw Error(constructor, name,
                            $"a constructor sets the tag to {enumerator.Name} with the kind {kind.Name}, but the kinds " +
                            $"pair with the tag's values in declaration order ({kind.Name} is {kind.Tag})");
            }
        }

        var defaultKind = DefaultKind(record, name, kindNames);
        var tagDefault = new DefaultValue.Text(kinds.First(kind => kind.Name == defaultKind).Tag);
        var tag = Field(tagField, $"{name}::{tagField.Name}", tagType, tagDefault);
        return new TaggedUnionDescription(name, size, alignment, doc, role, tag, Offset(unionField), kinds, defaultKind);
    }

    /// <summary>The kind the tagged union's default constructor holds, which must be given its own defaults.</summary>
    private string DefaultKind(CXXRecordDecl record, string name, HashSet<string> kindNames)
    {
        var constructor = record.Ctors.FirstOrDefault(c => c.IsDefaultConstructor && c.IsUserProvided) ??
                          throw Error(record, name, "a tagged union needs a default constructor that chooses its kind");

        string? kind;
        Expr? value;
        if (constructor.IsDelegatingConstructor)
        {
            var target = constructor.TargetConstructor;
            kind = target is null ? null : MemberInits(target).Keys.FirstOrDefault(kindNames.Contains);
            value = Strip(constructor.InitExprs[0]) is CXXConstructExpr { NumArgs: 1 } call ? call.Args[0] : null;
        }
        else
        {
            var inits = MemberInits(constructor);
            kind = inits.Keys.FirstOrDefault(kindNames.Contains);
            value = kind is null ? null : inits[kind];
        }

        if (kind is null || (value is not null && !IsOwnDefaults(value)))
        {
            throw Error(constructor, name,
                        "its default constructor does not hold one kind with that kind's own defaults (`Kind{}`)");
        }

        return kind;
    }

    /// <summary>A constructor's member initialisers by member name (libclang lists each as a member reference, then its expression).</summary>
    private static Dictionary<string, Expr> MemberInits(CXXConstructorDecl constructor)
    {
        var inits = new Dictionary<string, Expr>(StringComparer.Ordinal);
        string? member = null;
        foreach (var child in constructor.CursorChildren)
        {
            if (child is Ref reference && reference.CursorKind == CXCursorKind.CXCursor_MemberRef)
            {
                member = reference.Spelling;
            }
            else if (member is not null && child is Expr expression)
            {
                inits[member] = expression;
                member = null;
            }
        }

        return inits;
    }

    // --- Kinds ---

    private FieldType Classify(Type declared, Decl at, string subject)
    {
        var type = declared.CanonicalType;
        if (ScalarKind(type) is { } scalar)
        {
            return new FieldType(scalar);
        }

        switch (type.Handle.kind)
        {
            // The engine's own fixed-layout values, recognised by qualified name: the tool is binding-agnostic but not
            // engine-agnostic. Neither is described for the kind.
            case CXTypeKind.CXType_Record when IsEngineEntity(type):
                return new FieldType("entity");
            case CXTypeKind.CXType_Record when AssetHandleTarget(type) is { } target:
                return new FieldType("reference", Target: target)
                {
                    SelectedHandle = m_names.GetValueOrDefault(type.AsString),
                };
            case CXTypeKind.CXType_ConstantArray when IsTextUnit(((ArrayType)type).ElementType.CanonicalType):
                return new FieldType("fixed-string", Length: type.Handle.ArraySize);
            case CXTypeKind.CXType_Enum when IsHandle(type):
                var enumDecl = (EnumDecl)type.AsTagDecl!.Definition!;
                return new FieldType(IntegerKind(enumDecl.IntegerType.CanonicalType) ??
                                     throw Error(at, subject, $"its type '{declared.AsString}' is not a kind the description records"));
            case CXTypeKind.CXType_Enum:
                return new FieldType("enum", RegisterReferenced(type, subject));
            // The owning kinds, recognised by qualified name as the engine's values are. Which roles may hold them is
            // CheckOwningKinds's to say, once every type that reaches this one is known.
            case CXTypeKind.CXType_Record when StdSpecialisation(type, "basic_string") is { } text:
                return IsTextUnit(text.TemplateArgs[0].AsType.CanonicalType)
                           ? new FieldType("string")
                           : throw Error(at, subject, $"a {declared.AsString}: a string is a std::string of UTF-8 text");
            case CXTypeKind.CXType_Record when StdSpecialisation(type, "vector") is { } vector:
                return ListField(vector.TemplateArgs[0].AsType, at, subject);
            case CXTypeKind.CXType_Record when StdSpecialisation(type, "optional") is { } optional:
                var held = Classify(optional.TemplateArgs[0].AsType, at, subject);
                return held.Kind == "optional"
                           ? throw Error(at, subject, "an optional of an optional, which the kinds cannot express")
                           : new FieldType("optional", Element: held);
            case CXTypeKind.CXType_Record when IsFilesystemPath(type):
                return new FieldType("path");
            case CXTypeKind.CXType_Record when StdArray(type) is var (elementType, length):
                return ArrayField(elementType, length, at, subject);
            case CXTypeKind.CXType_Record:
                return new FieldType("struct", RegisterReferenced(type, subject));
            case CXTypeKind.CXType_Pointer:
                return new FieldType("pointer");
            case CXTypeKind.CXType_ConstantArray:
                return ArrayField(((ArrayType)type).ElementType, type.Handle.ArraySize, at, subject);
            case CXTypeKind.CXType_LValueReference:
            case CXTypeKind.CXType_RValueReference:
                throw Error(at, subject, "a reference member: a shared type holds values, not references");
        }

        throw Error(at, subject, $"its type '{declared.AsString}' is not a kind the description records");
    }

    /// <summary>
    /// The kind of a scalar type, as a field and a native parameter alike record it: <c>bool</c>, <c>char16</c>,
    /// <c>u32</c> for a <c>char32_t</c>, <c>f32</c>, <c>f64</c>, <c>u8</c> for a <c>char</c> of either signedness, or
    /// an integer's kind. Null for anything else.
    /// </summary>
    internal static string? ScalarKind(Type type)
    {
        return type.Handle.kind switch
        {
            CXTypeKind.CXType_Bool => "bool",
            CXTypeKind.CXType_Char16 => "char16",
            // A UTF-32 code point: C# has no 32-bit char, and its bytes are an unsigned integer's.
            CXTypeKind.CXType_Char32 => "u32",
            CXTypeKind.CXType_Float => "f32",
            CXTypeKind.CXType_Double => "f64",
            // A char is a byte of text, unsigned on every target, so a description reads the same everywhere.
            CXTypeKind.CXType_Char_S or CXTypeKind.CXType_Char_U => "u8",
            _ => IntegerKind(type),
        };
    }

    internal static string? IntegerKind(Type type)
    {
        var bits = type.Handle.SizeOf * 8;
        return type.Handle.kind switch
        {
            CXTypeKind.CXType_Char_S or CXTypeKind.CXType_SChar or CXTypeKind.CXType_Short or CXTypeKind.CXType_Int or
                CXTypeKind.CXType_Long or CXTypeKind.CXType_LongLong => $"i{bits}",
            CXTypeKind.CXType_Char_U or CXTypeKind.CXType_UChar or CXTypeKind.CXType_UShort or CXTypeKind.CXType_UInt or
                CXTypeKind.CXType_ULong or CXTypeKind.CXType_ULongLong => $"u{bits}",
            _ => null,
        };
    }

    /// <summary><c>Fenrir::Entity</c>, the <c>entity</c> kind.</summary>
    private static bool IsEngineEntity(Type canonical) =>
        canonical.AsCXXRecordDecl is { Name: "Entity" } record && IsInFenrirNamespace(record);

    /// <summary>
    /// The canonical name of <c>T</c> when the type is a <c>Fenrir::AssetHandle&lt;T&gt;</c>, the <c>reference</c> kind's
    /// target; null for any other type.
    /// </summary>
    private static string? AssetHandleTarget(Type canonical) =>
        canonical.AsTagDecl is ClassTemplateSpecializationDecl { Name: "AssetHandle" } handle && IsInFenrirNamespace(handle) &&
        handle.TemplateArgs is [{ Kind: CXTemplateArgumentKind.CXTemplateArgumentKind_Type } argument]
            ? argument.AsType.CanonicalType.AsString
            : null;

    private static bool IsInFenrirNamespace(Decl decl) =>
        decl.DeclContext is NamespaceDecl { Name: "Fenrir", DeclContext: TranslationUnitDecl };

    /// <summary>
    /// A <c>char</c> of either signedness or a <c>char8_t</c>: a byte of UTF-8 text, so an array of them is a
    /// <c>fixed-string</c>. A <c>signed char</c>, <c>unsigned char</c> and <c>std::uint8_t</c> are bytes, not text.
    /// </summary>
    private static bool IsTextUnit(Type canonical) =>
        canonical.Handle.kind is CXTypeKind.CXType_Char_S or CXTypeKind.CXType_Char_U ||
        // libclang has no kind of its own for char8_t, so it is known by its spelling.
        canonical.AsString == "char8_t";

    /// <summary>The <c>array</c> kind: <paramref name="length"/> elements of <paramref name="elementType"/>, whether a C array's or a <c>std::array</c>'s, never an array of arrays.</summary>
    private FieldType ArrayField(Type elementType, long length, Decl at, string subject)
    {
        var element = Classify(elementType, at, subject);
        if (element.Kind is "array" or "fixed-string")
        {
            throw Error(at, subject, "an array of arrays, which the kinds cannot express");
        }

        if (element.IsOwning)
        {
            throw Error(at, subject, $"an array of {element.Kind}s, which the kinds cannot express: hold a list");
        }

        return new FieldType("array", Element: element, Length: length);
    }

    /// <summary>
    /// The <c>list</c> kind: a <c>std::vector</c> of any kind, a list included, but <c>bool</c>, whose
    /// <c>std::vector</c> packs bits a visitor cannot hand out one by one.
    /// </summary>
    private FieldType ListField(Type elementType, Decl at, string subject)
    {
        var element = Classify(elementType, at, subject);
        if (element.Kind == "bool")
        {
            throw Error(at, subject, "a std::vector<bool>, whose elements are bits rather than values: hold a list of u8");
        }

        return new FieldType("list", Element: element);
    }

    /// <summary>The specialisation of the standard's class template of this name the type is, or null.</summary>
    private static ClassTemplateSpecializationDecl? StdSpecialisation(Type canonical, string name) =>
        canonical.AsTagDecl is ClassTemplateSpecializationDecl specialisation && specialisation.Name == name &&
        specialisation.IsInStdNamespace && specialisation.TemplateArgs.Count > 0 &&
        specialisation.TemplateArgs[0].Kind == CXTemplateArgumentKind.CXTemplateArgumentKind_Type
            ? specialisation
            : null;

    /// <summary><c>std::filesystem::path</c>, the <c>path</c> kind, wherever the library's inline namespaces put it.</summary>
    private static bool IsFilesystemPath(Type canonical)
    {
        if (canonical.AsCXXRecordDecl is not { Name: "path" } record)
        {
            return false;
        }

        var context = record.DeclContext;
        while (context is NamespaceDecl { IsInline: true } inline)
        {
            context = inline.DeclContext;
        }

        return context is NamespaceDecl { Name: "filesystem" } filesystem && filesystem.IsInStdNamespace;
    }

    /// <summary>
    /// A <c>std::array&lt;T, N&gt;</c>, the standard's wrapper of one C array, which a shared type holds as it would
    /// <c>T[N]</c> (the same bytes, and the wrapper's one member has a name of the library's own): its element type and
    /// length, or null for any other class.
    /// </summary>
    private static (Type Element, long Length)? StdArray(Type canonical)
    {
        if (canonical.AsTagDecl is not ClassTemplateSpecializationDecl { Name: "array" } array || !array.IsInStdNamespace)
        {
            return null;
        }

        var arguments = array.TemplateArgs;
        if (arguments.Count != 2 || arguments[0].Kind != CXTemplateArgumentKind.CXTemplateArgumentKind_Type ||
            arguments[1].Kind != CXTemplateArgumentKind.CXTemplateArgumentKind_Integral)
        {
            return null;
        }

        return (arguments[0].AsType, arguments[1].AsIntegral);
    }

    private string RegisterReferenced(Type canonical, string subject)
    {
        var decl = (Decl?)canonical.AsTagDecl?.Definition ?? canonical.AsTagDecl!;
        var name = Register(canonical, canonical.AsString, Role.Nested, decl, subject);
        if (canonical.AsTagDecl is EnumDecl)
        {
            m_enums[name] = (EnumDecl)decl;
        }
        else if (m_describing is not null)
        {
            m_holds.Add((m_describing, subject, name));
        }

        return name;
    }

    /// <summary>
    /// The owning kinds' rule (SCHEMA.md, "Owning kinds"): a type may hold one only when every type reaching it, by
    /// value through any chain of fields, is a data asset or settings type, or it is one itself. A type a fixed-layout
    /// role reaches fails naming each owning field; one both kinds of role reach fails naming a path from each.
    /// </summary>
    private void CheckOwningKinds()
    {
        // The first path by which a fixed-layout and an owning role reach each type: the role at its start and the
        // field reaching the type (null for a selected type, which its own role reaches).
        var fixedFrom = new Dictionary<string, (Role Role, string? Through)>(StringComparer.Ordinal);
        var owningFrom = new Dictionary<string, (Role Role, string? Through)>(StringComparer.Ordinal);
        var queue = new Queue<string>();
        // A selected type is described under the name the selection gives it (an enum under its canonical name).
        foreach (var (name, role) in m_selection.Types)
        {
            if (m_roles.ContainsKey(name) && role is not (Role.BoundType or Role.Enum))
            {
                (Roles.IsOwning(role) ? owningFrom : fixedFrom).TryAdd(name, (role, null));
                queue.Enqueue(name);
            }
        }

        while (queue.Count > 0)
        {
            var from = queue.Dequeue();
            foreach (var (_, subject, to) in m_holds.Where(hold => hold.From == from))
            {
                var reached = false;
                if (fixedFrom.TryGetValue(from, out var viaFixed))
                {
                    reached |= fixedFrom.TryAdd(to, (viaFixed.Role, viaFixed.Through ?? subject));
                }

                if (owningFrom.TryGetValue(from, out var viaOwning))
                {
                    reached |= owningFrom.TryAdd(to, (viaOwning.Role, viaOwning.Through ?? subject));
                }

                if (reached)
                {
                    queue.Enqueue(to);
                }
            }
        }

        foreach (var (name, fields) in m_owningFields.Where(type => fixedFrom.ContainsKey(type.Key)))
        {
            var (role, through) = fixedFrom[name];
            var holder = through is null ? Roles.Phrase(role) : $"a nested type reached from {Roles.Phrase(role)}";
            if (owningFrom.TryGetValue(name, out var owning))
            {
                var (at, subject, kind) = fields[0];
                m_errors.Add(Error(at, name,
                                   $"holds {KindPhrase(kind)} ({subject}), which {Roles.Phrase(role)} cannot reach, but " +
                                   $"is reached from {Roles.Phrase(role)} {Path(through)} and from " +
                                   $"{Roles.Phrase(owning.Role)} {Path(owning.Through)}").Message);
                continue;
            }

            foreach (var (at, subject, kind) in fields)
            {
                m_errors.Add(Error(at, subject,
                                   $"{KindPhrase(kind)}, which {holder} cannot hold: the owning kinds (string, list, " +
                                   "optional and path) are for data assets and settings types" +
                                   (through is null ? "" : $" (reached through {through})")).Message);
            }
        }

        static string Path(string? through) => through is null ? "where it is selected" : $"through {through}";
    }

    /// <summary>
    /// Marks the structs a generated visitor reaches by member (<see cref="StructDescription.ByMember" />) and drops their
    /// offsets: the data assets and settings types, and every struct whose layout is the standard library's: one holding
    /// an owning kind, or holding such a struct by value, or extending one. (A data asset of numbers alone has a fixed
    /// layout, so a struct holding it keeps its offsets.)
    /// </summary>
    private void MarkByMember()
    {
        var structs = m_described.Values.OfType<StructDescription>().ToList();
        var libraryLayout = m_owningFields.Keys.ToHashSet(StringComparer.Ordinal);
        bool added;
        do
        {
            added = false;
            foreach (var (from, _, to) in m_holds.Where(hold => libraryLayout.Contains(hold.To)))
            {
                added |= libraryLayout.Add(from);
            }
        }
        while (added);

        foreach (var type in structs.Where(type => Roles.IsOwning(type.Role) || libraryLayout.Contains(type.Name)))
        {
            m_described[type.Name] = type with
            {
                ByMember = true,
                Fields = type.Fields.Select(field => field with { Offset = null }).ToList(),
            };
        }
    }

    private static string KindPhrase(string kind) => kind == "optional" ? "an optional" : $"a {kind}";

    // --- Defaults ---

    private DefaultValue Default(FieldType type, Expr? init, Decl at, string subject)
    {
        if (type.Kind == "struct")
        {
            return StructDefault(type, init, at, subject);
        }

        switch (type.Kind)
        {
            case "array":
                return ArrayDefault(type, init, at, subject);
            case "entity":
                return EmptyDefault(init, at, subject, "an entity", "the empty entity", "Entity()");
            case "reference":
                return EmptyDefault(init, at, subject, "a reference", "the empty handle", "AssetHandle<T>()");
            case "fixed-string":
                return FixedStringDefault(type, init, at, subject);
            case "string" or "path":
                return TextDefault(type.Kind, init, at, subject);
            case "list":
                return init is null || IsOwnDefaults(init)
                           ? new DefaultValue.Array([])
                           : throw Error(at, subject,
                                         $"the default '{SourceText(init)}' is not one a list records: its default is the " +
                                         "empty list (no initialiser or {})");
            case "optional":
                return init is null || IsOwnDefaults(init) || IsNullopt(init)
                           ? new DefaultValue.Null()
                           : throw Error(at, subject,
                                         $"the default '{SourceText(init)}' is not one an optional records: its default is " +
                                         "empty (no initialiser, {} or std::nullopt)");
        }

        if (init is null)
        {
            throw Error(at, subject, "has no default member initialiser");
        }

        var expression = Strip(init);
        if (IsZeroInit(expression))
        {
            return Zero(type, at, subject);
        }

        switch (type.Kind)
        {
            case "enum":
                if (expression is DeclRefExpr { Decl: EnumConstantDecl enumerator })
                {
                    return new DefaultValue.Text(enumerator.Name);
                }

                break;
            case "pointer":
                if (expression is CXXNullPtrLiteralExpr or GNUNullExpr ||
                    (expression is IntegerLiteral && Evaluate(init, "u64", at, subject) is DefaultValue.Number { Spelling: "0" }))
                {
                    return new DefaultValue.Null();
                }

                break;
            default:
                // A number's or bool's default is any constant libclang evaluates: a literal, a named constant, an
                // arithmetic expression over them.
                if (TryEvaluate(init, type.Kind, at, subject) is { } evaluated)
                {
                    return evaluated;
                }

                break;
        }

        throw UnknownDefault(init, at, subject);
    }

    /// <summary>
    /// An entity's or reference's default, which names nothing: no initialiser, or the value's own defaults
    /// (<c>{}</c>, <c>Entity()</c>). Recorded as <c>null</c>.
    /// </summary>
    private DefaultValue EmptyDefault(Expr? init, Decl at, string subject, string what, string empty, string spelling)
    {
        if (init is null || IsOwnDefaults(init))
        {
            return new DefaultValue.Null();
        }

        throw Error(at, subject,
                    $"the default '{SourceText(init)}' is not one {what} records: its default is {empty} (no " +
                    $"initialiser, {{}} or {spelling})");
    }

    /// <summary>
    /// A fixed string's default, the text its initialiser spells: a string literal, a braced list of character
    /// constants (ending at the first zero), or <c>{}</c> or no initialiser for the empty string. The text must leave
    /// room for its terminator and be UTF-8.
    /// </summary>
    private DefaultValue FixedStringDefault(FieldType type, Expr? init, Decl at, string subject)
    {
        if (init is null || IsZeroInit(Strip(init)))
        {
            return new DefaultValue.Text("");
        }

        byte[]? bytes = Strip(init) switch
        {
            StringLiteral literal => Encoding.UTF8.GetBytes(literal.String),
            InitListExpr list => CharacterBytes(list, at, subject),
            _ => null,
        };
        if (bytes is null)
        {
            throw Error(at, subject,
                        $"the default '{SourceText(init)}' is not one a fixed string records: a string literal, a " +
                        "braced list of character constants, or {}");
        }

        var end = Array.IndexOf(bytes, (byte)0);
        var text = end < 0 ? bytes : bytes[..end];
        if (text.Length >= type.Length)
        {
            throw Error(at, subject,
                        $"its default fills all {type.Length} bytes, leaving no room for the terminator a fixed string keeps");
        }

        try
        {
            return new DefaultValue.Text(new UTF8Encoding(false, true).GetString(text));
        }
        catch (DecoderFallbackException)
        {
            throw Error(at, subject, $"the default '{SourceText(init)}' is not UTF-8 text");
        }
    }

    /// <summary>
    /// A string's or path's default, the literal its initialiser constructs it from (<c>= "assets"</c>,
    /// <c>{"assets"}</c>), or the empty text for no initialiser or its own defaults (<c>{}</c>).
    /// </summary>
    private DefaultValue TextDefault(string kind, Expr? init, Decl at, string subject)
    {
        if (init is null || IsOwnDefaults(init))
        {
            return new DefaultValue.Text("");
        }

        var expression = Strip(init);
        if (expression is CXXConstructExpr construct &&
            construct.Args.Where(argument => argument is not CXXDefaultArgExpr).ToList() is [var argument])
        {
            expression = Strip(argument);
        }

        return expression is StringLiteral literal
                   ? new DefaultValue.Text(literal.String)
                   : throw Error(at, subject,
                                 $"the default '{SourceText(init)}' is not one {KindPhrase(kind)} records: a string " +
                                 $"literal, or {{}} or no initialiser for the empty {kind}");
    }

    /// <summary><c>std::nullopt</c>, or an optional constructed from it.</summary>
    private static bool IsNullopt(Expr init)
    {
        var expression = Strip(init);
        if (expression is CXXConstructExpr { NumArgs: 1 } construct)
        {
            expression = Strip(construct.Args[0]);
        }

        return expression is DeclRefExpr { Decl: VarDecl { Name: "nullopt" } nullopt } && nullopt.IsInStdNamespace;
    }

    // A braced list's characters as bytes, or null when one is not a constant libclang evaluates.
    private byte[]? CharacterBytes(InitListExpr list, Decl at, string subject)
    {
        var bytes = new List<byte>();
        foreach (var item in list.Inits.Cast<Expr>())
        {
            if (TryEvaluate(item, "u8", at, subject) is not DefaultValue.Number number)
            {
                return null;
            }

            bytes.Add(byte.Parse(number.Spelling, CultureInfo.InvariantCulture));
        }

        return [.. bytes];
    }

    private DefaultValue StructDefault(FieldType type, Expr? init, Decl at, string subject)
    {
        if (init is null)
        {
            return DefaultValue.TypeDefaults;
        }

        var expression = Strip(init);
        if (IsOwnDefaults(expression))
        {
            return DefaultValue.TypeDefaults;
        }

        if (m_roles[type.TypeName!] == Role.BoundType && expression is CXXConstructExpr { NumArgs: > 0 } construct)
        {
            return BoundDefault(construct, type.TypeName!, at, subject);
        }

        if (m_roles[type.TypeName!] != Role.BoundType && expression is InitListExpr list &&
            expression.Type.CanonicalType.AsCXXRecordDecl?.Definition is CXXRecordDecl record &&
            !IsTaggedUnionShape(record.Fields) && list.NumInits == record.Fields.Count)
        {
            return Overrides(record, list, at, subject);
        }

        if (m_roles[type.TypeName!] != Role.BoundType && NamedConstant(expression) is Expr constant)
        {
            return StructDefault(type, constant, at, subject);
        }

        throw UnknownDefault(init, at, subject);
    }

    /// <summary>
    /// The initialiser of the constant <paramref name="expression"/> names (<c>Colour::Black</c>, declared
    /// <c>static const</c> in its class and defined <c>inline constexpr</c> after it), or null for anything else: a nested
    /// struct's default may be a named constant, recorded as the values its initialiser gives.
    /// </summary>
    private static Expr? NamedConstant(Expr expression)
    {
        if (expression is not DeclRefExpr { Decl: VarDecl variable })
        {
            return null;
        }

        var definition = variable.Definition as VarDecl ?? variable;
        return definition.Type.Handle.IsConstQualified ? definition.Init : null;
    }

    /// <summary>
    /// An aggregate initialiser of a nested struct (<c>{.frequency = 2.0f}</c>): the members it sets, in storage order.
    /// The rest keep the nested struct's own defaults.
    /// </summary>
    private DefaultValue.Object Overrides(CXXRecordDecl record, InitListExpr list, Decl at, string subject)
    {
        var members = new List<KeyValuePair<string, DefaultValue>>();
        for (var i = 0; i < record.Fields.Count; ++i)
        {
            var init = (Expr)list.Inits[i];
            if (Strip(init) is CXXDefaultInitExpr or ImplicitValueInitExpr)
            {
                continue;
            }

            var field = record.Fields[i];
            var memberSubject = $"{subject}.{field.Name}";
            members.Add(new(field.Name, Default(Classify(field.Type, field, memberSubject), init, at, memberSubject)));
        }

        return new DefaultValue.Object(members);
    }

    /// <summary>A bound type's constructor with literal arguments, as its components' values in storage order.</summary>
    private DefaultValue BoundDefault(CXXConstructExpr construct, string typeName, Decl at, string subject)
    {
        var arguments = construct.Args;
        if (arguments.Any(argument => !IsLiteral(Strip(argument)) && !IsNumericLimits(Strip(argument))))
        {
            throw UnknownDefault(construct, at, subject);
        }

        var record = (CXXRecordDecl)construct.Type.CanonicalType.AsCXXRecordDecl!.Definition!;
        var components = Components(record).ToList();
        if (components.Count == 0 || components.Any(c => c.Kind is "struct" or "enum" or "array" or "pointer"))
        {
            throw Error(at, subject, $"the bound type {typeName} is not made of numbers, so its constructor's arguments cannot be recorded");
        }

        // A parameter is often named after its component with a leading underscore, so it shadows nothing (`_x`).
        var parameters = construct.Constructor.Parameters.Select(parameter => parameter.Name.TrimStart('_')).ToList();
        var values = new DefaultValue[components.Count];
        if (arguments.Count == components.Count && parameters.Distinct().Count() == parameters.Count &&
            parameters.All(parameter => components.Any(c => c.Name == parameter)))
        {
            for (var i = 0; i < arguments.Count; ++i)
            {
                var component = components.FindIndex(c => c.Name == parameters[i]);
                values[component] = Evaluate(arguments[i], components[component].Kind, at, subject);
            }
        }
        else if (arguments.Count == 1 && components.All(c => c.Kind == components[0].Kind))
        {
            Array.Fill(values, Evaluate(arguments[0], components[0].Kind, at, subject));
        }
        else
        {
            throw Error(at, subject,
                        $"cannot tell which component of {typeName} each argument sets: the constructor's parameters " +
                        $"({string.Join(", ", parameters)}) are not named after its components " +
                        $"({string.Join(", ", components.Select(c => c.Name))})");
        }

        return new DefaultValue.Object(components.Select((c, i) => new KeyValuePair<string, DefaultValue>(c.Name, values[i]))
                                           .ToList());
    }

    /// <summary>A bound type's components in storage order; of an anonymous union, its first member's.</summary>
    private static IEnumerable<(string Name, string Kind)> Components(RecordDecl record) =>
        record.Fields.SelectMany(Components);

    private static IEnumerable<(string Name, string Kind)> Components(FieldDecl field)
    {
        if (field.IsAnonymousStructOrUnion && field.Type.CanonicalType.AsCXXRecordDecl?.Definition is RecordDecl inner)
        {
            return inner.IsUnion ? inner.Fields.Take(1).SelectMany(Components) : Components(inner);
        }

        var type = field.Type.CanonicalType;
        return [(field.Name, type.Handle.kind switch
        {
            CXTypeKind.CXType_Float => "f32",
            CXTypeKind.CXType_Double => "f64",
            CXTypeKind.CXType_Bool => "bool",
            _ => IntegerKind(type) ?? "struct",
        })];
    }

    /// <summary>
    /// A bound type's components as its description records them: every scalar it holds, in storage order, with its
    /// offset; of an anonymous union, its first member's; a nested struct's members and an array's elements by path
    /// (<c>m_value[0].x</c>). Null when it holds anything else (a pointer, an enum), which a visitor cannot present.
    /// </summary>
    private static List<BoundComponent>? BoundComponents(RecordDecl record)
    {
        var components = new List<BoundComponent>();
        return Collect(record, "", 0) ? components : null;

        // A record's members, from `offset`, each named `prefix` and its own name (an anonymous one adds no name).
        bool Collect(RecordDecl inner, string prefix, long offset)
        {
            foreach (var field in inner.IsUnion ? inner.Fields.Take(1) : inner.Fields)
            {
                var anonymous = field.IsAnonymousStructOrUnion;
                if (!CollectType(field.Type.CanonicalType, anonymous ? prefix : prefix + field.Name, offset + Offset(field),
                                 anonymous))
                {
                    return false;
                }
            }

            return true;
        }

        bool CollectType(Type type, string name, long offset, bool anonymous)
        {
            if (ScalarKind(type) is { } kind)
            {
                components.Add(new BoundComponent(name, offset, kind));
                return true;
            }

            // A std::array is the C array it holds, as a field's kind records it: its member's name is the library's.
            (Type Element, long Length)? array = type.Handle.kind == CXTypeKind.CXType_ConstantArray
                            ? (((ArrayType)type).ElementType, type.Handle.ArraySize)
                            : StdArray(type);
            if (array is var (elementType, length))
            {
                var element = elementType.CanonicalType;
                for (long i = 0; i < length; ++i)
                {
                    if (!CollectType(element, $"{name}[{i}]", offset + (i * element.Handle.SizeOf), false))
                    {
                        return false;
                    }
                }

                return true;
            }

            return type.Handle.kind == CXTypeKind.CXType_Record && type.AsCXXRecordDecl?.Definition is RecordDecl nested &&
                   Collect(nested, anonymous ? name : name + ".", offset);
        }
    }

    private DefaultValue ArrayDefault(FieldType type, Expr? init, Decl at, string subject)
    {
        var element = type.Element!;
        if (init is null)
        {
            // Only these kinds have a default without an initialiser (T{}, the empty entity, the empty handle); a
            // scalar's would be indeterminate.
            if (element.Kind is not ("struct" or "entity" or "reference"))
            {
                throw Error(at, subject, "has no default member initialiser");
            }

            return new DefaultValue.Array(Enumerable.Repeat(Zero(element, at, subject), (int)type.Length).ToList());
        }

        if (Strip(init) is not InitListExpr list)
        {
            throw UnknownDefault(init, at, subject);
        }

        // Clang gives a std::array's braced list as a list of its one member's: `{1.0f, 2.0f}` (braces elided) and
        // `{{1.0f, 2.0f}}` both arrive as a list of one list, `{}` as one implicit value or nothing, so the elements
        // are read from the inner list as a C array's are.
        if (StdArray(list.Type.CanonicalType) is not null && list.NumInits > 0)
        {
            if (list.NumInits != 1)
            {
                throw UnknownDefault(init, at, subject);
            }

            switch (Strip((Expr)list.Inits[0]))
            {
                case InitListExpr inner:
                    list = inner;
                    break;
                case ImplicitValueInitExpr:
                    return Zero(type, at, subject);
                default:
                    throw UnknownDefault(init, at, subject);
            }
        }

        var elements = list.Inits.Cast<Expr>().Select(item => Default(element, item, at, subject)).ToList();
        while (elements.Count < type.Length)
        {
            elements.Add(Zero(element, at, subject));
        }

        return new DefaultValue.Array(elements);
    }

    private DefaultValue Zero(FieldType type, Decl at, string subject) => type.Kind switch
    {
        "bool" => new DefaultValue.Bool(false),
        "pointer" or "entity" or "reference" or "optional" => new DefaultValue.Null(),
        "fixed-string" or "string" or "path" => new DefaultValue.Text(""),
        "list" => new DefaultValue.Array([]),
        "struct" => DefaultValue.TypeDefaults,
        "array" => new DefaultValue.Array(Enumerable.Range(0, (int)type.Length).Select(_ => Zero(type.Element!, at, subject)).ToList()),
        "enum" => new DefaultValue.Text(
            m_enums[type.TypeName!].Enumerators.FirstOrDefault(e => e.InitVal == 0)?.Name ??
            throw Error(at, subject, $"is zero-initialised, but {type.TypeName} has no enumerator with the value 0")),
        _ => new DefaultValue.Number("0"),
    };

    private DefaultValue Evaluate(Expr expression, string kind, Decl at, string subject) =>
        TryEvaluate(expression, kind, at, subject) ??
        throw Error(at, subject, $"a default libclang could not evaluate as {kind}");

    /// <summary>
    /// The constant <paramref name="expression"/> evaluates to, as <paramref name="kind"/>; null when libclang cannot
    /// evaluate it as that kind.
    /// </summary>
    private DefaultValue? TryEvaluate(Expr expression, string kind, Decl at, string subject)
    {
        using var result = expression.Handle.Evaluate;
        switch (result.Kind)
        {
            case CXEvalResultKind.CXEval_Float:
                var number = result.AsDouble;
                if (kind is not ("f32" or "f64"))
                {
                    break;
                }

                if (double.IsNaN(number))
                {
                    throw Error(at, subject, "a NaN default, which the description cannot record");
                }

                if (double.IsInfinity(number))
                {
                    return new DefaultValue.Text(number > 0 ? "inf" : "-inf");
                }

                return new DefaultValue.Number(kind == "f32" ? ((float)number).ToString("R", CultureInfo.InvariantCulture)
                                                             : number.ToString("R", CultureInfo.InvariantCulture));
            case CXEvalResultKind.CXEval_Int:
                var bits = result.IsUnsignedInt ? result.AsUnsigned : unchecked((ulong)result.AsLongLong);
                return kind switch
                {
                    "bool" => new DefaultValue.Bool(bits != 0),
                    "f32" => new DefaultValue.Number(((float)(result.IsUnsignedInt ? bits : (double)result.AsLongLong)).ToString("R", CultureInfo.InvariantCulture)),
                    "f64" => new DefaultValue.Number((result.IsUnsignedInt ? bits : (double)result.AsLongLong).ToString("R", CultureInfo.InvariantCulture)),
                    _ => new DefaultValue.Number(Integer(bits, kind)),
                };
        }

        return null;
    }

    /// <summary>The value an integer of these bits has as this kind, as C++'s conversion gives it.</summary>
    private static string Integer(ulong bits, string kind) => unchecked(kind switch
    {
        "i8" => ((sbyte)bits).ToString(CultureInfo.InvariantCulture),
        "i16" => ((short)bits).ToString(CultureInfo.InvariantCulture),
        "i32" => ((int)bits).ToString(CultureInfo.InvariantCulture),
        "i64" => ((long)bits).ToString(CultureInfo.InvariantCulture),
        "u8" => ((byte)bits).ToString(CultureInfo.InvariantCulture),
        "u16" or "char16" => ((ushort)bits).ToString(CultureInfo.InvariantCulture),
        "u32" => ((uint)bits).ToString(CultureInfo.InvariantCulture),
        _ => bits.ToString(CultureInfo.InvariantCulture),
    });

    // --- Expression shapes ---

    /// <summary>The expression as written: implicit conversions, parentheses, temporaries and copies removed.</summary>
    private static Expr Strip(Expr expression)
    {
        while (true)
        {
            switch (expression)
            {
                case ImplicitCastExpr cast:
                    expression = cast.SubExpr;
                    break;
                case CXXFunctionalCastExpr cast:
                    expression = cast.SubExpr;
                    break;
                case ParenExpr paren:
                    expression = paren.SubExpr;
                    break;
                case FullExpr full:
                    expression = full.SubExpr;
                    break;
                case MaterializeTemporaryExpr temporary:
                    expression = temporary.SubExpr;
                    break;
                case CXXBindTemporaryExpr bind:
                    expression = bind.SubExpr;
                    break;
                case CXXConstructExpr { NumArgs: 1 } copy when copy.IsElidable || copy.Constructor.IsCopyOrMoveConstructor:
                    expression = copy.Args[0];
                    break;
                default:
                    return expression;
            }
        }
    }

    private static bool IsZeroInit(Expr expression) =>
        expression is InitListExpr { NumInits: 0 } or CXXScalarValueInitExpr or ImplicitValueInitExpr;

    /// <summary><c>{}</c>, <c>T{}</c> or <c>T()</c>: the type's own defaults.</summary>
    private static bool IsOwnDefaults(Expr expression)
    {
        expression = Strip(expression);
        // Clang spells an aggregate's `{}` with one initialiser per member: its default member initialiser, or none.
        return IsZeroInit(expression) || expression is CXXConstructExpr { NumArgs: 0 } ||
               (expression is InitListExpr list &&
                list.Inits.All(init => Strip((Expr)init) is CXXDefaultInitExpr or ImplicitValueInitExpr));
    }

    /// <summary>
    /// A literal or a <c>std::numbers</c> constant, or operators applied to them (<c>-1.0f</c>, <c>~0u</c>,
    /// <c>1u &lt;&lt; 3</c>, <c>std::numbers::pi_v&lt;float&gt; / 4.0f</c>).
    /// </summary>
    private static bool IsLiteral(Expr expression)
    {
        expression = Strip(expression);
        return expression switch
        {
            IntegerLiteral or FloatingLiteral or CXXBoolLiteralExpr or CharacterLiteral => true,
            DeclRefExpr { Decl: VarDecl { DeclContext: NamespaceDecl { Name: "numbers" } numbers } } => numbers.IsInStdNamespace,
            UnaryOperator unary => unary.Opcode is CXUnaryOperatorKind.CXUnaryOperator_Minus or
                                       CXUnaryOperatorKind.CXUnaryOperator_Plus or CXUnaryOperatorKind.CXUnaryOperator_Not or
                                       CXUnaryOperatorKind.CXUnaryOperator_LNot && IsLiteral(unary.SubExpr),
            BinaryOperator binary => !binary.IsAssignmentOp && !binary.IsCommaOp && IsLiteral(binary.LHS) && IsLiteral(binary.RHS),
            _ => false,
        };
    }

    /// <summary><c>std::numeric_limits&lt;T&gt;::infinity()</c>, <c>max()</c> or <c>lowest()</c>.</summary>
    private static bool IsNumericLimits(Expr expression) =>
        Strip(expression) is CallExpr { NumArgs: 0, DirectCallee: CXXMethodDecl method } &&
        method.Name is "infinity" or "max" or "lowest" &&
        method.Parent is CXXRecordDecl { Name: "numeric_limits" } limits && limits.IsInStdNamespace;

    // --- Diagnostics, docs and layout ---

    private static long Offset(FieldDecl field) => field.Handle.OffsetOfField / 8;

    /// <summary>
    /// The decl's raw doc comment, or null when it has none. Clang attaches a comment to the next enumerator when only a
    /// comma lies between, so a comment that ends before the previous sibling does is that sibling's, not this one's.
    /// </summary>
    private static string? RawDoc(Decl? decl, Decl? previous = null)
    {
        if (decl is null)
        {
            return null;
        }

        if (previous is not null)
        {
            decl.Handle.CommentRange.End.GetFileLocation(out _, out _, out _, out var commentEnd);
            previous.Extent.End.GetFileLocation(out _, out _, out _, out var previousEnd);
            if (commentEnd <= previousEnd)
            {
                return null;
            }
        }

        var raw = decl.Handle.RawCommentText.ToString();
        return string.IsNullOrWhiteSpace(raw) ? null : raw;
    }

    /// <summary>A type's doc comment, split into its text and what its doc commands give.</summary>
    private DocComment.TypeDoc TypeDoc(Decl decl, string subject) =>
        WithDocCommandDiagnostics(decl, subject, () => DocComment.Type(RawDoc(decl)));

    /// <summary>The doc comment of <paramref name="what"/>, which takes no doc command.</summary>
    private string? PlainDoc(Decl decl, string subject, string what, Decl? previous = null) =>
        WithDocCommandDiagnostics(decl, subject, () => DocComment.Plain(RawDoc(decl, previous), what));

    // Splits a doc comment, turning a doc command's failure into a diagnostic naming the subject.
    private T WithDocCommandDiagnostics<T>(Decl at, string subject, Func<T> split)
    {
        try
        {
            return split();
        }
        catch (DocCommandException exception)
        {
            throw Error(at, subject, exception.Message);
        }
    }

    private DescribeException Error(Cursor at, string subject, string message) =>
        new($"{Where(at)}: error: {subject}: {message}");

    private DescribeException UnknownDefault(Expr init, Cursor at, string subject) =>
        Error(at, subject,
              $"the default '{SourceText(init)}' is not one the description records: for a number or bool, a " +
              "constant expression libclang evaluates; an enum value, a bound type's constructor with literal " +
              "arguments, a nested type's own defaults (T{}), or a constant of its type");

    private static string Where(Cursor at)
    {
        at.Location.GetFileLocation(out var file, out var line, out var column, out _);
        return $"{file.Name}:{line}:{column}";
    }

    private string SourceText(Cursor at)
    {
        var range = at.Extent;
        range.Start.GetFileLocation(out var file, out _, out _, out var start);
        range.End.GetFileLocation(out _, out _, out _, out var end);
        var path = file.Name.ToString();
        if (!m_sources.TryGetValue(path, out var text))
        {
            text = File.Exists(path) ? File.ReadAllText(path) : "";
            m_sources[path] = text;
        }

        // A class's copy-initialisation (`= {1.0f, 2.0f}`) spans its `=`, which is not the initialiser's.
        return end > start && end <= text.Length ? text[(int)start..(int)end].TrimStart('=').TrimStart() : at.Spelling;
    }

    /// <summary>Every described type, dependencies first, then by name.</summary>
    private List<TypeDescription> InDependencyOrder()
    {
        var ordered = new List<TypeDescription>();
        var visited = new HashSet<string>(StringComparer.Ordinal);
        foreach (var name in m_described.Keys.Order(StringComparer.Ordinal))
        {
            Visit(name);
        }

        return ordered;

        void Visit(string name)
        {
            if (!visited.Add(name))
            {
                return;
            }

            var type = m_described[name];
            foreach (var dependency in type.References().Distinct().Order(StringComparer.Ordinal))
            {
                Visit(dependency);
            }

            ordered.Add(type);
        }
    }
}
