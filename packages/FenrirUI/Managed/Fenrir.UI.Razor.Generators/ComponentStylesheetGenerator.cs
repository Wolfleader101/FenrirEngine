using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Text;

namespace Fenrir.UI.Razor.Generators;

/// <summary>
/// Records each <c>Foo.razor.css</c> on its Razor component (ADR-0070): for every such additional file beside a
/// <c>Foo.razor</c>, a partial <c>Foo</c> carrying <c>[ComponentStylesheet(path, text)]</c>, the path absolute as the
/// build saw it. <c>Foo</c> is named as the SDK's Razor generator names it: the <c>@namespace</c> in the file, else the
/// one in the nearest <c>_Imports.razor</c> above it with the folders between appended, else the root namespace with the
/// file's folders; the class is the file name. A <c>.razor.css</c> with no <c>.razor</c> beside it is warned about.
/// </summary>
[Generator(LanguageNames.CSharp)]
public sealed class ComponentStylesheetGenerator : IIncrementalGenerator
{
    private const string SheetSuffix = ".razor.css";
    private const string RazorSuffix = ".razor";
    private const string ImportsName = "_Imports.razor";

    private static readonly DiagnosticDescriptor NoComponent = new(
        "FUIR001",
        "A component stylesheet has no Razor component",
        "The stylesheet {0} has no Razor component {1} beside it, so nothing loads it",
        "Fenrir.UI.Razor",
        DiagnosticSeverity.Warning,
        isEnabledByDefault: true);

    // A directive at the start of a line; comments are stripped first, so a commented-out one is not read.
    private static readonly Regex NamespaceDirective =
        new(@"^[ \t]*@namespace[ \t]+([A-Za-z_@][\w.@]*)", RegexOptions.Multiline | RegexOptions.CultureInvariant);

    private static readonly Regex TypeParamDirective =
        new(@"^[ \t]*@typeparam[ \t]+([A-Za-z_@]\w*)", RegexOptions.Multiline | RegexOptions.CultureInvariant);

    private static readonly Regex RazorComment = new(@"@\*.*?\*@", RegexOptions.Singleline | RegexOptions.CultureInvariant);

    /// <summary>An additional file, as plain data: its path with forward slashes, the path Razor names it by, and its text.</summary>
    private sealed record SourceFile(string Path, string RelativePath, string Text);

    private sealed record Project(string RootNamespace, string Directory);

    public void Initialize(IncrementalGeneratorInitializationContext context)
    {
        IncrementalValuesProvider<SourceFile> files = context.AdditionalTextsProvider
            .Where(static file => file.Path.EndsWith(RazorSuffix, StringComparison.OrdinalIgnoreCase) ||
                                  file.Path.EndsWith(SheetSuffix, StringComparison.OrdinalIgnoreCase))
            .Combine(context.AnalyzerConfigOptionsProvider)
            .Select(static (pair, token) =>
            {
                AnalyzerConfigOptions options = pair.Right.GetOptions(pair.Left);
                options.TryGetValue("build_metadata.AdditionalFiles.TargetPath", out string? target);
                pair.Right.GlobalOptions.TryGetValue("build_property.MSBuildProjectDirectory", out string? directory);
                string path = Slashed(pair.Left.Path);
                string relative = string.IsNullOrEmpty(target) ? RelativeTo(path, Slashed(directory ?? "")) : Decode(target!);
                return new SourceFile(path, relative, pair.Left.GetText(token)?.ToString() ?? "");
            });

        IncrementalValueProvider<Project> project = context.AnalyzerConfigOptionsProvider.Select(static (options, _) =>
        {
            options.GlobalOptions.TryGetValue("build_property.RootNamespace", out string? root);
            options.GlobalOptions.TryGetValue("build_property.MSBuildProjectDirectory", out string? directory);
            return new Project(root ?? "", Slashed(directory ?? ""));
        });

        context.RegisterSourceOutput(files.Collect().Combine(project), static (output, input) => Emit(output, input.Left, input.Right));
    }

    private static void Emit(SourceProductionContext output, ImmutableArray<SourceFile> files, Project project)
    {
        var components = new Dictionary<string, SourceFile>(StringComparer.OrdinalIgnoreCase);
        var imports = new Dictionary<string, SourceFile>(StringComparer.OrdinalIgnoreCase);
        foreach (SourceFile file in files)
        {
            if (file.Path.EndsWith(SheetSuffix, StringComparison.OrdinalIgnoreCase))
                continue;
            if (FileName(file.RelativePath).Equals(ImportsName, StringComparison.OrdinalIgnoreCase))
                imports[Folder(file.RelativePath)] = file;
            else
                components[file.Path] = file;
        }

        var emitted = new HashSet<string>(StringComparer.Ordinal);
        foreach (SourceFile sheet in files.Where(static file => file.Path.EndsWith(SheetSuffix, StringComparison.OrdinalIgnoreCase))
                                          .OrderBy(static file => file.Path, StringComparer.Ordinal))
        {
            string componentPath = sheet.Path.Substring(0, sheet.Path.Length - ".css".Length);
            if (!components.TryGetValue(componentPath, out SourceFile? component))
            {
                output.ReportDiagnostic(Diagnostic.Create(NoComponent, Location.Create(sheet.Path, default, default),
                                                          sheet.Path, FileName(componentPath)));
                continue;
            }
            string? ns = NamespaceOf(component, imports, project.RootNamespace);
            string name = Identifier(FileName(component.RelativePath).Substring(0, FileName(component.RelativePath).Length - RazorSuffix.Length));
            string full = ns == null ? name : $"{ns}.{name}";
            if (!emitted.Add(full))
                continue;
            output.AddSource($"{full}.ComponentStylesheet.g.cs", SourceText.From(Partial(ns, name, TypeParameters(component.Text), sheet), Encoding.UTF8));
        }
    }

