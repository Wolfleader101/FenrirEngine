using System.Globalization;
using System.Text;

namespace DescribeApi;

/// <summary>A doc command that is unknown, malformed or on the wrong subject; the describer says where.</summary>
internal sealed class DocCommandException(string message) : Exception(message);

/// <summary>
/// Turns a raw C++ doc comment into the text the description records and the doc commands split out of it. The text is
/// the comment without its markers (<c>/**</c>, <c>*/</c>, leading <c>*</c>, <c>///</c>), its <c>@brief</c> tag or its
/// command lines: lines of one paragraph are joined with a space, paragraphs separated by a blank line, nothing else
/// rewritten; null when nothing is left.
/// </summary>
internal static class DocComment
{
    /// <summary>
    /// A parameter's doc: whether <c>@param[out]</c> marks it an output, <c>@param[in,out]</c> a pointer the function
    /// reads and writes, or <c>@param[in]</c> a pointer to one value it reads (never a span), and its text.
    /// </summary>
    public sealed record ParameterDoc(bool Output, bool InOut, bool Input, string? Text);

    /// <summary>
    /// A function's doc: its text without the commands below, each <c>@param</c>, <c>@return</c>, the
    /// <c>@notrunning</c> value, and every <c>@facade</c> mark as written (one at most is valid; the describer checks).
    /// </summary>
    public sealed record FunctionDoc(string? Text, IReadOnlyDictionary<string, ParameterDoc> Parameters, string? Returns,
                                     string? NotRunning, IReadOnlyList<string> Facades);

    /// <summary>
    /// A function's doc comment, split: <c>@param name</c>, <c>@param[in] name</c>, <c>@param[out] name</c> or
    /// <c>@param[in,out] name</c> and <c>@return</c> (or <c>@returns</c>) each take the text up to the next command or
    /// blank line, <c>@notrunning</c> the rest of its line, a C++ expression, and <c>@facade</c> the rest of its line,
    /// the public <c>Class.Method</c> the native is named as; the rest is the function's text, as the class gives it.
    /// </summary>
    public static FunctionDoc Function(string? raw)
    {
        var parameters = new Dictionary<string, ParameterDoc>(StringComparer.Ordinal);
        if (string.IsNullOrWhiteSpace(raw))
        {
            return new FunctionDoc(null, parameters, null, null, []);
        }

        var body = new List<string>();
        string? returns = null;
        string? notRunning = null;
        var facades = new List<string>();
        List<string>? command = null;
        Action<string?>? finish = null;
        foreach (var line in Lines(raw).Append(""))
        {
            var isParam = line.StartsWith("@param", StringComparison.Ordinal) || line.StartsWith("\\param", StringComparison.Ordinal);
            var isReturn = line.StartsWith("@return", StringComparison.Ordinal) || line.StartsWith("\\return", StringComparison.Ordinal);
            var isNotRunning = line.StartsWith("@notrunning", StringComparison.Ordinal) ||
                               line.StartsWith("\\notrunning", StringComparison.Ordinal);
            var isFacade = line.StartsWith("@facade", StringComparison.Ordinal) || line.StartsWith("\\facade", StringComparison.Ordinal);
            if (command is not null && (line.Length == 0 || isParam || isReturn || isNotRunning || isFacade))
            {
                finish!(command.Count == 0 ? null : string.Join(' ', command));
                command = null;
            }

            if (isParam)
            {
                var rest = line[6..];
                var direction = "";
                if (rest.StartsWith('['))
                {
                    var close = rest.IndexOf(']');
                    direction = close > 0 ? rest[1..close].Replace(" ", "", StringComparison.Ordinal) : "";
                    rest = close > 0 ? rest[(close + 1)..] : rest;
                }

                rest = rest.Trim();
                var space = rest.IndexOf(' ');
                var name = space < 0 ? rest : rest[..space];
                command = space < 0 ? [] : [rest[(space + 1)..].Trim()];
                finish = text => parameters[name] =
                    new ParameterDoc(direction == "out", direction == "in,out", direction == "in", text);
            }
            else if (isNotRunning)
            {
                notRunning = line["@notrunning".Length..].Trim();
            }
            else if (isFacade)
            {
                facades.Add(line["@facade".Length..].Trim());
            }
            else if (isReturn)
            {
                var rest = line[(line.StartsWith("@returns", StringComparison.Ordinal) || line.StartsWith("\\returns", StringComparison.Ordinal) ? 8 : 7)..].Trim();
                command = rest.Length == 0 ? [] : [rest];
                finish = text => returns = text;
            }
            else if (command is not null)
            {
                command.Add(line);
            }
            else
            {
                body.Add(line);
            }
        }

        return new FunctionDoc(Join(body), parameters, returns, notRunning is { Length: > 0 } ? notRunning : null, facades);
    }

    /// <summary>The doc commands a type takes, in the order SCHEMA.md ("Type and field doc commands") lists them.</summary>
    private static readonly string[] TypeCommandNames = ["version", "formerly", "transient", "editor-only"];

