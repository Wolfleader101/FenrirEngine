namespace Fenrir.Scripting;

/// <summary>
/// Marks a <c>partial struct</c> with explicit layout as a tagged union. The struct declares a tag enum field at
/// <c>[FieldOffset(0)]</c> and one private field per kind at one shared offset, each named <c>_</c> plus its tag member
/// with the first letter lowered (<c>ConvexHull</c> is <c>_convexHull</c>). The source generator writes the rest: a
/// parameterless constructor making the kind whose tag value is 0, <c>Type</c>, one checked view per kind named after its
/// tag member, an implicit conversion from each kind, and the <see cref="InvalidOperationException"/> a wrong view throws.
/// </summary>
/// <remarks>
/// The offset is written out because C# cannot see a C++ union's alignment; a wrong one is refused when the assembly loads.
/// <c>default</c> of the struct is the tag-0 kind with every setting zero, not what <c>new()</c> makes.
/// </remarks>
[AttributeUsage(AttributeTargets.Struct, AllowMultiple = false, Inherited = false)]
public sealed class TaggedUnionAttribute : Attribute
{
    /// <summary>
    /// What the wrong-view message calls the union: "The {Noun} is a Sphere, not a Box.". The struct's name when left out.
    /// </summary>
    public string? Noun { get; set; }
}
