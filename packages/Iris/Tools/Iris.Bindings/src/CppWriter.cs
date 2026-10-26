namespace Iris.Bindings;

/// <summary>
/// Writes the C++ side: the Iris mappings header, the registration lists and the defaults test. Each file turns
/// clang-format off, so a repository's formatting check passes it as written.
/// </summary>
internal static class CppWriter
{
    /// <summary>
    /// One <c>Iris::StructMapping</c> per struct and tagged union and one <c>Iris::EnumMapping</c> per enum, each beside a
    /// <c>static_assert</c> of the layout it was described with; a bound type gets only the assert, since the binding maps
    /// it by hand. A type a binding in <c>uses</c> mirrors gets nothing: that binding's mappings, included here, map it.
    /// </summary>
    public static string Mappings(Binding binding)
    {
        var text = Header(binding, "The Iris mappings of every described type: each field by kind and byte offset, so no member is named.");
        text.Line("#pragma once");
        text.Line();
        text.Line("#include \"Iris/Iris.hpp\"");
        foreach (var include in binding.Config.Cpp.Includes)
        {
            text.Line($"#include \"{include}\"");
        }

        foreach (var used in binding.Uses)
        {
            text.Line($"#include \"{Relative(binding.Config.Cpp.Mappings, used.Config.Cpp.Mappings)}\"");
        }

        text.Line();
        text.Line("#include <cstdint>");
        text.Line("#include <string_view>");
        foreach (var type in binding.Types.Where(type => !binding.IsUsed(type)))
        {
            text.Line();
            if (type.Category != Category.BoundType)
            {
                Mapping(binding, type, text);
            }

            text.Line($"static_assert(sizeof({type.Name}) == {type.Size} && alignof({type.Name}) == {type.Alignment},");
            text.Line($"              \"{type.Name} is not the layout {Path.GetFileName(type.Source)} describes: \"");
            text.Line("              \"regenerate the description, then this binding\");");
        }

        text.Line();
        text.Line("// clang-format on");
        return text.ToString();
    }

    private static void Mapping(Binding binding, DescribedType type, Writer text)
    {
        if (type.Category == Category.Enum)
        {
            text.Line("template <>");
            text.Line($"struct Iris::EnumMapping<{type.Name}>");
            text.Line("{");
            text.Line($"    static constexpr std::string_view typeName = \"{binding.QualifiedName(type)}\";");
            text.Line("};");
            return;
        }

        var fields = new List<string>();
        if (type.Category == Category.TaggedUnion)
        {
            fields.Add(Field(binding.CppType(type.Tag!.Kind), Binding.TagFieldName, type.Tag.Offset));
            fields.AddRange(type.Kinds.Select(kind => Field(binding.CppType(kind.Kind), binding.KindFieldName(type, kind),
                                                            type.UnionOffset)));
        }
        else
        {
            fields.AddRange(type.Fields.Select(field => Field(binding.CppType(field.Kind),
                                                              binding.MemberName(type, field.Name), field.Offset)));
        }

        text.Line("template <>");
        text.Line($"struct Iris::StructMapping<{type.Name}>");
        text.Line("{");
        text.Line($"    static constexpr std::string_view typeName = \"{binding.QualifiedName(type)}\";");
        if (fields.Count == 0)
        {
            text.Line("    static constexpr auto fields = Iris::StructFields();");
        }
        else
        {
            text.Line("    static constexpr auto fields = Iris::StructFields(");
            for (var index = 0; index < fields.Count; index++)
            {
                text.Line($"        {fields[index]}{(index + 1 < fields.Count ? "," : ");")}");
            }
        }

