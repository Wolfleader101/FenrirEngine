using System.Globalization;
using System.Text;

namespace Fenrir.UI.Razor;

/// <summary>
/// One <c>AddMarkupContent</c> string parsed (spec: "The builder", folded markup): the elements, attributes and text
/// Razor folded because they hold no C#, in the well-formed HTML subset Razor's encoder writes. Entities are decoded,
/// void elements and <c>/&gt;</c> close themselves, comments are skipped, a <c>&lt;style&gt;</c> keeps its text raw, and
/// whitespace-only text is dropped. The builder replays the nodes through the same rules as generated calls.
/// </summary>
internal sealed class MarkupFragment
{
    internal enum NodeKind
    {
        Element,
        Text,
        Style,
    }

    /// <summary>An element (tag, attributes and children), a run of text, or a <c>&lt;style&gt;</c>'s sheet text.</summary>
    internal sealed class Node(NodeKind kind, string text)
    {
        public NodeKind Kind { get; } = kind;

        /// <summary>An element's tag, lower case; a text run's decoded text; a style's sheet text.</summary>
        public string Text { get; } = text;

        public List<KeyValuePair<string, string>> Attributes { get; } = [];

        public List<Node> Children { get; } = [];
    }

    private static readonly HashSet<string> VoidElements =
    [
        "area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr",
    ];

    private MarkupFragment(List<Node> nodes)
    {
        Nodes = nodes;
    }

    /// <summary>The top-level nodes, in order.</summary>
    public List<Node> Nodes { get; }

    /// <summary>Whether it is text alone, with no element or style: it joins the text around it.</summary>
    public bool IsTextOnly => Nodes.TrueForAll(node => node.Kind == NodeKind.Text);

    /// <summary>Parses <paramref name="markup"/>; throws a <see cref="FormatException"/> on markup it cannot read.</summary>
    public static MarkupFragment Parse(string markup)
    {
        var parser = new Parser(markup);
        List<Node> nodes = [];
        parser.ParseContent(nodes, closing: null);
        return new MarkupFragment(nodes);
    }

