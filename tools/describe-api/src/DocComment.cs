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

        var paragraphs = new List<string>();
        var paragraph = new StringBuilder();
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
