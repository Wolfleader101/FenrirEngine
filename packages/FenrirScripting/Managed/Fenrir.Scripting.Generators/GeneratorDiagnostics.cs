using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.Text;

namespace Fenrir.Scripting.Generators;

// A source position as plain data. A Location holds a syntax tree, which would keep the previous compilation alive
// in a generator's cache and make every edit regenerate everything.
internal sealed record LocationInfo(string Path, TextSpan Span, LinePositionSpan LineSpan)
{
    public static LocationInfo From(Location location) =>
        new(location.SourceTree?.FilePath ?? "", location.SourceSpan, location.GetLineSpan().Span);

    public Location ToLocation() => Location.Create(Path, Span, LineSpan);
}

// A diagnostic as plain data, so a generator's model compares by value and an unchanged input is not regenerated.
internal sealed record DiagnosticInfo(DiagnosticDescriptor Descriptor, LocationInfo Location, string[] Arguments)
{
    public Diagnostic ToDiagnostic() => Diagnostic.Create(Descriptor, Location.ToLocation(), Arguments.Cast<object>().ToArray());

    public bool Equals(DiagnosticInfo? other) =>
        other is not null && Descriptor.Id == other.Descriptor.Id && Location == other.Location && Arguments.SequenceEqual(other.Arguments);

    public override int GetHashCode() => (Descriptor.Id, Location).GetHashCode();
}
