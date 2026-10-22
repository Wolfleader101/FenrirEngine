using System.Text;

namespace DescribeApi;

/// <summary>Turns a raw C++ doc comment into the text the description records.</summary>
internal static class DocComment
{
    /// <summary>
    /// The comment's text without its markers (<c>/**</c>, <c>*/</c>, leading <c>*</c>, <c>///</c>) or its
    /// <c>@brief</c> tag. Lines of one paragraph are joined with a space; paragraphs are separated by a blank line.
    /// Nothing else is rewritten. Null when there is no comment.
    /// </summary>
    public static string? Text(string? raw)
    {
        if (string.IsNullOrWhiteSpace(raw))
        {
            return null;
        }

        return Join(Lines(raw));
    }

    /// <summary>
    /// A parameter's doc: whether <c>@param[out]</c> marks it an output, or <c>@param[in,out]</c> a pointer the function
    /// reads and writes, and its text.
    /// </summary>
    public sealed record ParameterDoc(bool Output, bool InOut, string? Text);

    /// <summary>
    /// A function's doc: its text without the commands below, each <c>@param</c>, <c>@return</c>, and the
    /// <c>@notrunning</c> value.
    /// </summary>
    public sealed record FunctionDoc(string? Text, IReadOnlyDictionary<string, ParameterDoc> Parameters, string? Returns,
                                     string? NotRunning);

    /// <summary>
    /// A function's doc comment, split: <c>@param name</c>, <c>@param[in] name</c>, <c>@param[out] name</c> or
    /// <c>@param[in,out] name</c> and <c>@return</c> (or <c>@returns</c>) each take the text up to the next command or
    /// blank line, and <c>@notrunning</c> the rest of its line, a C++ expression; the rest is the function's text, as
    /// <see cref="Text" /> gives it.
    /// </summary>
    public static FunctionDoc Function(string? raw)
    {
        var parameters = new Dictionary<string, ParameterDoc>(StringComparer.Ordinal);
        if (string.IsNullOrWhiteSpace(raw))
        {
            return new FunctionDoc(null, parameters, null, null);
        }

        var body = new List<string>();
        string? returns = null;
        string? notRunning = null;
        List<string>? command = null;
        Action<string?>? finish = null;
        foreach (var line in Lines(raw).Append(""))
        {
            var isParam = line.StartsWith("@param", StringComparison.Ordinal) || line.StartsWith("\\param", StringComparison.Ordinal);
            var isReturn = line.StartsWith("@return", StringComparison.Ordinal) || line.StartsWith("\\return", StringComparison.Ordinal);
            var isNotRunning = line.StartsWith("@notrunning", StringComparison.Ordinal) ||
                               line.StartsWith("\\notrunning", StringComparison.Ordinal);
            if (command is not null && (line.Length == 0 || isParam || isReturn || isNotRunning))
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
                finish = text => parameters[name] = new ParameterDoc(direction == "out", direction == "in,out", text);
            }
            else if (isNotRunning)
            {
                notRunning = line["@notrunning".Length..].Trim();
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

        return new FunctionDoc(Join(body), parameters, returns, notRunning is { Length: > 0 } ? notRunning : null);
    }

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
