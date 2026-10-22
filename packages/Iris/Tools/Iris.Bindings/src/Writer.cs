using System.Text;

namespace Iris.Bindings;

/// <summary>Generated text: indented lines, LF line ends, four spaces a level.</summary>
internal sealed class Writer
{
    private readonly StringBuilder text = new();

    /// <summary>The columns the current indent takes.</summary>
    public int Indentation { get; private set; }

    public void Indent() => Indentation += 4;

    public void Outdent() => Indentation -= 4;

    /// <summary>Writes one line at the current indent; an empty line has no indent.</summary>
    public void Line(string line = "")
    {
        if (line.Length > 0)
        {
            text.Append(' ', Indentation).Append(line);
        }

        text.Append('\n');
    }

    public override string ToString() => text.ToString();
}