    /// <summary>The doc commands a field takes, in the order SCHEMA.md ("Type and field doc commands") lists them.</summary>
    private static readonly string[] FieldCommandNames =
        ["formerly", "range", "step", "display", "group", "readonly", "hidden", "multiline", "units", "transient"];

    /// <summary>A type's doc: its text without its doc commands, and what they give.</summary>
    public sealed record TypeDoc(string? Text, TypeCommands Commands);

    /// <summary>
    /// A field's doc: its text without its doc commands, its former names, its metadata, and whether <c>@transient</c>
    /// leaves it out of the description.
    /// </summary>
    public sealed record FieldDoc(string? Text, IReadOnlyList<string> Formerly, FieldMetadata Metadata, bool Transient);

    /// <summary>
    /// A type's doc comment, split: <c>@version N</c>, <c>@formerly Old::Name</c> (repeatable), <c>@transient</c> and
    /// <c>@editor-only</c>, each a line of its own; the rest is the type's text, as the class gives it. A
    /// field's command, a malformed one or an unknown <c>@</c> word throws <see cref="DocCommandException" />.
    /// </summary>
    public static TypeDoc Type(string? raw)
    {
        var (text, commands) = Split(raw);
        if (commands.Count == 0)
        {
            return new TypeDoc(text, TypeCommands.None);
        }

        int? version = null;
        var formerly = new List<string>();
        var transient = false;
        var editorOnly = false;
        foreach (var (name, argument, line) in commands)
        {
            CheckSubject(name, TypeCommandNames, FieldCommandNames, "a type's", "a field's");
            switch (name)
            {
                case "version":
                    if (!int.TryParse(argument, NumberStyles.None, CultureInfo.InvariantCulture, out var number) || number < 1)
                    {
                        throw new DocCommandException($"@version takes a whole number from 1: '{line}'");
                    }

                    version = version is null ? number : throw Twice(name);
                    break;
                case "formerly":
                    formerly.Add(IsQualifiedName(argument)
                                     ? argument
                                     : throw new DocCommandException($"@formerly takes the type's former qualified name (Ns::Type): '{line}'"));
                    break;
                case "transient":
                    transient = !transient ? Flag(name, argument, line) : throw Twice(name);
                    break;
                case "editor-only":
                    editorOnly = !editorOnly ? Flag(name, argument, line) : throw Twice(name);
                    break;
            }
        }

        return new TypeDoc(text, new TypeCommands(version ?? 1, formerly, transient, editorOnly));
    }

    /// <summary>
    /// A field's doc comment, split: <c>@formerly oldName</c> (repeatable), <c>@range min max</c>, <c>@step s</c>,
    /// <c>@display text</c>, <c>@group text</c>, <c>@readonly</c>, <c>@hidden</c>, <c>@multiline</c>,
    /// <c>@units text</c> and <c>@transient</c>, each a line of its own; the rest is the field's text. A type's command, a malformed one or
    /// an unknown <c>@</c> word throws <see cref="DocCommandException" />.
    /// </summary>
    public static FieldDoc Field(string? raw)
    {
        var (text, commands) = Split(raw);
        var formerly = new List<string>();
        var metadata = FieldMetadata.None;
        var transient = false;
        var seen = new HashSet<string>(StringComparer.Ordinal);
        foreach (var (name, argument, line) in commands)
        {
            CheckSubject(name, FieldCommandNames, TypeCommandNames, "a field's", "a type's");
            if (name != "formerly" && !seen.Add(name))
            {
                throw Twice(name);
            }

            switch (name)
            {
                case "formerly":
                    formerly.Add(IsIdentifier(argument)
                                     ? argument
                                     : throw new DocCommandException($"@formerly takes the field's former name: '{line}'"));
                    break;
                case "range":
                    var bounds = argument.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    if (bounds.Length != 2 || Number(bounds[0]) is not { } min || Number(bounds[1]) is not { } max || max < min)
                    {
                        throw new DocCommandException(
                            $"@range takes two numbers, a minimum and a maximum not below it: '{line}'");
                    }

                    metadata = metadata with { Range = (min, max) };
                    break;
                case "step":
                    metadata = metadata with
                    {
                        Step = Number(argument) is > 0 and var step
                                   ? step
                                   : throw new DocCommandException($"@step takes one number above 0: '{line}'"),
                    };
                    break;
                case "display":
                    metadata = metadata with { Display = Words(name, argument, line) };
                    break;
                case "group":
                    metadata = metadata with { Group = Words(name, argument, line) };
                    break;
                case "readonly":
                    metadata = metadata with { Readonly = Flag(name, argument, line) };
                    break;
                case "hidden":
                    metadata = metadata with { Hidden = Flag(name, argument, line) };
                    break;
                case "multiline":
                    metadata = metadata with { Multiline = Flag(name, argument, line) };
                    break;
                case "units":
                    metadata = metadata with { Units = Words(name, argument, line) };
                    break;
                case "transient":
                    transient = Flag(name, argument, line);
                    break;
            }
        }

        return new FieldDoc(text, formerly, metadata, transient);
    }

