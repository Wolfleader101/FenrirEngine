using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.RegularExpressions;
using ClangSharp;
using ClangSharp.Interop;
using Tomlyn;
using Tomlyn.Model;
using Type = ClangSharp.Type;

namespace DescribeApi;

/// <summary>
/// The <c>[natives]</c> section of a binding's configuration (<c>&lt;Name&gt;.binding.toml</c>), as far as this tool
/// reads it: the C++ sources holding the native functions, in the order they are described, and the call context every
/// native takes first, which the binding states or its defaults file (the top-level <c>defaults</c>, the SDK's names
/// stated once) gives. The binding's generator reads the rest of both files; this tool ignores it.
/// </summary>
internal sealed record NativesSelection(string Path, string Binding, IReadOnlyList<string> Sources, string Context)
{
    public const string Extension = ".binding.toml";

    /// <summary>Where the natives description is written: <c>&lt;Name&gt;.natives.json</c> beside the configuration.</summary>
    public string DescriptionPath =>
        System.IO.Path.Combine(System.IO.Path.GetDirectoryName(Path)!, Binding + ".natives.json");

    /// <summary>Reads the configuration's <c>[natives]</c>, or throws <see cref="DescribeException" /> saying what is wrong.</summary>
    public static NativesSelection Read(string path)
    {
        path = System.IO.Path.GetFullPath(path);
        var fileName = System.IO.Path.GetFileName(path);
        if (!fileName.EndsWith(Extension, StringComparison.Ordinal) || fileName.Length == Extension.Length)
        {
            throw new DescribeException($"{path}: a binding's configuration is named <Name>{Extension}");
        }

        TomlTable table;
        try
        {
            table = Toml.ToModel(File.ReadAllText(path), path);
        }
        catch (TomlException exception)
        {
            throw new DescribeException($"{path}: {exception.Message}");
        }

        if (!table.TryGetValue("natives", out var section) || section is not TomlTable natives)
        {
            throw new DescribeException($"{path}: [natives] is missing");
        }

        if (!natives.TryGetValue("sources", out var sourcesValue) || sourcesValue is not TomlArray sourceArray ||
            sourceArray.Count == 0 || sourceArray.Any(source => source is not string))
        {
            throw new DescribeException($"{path}: [natives] 'sources' is a list of C++ source paths, relative to the file");
        }

        var directory = System.IO.Path.GetDirectoryName(path)!;
        var context = StatedContext(natives, path) ?? DefaultContext(table, path, directory) ??
                      throw new DescribeException(
                          $"{path}: [natives] 'context' is the qualified name of the call context a native function takes " +
                          "first, stated here or in the file 'defaults' names");

        var sources = sourceArray.Cast<string>()
                          .Select(source => System.IO.Path.GetFullPath(System.IO.Path.Combine(directory, source)))
                          .ToList();
        foreach (var source in sources.Where(source => !File.Exists(source)))
        {
            throw new DescribeException($"{path}: source '{source}' does not exist");
        }

        return new NativesSelection(path, fileName[..^Extension.Length], sources, context);
    }

    // The [natives] context a table states, or null when it states none.
    private static string? StatedContext(TomlTable natives, string path)
    {
        if (!natives.TryGetValue("context", out var value))
        {
            return null;
        }

        return value is string context && context.Length > 0
                   ? context
                   : throw new DescribeException($"{path}: [natives] 'context' is the qualified name of the call context");
    }

    // The [natives] context of the defaults file the binding's `defaults` names, or null when it names none or the file
    // states none. Nothing else of the file is read here; the binding's generator checks the rest.
    private static string? DefaultContext(TomlTable binding, string path, string directory)
    {
        if (!binding.TryGetValue("defaults", out var value))
        {
            return null;
        }

        if (value is not string relative || relative.Length == 0)
        {
            throw new DescribeException($"{path}: 'defaults' is the path of the binding's defaults file, relative to the file");
        }

        var defaultsPath = System.IO.Path.GetFullPath(System.IO.Path.Combine(directory, relative));
        if (!File.Exists(defaultsPath))
        {
            throw new DescribeException($"{path}: defaults names {defaultsPath}, which does not exist");
        }

        TomlTable defaults;
        try
        {
            defaults = Toml.ToModel(File.ReadAllText(defaultsPath), defaultsPath);
        }
        catch (TomlException exception)
        {
            throw new DescribeException($"{defaultsPath}: {exception.Message}");
        }

        return defaults.TryGetValue("natives", out var section) && section is TomlTable natives ? StatedContext(natives, defaultsPath) : null;
    }
}