    private ref struct Parser(string text)
    {
        private readonly string text = text;
        private int at;

        public void ParseContent(List<Node> into, string? closing)
        {
            var run = new StringBuilder();
            while (at < text.Length)
            {
                if (text[at] != '<')
                {
                    int lt = text.IndexOf('<', at);
                    int end = lt < 0 ? text.Length : lt;
                    run.Append(Decode(text.AsSpan(at, end - at)));
                    at = end;
                    continue;
                }
                if (Starts("<!--"))
                {
                    int close = text.IndexOf("-->", at + 4, StringComparison.Ordinal);
                    if (close < 0)
                        throw Error("a comment is not closed");
                    at = close + 3;
                    continue;
                }
                if (Starts("</"))
                {
                    at += 2;
                    string name = ReadName();
                    SkipSpace();
                    Expect('>');
                    if (closing == null || !name.Equals(closing, StringComparison.OrdinalIgnoreCase))
                        throw Error($"</{name}> closes no open element");
                    Flush(run, into);
                    return;
                }
                Flush(run, into);
                ParseElement(into);
            }
            if (closing != null)
                throw Error($"<{closing}> is not closed");
            Flush(run, into);
        }

        private void ParseElement(List<Node> into)
        {
            Expect('<');
            string tag = ReadName().ToLowerInvariant();
            if (tag.Length == 0)
                throw Error("a '<' starts no tag");
            var attributes = new List<KeyValuePair<string, string>>();
            bool selfClosed = false;
            while (true)
            {
                SkipSpace();
                if (at >= text.Length)
                    throw Error($"<{tag} is not closed");
                if (text[at] == '>')
                {
                    ++at;
                    break;
                }
                if (Starts("/>"))
                {
                    at += 2;
                    selfClosed = true;
                    break;
                }
                string name = ReadName();
                if (name.Length == 0)
                    throw Error($"<{tag}> has an attribute with no name");
                SkipSpace();
                string value = "";
                if (at < text.Length && text[at] == '=')
                {
                    ++at;
                    SkipSpace();
                    value = ReadValue();
                }
                attributes.Add(new(name, value));
            }

            if (tag == "style")
            {
                if (selfClosed)
                    return;
                int close = text.IndexOf("</style", at, StringComparison.OrdinalIgnoreCase);
                if (close < 0)
                    throw Error("<style> is not closed");
                string sheet = text[at..close];
                at = close + "</style".Length;
                SkipSpace();
                Expect('>');
                into.Add(new Node(NodeKind.Style, sheet));
                return;
            }

            var element = new Node(NodeKind.Element, tag);
            element.Attributes.AddRange(attributes);
            into.Add(element);
            if (!selfClosed && !VoidElements.Contains(tag))
                ParseContent(element.Children, tag);
        }

        private string ReadName()
        {
            int start = at;
            while (at < text.Length && !char.IsWhiteSpace(text[at]) && text[at] is not ('>' or '/' or '=' or '<' or '"' or '\''))
                ++at;
            return text[start..at];
        }

        private string ReadValue()
        {
            if (at < text.Length && text[at] is '"' or '\'')
            {
                char quote = text[at++];
                int close = text.IndexOf(quote, at);
                if (close < 0)
                    throw Error("an attribute value is not closed");
                string quoted = Decode(text.AsSpan(at, close - at));
                at = close + 1;
                return quoted;
            }
            int start = at;
            while (at < text.Length && !char.IsWhiteSpace(text[at]) && text[at] != '>')
                ++at;
            return Decode(text.AsSpan(start, at - start));
        }

        private void SkipSpace()
        {
            while (at < text.Length && char.IsWhiteSpace(text[at]))
                ++at;
        }

        private void Expect(char c)
        {
            if (at >= text.Length || text[at] != c)
                throw Error($"'{c}' was expected");
            ++at;
        }

        private readonly bool Starts(string prefix) => string.CompareOrdinal(text, at, prefix, 0, prefix.Length) == 0;

        private readonly FormatException Error(string what) =>
            new($"Folded markup at character {at}: {what}, in \"{text}\"");

        private static void Flush(StringBuilder run, List<Node> into)
        {
            if (run.Length == 0)
                return;
            string joined = run.ToString();
            run.Clear();
            if (!string.IsNullOrWhiteSpace(joined))
                into.Add(new Node(NodeKind.Text, joined));
        }
    }

    /// <summary>Decodes the character references Razor's encoder writes: the named few, and any numeric one.</summary>
    internal static string Decode(ReadOnlySpan<char> encoded)
    {
        int amp = encoded.IndexOf('&');
        if (amp < 0)
            return encoded.ToString();
        var decoded = new StringBuilder(encoded.Length);
        while (amp >= 0)
        {
            decoded.Append(encoded[..amp]);
            encoded = encoded[amp..];
            int semicolon = encoded.IndexOf(';');
            string? character = semicolon > 1 ? Reference(encoded[1..semicolon]) : null;
            if (character == null)
            {
                decoded.Append('&');
                encoded = encoded[1..];
            }
            else
            {
                decoded.Append(character);
                encoded = encoded[(semicolon + 1)..];
            }
            amp = encoded.IndexOf('&');
        }
        decoded.Append(encoded);
        return decoded.ToString();
    }

    private static string? Reference(ReadOnlySpan<char> name)
    {
        if (name[0] == '#')
        {
            bool hex = name.Length > 1 && name[1] is 'x' or 'X';
            ReadOnlySpan<char> digits = hex ? name[2..] : name[1..];
            NumberStyles style = hex ? NumberStyles.AllowHexSpecifier : NumberStyles.None;
            if (!int.TryParse(digits, style, CultureInfo.InvariantCulture, out int code) || code < 0 || code > 0x10FFFF ||
                code is >= 0xD800 and <= 0xDFFF)
                return null;
            return char.ConvertFromUtf32(code);
        }
        return name switch
        {
            "amp" => "&",
            "lt" => "<",
            "gt" => ">",
            "quot" => "\"",
            "apos" => "'",
            "nbsp" => " ",
            _ => null,
        };
    }
}