        text.Line("};");
    }

    private static string Field(string type, string name, int offset) => $"Iris::Field<{type}>(\"{name}\", {offset})";

    /// <summary>
    /// The registration lists: one function that sets the extension's components, events and shared structs, each made
    /// with the C# name derived from the namespace and the type, and with natives its functions, each declared here and
    /// made with the C# name of its declaration. A native with outputs is made through <c>zero-outputs</c>
    /// (<c>ZeroingOutputs&lt;&amp;F, indices...&gt;::Call</c>), so each is zeroed before it runs (ADR-0045).
    /// </summary>
    public static string Registration(Binding binding, RegistrationConfig registration, NativesDescription? natives,
                                      IReadOnlyList<BoundNative> bound)
    {
        var registered = registration.HasResources ? "component, event, resource and shared struct" : "component, event and shared struct";
        var text = Header(binding,
                          natives == null
                              ? $"The extension's registration lists: every described {registered}."
                              : $"The extension's registration lists: every described {registered.Replace(" and ", ", ")} and native function.",
                          natives);
        text.Line($"#include \"{Relative(registration.Source, binding.Config.Cpp.Mappings)}\"");
        foreach (var include in registration.Includes)
        {
            text.Line($"#include \"{include}\"");
        }

        if (natives != null)
        {
            Declarations(text, natives);
        }

        var separator = registration.Function.LastIndexOf("::", StringComparison.Ordinal);
        var function = registration.Function[(separator < 0 ? 0 : separator + 2)..];
        var signature = $"void {function}({registration.Extension}& extension)";
        text.Line();
        if (separator > 0)
        {
            text.Line($"namespace {registration.Function[..separator]}");
            text.Line("{");
            text.Indent();
        }

        text.Line($"{signature};");
        text.Line();
        text.Line(signature);
        text.Line("{");
        text.Indent();
        List(text, "components", binding.Listed.Where(type => type.Role == Role.Component),
             type => registration.ReadOnly.Contains(type.Name) ? registration.MakeReadOnlyComponent : registration.MakeComponent,
             binding.RegisteredName);
        List(text, "events", binding.Listed.Where(type => type.Role == Role.Event), _ => registration.MakeEvent,
             binding.RegisteredName);
        List(text, "sharedStructs", binding.Listed.Where(type => type.Role == Role.SharedStruct),
             _ => registration.MakeSharedStruct, binding.QualifiedName);
        if (registration.HasResources)
        {
            List(text, "resources", binding.Listed.Where(Binding.IsResource), type => binding.ResourceFactory(registration, type)!,
                 binding.RegisteredName);
        }
        if (natives != null)
        {
            var config = binding.Config.Natives!;
            if (natives.Functions.Count == 0)
            {
                text.Line("extension.functions = {};");
            }
            else
            {
                text.Line("extension.functions = {");
                foreach (var native in bound)
                {
                    var described = native.Function;
                    // The zeroing template, given the native and its outputs' indices, has the native's signature, so it
                    // stands where the native would; the zeroing then happens inside the factory's not-running check.
                    var made = native.Outputs.Count == 0
                                   ? $"&{described.QualifiedName}"
                                   : $"&{config.ZeroOutputs}<&{described.QualifiedName}, {string.Join(", ", native.Outputs)}>::Call";
                    // A native's @notrunning value is what C# gets when it cannot be called.
                    var notRunning = described.NotRunning == null ? "" : $", {described.NotRunning}";
                    text.Line($"    {config.Make}<{made}>(\"{binding.Config.Namespace}.{config.Class}.{described.Name}\"{notRunning}),");
                }

                text.Line("};");
            }
        }

        text.Outdent();
        text.Line("}");
        if (separator > 0)
        {
            text.Outdent();
            text.Line($"}} // namespace {registration.Function[..separator]}");
        }

        text.Line();
        text.Line("// clang-format on");
        return text.ToString();
    }

    // Each native's declaration, so the registration can name it from this file: consecutive natives of one namespace
    // inside one block. The context and state are unnamed (a parameter may take either name).
    private static void Declarations(Writer text, NativesDescription natives)
    {
        var groups = new List<List<NativeFunction>>();
        foreach (var function in natives.Functions)
        {
            if (groups.Count == 0 || groups[^1][0].Namespace != function.Namespace)
            {
                groups.Add([]);
            }

            groups[^1].Add(function);
        }

        foreach (var group in groups)
        {
            var space = group[0].Namespace;
            text.Line();
            if (space.Length > 0)
            {
                text.Line($"namespace {space}");
                text.Line("{");
                text.Indent();
            }

            foreach (var function in group)
            {
                var parameters = new List<string> { $"const {natives.Context}&" };
                if (function.State != null)
                {
                    parameters.Add($"{function.State}&");
                }

                parameters.AddRange(function.Parameters.Select(parameter => $"{parameter.Type.Spelling} {parameter.Name}"));
                text.Line($"{function.Returns.Spelling} {function.Name}({string.Join(", ", parameters)})" +
                          $"{(function.NoExcept ? " noexcept" : "")};");
            }

            if (space.Length > 0)
            {
                text.Outdent();
                text.Line($"}} // namespace {space}");
            }
        }
    }

    private static void List(Writer text, string member, IEnumerable<DescribedType> types,
                             Func<DescribedType, string> make, Func<DescribedType, string> name)
    {
        var entries = types.ToList();
        if (entries.Count == 0)
        {
            text.Line($"extension.{member} = {{}};");
            return;
        }

        text.Line($"extension.{member} = {{");
        foreach (var type in entries)
        {
            text.Line($"    {make(type)}<{type.Name}>(\"{name(type)}\"),");
        }

        text.Line("};");
    }

    /// <summary>
    /// The defaults test: one case per registered type, calling the C# class's method for it and comparing the value with
    /// C++'s <c>T{}</c> field by field through the mapping. Padding is never compared, and of a tagged union only the tag
    /// and the kind it selects.
    /// </summary>
    public static string DefaultsTest(Binding binding, TestsConfig tests)
    {
        var text = Header(binding,
                          "C#'s new T() equals C++'s T{} for every type the binding registers, field by field.");
        text.Line($"#include \"{Relative(tests.Cpp, binding.Config.Cpp.Mappings)}\"");
        foreach (var include in tests.Includes)
        {
            text.Line($"#include \"{include}\"");
        }

        text.Line();
        text.Line("#include <catch2/catch_test_macros.hpp>");
        text.Line();
        text.Line("#include <cstddef>");
        text.Line("#include <cstdint>");
        text.Line("#include <cstring>");
        text.Line("#include <span>");
        text.Line("#include <string>");
        text.Line("#include <string_view>");
        text.Line("#include <vector>");
        text.Line();
        text.Line("namespace");
        text.Line("{");
        text.Indent();
        text.Line($"constexpr std::string_view DefaultsClass = \"{tests.Class}\";");
        text.Line();
        Unions(binding, text);
        text.Line();
        foreach (var line in Comparison)
        {
            text.Line(line);
        }

        text.Outdent();
        text.Line("} // namespace");
        foreach (var type in binding.Registered)
        {
            text.Line();
            text.Line($"TEST_CASE(\"C#'s new {binding.CSharpName(type)}() has C++'s {type.Name}{{}} defaults, field by field\", \"{tests.Tags}\")");
            text.Line("{");
            text.Indent();
            text.Line(tests.Setup);
            text.Line($"CheckDefaults<{type.Name}>({tests.Component}, \"{binding.CSharpName(type)}\");");
            text.Outdent();
            text.Line("}");
        }

        text.Line();
        text.Line("// clang-format on");
        return text.ToString();
    }

    // Each tagged union by mapped name, with each tag value (its bytes as an unsigned integer) and the index in the
    // mapping of the kind it selects.
    private static void Unions(Binding binding, Writer text)
    {
        text.Line("struct UnionKind");
        text.Line("{");
        text.Line("    std::uint64_t tag;");
        text.Line("    std::size_t field;");
        text.Line("};");
        text.Line();
        text.Line("struct TaggedUnion");
        text.Line("{");
        text.Line("    std::string_view typeName;");
        text.Line("    std::vector<UnionKind> kinds;");
        text.Line("};");
        text.Line();
        text.Line("// Of a tagged union only the tag and the kind it selects are compared: the other kinds' bytes are not set.");
        text.Line("const std::vector<TaggedUnion> taggedUnions = {");
        foreach (var union in binding.Types.Where(type => type.Category == Category.TaggedUnion))
        {
            var tag = binding.Type(union.Tag!.Kind.Type!);
            var mask = tag.Size >= 8 ? ulong.MaxValue : (1UL << (tag.Size * 8)) - 1;
            var kinds = union.Kinds.Select((kind, index) =>
            {
                var value = (ulong)tag.Values.First(enumerator => enumerator.Name == kind.Tag).Value & mask;
                return $"{{{value}u, {index + 1}}}";
            });
            text.Line($"    {{\"{binding.QualifiedName(union)}\", {{{string.Join(", ", kinds)}}}}},");
        }

        text.Line("};");
    }

    // The comparison, written into every defaults test so the test needs only Iris's headers. It walks Iris's own record
    // of a mapping, which is what the bridge checks the C# struct against.
    private static readonly string[] Comparison =
    [
        "using Iris::Detail::StructInfo;",
        "using Iris::Detail::ValueKind;",
        "",
        "std::size_t ScalarSize(ValueKind kind)",
        "{",
        "    switch (kind)",
        "    {",
        "    case ValueKind::Bool:",
        "    case ValueKind::Int8:",
        "    case ValueKind::UInt8:",
        "        return 1;",
        "    case ValueKind::Int16:",
        "    case ValueKind::UInt16:",
        "    case ValueKind::Char:",
        "        return 2;",
        "    case ValueKind::Int32:",
        "    case ValueKind::UInt32:",
        "    case ValueKind::Float:",
        "        return 4;",
        "    default:",
        "        return 8;",
        "    }",
        "}",
        "",
        "std::string Hex(std::span<const std::byte> bytes)",
        "{",
        "    constexpr std::string_view digits = \"0123456789abcdef\";",
        "    std::string text;",
        "    for (const std::byte byte : bytes)",
        "    {",
        "        text += digits[static_cast<unsigned>(byte) >> 4];",
        "        text += digits[static_cast<unsigned>(byte) & 15];",
        "    }",
        "    return text;",
        "}",
        "",
        "struct Comparison",
        "{",
        "    std::span<const std::byte> native;",
        "    std::span<const std::byte> managed;",
        "    std::vector<std::string> differences;",
        "",
        "    void Bytes(std::size_t offset, std::size_t size, const std::string& path)",
        "    {",
        "        const auto cpp = native.subspan(offset, size);",
        "        const auto cs = managed.subspan(offset, size);",
        "        if (std::memcmp(cpp.data(), cs.data(), size) != 0)",
        "        {",
        "            differences.push_back(path + \" at offset \" + std::to_string(offset) + \": C++ \" + Hex(cpp) + \", C# \" + Hex(cs));",
        "        }",
        "    }",
        "",
        "    std::uint64_t Unsigned(std::size_t offset, std::size_t size) const",
        "    {",
        "        std::uint64_t value = 0;",
        "        std::memcpy(&value, native.subspan(offset, size).data(), size);",
        "        return value;",
        "    }",
        "",
        "    void Value(ValueKind kind, const StructInfo* nested, std::size_t offset, const std::string& path)",
        "    {",
        "        if (kind == ValueKind::Struct)",
        "            Struct(*nested, offset, path);",
        "        else",
        "            Bytes(offset, kind == ValueKind::Enum ? nested->size : ScalarSize(kind), path);",
        "    }",
        "",
        "    void Struct(const StructInfo& info, std::size_t offset, const std::string& path)",
        "    {",
        "        for (const TaggedUnion& union_ : taggedUnions)",
        "        {",
        "            if (union_.typeName != info.typeName)",
        "                continue;",
        "            const auto& tag = info.fields[0];",
        "            Value(tag.kind, tag.nested, offset + tag.offset, path + \".\" + std::string(tag.name));",
        "            const std::uint64_t value = Unsigned(offset + tag.offset, tag.nested->size);",
        "            for (const UnionKind& kind : union_.kinds)",
        "            {",
        "                if (kind.tag == value)",
        "                {",
        "                    const auto& field = info.fields[kind.field];",
        "                    Value(field.kind, field.nested, offset + field.offset, path + \".\" + std::string(field.name));",
        "                }",
        "            }",
        "            return;",
        "        }",
        "        for (const auto& field : info.fields)",
        "        {",
        "            const std::string name = path + \".\" + std::string(field.name);",
        "            if (field.kind != ValueKind::InlineArray)",
        "            {",
        "                Value(field.kind, field.nested, offset + field.offset, name);",
        "                continue;",
        "            }",
        "            const std::size_t size = field.element == ValueKind::Struct || field.element == ValueKind::Enum",
        "                                         ? field.nested->size",
        "                                         : ScalarSize(field.element);",
        "            for (std::size_t index = 0; index < field.length; ++index)",
        "            {",
        "                Value(field.element, field.nested, offset + field.offset + index * size,",
        "                      name + \"[\" + std::to_string(index) + \"]\");",
        "            }",
        "        }",
        "    }",
        "};",
        "",
        "// Calls the C# class's method for T and compares what it returns with T{}, naming every field that differs.",
        "template <typename T>",
        "void CheckDefaults(const Iris::Component* component, std::string_view method)",
        "{",
        "    {",
        "        INFO(\"There is no loaded C# assembly to take the C# defaults from\");",
        "        REQUIRE(component != nullptr);",
        "    }",
        "    const auto type = component->FindType(DefaultsClass);",
        "    {",
        "        INFO(\"The defaults class \" << DefaultsClass << \" was not found: \" << type.diagnostic);",
        "        REQUIRE(type.Succeeded());",
        "    }",
        "    const auto handle = type.value->FindMethod<T()>(method);",
        "    {",
        "        INFO(\"The defaults method \" << method << \" was not found: \" << handle.diagnostic);",
        "        REQUIRE(handle.Succeeded());",
        "    }",
        "    const auto result = handle.value->Invoke();",
        "    {",
        "        INFO(\"The defaults method \" << method << \" failed: \" << result.diagnostic);",
        "        REQUIRE(result.Succeeded());",
        "    }",
        "    const T managed = *result.value;",
        "    const T native{};",
        "    Comparison comparison{std::as_bytes(std::span(&native, 1)), std::as_bytes(std::span(&managed, 1)), {}};",
        "    const StructInfo& info = Iris::Detail::StructInfoOf<T>;",
        "    comparison.Struct(info, 0, std::string(info.typeName));",
        "    std::string differences;",
        "    for (const std::string& difference : comparison.differences)",
        "        differences += \"\\n  \" + difference;",
        "    INFO(\"These fields of C#'s new() differ from C++'s T{}:\" << differences);",
        "    REQUIRE(comparison.differences.empty());",
        "}",
    ];

    private static Writer Header(Binding binding, string purpose, NativesDescription? natives = null)
    {
        // Formatting is off from the first line: the header names every description, which can run past any width.
        var text = new Writer();
        text.Line("// clang-format off");
        text.Line();
        var inputs = binding.Types.Select(type => Path.GetFileName(type.Source)).Distinct().ToList();
        if (natives != null)
        {
            inputs.Add(Path.GetFileName(natives.Path));
        }

        text.Line($"// Generated by Iris.Bindings from {Path.GetFileName(binding.Config.Path)} and " +
                  $"{string.Join(", ", inputs)}: do not edit.");
        text.Line($"// {purpose}");
        text.Line();
        return text;
    }

    /// <summary>The include spelling of <paramref name="target" /> from the directory of <paramref name="from" />.</summary>
    private static string Relative(string from, string target) =>
        Path.GetRelativePath(Path.GetDirectoryName(from)!, target).Replace('\\', '/');
}