/// <summary>A native function's parameter or result: its C++ spelling and kind, and for a pointer what it points at.</summary>
internal sealed record NativeType(string Spelling, string Kind, string? Pointee = null, string? PointeeKind = null,
                                  bool Const = false);

/// <summary>
/// A parameter after the call context and the extension state: an output (<c>@param[out]</c>), a pointer the function
/// reads and writes (<c>@param[in,out]</c>), a pointer to one value it reads that is never a span whatever follows it
/// (<c>@param[in]</c>, <c>Input</c>), or an input.
/// </summary>
internal sealed record NativeParameter(string Name, NativeType Type, bool Output, bool InOut, bool Input, string? Doc);

/// <summary>
/// One native function: a free function taking the call context first, and optionally its extension state.
/// <c>NotRunning</c> is the C++ expression its <c>@notrunning</c> gives, the value a caller gets when the function
/// cannot be called; <c>Facade</c> is the <c>Class.Method</c> its <c>@facade</c> gives, the public static class and
/// method a binding names it as.
/// </summary>
internal sealed record NativeFunctionDescription(string Name, string Namespace, string Source, string? State, bool NoExcept,
                                                 string? Doc, NativeType Returns, string? ReturnsDoc,
                                                 IReadOnlyList<NativeParameter> Parameters, string? NotRunning,
                                                 string? Facade);

/// <summary>What the natives mode writes: a binding's native functions in source order.</summary>
internal sealed record NativesDescription(string Binding, string Context, IReadOnlyList<NativeFunctionDescription> Functions);

/// <summary>
/// Reads a binding's C++ sources with libclang and describes every native function they define: each free function whose
/// first parameter is <c>const Context&amp;</c>, in the order of the sources and, within one, of the definitions. Each
/// source is read on its own, with the same compile arguments as headers are. Anything the description cannot record
/// fails with a diagnostic naming the function and parameter; all of them are reported at once.
/// </summary>
internal sealed class NativesDescriber
{
    private readonly NativesSelection m_selection;
    private readonly List<string> m_errors = [];

    private NativesDescriber(NativesSelection selection)
    {
        m_selection = selection;
    }

    /// <summary>Describes the natives, or throws <see cref="DescribeException" /> with every diagnostic.</summary>
    public static NativesDescription Describe(NativesSelection selection, IReadOnlyList<string> compileArguments)
    {
        var describer = new NativesDescriber(selection);
        var functions = selection.Sources.SelectMany(source => describer.DescribeSource(source, compileArguments)).ToList();
        foreach (var group in functions.GroupBy(function => function.Name).Where(group => group.Count() > 1))
        {
            describer.m_errors.Add($"{selection.Path}: error: {group.Key}: defined {group.Count()} times; a native " +
                                   "function is named by its C++ name alone");
        }

        if (describer.m_errors.Count > 0)
        {
            throw new DescribeException(string.Join("\n", describer.m_errors));
        }

        return new NativesDescription(selection.Binding, selection.Context, functions);
    }

