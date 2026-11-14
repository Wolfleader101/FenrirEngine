using Fenrir.Scripting;

namespace Fenrir.Ragdoll;

/// <remarks>
/// A description stays C++'s (it holds a list of parts), so C# holds only this handle to it. Editing a description
/// before building is C++-only in this version: from C#, build it and edit the spawned body and joint entities. A
/// handle is good in the scene it was described in, until <see cref="Ragdoll.Build(Entity, RagdollDescriptionHandle)"/>
/// takes it; one never built is dropped when its scene ends.
/// </remarks>
public readonly partial struct RagdollDescriptionHandle
{
    // How RagdollNative builds a handle a native returns (it crosses as its id): only C++ hands out ids.
    internal RagdollDescriptionHandle(uint id)
    {
        Id = id;
    }

    /// <summary>No description: what a refused <see cref="Ragdoll.DescribeHumanoid"/> gives.</summary>
    public static readonly RagdollDescriptionHandle None = default;

    /// <summary>Whether this is no description.</summary>
    public bool IsNone => Id == 0;
}
