namespace Fenrir.Scripting;

/// <summary>
/// A component that scripts may read but not change, such as the entity hierarchy, which only the engine edits. It is
/// reached through <see cref="ReadOnlyRef{T}"/>, which cannot be written through, and has no <see cref="Ref{T}"/>.
/// </summary>
public interface IReadOnlyComponent
{
}

/// <summary>
/// Marks a struct as an ECS component. A component is an <c>unmanaged</c> struct: plain data, no references. For every
/// component the Fenrir.Scripting source generator adds one property per public field on <see cref="Ref{T}"/>, so
/// <c>var t = entity.Get&lt;Transform&gt;(); t.Position.X += 1;</c> writes straight into the engine's storage.
/// </summary>
public interface IComponent : IReadOnlyComponent
{
}