    private List<NativeFunctionDescription> DescribeSource(string source, IReadOnlyList<string> compileArguments)
    {
        using var index = CXIndex.Create();
        var arguments = Describer.Arguments(compileArguments);
        var error = CXTranslationUnit.TryParse(index, source, arguments, [], CXTranslationUnit_Flags.CXTranslationUnit_None,
                                               out var handle);
        if (error != CXErrorCode.CXError_Success)
        {
            throw new DescribeException($"{source}: libclang could not read the source ({error})");
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

        var functions = new List<NativeFunctionDescription>();
        foreach (var function in Functions(translationUnit.TranslationUnitDecl.Decls))
        {
            if (IsNative(function) && TryDescribe(function, source) is { } described)
            {
                functions.Add(described);
            }
        }

        return functions;
    }

    // Every free function defined in the source itself, in source order.
    private static IEnumerable<FunctionDecl> Functions(IEnumerable<Decl> decls)
    {
        foreach (var decl in decls)
        {
            switch (decl)
            {
                case NamespaceDecl ns:
                    foreach (var function in Functions(ns.Decls))
                    {
                        yield return function;
                    }

                    break;
                case LinkageSpecDecl linkage:
                    foreach (var function in Functions(linkage.Decls))
                    {
                        yield return function;
                    }

                    break;
                case FunctionDecl function when function is not CXXMethodDecl && function.Location.IsFromMainFile &&
                                                function.IsThisDeclarationADefinition:
                    yield return function;
                    break;
            }
        }
    }

    private bool IsNative(FunctionDecl function) =>
        function.Parameters.Count > 0 && ReferencedRecord(function.Parameters[0].Type, wantConst: true) == m_selection.Context;

    // The qualified name of the class an lvalue reference parameter names, when it is const (or not) as asked.
    private static string? ReferencedRecord(Type type, bool wantConst)
    {
        var canonical = type.CanonicalType;
        if (canonical.Handle.kind != CXTypeKind.CXType_LValueReference)
        {
            return null;
        }

        var pointee = ((ReferenceType)canonical).PointeeType.CanonicalType;
        if (pointee.Handle.kind != CXTypeKind.CXType_Record || pointee.IsLocalConstQualified != wantConst)
        {
            return null;
        }

        var name = pointee.AsString;
        return name.StartsWith("const ", StringComparison.Ordinal) ? name["const ".Length..] : name;
    }

    private NativeFunctionDescription? TryDescribe(FunctionDecl function, string source)
    {
        var name = function.Name;
        var errorsBefore = m_errors.Count;
        if (function.Handle.Linkage != CXLinkageKind.CXLinkage_External)
        {
            Error(function, name, "has internal linkage (static, or in an anonymous namespace): the generated " +
                                  "registration declares and names it from another source file");
        }

        if (function.IsVariadic)
        {
            Error(function, name, "is variadic: C# passes a fixed list of arguments");
        }

        var parameters = function.Parameters.ToList();
        var state = parameters.Count > 1 ? ReferencedRecord(parameters[1].Type, wantConst: false) : null;
        var first = state is null ? 1 : 2;

        var doc = DocComment.Function(function.Handle.RawCommentText.ToString());
        var described = new List<NativeParameter>();
        for (var i = first; i < parameters.Count; ++i)
        {
            var parameter = parameters[i];
            var subject = $"{name}::{parameter.Name}";
            if (parameter.Name.Length == 0)
            {
                Error(parameter, $"{name} (parameter {i + 1})", "is unnamed: a native's parameters are named in C#");
                continue;
            }

            var type = Spell(parameter.Type, parameter, subject);
            var output = false;
            var inOut = false;
            var input = false;
            if (doc.Parameters.TryGetValue(parameter.Name, out var parameterDoc))
            {
                output = parameterDoc.Output;
                inOut = parameterDoc.InOut;
                input = parameterDoc.Input;
                if ((output || inOut) && (type?.Kind != "pointer" || type.Const))
                {
                    Error(parameter, subject,
                          $"is documented @param[{(output ? "out" : "in,out")}] but is not a pointer to non-const data");
                }

                if (input && (type?.Kind != "pointer" || !type.Const))
                {
                    Error(parameter, subject,
                          "is documented @param[in] but is not a pointer to const data: [in] marks one value a native " +
                          "reads through a pointer, which a count after it does not make a span");
                }
            }

            if (type is not null)
            {
                described.Add(new NativeParameter(parameter.Name, type, output, inOut, input, parameterDoc?.Text));
            }
        }

        foreach (var documented in doc.Parameters.Keys.Where(documented => parameters.All(parameter => parameter.Name != documented)))
        {
            Error(function, name, $"documents a parameter '{documented}' it does not have");
        }

        var returns = Spell(function.ReturnType, function, $"{name} (result)");
        if (m_errors.Count > errorsBefore || returns is null)
        {
            return null;
        }

        if (returns.Kind == "pointer" && returns.Pointee is null)
        {
            return null;
        }

        var where = Path.GetRelativePath(Path.GetDirectoryName(m_selection.DescriptionPath)!, source).Replace('\\', '/');
        var noexcept = (CXCursor_ExceptionSpecificationKind)function.Handle.ExceptionSpecificationType is
            CXCursor_ExceptionSpecificationKind.CXCursor_ExceptionSpecificationKind_BasicNoexcept or
            CXCursor_ExceptionSpecificationKind.CXCursor_ExceptionSpecificationKind_NoThrow;
        if (doc.NotRunning is not null && returns.Kind == "void")
        {
            Error(function, name, "documents @notrunning but returns nothing");
            return null;
        }

        if (doc.Facades.Count > 1)
        {
            Error(function, name, "is marked @facade twice; a native names one public method");
            return null;
        }

        var facade = doc.Facades.SingleOrDefault();
        if (facade is not null && !FacadeMark.IsMatch(facade))
        {
            Error(function, name, "@facade names a public method as Class.Method");
            return null;
        }

        return new NativeFunctionDescription(name, Namespace(function.Handle), where, state, noexcept, doc.Text, returns,
                                             doc.Returns, described, doc.NotRunning, facade);
    }

    // A @facade mark: two C++-style identifiers joined by one dot, the public class and the method name it has there.
    private static readonly Regex FacadeMark = new(@"^[A-Za-z_][A-Za-z0-9_]*\.[A-Za-z_][A-Za-z0-9_]*$");

    // --- Types ---

    // A parameter's or result's spelling and kind: a value, or one pointer to a value. Null (with an error) otherwise.
    private NativeType? Spell(Type declared, Cursor at, string subject)
    {
        var canonical = declared.CanonicalType;
        if (canonical.Handle.kind == CXTypeKind.CXType_Pointer)
        {
            var pointee = ((PointerType)canonical).PointeeType;
            var target = SpellValue(DeclaredPointee(declared) ?? pointee, at, subject);
            if (target is null)
            {
                return null;
            }

            if (target.Kind == "pointer")
            {
                Error(at, subject, "is a pointer to a pointer, which the description cannot record");
                return null;
            }

            if (target.Kind == "string")
            {
                Error(at, subject, "is a pointer to a std::string_view: a native takes text as a std::string_view by value");
                return null;
            }

            var isConst = pointee.IsLocalConstQualified;
            return new NativeType($"{(isConst ? "const " : "")}{target.Spelling}*", "pointer", target.Spelling,
                                  target.Kind, isConst);
        }

        return SpellValue(declared, at, subject);
    }

    // The pointee as written, keeping its alias (a `const Math::Vec3*` points at `Math::Vec3`, not glm's type).
    private static Type? DeclaredPointee(Type declared)
    {
        var type = declared;
        while (true)
        {
            switch (type)
            {
                case PointerType pointer:
                    return pointer.PointeeType;
                case ElaboratedType elaborated:
                    type = elaborated.NamedType;
                    break;
                case TypedefType typedef:
                    type = typedef.Decl.UnderlyingType;
                    break;
                default:
                    return null;
            }
        }
    }

    private NativeType? SpellValue(Type declared, Cursor at, string subject)
    {
        var canonical = declared.CanonicalType;
        var alias = Alias(declared);
        if (Describer.ScalarKind(canonical) is { } scalar)
        {
            return new NativeType(alias ?? ScalarSpelling(canonical, scalar), scalar);
        }

        switch (canonical.Handle.kind)
        {
            case CXTypeKind.CXType_Void:
                return new NativeType("void", "void");
            case CXTypeKind.CXType_Record when IsStringView(canonical):
                return new NativeType("std::string_view", "string");
            case CXTypeKind.CXType_Record:
                return new NativeType(alias ?? Unqualified(canonical.AsString), "struct");
            case CXTypeKind.CXType_Enum:
                return new NativeType(alias ?? Unqualified(canonical.AsString), "enum");
            case CXTypeKind.CXType_Pointer:
                return new NativeType(declared.AsString, "pointer");
            case CXTypeKind.CXType_LValueReference:
            case CXTypeKind.CXType_RValueReference:
                Error(at, subject, "is a reference: after the call context and the extension state, a native takes " +
                                   "values and pointers");
                return null;
        }

        Error(at, subject, $"its type '{declared.AsString}' is not a kind the description records");
        return null;
    }

    // How a scalar of the given kind is spelled in a declaration: the keyword for the types that have one, else the
    // fixed-width alias of its kind (`std::int32_t`, never `int` or `long`).
    private static string ScalarSpelling(Type canonical, string kind) => canonical.Handle.kind switch
    {
        CXTypeKind.CXType_Bool => "bool",
        CXTypeKind.CXType_Char16 => "char16_t",
        CXTypeKind.CXType_Char32 => "char32_t",
        CXTypeKind.CXType_Float => "float",
        CXTypeKind.CXType_Double => "double",
        CXTypeKind.CXType_Char_S or CXTypeKind.CXType_Char_U => "char",
        _ => FixedWidth(kind),
    };

    // The qualified name of the alias the type is written as, unless it is the standard library's or the global
    // namespace's (std::uint32_t is recorded as itself, by kind, on every platform).
    private static string? Alias(Type declared)
    {
        var type = declared;
        while (true)
        {
            switch (type)
            {
                case ElaboratedType elaborated:
                    type = elaborated.NamedType;
                    break;
                case TypedefType typedef:
                    var name = Qualified(typedef.Decl.Handle);
                    if (name.Contains("::", StringComparison.Ordinal) && !name.StartsWith("std::", StringComparison.Ordinal))
                    {
                        return name;
                    }

                    type = typedef.Decl.UnderlyingType;
                    break;
                default:
                    return null;
            }
        }
    }

    // std::string_view, std::basic_string_view<char>, as clang spells it under any standard library's inline namespace.
    private static bool IsStringView(Type canonical) =>
        Regex.IsMatch(Unqualified(canonical.AsString), @"^std::(\w+::)*basic_string_view<char(,|>)");

    private static string FixedWidth(string kind) => kind[0] == 'i' ? $"std::int{kind[1..]}_t" : $"std::uint{kind[1..]}_t";

    private static string Unqualified(string spelling) =>
        spelling.StartsWith("const ", StringComparison.Ordinal) ? spelling["const ".Length..] : spelling;

    private static string Qualified(CXCursor cursor)
    {
        var parent = Namespace(cursor);
        return parent.Length == 0 ? cursor.Spelling.ToString() : $"{parent}::{cursor.Spelling}";
    }

    // The namespaces and classes enclosing a declaration, `::`-joined; an anonymous one is `(anonymous namespace)`.
    private static string Namespace(CXCursor cursor)
    {
        var parts = new List<string>();
        for (var parent = cursor.SemanticParent;
             !parent.IsNull && parent.kind != CXCursorKind.CXCursor_TranslationUnit;
             parent = parent.SemanticParent)
        {
            parts.Add(parent.kind == CXCursorKind.CXCursor_Namespace && parent.IsAnonymous
                          ? "(anonymous namespace)"
                          : parent.Spelling.ToString());
        }

        parts.Reverse();
        return string.Join("::", parts);
    }

    private void Error(Cursor at, string subject, string message)
    {
        at.Location.GetFileLocation(out var file, out var line, out var column, out _);
        m_errors.Add($"{file.Name}:{line}:{column}: error: {subject}: {message}");
    }
}

/// <summary>Writes a natives description as SCHEMA.md documents it: two-space indents, LF line ends, a final newline.</summary>
internal static class NativesWriter
{
    public static string Write(NativesDescription description)
    {
        var buffer = new MemoryStream();
        var options = new JsonWriterOptions
        {
            Indented = true,
            IndentSize = 2,
            NewLine = "\n",
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        };
        using (var json = new Utf8JsonWriter(buffer, options))
        {
            json.WriteStartObject();
            json.WriteString("binding", description.Binding);
            json.WriteString("context", description.Context);
            json.WriteStartArray("functions");
            foreach (var function in description.Functions)
            {
                json.WriteStartObject();
                json.WriteString("name", function.Name);
                json.WriteString("namespace", function.Namespace);
                json.WriteString("source", function.Source);
                if (function.State is not null)
                {
                    json.WriteString("state", function.State);
                }

                if (function.NoExcept)
                {
                    json.WriteBoolean("noexcept", true);
                }

                WriteDoc(json, function.Doc);
                if (function.NotRunning is not null)
                {
                    json.WriteString("notRunning", function.NotRunning);
                }

                if (function.Facade is not null)
                {
                    json.WriteString("facade", function.Facade);
                }

                json.WriteStartObject("returns");
                WriteType(json, function.Returns);
                WriteDoc(json, function.ReturnsDoc);
                json.WriteEndObject();
                json.WriteStartArray("parameters");
                foreach (var parameter in function.Parameters)
                {
                    json.WriteStartObject();
                    json.WriteString("name", parameter.Name);
                    WriteType(json, parameter.Type);
                    if (parameter.Output)
                    {
                        json.WriteBoolean("output", true);
                    }

                    if (parameter.InOut)
                    {
                        json.WriteBoolean("inOut", true);
                    }

                    if (parameter.Input)
                    {
                        json.WriteBoolean("input", true);
                    }

                    WriteDoc(json, parameter.Doc);
                    json.WriteEndObject();
                }

                json.WriteEndArray();
                json.WriteEndObject();
            }

            json.WriteEndArray();
            json.WriteEndObject();
        }

        return Encoding.UTF8.GetString(buffer.ToArray()) + "\n";
    }

    private static void WriteType(Utf8JsonWriter json, NativeType type)
    {
        json.WriteString("type", type.Spelling);
        json.WriteString("kind", type.Kind);
        if (type.Pointee is not null)
        {
            json.WriteString("pointee", type.Pointee);
            json.WriteString("pointeeKind", type.PointeeKind);
        }

        if (type.Const)
        {
            json.WriteBoolean("const", true);
        }
    }

    private static void WriteDoc(Utf8JsonWriter json, string? doc)
    {
        if (doc is not null)
        {
            json.WriteString("doc", doc);
        }
    }
}