    /// <summary>
    /// The doc comment of something that takes no doc command (an enumerator, a tagged union's kind), as the
    /// class gives it; a command throws <see cref="DocCommandException" /> naming <paramref name="what" />.
    /// </summary>
    public static string? Plain(string? raw, string what)
    {
        var (text, commands) = Split(raw);
        return commands.Count == 0 ? text : throw new DocCommandException($"@{commands[0].Name}: {what} takes no doc command");
    }

    // The comment's text without its command lines, and each command line: the word after '@', the rest, the line.
    private static (string? Text, List<(string Name, string Argument, string Line)> Commands) Split(string? raw)
    {
        var commands = new List<(string, string, string)>();
        if (string.IsNullOrWhiteSpace(raw))
        {
            return (null, commands);
        }

        var body = new List<string>();
        foreach (var line in Lines(raw))
        {
            if (line.Length > 1 && line[0] == '@' && char.IsAsciiLetter(line[1]))
            {
                var space = line.IndexOf(' ');
                var name = space < 0 ? line[1..] : line[1..space];
                commands.Add((name, space < 0 ? "" : line[(space + 1)..].Trim(), line));
            }
            else
            {
                body.Add(line);
            }
        }

        return (Join(body), commands);
    }

    private static void CheckSubject(string name, string[] own, string[] other, string subject, string otherSubject)
    {
        if (own.Contains(name))
        {
            return;
        }

        if (other.Contains(name))
        {
            throw new DocCommandException($"@{name} is {otherSubject} doc command, not {subject}");
        }

        throw new DocCommandException(
            $"@{name} is not a doc command (a type takes {List(TypeCommandNames)}; a field {List(FieldCommandNames)})");

        static string List(string[] names) =>
            string.Join(", ", names[..^1].Select(n => "@" + n)) + " and @" + names[^1];
    }

    private static bool Flag(string name, string argument, string line) =>
        argument.Length == 0 ? true : throw new DocCommandException($"@{name} takes nothing after it: '{line}'");

    private static string Words(string name, string argument, string line) =>
        argument.Length > 0 ? argument : throw new DocCommandException($"@{name} takes text after it: '{line}'");

    private static DocCommandException Twice(string name) => new($"@{name} is written twice; it is given once");

    private static double? Number(string spelling) =>
        double.TryParse(spelling, NumberStyles.AllowLeadingSign | NumberStyles.AllowDecimalPoint | NumberStyles.AllowExponent,
                        CultureInfo.InvariantCulture, out var value) && double.IsFinite(value)
            ? value
            : null;

    private static bool IsIdentifier(string text) =>
        text.Length > 0 && (char.IsAsciiLetter(text[0]) || text[0] == '_') &&
        text.All(c => char.IsAsciiLetterOrDigit(c) || c == '_');

    private static bool IsQualifiedName(string text) => text.Split("::").All(IsIdentifier);

    // The comment's lines without their markers or @brief, trimmed; blank lines kept as empty strings.
    private static List<string> Lines(string raw)
    {
        var lines = new List<string>();
        foreach (var rawLine in raw.Replace("\r", "", StringComparison.Ordinal).Split('\n'))
        {
            var line = rawLine.Trim();
            if (line.StartsWith("///", StringComparison.Ordinal) || line.StartsWith("//!", StringComparison.Ordinal))
            {
                line = line[3..];
            }
            else
            {
                if (line.StartsWith("/**", StringComparison.Ordinal) || line.StartsWith("/*!", StringComparison.Ordinal))
                {
                    line = line[3..];
                }

                if (line.EndsWith("*/", StringComparison.Ordinal))
                {
                    line = line[..^2];
                }

                line = line.Trim();
                if (line.StartsWith('*'))
                {
                    line = line[1..];
                }
            }

            line = line.Trim();
            if (line.StartsWith("@brief", StringComparison.Ordinal) || line.StartsWith("\\brief", StringComparison.Ordinal))
            {
                line = line[6..].TrimStart();
            }

            lines.Add(line);
        }

        return lines;
    }

    // Lines of one paragraph joined with a space, paragraphs with a blank line; null when there is no text.
    private static string? Join(IEnumerable<string> lines)
    {
        var paragraphs = new List<string>();
        var paragraph = new StringBuilder();
        foreach (var line in lines)
        {
            if (line.Length == 0)
            {
                Flush();
                continue;
            }

            if (paragraph.Length > 0)
            {
                paragraph.Append(' ');
            }

            paragraph.Append(line);
        }

        Flush();
        return paragraphs.Count == 0 ? null : string.Join("\n\n", paragraphs);

        void Flush()
        {
            if (paragraph.Length > 0)
            {
                paragraphs.Add(paragraph.ToString());
                paragraph.Clear();
            }
        }
    }
}
