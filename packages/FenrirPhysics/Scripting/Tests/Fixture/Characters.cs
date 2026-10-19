using Fenrir.Physics;
using Fenrir.Scripting;

namespace Fenrir.PhysicsScriptFixture;

// Drives every character as a game's controller would, each tick: gravity added to the velocity physics wrote back, and
// the walk set by the tests as the horizontal velocity. No native move or state call.
[System(Phase.Tick)]
public sealed class CharacterController : ISystem
{
    public void OnUpdate(in SystemContext context)
    {
        // Gravity is read only with a character, so scenes without one never ask a world that may not exist yet.
        foreach (var (_, body) in context.Query<CharacterBody>())
        {
            body.Velocity += PhysicsWorld.Gravity * context.Time.TickRate;
            body.Velocity.X = Characters.Walk.X;
            body.Velocity.Z = Characters.Walk.Z;
        }
    }
}

// Characters from a script. The tests call these through Iris, then play the changes back as the runner does after a
// system.
public static class Characters
{
    // The horizontal velocity the controller gives every character.
    public static Vector3 Walk;

    public static void AddNewCharacter(uint entity) => new Entity(entity).Add(new CharacterBody());

    // A standing capsule character at (x, y, z), 1.8 m tall, made in one call as a system makes one. Stops the walk.
    public static uint MakeCharacter(float x, float y, float z)
    {
        Walk = Vector3.Zero;
        var character = Entity.Create();
        character.Add(new Transform { Position = new Vector3(x, y, z), Rotation = Quaternion.Identity, Scale = Vector3.One });
        character.Add(new Collider(new CapsuleShape { HalfHeight = 0.6f, Radius = 0.3f }));
        character.Add(new CharacterBody());
        return character.Value;
    }

    public static void ReportContacts(uint character) => new Entity(character).Add(new ContactEvents());

    public static void AddDefaultCharacter(uint entity) => new Entity(entity).Add(default(CharacterBody));

    public static void Disable(uint character) => new Entity(character).Get<CharacterBody>().Enabled = false;

    public static void AddRigidBody(uint character) => new Entity(character).Add(new RigidBody());

    // Whether the state, read through Ref<CharacterBody>, is a new CharacterState, field by field.
    public static bool StateIsNew(uint character)
    {
        ref readonly CharacterState state = ref new Entity(character).Get<CharacterBody>().State;
        return state.Ground == GroundState.InAir && state.GroundBody.IsNull && state.GroundCollider.IsNull &&
               state.GroundPoint == Vector3.Zero && state.GroundNormal == Vector3.Zero &&
               state.GroundVelocity == Vector3.Zero && state.GroundAngularVelocity == Vector3.Zero &&
               state.MovedVelocity == Vector3.Zero && state.CarriedVelocity == Vector3.Zero && !state.OnWall &&
               !state.OnCeiling && !state.ShapeBlocked;
    }

    public static void SetWalk(float x, float z) => Walk = new Vector3(x, 0, z);

    // Read through Ref<CharacterBody>, the reference a controller writes Velocity through.
    public static int Ground(uint character) => (int)new Entity(character).Get<CharacterBody>().State.Ground;

    public static uint GroundBody(uint character) => new Entity(character).Get<CharacterBody>().State.GroundBody.Target.Value;

    public static float Height(uint character) => new Entity(character).Get<Transform>().Position.Y;

    // Writes the character's capsule shape once, as a crouch or a stand-up does.
    public static void SetCapsule(uint character, float halfHeight) =>
        new Entity(character).Get<Collider>().Value.Shape = new CapsuleShape { HalfHeight = halfHeight, Radius = 0.3f };

    public static bool ShapeBlocked(uint character) => new Entity(character).Get<CharacterBody>().State.ShapeBlocked;

    public static float East(uint character) => new Entity(character).Get<Transform>().Position.X;
}
