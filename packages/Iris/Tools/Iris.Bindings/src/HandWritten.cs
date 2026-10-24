using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace Iris.Bindings;

/// <summary>
/// Reads the hand-written parts of the contract: C# beside a generated mirror is a second <c>partial</c> part that adds
/// behaviour only. A field there would change the layout the mapping describes, so each one is an error.
/// </summary>
internal static class HandWritten
{
    /// <summary>
    /// Every instance field (or auto-property, which has one) declared in a hand-written part of a generated mirror,
    /// as one diagnostic line each.
    /// </summary>
    public static IReadOnlyList<string> Fields(Binding binding)
    {
        var generated = binding.Mirrored.Where(type => type.Category != Category.Enum)
                            .ToDictionary(binding.QualifiedName, type => type.Name, StringComparer.Ordinal);
        var problems = new List<string>();
        var contract = binding.Config.CSharp.Contract;
        if (!Directory.Exists(contract))
        {
            return problems;
        }

        foreach (var file in Directory.EnumerateFiles(contract, "*.cs", SearchOption.AllDirectories)
                     .Where(file => !Inside(file, binding.Config.CSharp.Generated) && !Inside(file, Path.Combine(contract, "bin")) &&
                                    !Inside(file, Path.Combine(contract, "obj")))
                     .Order(StringComparer.Ordinal))
        {
            var root = CSharpSyntaxTree.ParseText(File.ReadAllText(file), path: file).GetRoot();
            foreach (var declaration in root.DescendantNodes().OfType<StructDeclarationSyntax>())
            {
                if (!declaration.Modifiers.Any(SyntaxKind.PartialKeyword) ||
                    !generated.TryGetValue(QualifiedName(declaration), out var cppName))
                {
                    continue;
                }

                foreach (var member in declaration.Members)
                {
                    foreach (var name in InstanceFields(member))
                    {
                        var line = member.GetLocation().GetLineSpan().StartLinePosition.Line + 1;
                        problems.Add($"{file}:{line}: error: {QualifiedName(declaration)} declares the field '{name}' " +
                                     $"by hand, but its fields are generated from {cppName}: add the member to the C++ " +
                                     "type, or remove it here");
                    }
                }
            }
        }

        return problems;
    }

    private static IEnumerable<string> InstanceFields(MemberDeclarationSyntax member)
    {
        if (member.Modifiers.Any(SyntaxKind.StaticKeyword) || member.Modifiers.Any(SyntaxKind.ConstKeyword))
        {
            return [];
        }

        return member switch
        {
            FieldDeclarationSyntax field => field.Declaration.Variables.Select(variable => variable.Identifier.Text),
            PropertyDeclarationSyntax { AccessorList: { } accessors } property
                when accessors.Accessors.All(accessor => accessor.Body == null && accessor.ExpressionBody == null) =>
                [property.Identifier.Text],
            _ => [],
        };
    }

    private static string QualifiedName(BaseTypeDeclarationSyntax declaration)
    {
        var parts = new List<string> { declaration.Identifier.Text };
        for (var parent = declaration.Parent; parent != null; parent = parent.Parent)
        {
            switch (parent)
            {
                case BaseTypeDeclarationSyntax type:
                    parts.Insert(0, type.Identifier.Text);
                    break;
                case BaseNamespaceDeclarationSyntax space:
                    parts.Insert(0, space.Name.ToString());
                    break;
            }
        }

        return string.Join('.', parts);
    }

    private static bool Inside(string file, string directory) =>
        Path.GetFullPath(file).StartsWith(Path.GetFullPath(directory) + Path.DirectorySeparatorChar,
                                          StringComparison.OrdinalIgnoreCase);
}
