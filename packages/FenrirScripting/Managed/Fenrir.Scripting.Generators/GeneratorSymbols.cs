using System.Collections.Generic;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;

namespace Fenrir.Scripting.Generators;

/// <summary>What more than one generator asks of a symbol, and how each writes a name or text into generated code.</summary>
internal static class GeneratorSymbols
{
    /// <summary>Whether <paramref name="symbol"/> carries the attribute whose full name is <paramref name="attribute"/>.</summary>
    public static bool HasAttribute(ISymbol symbol, string attribute) =>
        symbol.GetAttributes().Any(a => a.AttributeClass?.ToDisplayString() == attribute);

    /// <summary><paramref name="text"/> as a C# string literal.</summary>
    public static string Literal(string text) => SymbolDisplay.FormatLiteral(text, quote: true);

    /// <summary>The type and the types around it, outermost first.</summary>
    public static IEnumerable<INamedTypeSymbol> Chain(INamedTypeSymbol symbol)
    {
        var chain = new List<INamedTypeSymbol>();
        for (INamedTypeSymbol? current = symbol; current != null; current = current.ContainingType)
            chain.Insert(0, current);
        return chain;
    }

    /// <summary>Whether the type is nested in something private or protected, which generated code outside it cannot name.</summary>
    public static bool HasHiddenContainer(INamedTypeSymbol symbol) =>
        Chain(symbol).Any(static s => s.DeclaredAccessibility is Accessibility.Private or Accessibility.Protected or Accessibility.ProtectedAndInternal);

    /// <summary>A member name as code, escaped with <c>@</c> when it is a keyword.</summary>
    public static string Identifier(string name) =>
        SyntaxFacts.GetKeywordKind(name) != SyntaxKind.None || SyntaxFacts.GetContextualKeywordKind(name) != SyntaxKind.None
            ? "@" + name
            : name;
}
