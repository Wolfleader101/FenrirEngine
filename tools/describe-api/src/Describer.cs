using System.Diagnostics;
using System.Globalization;
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

    private Describer(Selection selection)
    {
        m_selection = selection;
    }

    private sealed record Pending(Type Type, string Name, Role Role, Decl Decl, string? ReachedFrom);

    /// <summary>Describes the selection, or throws <see cref="DescribeException" /> with every diagnostic.</summary>
    public static Description Describe(Selection selection, IReadOnlyList<string> compileArguments)
    {
        var describer = new Describer(selection);
        return describer.Run(compileArguments);
    }

    private Description Run(IReadOnlyList<string> compileArguments)
    {
        var directory = Path.GetDirectoryName(m_selection.Path)!;
        var mainPath = Path.Combine(directory, m_selection.Package + ".describe-api.cpp").Replace('\\', '/');
        var source = string.Concat(m_selection.Headers.Select(header => $"#include \"{header.Replace('\\', '/')}\"\n"));

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

        while (m_pending.Count > 0)
        {
            var pending = m_pending.Dequeue();
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

        if (m_errors.Count > 0)
        {
            throw new DescribeException(string.Join("\n", m_errors));
        }

        return new Description(m_selection.Package, InDependencyOrder());
    }

    private static string[] Arguments(IReadOnlyList<string> compileArguments)
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
        var found = Find(root.Decls, name.Split("::"), 0).FirstOrDefault();
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
            m_errors.Add($"{Where(found)}: error: {name}: an enum takes no role; it is described when a field uses it");
            return;
        }

        var decl = found is TypedefNameDecl ? (Decl?)canonical.AsTagDecl ?? found : found;
        Register(canonical, name, role, decl, null);
        if (found is TypedefNameDecl && role == Role.BoundType)
        {
            // A bound type is often an alias: its own comment, if any, is the one the selection named.
            m_aliasDocs[name] = found;
        }
    }

    private readonly Dictionary<string, Decl> m_aliasDocs = new(StringComparer.Ordinal);

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

        var doc = Doc(m_aliasDocs.GetValueOrDefault(name) ?? pending.Decl);
        if (pending.Role == Role.BoundType)
        {
            return new BoundTypeDescription(name, size, alignment, doc ?? Doc(type.AsTagDecl));
        }

        if (type.AsTagDecl?.Definition is EnumDecl enumDecl)
        {
            return DescribeEnum(enumDecl, name, size, alignment);
        }

        if (type.AsCXXRecordDecl?.Definition is not CXXRecordDecl record)
        {
            throw Error(pending.Decl, name, $"'{type.AsString}' is not a class, struct or enum");
        }

        if (record.IsUnion)
        {
            throw Error(record, name, $"a union outside the tagged-union shape ({TaggedUnionShape})");
        }

        CheckPlainData(record, name);
        var fields = record.Fields;
        if (IsTaggedUnionShape(fields))
        {
            return DescribeTaggedUnion(record, name, pending.Role, size, alignment, doc);
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

        return new StructDescription(name, size, alignment, doc, pending.Role, described);
    }

    private EnumDescription DescribeEnum(EnumDecl enumDecl, string name, long size, long alignment)
    {
        var underlying = IntegerKind(enumDecl.IntegerType.CanonicalType) ??
                         throw Error(enumDecl, name, $"an underlying type '{enumDecl.IntegerType.AsString}' that is not an integer kind");
        var unsigned = underlying.StartsWith('u');
        var values = enumDecl.Enumerators
                         .Select(enumerator => new EnumValue(
                                     enumerator.Name,
                                     unsigned ? enumerator.UnsignedInitVal.ToString(CultureInfo.InvariantCulture)
                                              : enumerator.InitVal.ToString(CultureInfo.InvariantCulture),
                                     Doc(enumerator, enumDecl.Enumerators.TakeWhile(e => e != enumerator).LastOrDefault())))
                         .ToList();
        return new EnumDescription(name, size, alignment, Doc(enumDecl), underlying, values);
    }

    private void CheckPlainData(CXXRecordDecl record, string name)
    {
        foreach (var method in record.Methods.Where(method => method.IsVirtual))
        {
            throw Error(method, $"{name}::{method.Name}", "a virtual function: a shared type is plain data");
        }

        foreach (var baseSpecifier in record.Bases)
        {
            var baseRecord = baseSpecifier.Type.CanonicalType.AsCXXRecordDecl?.Definition as CXXRecordDecl;
            if (baseSpecifier.IsVirtual || baseRecord is null || HasData(baseRecord) || IsPolymorphic(baseRecord))
            {
                throw Error(record, name,
                            $"its base class {baseSpecifier.Type.AsString} has data members or virtual functions");
            }
        }

        if (record.HasUserDeclaredCopyConstructor || record.HasUserDeclaredMoveConstructor ||
            record.HasUserDeclaredCopyAssignment || record.HasUserDeclaredMoveAssignment ||
            record.HasUserDeclaredDestructor)
        {
            throw Error(record, name,
                        "not trivially copyable: it declares its own copy, move, assignment or destructor");
        }
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
        if (field.Access != CX_CXXAccessSpecifier.CX_CXXPublic)
        {
            var access = field.Access == CX_CXXAccessSpecifier.CX_CXXProtected ? "protected" : "private";
            throw Error(field, subject, $"a {access} data member outside the tagged-union shape ({TaggedUnionShape})");
        }

        if (field.IsBitField)
        {
            throw Error(field, subject, "a bit-field: a shared type's fields are whole bytes");
        }

        var type = Classify(field.Type, field, subject);
        var value = Default(type, field.InClassInitializer, field, subject);
        return new FieldDescription(field.Name, Offset(field), type, value, Doc(field));
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
                                                                Doc(member)))
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
        var tag = new FieldDescription(tagField.Name, Offset(tagField), tagType, tagDefault, Doc(tagField));
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
        switch (type.Handle.kind)
        {
            case CXTypeKind.CXType_Bool:
                return new FieldType("bool");
            case CXTypeKind.CXType_Char16:
                return new FieldType("char16");
            case CXTypeKind.CXType_Float:
                return new FieldType("f32");
            case CXTypeKind.CXType_Double:
                return new FieldType("f64");
            case CXTypeKind.CXType_Enum:
                return new FieldType("enum", RegisterReferenced(type, subject));
            case CXTypeKind.CXType_Record:
                return new FieldType("struct", RegisterReferenced(type, subject));
            case CXTypeKind.CXType_Pointer:
                return new FieldType("pointer");
            case CXTypeKind.CXType_ConstantArray:
                var element = Classify(((ArrayType)type).ElementType, at, subject);
                if (element.Kind == "array")
                {
                    throw Error(at, subject, "an array of arrays, which the kinds cannot express");
                }

                return new FieldType("array", Element: element, Length: type.Handle.ArraySize);
            case CXTypeKind.CXType_LValueReference:
            case CXTypeKind.CXType_RValueReference:
                throw Error(at, subject, "a reference member: a shared type holds values, not references");
        }

        if (IntegerKind(type) is { } integer)
        {
            return new FieldType(integer);
        }

        throw Error(at, subject, $"its type '{declared.AsString}' is not a kind the description records");
    }

    private static string? IntegerKind(Type type)
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

    private string RegisterReferenced(Type canonical, string subject)
    {
        var decl = (Decl?)canonical.AsTagDecl?.Definition ?? canonical.AsTagDecl!;
        if (canonical.AsTagDecl is EnumDecl)
        {
            var name = Register(canonical, canonical.AsString, Role.Nested, decl, subject);
            m_enums[name] = (EnumDecl)decl;
            return name;
        }

        return Register(canonical, canonical.AsString, Role.Nested, decl, subject);
    }

    // --- Defaults ---

    private DefaultValue Default(FieldType type, Expr? init, Decl at, string subject)
    {
        if (type.Kind == "struct")
        {
            return StructDefault(type, init, at, subject);
        }

        if (type.Kind == "array")
        {
            return ArrayDefault(type, init, at, subject);
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
                if (IsLiteral(expression) || IsNumericLimits(expression))
                {
                    return Evaluate(init, type.Kind, at, subject);
                }

                break;
        }

        throw UnknownDefault(init, at, subject);
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

        throw UnknownDefault(init, at, subject);
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

    private DefaultValue ArrayDefault(FieldType type, Expr? init, Decl at, string subject)
    {
        var element = type.Element!;
        if (init is null)
        {
            if (element.Kind != "struct")
            {
                throw Error(at, subject, "has no default member initialiser");
            }

            return new DefaultValue.Array(Enumerable.Repeat<DefaultValue>(DefaultValue.TypeDefaults, (int)type.Length).ToList());
        }

        if (Strip(init) is not InitListExpr list)
        {
            throw UnknownDefault(init, at, subject);
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
        "pointer" => new DefaultValue.Null(),
        "struct" => DefaultValue.TypeDefaults,
        "array" => new DefaultValue.Array(Enumerable.Range(0, (int)type.Length).Select(_ => Zero(type.Element!, at, subject)).ToList()),
        "enum" => new DefaultValue.Text(
            m_enums[type.TypeName!].Enumerators.FirstOrDefault(e => e.InitVal == 0)?.Name ??
            throw Error(at, subject, $"is zero-initialised, but {type.TypeName} has no enumerator with the value 0")),
        _ => new DefaultValue.Number("0"),
    };

    private DefaultValue Evaluate(Expr expression, string kind, Decl at, string subject)
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

        throw Error(at, subject, $"a default libclang could not evaluate as {kind}");
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
    /// The decl's doc comment. Clang attaches a comment to the next enumerator when only a comma lies between, so a
    /// comment that ends before the previous sibling does is that sibling's, not this one's.
    /// </summary>
    private static string? Doc(Decl? decl, Decl? previous = null)
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

        return DocComment.Text(decl.Handle.RawCommentText.ToString());
    }

    private DescribeException Error(Cursor at, string subject, string message) =>
        new($"{Where(at)}: error: {subject}: {message}");

    private DescribeException UnknownDefault(Expr init, Cursor at, string subject) =>
        Error(at, subject,
              $"the default '{SourceText(init)}' is not one the description records: a literal, an enum value, " +
              "std::numeric_limits<T>::infinity(), max() or lowest(), a bound type's constructor with literal " +
              "arguments, or a nested type's own defaults (T{})");

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

        return end > start && end <= text.Length ? text[(int)start..(int)end] : at.Spelling;
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
