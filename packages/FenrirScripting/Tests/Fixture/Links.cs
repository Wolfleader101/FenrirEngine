using System.Reflection;
using Fenrir.Scripting;

namespace Fenrir.ScriptFixture;

// A native component with an Entity field, registered by the tests under "Test.Tether", as a joint names its bodies.
[NativeComponent("Test.Tether")]
public struct Tether : IComponent
{
    public Entity Target;
    public int Length;
}

// Entity fields written from C#, called by the tests through Iris.
public static class Links
{
    // Links the holder's Tether to an entity made here, as a system does. Returns the target's value: C++ checks that
    // the entity written carries the active scene's list, which Create gave it.
    public static uint LinkToCreated(uint holder)
    {
        var target = Entity.Create();
        new Entity(holder).Get<Tether>().Target = target;
        return target.Value;
    }

    // Links the holder's Tether to the entity a query yields for it, the holder itself: C++ checks that a query's
    // entity carries the active scene's list.
    public static void LinkToQueried(uint holder)
    {
        foreach (var (entity, tether) in World.Query<Tether>())
            if (entity.Value == holder)
                tether.Target = entity;
    }

    // As a system builds a joint: creates a target and a holder, and links them before either exists in storage.
    // Returns the holder.
    public static uint CreateLinkedPair()
    {
        var target = Entity.Create();
        var holder = Entity.Create();
        holder.Add(new Tether { Target = target, Length = 3 });
        return holder.Value;
    }

    // Empties the holder's field.
    public static void Unlink(uint holder)
    {
        var tether = new Entity(holder).Get<Tether>();
        tether.Target = Entity.None;
    }

    // Writes a zeroed entity, as default(Joint) would hold.
    public static void WriteZeroed(uint holder)
    {
        var tether = new Entity(holder).Get<Tether>();
        tether.Target = default;
    }

    // The entity value the holder's field names: uint.MaxValue when it is None.
    public static uint TargetOf(uint holder) => new Entity(holder).Get<Tether>().Target.Value;

    // None is the null value, and not the zeroed entity.
    public static bool NoneIsEmpty() => Entity.None.Value == uint.MaxValue && Entity.None != default && !Entity.None.IsValid;

    // What a script can read off an Entity: its public fields and properties, as "Name:Type", sorted. The owner is not
    // among them.
    public static string? ReadableMembers()
    {
        const BindingFlags everyPublic =
            BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
        var fields = typeof(Entity).GetFields(everyPublic).Select(field => $"{field.Name}:{field.FieldType.Name}");
        var properties = typeof(Entity)
                             .GetProperties(everyPublic)
                             .Select(property => $"{property.Name}:{property.PropertyType.Name}");
        return string.Join(",", fields.Concat(properties).Order(StringComparer.Ordinal));
    }
}