    /// <summary>The namespace Razor gives the component; null for the global namespace.</summary>
    private static string? NamespaceOf(SourceFile component, Dictionary<string, SourceFile> imports, string rootNamespace)
    {
        string folder = Folder(component.RelativePath);
        if (DirectiveIn(component.Text) is { } own)
            return own;

        // The nearest _Imports.razor above the file with a @namespace, the folders below it appended.
        for (string? at = folder; at != null; at = Parent(at))
        {
            if (imports.TryGetValue(at, out SourceFile? import) && DirectiveIn(import.Text) is { } imported)
                return Joined(imported, Below(folder, at));
        }
        return Joined(rootNamespace, folder) is { Length: > 0 } joined ? joined : null;
    }

    private static string? DirectiveIn(string text)
    {
        Match match = NamespaceDirective.Match(RazorComment.Replace(text, ""));
        return match.Success ? match.Groups[1].Value : null;
    }

    private static string TypeParameters(string text)
    {
        string[] names = TypeParamDirective.Matches(RazorComment.Replace(text, "")).Cast<Match>()
                                           .Select(static match => match.Groups[1].Value).ToArray();
        return names.Length == 0 ? "" : $"<{string.Join(", ", names)}>";
    }

    private static string Partial(string? ns, string name, string typeParameters, SourceFile sheet)
    {
        var source = new StringBuilder();
        source.AppendLine("// <auto-generated/>");
        source.AppendLine($"// The stylesheet {sheet.RelativePath}, recorded by Fenrir.UI.Razor.Generators (ADR-0070).");
        if (ns != null)
            source.AppendLine($"namespace {ns};").AppendLine();
        source.AppendLine($"[global::Fenrir.UI.Razor.ComponentStylesheet({Verbatim(sheet.Path)}, {Verbatim(sheet.Text)})]");
        source.AppendLine($"partial class {name}{typeParameters}");
        source.AppendLine("{");
        source.AppendLine("}");
        return source.ToString();
    }

    private static string Verbatim(string text) => "@\"" + text.Replace("\"", "\"\"") + "\"";

    /// <summary>The namespace with the folder's segments appended, each made an identifier as Razor makes it.</summary>
    private static string Joined(string ns, string folder)
    {
        IEnumerable<string> segments = folder.Split(new[] { '/' }, StringSplitOptions.RemoveEmptyEntries).Select(Identifier);
        return string.Join(".", new[] { ns }.Concat(segments).Where(static part => part.Length > 0));
    }

    /// <summary>A C# identifier from a name, as Razor sanitizes one: a leading digit gets an underscore, others become underscores.</summary>
    private static string Identifier(string name)
    {
        var identifier = new StringBuilder(name.Length + 1);
        if (name.Length == 0 || !(char.IsLetter(name[0]) || name[0] == '_'))
            identifier.Append('_');
        foreach (char c in name)
            identifier.Append(char.IsLetterOrDigit(c) || c == '_' ? c : '_');
        return identifier.ToString();
    }

    private static string Slashed(string path) => path.Replace('\\', '/').TrimEnd('/');

    private static string RelativeTo(string path, string directory) =>
        directory.Length != 0 && path.StartsWith(directory + "/", StringComparison.OrdinalIgnoreCase)
            ? path.Substring(directory.Length + 1)
            : FileName(path);

    private static string Decode(string target)
    {
        try
        {
            return Slashed(Encoding.UTF8.GetString(Convert.FromBase64String(target))).TrimStart('/');
        }
        catch (FormatException)
        {
            return Slashed(target).TrimStart('/');
        }
    }

    private static string FileName(string path) => path.Substring(path.LastIndexOf('/') + 1);

    private static string Folder(string relative)
    {
        int slash = relative.LastIndexOf('/');
        return slash < 0 ? "" : relative.Substring(0, slash);
    }

    private static string? Parent(string folder)
    {
        if (folder.Length == 0)
            return null;
        int slash = folder.LastIndexOf('/');
        return slash < 0 ? "" : folder.Substring(0, slash);
    }

    private static string Below(string folder, string ancestor) =>
        ancestor.Length == 0 ? folder : folder.Substring(Math.Min(folder.Length, ancestor.Length + 1));
}
