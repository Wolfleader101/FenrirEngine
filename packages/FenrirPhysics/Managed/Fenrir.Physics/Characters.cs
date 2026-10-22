using Fenrir.Scripting;

namespace Fenrir.Physics;

// The hand-written parts of FenrirPhysics's character body (FenrirPhysics/Characters.hpp, ADR-0014). The fields,
// defaults and docs are in the generated mirrors (Generated/), checked against the native layout whenever a script
// assembly loads, as the components are; the C++ documentation is the reference for each rule, and these remarks carry
// what a C# controller needs beside it. Each has the C++ defaults when made with `new`. The state's bools are read in
// place and CharacterState never crosses from C++ by value, so they are safe.

/// <remarks>
/// From C#: read it through <c>Ref&lt;CharacterBody&gt;</c>, in place; C# cannot write it. Landing and coyote time are
/// game code: keep last tick's <see cref="Ground"/> in a field or component of your own.
/// </remarks>
public readonly partial struct CharacterState
{
}

/// <remarks>
/// From C#: a <see cref="Phase.Tick"/> system writes <see cref="Velocity"/> through <c>Ref&lt;CharacterBody&gt;</c>
/// and reads <see cref="State"/> through the same reference; C# cannot write <see cref="State"/>. Gravity is game code's:
/// add <c>PhysicsWorld.Gravity * dt</c> each tick. To crouch, write the entity's <see cref="Collider.Shape"/> once;
/// physics never needs it written again, and a stand-up that does not fit yet sets
/// <see cref="CharacterState.ShapeBlocked"/> until it does. Characters use <see cref="ContactBegin"/>,
/// <see cref="ContactEnd"/>, <see cref="TriggerEnter"/> and <see cref="TriggerExit"/> like bodies, and
/// <see cref="PhysicsWorld.ContactsOf"/> lists a character's contacts. Nothing throws. Make it with <c>new</c>.
/// <c>default(CharacterBody)</c> is all zero bytes: a disabled character with every rule off, and no warning.
/// </remarks>
public partial struct CharacterBody
{
}
