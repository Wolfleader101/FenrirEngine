namespace Fenrir.Physics;

// The hand-written parts of the contact, trigger and joint events a physics world sends (FenrirPhysics/Events.hpp), once
// per step, in PostTick. The fields and docs are in the generated mirrors (Generated/). Read them through an
// EventReader<T> field; each reader sees each event once. Each is checked against its native layout whenever a script
// assembly loads, so a drift between the two fails the load and names the field. The entities are links as the engine
// stores them (as in Relationship): read each one's Target. They may already be destroyed by the time an end or exit is
// read, so every begin and enter has its end and exit.

/// <remarks>
/// From C#: physics also sets the joint's <see cref="Joint.Enabled"/> to false, which C# sees in the component. Read it
/// every frame: a system that reads only now and then misses breaks.
/// </remarks>
public readonly partial struct JointBroken
{
}
